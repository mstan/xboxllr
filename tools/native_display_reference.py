#!/usr/bin/env python3
"""Execute original display-layout and kernel persistence routines, without hooks.

Bounded reference: real x86 helpers, AV saved pointer and PTE persistence bits.
This does not emulate the complete PersistDisplay driver or a firmware reboot.
Caller-supplied private binaries stay outside the repository. The machine NV2A
scanout fixture supplies the independent RAM-to-display side of the contract.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from aot_inventory import parse_xbe
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

XBE_SHA = 'e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
KERNEL_SHA = '827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c'


def run(xbe_path, kernel_path):
    xbe, kernel = xbe_path.read_bytes(), kernel_path.read_bytes()
    assert hashlib.sha256(xbe).hexdigest() == XBE_SHA, 'unreviewed XBE'
    assert hashlib.sha256(kernel).hexdigest() == KERNEL_SHA, 'unreviewed kernel'
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x10000, 0x1000000)
    for section in parse_xbe(xbe)['sections']:
        # All exercised routines/tables are in the initial image. Later demand
        # overlays are not part of these bounded fixtures.
        if section['raw_size'] and section['address'] + section['raw_size'] <= 0x1010000:
            u.mem_write(section['address'], xbe[section['raw_offset']:section['raw_offset'] + section['raw_size']])
    u.mem_map(0x80010000, 0x70000)
    u.mem_write(0x80010000, kernel)
    u.mem_map(0xc0000000, 0x400000)
    u.mem_map(0x30000000, 0x10000)
    def write(address, value):
        u.mem_write(address, struct.pack('<I', value))
    def read(address):
        return struct.unpack('<I', u.mem_read(address, 4))[0]
    def call(address, args):
        stack, end = 0x30008000, 0x30009000
        write(stack, end)
        for i, value in enumerate(args):
            write(stack + 4 + i * 4, value)
        u.reg_write(UC_X86_REG_ESP, stack)
        u.emu_start(address, end, count=10000)
        assert u.reg_read(UC_X86_REG_EIP) == end
        assert u.reg_read(UC_X86_REG_ESP) == stack + 4 + len(args) * 4
        return u.reg_read(UC_X86_REG_EAX)
    pe = struct.unpack_from('<I', kernel, 0x3c)[0]
    exports = struct.unpack_from('<I', kernel, pe + 120)[0]
    fields = struct.unpack_from('<IIHHIIIIIII', kernel, exports)
    def export(ordinal):
        return 0x80010000 + struct.unpack_from('<I', kernel, fields[8] + 4 * (ordinal - fields[5]))[0]
    rows = []
    for width, height, fmt in [(64, 64, 0x12), (640, 480, 0x12), (640, 480, 0x1e), (17, 9, 0x12)]:
        size = call(0x13acc0, [width, height, 1, 1, fmt, 0, 0, 0, 0x30000100, 0x30000104])
        pitch = (width * 4 + 63) & ~63
        assert size == pitch * height
        assert read(0x30000100) == 0x10029 | (fmt << 8)
        assert read(0x30000104) == (width - 1) | ((height - 1) << 12) | ((pitch // 64 - 1) << 24)
        rows.append(dict(width=width, height=height, format=fmt, bytes=size,
                         header_format=read(0x30000100), header_size=read(0x30000104)))
    base = 0x8055e000
    pte = lambda address: 0xc0000000 + (address >> 12) * 4
    for page in range(4):
        write(pte(base + page * 4096), (0x55e000 + page * 4096) | 0x16b)
    write(0x8003d884, 0); write(0x8003da60, 0)  # Reviewed kernel interrupt state.
    for pointer in (0, base, 0):
        call(export(4), [pointer]); assert call(export(1), []) == pointer
    initial = [read(pte(base + i * 4096)) for i in range(4)]
    cases = [([base + 4095, 2, 1], [1, 1, 0, 0]), ([base, 16384, 1], [1, 1, 1, 1]),
             ([base + 4096, 4096, 0], [1, 0, 1, 1]), ([base, 16384, 0], [0, 0, 0, 0])]
    persistence = []
    for args, flags in cases:
        call(export(178), args)
        observed = [read(pte(base + i * 4096)) for i in range(4)]
        assert observed == [value | (flag << 10) for value, flag in zip(initial, flags)]
        persistence.append(dict(arguments=args, ptes=observed))
    return dict(xbe_sha256=XBE_SHA, kernel_sha256=KERNEL_SHA, layouts=rows, persistence=persistence)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(run(args.xbe, args.kernel), indent=2) + '\n')
    print('PASS: original surface layouts, AV saved pointer, page persistence ranges and return ABI')
