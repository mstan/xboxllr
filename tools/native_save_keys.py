#!/usr/bin/env python3
"""Prepare private signing inputs with the original-checked HMAC derivation.

No key bytes or game/kernel bodies belong in Git or tool output. This is an
offline input preparer, not a runtime kernel or an alternative save implementation.
The owned kernel supplies source-bound private material; normal preparation
does not execute it. Original execution remains an optional verification oracle.
"""
import argparse
import hashlib
import hmac
import json
import struct
from pathlib import Path

from aot_inventory import parse_xbe
from create_eeprom import validate_eeprom, _decrypt_security

KERNEL_SHA = '827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c'
KERNEL_BASE = 0x80010000
CERTIFICATE_KEY_OFFSET = 0x8004fa40 - KERNEL_BASE


def prepare_virtual(xbe, eeprom, output, *, root):
    """Real local-save HMAC keys for a new virtual identity, not retail keys.

    The caller owns/persists the root. Never use this to replace an imported
    identity or claim stock-save, stock-kernel or network compatibility.
    """
    if len(root) != 32 or not any(root):
        raise ValueError('Invalid virtual-console signing identity')
    image = parse_xbe(xbe)
    checked = validate_eeprom(eeprom)
    if not checked.ok:
        raise ValueError('Virtual EEPROM rejected: ' + checked.reason)
    cert = struct.unpack_from('<I', xbe, 0x118)[0] - image['base']
    headers = struct.unpack_from('<I', xbe, 0x108)[0]
    if cert < 0 or cert + 0x1d0 > headers:
        raise ValueError('Signing certificate is outside XBE headers')
    certificate_bytes = struct.unpack_from('<I', xbe, cert)[0]
    if certificate_bytes < 0x1d0 or cert + certificate_bytes > headers:
        raise ValueError('Signing certificate is incomplete or outside XBE headers')
    # Domain-separated keys still exercise the existing SHA/HMAC exports and
    # native game save verification. No accept-all or zero-key path exists.
    keys = b''.join(hmac.digest(root, b'xboxllr.local-save.v1\0' +
        struct.pack('<I', image['title_id']) + bytes([i]) +
        xbe[cert + 0xc0 + i*16:cert + 0xd0 + i*16], 'sha256')[:16] for i in range(17))
    disk_key = _decrypt_security(eeprom, checked.revision)[8:24]
    blob = b'XLLEKEY1' + hashlib.sha256(xbe).digest() + hashlib.sha256(eeprom).digest() + disk_key + keys
    output = Path(output)
    if output.exists() or output.with_suffix(output.suffix + '.json').exists():
        raise ValueError('Signing inputs already exist; preserve this identity')
    with output.open('xb') as file:
        file.write(blob)
    output.with_suffix(output.suffix + '.json').write_text(json.dumps(dict(
        format='XLLEKEY1', signing='virtual-local-v1', xbe_sha256=image['sha256'],
        eeprom_sha256=hashlib.sha256(eeprom).hexdigest(), bytes=len(blob),
        stock_key_equivalent=False, key_values_logged=False), indent=2)+'\n', encoding='utf-8')


def kernel_key(kernel):
    if hashlib.sha256(kernel).hexdigest() != KERNEL_SHA:
        raise ValueError('Unreviewed original kernel')
    key = kernel[CERTIFICATE_KEY_OFFSET:CERTIFICATE_KEY_OFFSET + 16]
    if len(key) != 16 or not any(key):
        raise ValueError('Original certificate root is unavailable')
    return key


def title_material(xbe, kernel):
    image = parse_xbe(xbe)
    base, header_bytes = struct.unpack_from('<II', xbe, 0x104)
    if base != 0x10000 or header_bytes > 0x10000:
        raise ValueError('Unsupported XBE header layout')
    cert = struct.unpack_from('<I', xbe, 0x118)[0] - base
    if cert < 0 or cert + 4 > header_bytes:
        raise ValueError('Certificate is outside XBE headers')
    cert_bytes = struct.unpack_from('<I', xbe, cert)[0]
    if cert_bytes < 0x1d0 or cert + cert_bytes > header_bytes:
        raise ValueError('Signing certificate is incomplete or outside XBE headers')
    root = kernel_key(kernel)
    keys = [hmac.digest(root, xbe[cert + 0xc0 + i*16:cert + 0xd0 + i*16], 'sha1')[:16]
            for i in range(17)]
    return image, header_bytes, keys[0], b''.join(keys[1:])


