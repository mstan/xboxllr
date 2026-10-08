#!/usr/bin/env python3
"""Bounded original Xbox kernel system-time snapshot/return ABI reference.

Runs the supplied kernel's KeQuerySystemTime without replacing any instruction
or callee. Clock values and one simulated interrupt update are fixture inputs;
this does not claim RTC initialization or the complete clock interrupt path.
"""
import argparse
import hashlib
import json
import struct
import datetime
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP

KERNEL_SHA = '827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c'


def run(path):
    kernel = path.read_bytes()
    assert hashlib.sha256(kernel).hexdigest() == KERNEL_SHA, 'unreviewed kernel'
    pe = struct.unpack_from('<I', kernel, 0x3c)[0]
    exports = struct.unpack_from('<I', kernel, pe + 120)[0]
    fields = struct.unpack_from('<IIHHIIIIIII', kernel, exports)
    def export(ordinal):
        return 0x80010000 + struct.unpack_from('<I', kernel, fields[8] + 4 * (ordinal - fields[5]))[0]
    function, clock = export(128), export(154)
    code = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(kernel[function - 0x80010000:function - 0x80010000 + 40], function))
    low_read = next(i.address for i in code if i.mnemonic == 'mov' and i.op_str == f'ecx, dword ptr [0x{clock:x}]')
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x80010000, 0x70000); u.mem_write(0x80010000, kernel)
    u.mem_map(0x30000000, 0x10000)
    stack, output, end = 0x30008000, 0x30000104, 0x30009000
    def publish(value):
        u.mem_write(clock, struct.pack('<III', value & 0xffffffff, value >> 32, value >> 32))
    rows = []
    for initial, updated in [(0, None), (0x01dbffffffffffff, None), (0x01dbffffffffffff, 0x01dc000000000007)]:
        publish(initial)
        u.mem_write(output - 4, b'\xcd' * 16)
        u.mem_write(stack, struct.pack('<II', end, output))
        u.reg_write(UC_X86_REG_ESP, stack)
        saved = [(r, 0x12345678 + n) for n, r in enumerate((UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP))]
        for register, value in saved: u.reg_write(register, value)
        reads = [0]
        def interrupt(cpu, address, size, user):
            if address == low_read:
                reads[0] += 1
                if updated is not None and reads[0] == 1: publish(updated)
        hook = u.hook_add(UC_HOOK_CODE, interrupt)
        u.emu_start(function, end, count=100)
        u.hook_del(hook)
        expected = initial if updated is None else updated
        assert u.reg_read(UC_X86_REG_EIP) == end and u.reg_read(UC_X86_REG_ESP) == stack + 8
        assert all(u.reg_read(r) == value for r, value in saved)
        assert bytes(u.mem_read(output - 4, 16)) == b'\xcd' * 4 + struct.pack('<Q', expected) + b'\xcd' * 4
        assert reads[0] == (1 if updated is None else 2)
        rows.append(dict(initial=initial, interrupt_update=updated, output=expected, snapshot_attempts=reads[0]))
    conversions = []
    def convert(ordinal, source, destination):
        u.mem_write(stack, struct.pack('<III', end, source, destination))
        u.reg_write(UC_X86_REG_ESP, stack)
        u.emu_start(export(ordinal), end, count=10000)
        assert u.reg_read(UC_X86_REG_EIP) == end and u.reg_read(UC_X86_REG_ESP) == stack + 12
        assert all(u.reg_read(r) == value for r, value in saved)
        return u.reg_read(UC_X86_REG_EAX) & 255
    source, target = 0x30001004, 0x30002004
    for values in [(1601,1,1,0,0,0,0), (1970,1,1,0,0,0,0), (2000,2,29,12,34,56,789),
                   (2100,3,1,23,59,59,999), (2400,2,29,0,0,0,1)]:
        date = datetime.datetime(*values[:6], values[6] * 1000)
        delta = date - datetime.datetime(1601,1,1)
        stamp = (delta.days * 86400 + delta.seconds) * 10000000 + delta.microseconds * 10
        calendar = (*values, (date.weekday() + 1) % 7)
        u.mem_write(source, struct.pack('<Q', stamp + 9999))  # Sub-ms truncation.
        u.mem_write(target - 4, b'\xcd' * 24)
        convert(305, source, target)
        assert bytes(u.mem_read(target - 4, 24)) == b'\xcd' * 4 + struct.pack('<8h', *calendar) + b'\xcd' * 4
        u.mem_write(source - 4, b'\xcd' * 16)
        assert convert(304, target, source) == 1
        assert bytes(u.mem_read(source - 4, 16)) == b'\xcd' * 4 + struct.pack('<Q', stamp) + b'\xcd' * 4
        conversions.append(dict(time=stamp, fields=calendar))
    invalid = [(2001,2,29,0,0,0,0,0), (2100,2,29,0,0,0,0,0), (2000,0,1,0,0,0,0,0),
               (2000,1,1,24,0,0,0,0), (2000,1,1,0,0,0,1000,0)]
    for calendar in invalid:
        u.mem_write(source, struct.pack('<8h', *calendar))
        u.mem_write(target - 4, b'\xcd' * 16)
        assert convert(304, source, target) == 0
        assert bytes(u.mem_read(target - 4, 16)) == b'\xcd' * 16
    return dict(kernel_sha256=KERNEL_SHA, query_export=function, clock_export=clock, cases=rows,
                conversions=conversions, invalid_fields=invalid)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(run(args.kernel), indent=2) + '\n')
    print('PASS: original system-time snapshot/retry, calendar conversions/leap boundaries, invalid-output preservation and ABI')
