#!/usr/bin/env python3
"""Owned-code IRQ oracle: actual connect, mask/defer, lower, ISR and IRET.

The small ISR is an authored ABI fixture, not a replacement kernel routine.
Port writes are retained as observations; no peripheral result is fabricated.
Use the native OHCI/PIC test for actual device assertion/INTA/EOI and DPCs.
Original kernel/capture and generated observations stay private.
"""
import argparse
import json
import struct
from pathlib import Path
from native_io_device_reference import Reference, SNAPSHOT_SHA
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP,
    UC_X86_REG_EIP, UC_X86_REG_CS, UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES,
    UC_X86_REG_EFLAGS, UC_X86_REG_GDTR)


class InterruptReference(Reference):
    def __init__(self, snapshot):
        self.ports = []
        super().__init__(snapshot)
        mask, level = self.word(0x8003d88c), self.word(0x8003d890)
        self.port_values = {0x21: mask & 255, 0xa1: (mask >> 8) & 255,
                            0x4d0: level & 255, 0x4d1: (level >> 8) & 255}
        self.cpu.mem_write(0x1000f000, struct.pack('<QQQ', 0,
            0x00cf9b000000ffff, 0x00cf93000000ffff))
        self.cpu.reg_write(UC_X86_REG_GDTR, (0, 0x1000f000, 23, 0))
        self.cpu.reg_write(UC_X86_REG_CS, 8)
        for reg in (UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES):
            self.cpu.reg_write(reg, 16)

    def port_out(self, cpu, port, size, value, user):
        if size != 1 or port not in (0x20, 0x21, 0xa0, 0xa1, 0x4d0, 0x4d1):
            return super().port_out(cpu, port, size, value, user)
        self.ports.append([port, value & 255])
        if port in self.port_values:
            self.port_values[port] = value & 255

    def port_in(self, cpu, port, size, user):
        if size == 1 and port in self.port_values:
            return self.port_values[port]
        return super().port_in(cpu, port, size, user)

    def inject(self, vector, pc, sp, flags):
        if vector != 0x31:
            raise RuntimeError('oracle supports IRQ1 only')
        lo, hi = self.word(0x8003de48), self.word(0x8003de4c)
        assert lo >> 16 == 8 and hi & 65535 == 0x8e00
        entry = (lo & 65535) | (hi & 0xffff0000)
        self.cpu.mem_write(sp - 12, struct.pack('<III', pc, 8, flags))
        self.cpu.reg_write(UC_X86_REG_ESP, sp - 12)
        self.cpu.reg_write(UC_X86_REG_EFLAGS, flags & ~0x20300)
        self.cpu.reg_write(UC_X86_REG_EIP, entry)
        return entry

    def interrupt(self, cpu, number, user):
        if number == 0x31:
            self.inject(number, cpu.reg_read(UC_X86_REG_EIP),
                        cpu.reg_read(UC_X86_REG_ESP), cpu.reg_read(UC_X86_REG_EFLAGS))
        else:
            super().interrupt(cpu, number, user)


def run(snapshot):
    r = InterruptReference(snapshot)
    cpu = r.cpu
    obj, isr, context = 0x10001000, 0x10002000, 0x10003000
    # mov eax,[esp+8]; inc dword[eax]; mov ecx,[kernel IRQL];
    # mov [eax+4],ecx; mov eax,1; ret 8.
    cpu.mem_write(isr, bytes.fromhex('8b442408ff008b0d60da0380894804b801000000c20800'))
    r.call(109, obj, isr, context, 0x31, 25, 0, 0)
    assert r.call(98, obj) & 255 == 1
    assert r.call(98, obj) & 255 == 0
    assert [0x4d0, 2] in r.ports  # original connection sets level trigger
    assert r.call(129) & 255 == 0 and r.word(0x8003da60) & 255 == 2
    cpu.reg_write(UC_X86_REG_ECX, 31)
    assert r.call(160) & 255 == 2
    entry = r.inject(0x31, 0x10009000, 0x10006000, 0x202)
    cpu.emu_start(entry, 0x10009000, count=1000000)
    assert not r.failure and cpu.reg_read(UC_X86_REG_EIP) == 0x10009000
    assert cpu.reg_read(UC_X86_REG_ESP) == 0x10006000
    assert bytes(cpu.mem_read(context, 4)) == bytes(4)
    assert r.word(0x8003d884) & (1 << 5)  # pending slot is IRQ1 + 4, not IRQL25
    cpu.reg_write(UC_X86_REG_ECX, 0)
    r.call(161)
    assert struct.unpack('<II', cpu.mem_read(context, 8)) == (1, 25)
    assert r.word(0x8003da60) & 255 == 0 and r.word(0x8003d884) == 0
    assert [0x20, 0x61] in r.ports  # original specific EOI for IRQ1
    assert r.call(100, obj) & 255 == 1
    assert r.call(100, obj) & 255 == 0
    # Real shutdown registrations form the kernel's ordered list; removal
    # unlinks actual records. This does not claim shutdown callback execution.
    a, b = 0x10004000, 0x10004100
    for va, priority in ((a, 5), (b, 10)):
        cpu.mem_write(va, struct.pack('<IIII', isr, priority, 0, 0))
        r.call(47, va, 1)
    assert struct.unpack('<I', cpu.mem_read(b + 8, 4))[0] == a + 8
    r.call(47, b, 0)
    assert struct.unpack('<I', cpu.mem_read(a + 12, 4))[0] != b + 8
    r.call(47, a, 0)
    return dict(snapshot_sha256=SNAPSHOT_SHA, ports=r.ports,
                connection_duplicate_disconnect=True, high_irql_deferred=True,
                deferred_isr_calls=1, isr_irql=25, restored_irql=0,
                shutdown_registration_order_and_removal=True,
                limits='IRQ1/software only; no hardware/PIC model or native scheduler in this oracle')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--snapshot', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    result = run(args.snapshot)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))
