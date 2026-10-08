#!/usr/bin/env python3
"""Run bounded scheduler operations from an owned Xbox kernel image in x86 LLE.

No kernel instruction is replaced and no service callback is fabricated. This
isolates ready-queue selection and base-priority arithmetic, not a full kernel
boot, context switch, quantum accounting, or interrupt/preemption oracle.
Addresses belong exclusively to the hash-bound reference fixture below.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP

BINDING = dict(
    sha256='827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c',
    base=0x80010000, select=0x8001f17c, ready=0x8001f204,
    set_base=0x8001c473, query_base=0x8001c3e7,
    queues=0x80043060, summary=0x80043040, idle=0x80043044,
    current=0x8003da64, next=0x8003da68, irql=0x8003da60,
    pending_interrupts=0x8003d884)


class Reference:
    def __init__(self, binary):
        if hashlib.sha256(binary).hexdigest() != BINDING['sha256']:
            raise ValueError('kernel revision has no reviewed scheduler binding')
        self.u = Uc(UC_ARCH_X86, UC_MODE_32)
        self.u.mem_map(BINDING['base'], 0x70000)
        self.u.mem_write(BINDING['base'], binary)
        self.u.mem_map(0x100000, 0x10000)
        self.u.mem_map(0x200000, 0x10000)
        self.reset()

    def write(self, address, value):
        self.u.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def read(self, address):
        return struct.unpack('<I', self.u.mem_read(address, 4))[0]

    def byte(self, address, value=None):
        if value is not None:
            self.u.mem_write(address, bytes([value & 255]))
        return self.u.mem_read(address, 1)[0]

    def reset(self):
        self.u.mem_write(0x200000, bytes(0x10000))
        for priority in range(32):
            head = BINDING['queues'] + priority * 8
            self.write(head, head)
            self.write(head + 4, head)
        for key in ['summary', 'idle', 'next', 'pending_interrupts']:
            self.write(BINDING[key], 0)
        # DISPATCH_LEVEL permits testing the selected-next state without
        # claiming that this fixture can execute an entire context switch.
        self.write(BINDING['irql'], 2)
        self.write(BINDING['current'], 0x200000)
        self.byte(0x200000 + 0x32, 31)

    def call(self, name, args=(), ecx=0):
        sp, sentinel = 0x108000, 0x109000
        self.write(sp, sentinel)
        for i, value in enumerate(args):
            self.write(sp + 4 + i * 4, value)
        self.u.reg_write(UC_X86_REG_ESP, sp)
        self.u.reg_write(UC_X86_REG_ECX, ecx)
        self.u.emu_start(BINDING[name], sentinel, count=10000)
        assert self.u.reg_read(UC_X86_REG_EIP) == sentinel, name
        assert self.u.reg_read(UC_X86_REG_ESP) == sp + 4 + len(args) * 4, name
        return self.u.reg_read(UC_X86_REG_EAX)

    def ready(self, slot, priority):
        address = 0x201000 + slot * 0x200
        self.byte(address + 0x32, priority)
        self.call('ready', ecx=address)
        return address


def verify(binary):
    ref = Reference(binary)
    cases = []
    for priority in range(1, 32):
        ref.reset()
        first = ref.ready(0, priority)
        second = ref.ready(1, priority)
        lower = ref.ready(2, priority - 1)
        assert ref.call('select', ecx=priority) == first
        assert ref.call('select', ecx=priority) == second
        assert ref.call('select', ecx=priority) == 0
        assert ref.call('select', ecx=0) == lower
        assert ref.read(BINDING['summary']) == 0
        cases.append(dict(priority=priority, equal_fifo=True, lower_excluded=True))
    # Readying a higher-priority thread selects it immediately, before any
    # host-worker startup could occur. Equal/lower priorities stay in queues.
    ref.reset()
    ref.byte(0x200000 + 0x32, 8)
    equal = ref.ready(0, 8)
    assert ref.read(BINDING['next']) == 0
    high = ref.ready(1, 10)
    assert ref.read(BINDING['next']) == high
    assert ref.byte(high + 0x2c) == 3
    higher = ref.ready(2, 15)
    assert ref.read(BINDING['next']) == higher
    assert ref.call('select', ecx=0) == high
    assert ref.call('select', ecx=0) == equal
    priorities = []
    for increment in [-32, -16, -15, -7, -2, 0, 2, 7, 15, 16, 32]:
        ref.reset()
        thread, process = 0x202000, 0x204000
        ref.write(thread + 0x44, process)
        ref.byte(process + 0x18, 8)
        ref.byte(thread + 0x70, 8)
        ref.byte(thread + 0x32, 8)
        ref.byte(thread + 0x2c, 5)  # waiting; no context switch required
        previous = ref.call('set_base', [thread, increment])
        queried = ref.call('query_base', [thread])
        if queried & 0x80000000:
            queried -= 0x100000000
        effective = ref.byte(thread + 0x32)
        expected = max(1, min(15, 8 + increment))
        expected_query = (-16 if increment <= -16 else 16 if increment >= 16 else expected - 8)
        assert previous == 0 and effective == expected and queried == expected_query
        priorities.append(dict(increment=increment, query=queried, effective=effective))
    return dict(kernel_sha256=BINDING['sha256'], queue_cases=cases,
                immediate_higher_priority_selection=True, priorities=priorities,
                limits=['No context switching, dynamic boosts, quanta or interrupt latency qualification'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.kernel.read_bytes())
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS: 31 priority/FIFO/exclusion cases, immediate preemption selection, 11 base-priority cases')