class OriginalCrypto:
    def __init__(self, kernel):
        kernel_key(kernel)
        try:
            from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
            from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP
        except ImportError as error:
            raise ValueError('Original verification requires the optional Unicorn reference dependency') from error
        self.reg_eip, self.reg_esp = UC_X86_REG_EIP, UC_X86_REG_ESP
        self.kernel = kernel
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.cpu.mem_map(KERNEL_BASE, 0x70000)
        self.cpu.mem_write(KERNEL_BASE, kernel)
        self.cpu.mem_map(0x30000000, 0x10000)
        pe = struct.unpack_from('<I', kernel, 0x3c)[0]
        directory = struct.unpack_from('<I', kernel, pe + 120)[0]
        self.exports = struct.unpack_from('<IIHHIIIIIII', kernel, directory)

    def export(self, ordinal):
        return KERNEL_BASE + struct.unpack_from('<I', self.kernel,
            self.exports[8] + 4 * (ordinal - self.exports[5]))[0]

    def call(self, address, *args):
        stack, end = 0x3000e000, 0x3000f000
        self.cpu.mem_write(stack, struct.pack('<' + 'I' * (len(args) + 1), end, *args))
        self.cpu.reg_write(self.reg_esp, stack)
        self.cpu.emu_start(address, end, count=2000000)
        if self.cpu.reg_read(self.reg_eip) != end or self.cpu.reg_read(self.reg_esp) != stack + 4 * (len(args) + 1):
            raise ValueError('Original crypto verification did not return with the expected stack')


def prepare(xbe, kernel, eeprom, output, *, verify_original=False):
    image, header_bytes, signature, alternate = title_material(xbe, kernel)
    checked = validate_eeprom(eeprom)
    if not checked.ok:
        raise ValueError('Signing EEPROM rejected: ' + checked.reason)
    disk_key = _decrypt_security(eeprom, checked.revision)[8:24]
    if len(disk_key) != 16:
        raise ValueError('Signing EEPROM disk key is incomplete')
    original = None
    if verify_original:
        original = OriginalCrypto(kernel)
        original.cpu.mem_map(0x10000, 0x10000)
        original.cpu.mem_write(0x10000, xbe[:header_bytes])
        original.call(0x80032ce6)
        actual = bytes(original.cpu.mem_read(original.export(325), 16))
        alternates = bytes(original.cpu.mem_read(original.export(354), 256))
        if not hmac.compare_digest(signature + alternate, actual + alternates):
            raise ValueError('Portable signing inputs differ from original execution')
    blob = (b'XLLEKEY1' + hashlib.sha256(xbe).digest() + hashlib.sha256(eeprom).digest()
            + disk_key + signature + alternate)
    output = Path(output)
    receipt_path = output.with_suffix(output.suffix + '.json')
    if output.exists() or output.is_symlink() or receipt_path.exists() or receipt_path.is_symlink():
        raise ValueError('Signing outputs already exist; preserve the existing profile')
    with output.open('xb') as f:
        f.write(blob)
    receipt = dict(kernel_sha256=KERNEL_SHA, xbe_sha256=image['sha256'],
                   eeprom_sha256=hashlib.sha256(eeprom).hexdigest(),
                   derivation='stdlib-hmac-sha1', original_execution=verify_original,
                   original_title_key_matches=17 if verify_original else 0,
                   per_title_key_count=17, eeprom_authenticated=True,
                   bytes=360, key_values_logged=False)
    if original:
        receipt.update(per_title_entry='0x80032ce6', original_hmac_entry=hex(original.export(340)))
    with receipt_path.open('x', encoding='utf-8') as file:
        file.write(json.dumps(receipt, indent=2) + '\n')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['xbe', 'kernel', 'eeprom', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--verify-original', action='store_true',
                        help='Also compare with bounded original-kernel execution; requires optional Unicorn')
    args = parser.parse_args()
    print(json.dumps(prepare(args.xbe.read_bytes(), args.kernel.read_bytes(),
                             args.eeprom.read_bytes(), args.output, verify_original=args.verify_original)))
