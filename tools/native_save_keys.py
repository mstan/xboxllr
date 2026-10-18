#!/usr/bin/env python3
"""Prepare private title signing inputs by executing the owner's original kernel.

No key bytes or game/kernel bodies belong in Git or tool output. This is an
offline input preparer, not a runtime kernel or an alternative save implementation.
"""
import argparse
import hashlib
import hmac
import json
import struct
from pathlib import Path

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP
from aot_inventory import parse_xbe
from create_eeprom import validate_eeprom, _decrypt_security

KERNEL_SHA = '827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c'
KERNEL_BASE = 0x80010000


class OriginalCrypto:
    def __init__(self, kernel):
        assert hashlib.sha256(kernel).hexdigest() == KERNEL_SHA, 'Unreviewed original kernel'
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
        self.cpu.reg_write(UC_X86_REG_ESP, stack)
        self.cpu.emu_start(address, end, count=2000000)
        assert self.cpu.reg_read(UC_X86_REG_EIP) == end
        assert self.cpu.reg_read(UC_X86_REG_ESP) == stack + 4 * (len(args) + 1)


def prepare(xbe, kernel, eeprom, output):
    image = parse_xbe(xbe)
    base, header_bytes = struct.unpack_from('<II', xbe, 0x104)
    assert base == 0x10000 and header_bytes <= 0x10000
    cert = struct.unpack_from('<I', xbe, 0x118)[0] - base
    cert_bytes = struct.unpack_from('<I', xbe, cert)[0]
    assert cert >= 0 and cert + cert_bytes <= header_bytes and cert_bytes >= 0x1d0
    original = OriginalCrypto(kernel)
    original.cpu.mem_map(base, 0x10000)
    original.cpu.mem_write(base, xbe[:header_bytes])
    # Source-bound retail per-title key producer, with its real XcHMAC/SHA path.
    original.call(0x80032ce6)
    signature = bytes(original.cpu.mem_read(original.export(325), 16))
    alternate = bytes(original.cpu.mem_read(original.export(354), 256))
    root_key = kernel[0x8004fa40 - KERNEL_BASE:0x8004fa50 - KERNEL_BASE]
    assert any(root_key), 'Original certificate root is unavailable'
    expected = hmac.digest(root_key, xbe[cert + 0xc0:cert + 0xd0], 'sha1')[:16]
    assert signature == expected
    for i in range(16):
        expected = hmac.digest(root_key, xbe[cert + 0xd0 + i*16:cert + 0xe0 + i*16], 'sha1')[:16]
        assert alternate[i*16:(i+1)*16] == expected
    checked = validate_eeprom(eeprom)
    assert checked.ok, checked.reason
    disk_key = _decrypt_security(eeprom, checked.revision)[8:24]
    assert len(disk_key) == 16
    blob = (b'XLLEKEY1' + hashlib.sha256(xbe).digest() + hashlib.sha256(eeprom).digest()
            + disk_key + signature + alternate)
    assert len(blob) == 360
    with output.open('xb') as f:
        f.write(blob)
    receipt = dict(kernel_sha256=KERNEL_SHA, xbe_sha256=image['sha256'],
                   eeprom_sha256=hashlib.sha256(eeprom).hexdigest(),
                   per_title_entry='0x80032ce6', original_hmac_entry=hex(original.export(340)),
                   independent_title_key_matches=17, eeprom_authenticated=True,
                   bytes=360, key_values_logged=False)
    output.with_suffix(output.suffix + '.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['xbe', 'kernel', 'eeprom', 'output']:
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.xbe.read_bytes(), args.kernel.read_bytes(),
                             args.eeprom.read_bytes(), args.output)))
