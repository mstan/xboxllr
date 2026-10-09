#!/usr/bin/env python3
"""Run original NTSC buffer settings detach/rebind with no substituted calls.

The settings fixture uses caller-owned storage. Kernel allocation/free and an
active hardware voice are outside this fixture. The separate stop-reference
fixture covers original completion polling over actual maintained VOICE_OFF;
native device tests cover stop/drain/free and real output after rebinding.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from aot_inventory import parse_xbe
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
    UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_EBP, UC_X86_REG_EIP, UC_X86_REG_ESP)


def run(path):
    blob = path.read_bytes()
    digest = hashlib.sha256(blob).hexdigest()
    assert digest == 'e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x10000, 0x1000000)
    u.mem_map(0x30000000, 0x10000)
    for section in parse_xbe(blob)['sections']:
        if section['raw_size'] and section['address'] + section['raw_size'] <= 0x1010000:
            u.mem_write(section['address'], blob[section['raw_offset']:section['raw_offset'] + section['raw_size']])
    obj, data, stack, end = 0x30000000, 0x30001000, 0x30008000, 0x30009000
    def put(address, *values):
        u.mem_write(address, struct.pack('<' + 'I' * len(values), *values))
    def get(address, count):
        return list(struct.unpack('<' + 'I' * count, u.mem_read(address, count * 4)))
    # The reached original caller pushes both zero arguments, before CALL.
    put(obj + 8, 0x12345678)
    u.reg_write(UC_X86_REG_EBX, obj)
    u.reg_write(UC_X86_REG_ESP, stack)
    u.emu_start(0xe69c8, 0xe69d0, count=10)
    assert u.reg_read(UC_X86_REG_EIP) == 0xe69d0
    assert get(stack - 12, 3) == [0x12345678, 0, 0]
    # Full settings helper, including original play/loop-region setter.
    u.mem_write(obj, bytes((i * 13 + 7) & 255 for i in range(0xd0)))
    put(obj + 8, 0x80000020)  # caller-owned storage; no fabricated allocator
    put(obj + 0xb8, data, 4096, 64, 1024, 128, 512)
    u.mem_write(data, b'\x5a' * 4096)
    prefix = bytes(u.mem_read(obj, 0xb8))
    rows = []
    for source, size in [(0, 0), (0, 0), (data, 4096), (data + 512, 1024), (0, 0)]:
        put(stack, end, source, size, 0xaabbccdd)
        u.reg_write(UC_X86_REG_ESP, stack)
        u.reg_write(UC_X86_REG_ECX, obj)
        saved = [(UC_X86_REG_EBX, 123), (UC_X86_REG_ESI, 456),
                 (UC_X86_REG_EDI, 789), (UC_X86_REG_EBP, 876)]
        for register, value in saved:
            u.reg_write(register, value)
        u.emu_start(0x176fa7, end, count=1000)
        assert u.reg_read(UC_X86_REG_EIP) == end
        assert u.reg_read(UC_X86_REG_EAX) == 0 and u.reg_read(UC_X86_REG_ESP) == stack + 12
        assert get(stack + 12, 1) == [0xaabbccdd]
        assert all(u.reg_read(register) == value for register, value in saved)
        assert get(obj + 0xb8, 6) == [source, size, 0, size, 0, size]
        assert bytes(u.mem_read(obj, 0xb8)) == prefix
        assert bytes(u.mem_read(data, 4096)) == b'\x5a' * 4096
        rows.append(dict(source=source, bytes=size, result=0))
    return dict(xbe_sha256=digest, cases=rows,
                scope='original caller argument prefix and complete caller-owned settings detach/rebind; active APU and allocator tested separately')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(run(args.xbe), indent=2) + '\n')
    print('PASS: original NULL/zero caller, detach/rebind, retained settings, source ownership and stack ABI')
