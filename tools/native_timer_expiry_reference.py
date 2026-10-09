#!/usr/bin/env python3
"""Private original timer expiration through maintained PIT/PIC and real IRQ0.

The hash-bound kernel programs its own clock. Explicit time is a device input;
no timer, clock global, scheduler queue or original callee is replaced.
"""
import argparse
import ctypes
import hashlib
import json
import os
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_INSN
from unicorn.x86_const import (UC_X86_INS_OUT, UC_X86_REG_EIP, UC_X86_REG_ESP,
    UC_X86_REG_EFLAGS, UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
    UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)
from native_clock_reference import KERNEL_SHA
from native_filesystem_reference import FilesystemReference
from native_io_device_reference import SNAPSHOT_SHA


class TimerReference(FilesystemReference):
    def __init__(self, snapshot, library, image, kernel):
        self.running_clock = False
        self.clock_blocks = 0
        super().__init__(snapshot, library, image)
        self.device.xlle_storage_clock.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
        self.device.xlle_storage_deadline.argtypes = [ctypes.c_void_p]
        self.device.xlle_storage_deadline.restype = ctypes.c_uint64
        self.ticks = 0
        self.interrupt_time = self.base + self.word(self.export_table + 4*(120-self.ordinal_base))
        self.system_time = self.base + self.word(self.export_table + 4*(154-self.ordinal_base))
        data = kernel.read_bytes()
        if hashlib.sha256(data).hexdigest() != KERNEL_SHA:
            raise ValueError('unreviewed clock initialization image')
        # Isolated revision binding: original INIT was retired before the RAM
        # capture. Execute the supplied, hash-checked page at its original VA
        # on a separate CPU, sending every actual OUT to the maintained device.
        init = Uc(UC_ARCH_X86, UC_MODE_32)
        init.mem_map(0x80078000, 4096)
        init.mem_write(0x80078000, data[0x68000:0x69000])
        init.mem_map(0x10000000, 0x10000)
        init.mem_write(0x10008000, struct.pack('<I', 0x10009000))
        init.reg_write(UC_X86_REG_ESP, 0x10008000)
        init.reg_write(UC_X86_REG_EFLAGS, 0x202)
        self.programming = []
        def output(cpu, port, size, value, user):
            if port not in (0x40, 0x43) or size != 1:
                raise RuntimeError('unqualified clock initialization I/O')
            self.programming.append([port, size, value])
            self.port(port, size, 1, value)
        init.hook_add(UC_HOOK_INSN, output, None, 1, 0, UC_X86_INS_OUT)
        init.emu_start(0x800786d4, 0x10009000, count=128)
        assert init.reg_read(UC_X86_REG_EIP) == 0x10009000
        assert init.reg_read(UC_X86_REG_ESP) == 0x10008004
        assert init.reg_read(UC_X86_REG_EFLAGS) == 0x202
        assert self.programming == [[0x43, 1, 0x34], [0x40, 1, 0x65], [0x40, 1, 4]]
        assert self.device.xlle_storage_deadline(self.instance) == 1125

    def advance(self):
        registers = (UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EFLAGS,
                     UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                     UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)
        saved = [self.cpu.reg_read(reg) for reg in registers]
        self.ticks = self.device.xlle_storage_deadline(self.instance)
        assert self.ticks and self.device.xlle_storage_clock(self.instance, self.ticks)
        assert self.device.xlle_storage_vector(self.instance, 1) == 0x30
        self.inject(0x30)
        entry = self.cpu.reg_read(UC_X86_REG_EIP)
        self.cpu.emu_start(entry, 0x10009000, count=1000000)
        if self.failure or self.cpu.reg_read(UC_X86_REG_EIP) != 0x10009000:
            raise RuntimeError(self.failure or f'original IRQ0 did not return: {self.cpu.reg_read(UC_X86_REG_EIP):08x}')
        assert self.device.xlle_storage_vector(self.instance, 0) < 0
        assert saved == [self.cpu.reg_read(reg) for reg in registers], 'IRQ register/flags preservation'

    def block(self, cpu, address, size, user):
        # Deterministic virtual time, deliberately unrelated to host speed. The
        # real PIC decides eligibility; the existing interpreter loop delivers
        # its vector. Original code owns expiry, DPCs and context switching.
        if self.running_clock:
            self.clock_blocks += 1
            if self.clock_blocks % 100 == 0:
                self.ticks = self.device.xlle_storage_deadline(self.instance)
                assert self.device.xlle_storage_clock(self.instance, self.ticks)
        super().block(cpu, address, size, user)

    def value(self, address, fmt='<I'):
        return struct.unpack(fmt, self.cpu.mem_read(address, struct.calcsize(fmt)))[0]

    def workers(self):
        """Real original thread creation/block/wake/exit, with authored test work."""
        entry, context, handle_out, event_out, timeout = (
            0x10003000, 0x10002020, 0x10002000, 0x10002100, 0x10001400)
        set_event = self.base + self.word(self.export_table + 4*(225-self.ordinal_base))
        # stdcall worker(ctx): count iterations; command 1 signals ctx.event;
        # command 2 exits through the original system-thread start wrapper.
        # These are test program instructions, never replacements for a callee.
        code = bytearray.fromhex('568b742408')  # preserve ESI; load context
        loop = len(code)
        code += bytes.fromhex('ff068b460483f80274'); exit_fix = len(code); code += b'\0'
        code += bytes.fromhex('83f80175'); loop_fix = len(code); code += b'\0'
        code += bytes.fromhex('c74604000000006a00ff7608b8') + struct.pack('<I', set_event)
        code += bytes.fromhex('ffd0eb'); repeat_fix = len(code); code += b'\0'
        done = len(code); code += bytes.fromhex('5ec20400')
        for fix, target in ((exit_fix, done), (loop_fix, loop), (repeat_fix, loop)):
            code[fix] = (target-fix-1) & 255
        self.cpu.mem_write(entry, bytes(code))
        assert self.call(254, handle_out, 0, entry, context, 0) == 0
        handle = self.value(handle_out)
        assert self.call(246, handle, 0, handle_out+8) == 0
        worker = self.value(handle_out+8)
        assert self.call(148, worker, 30) == 8
        self.cpu.reg_write(UC_X86_REG_ECX, worker)
        self.call(250)  # Original fastcall ObfDereferenceObject.
        rows = []
        def wait(ordinal, *args):
            start = self.word(self.interrupt_time)
            before = self.value(context)
            self.running_clock = True
            try:
                status = self.call(ordinal, *args)
            finally:
                self.running_clock = False
            assert self.word(0x8003da64) == self.thread, 'original caller did not regain CPU'
            return dict(status=status, elapsed_100ns=self.word(self.interrupt_time)-start,
                        worker_iterations=self.value(context)-before)
        for lifetime in range(4):
            for kind in (0, 1):
                assert self.call(189, event_out, 0, kind, 0) == 0
                event = self.value(event_out)
                self.cpu.mem_write(context+8, struct.pack('<I', event))
                self.cpu.mem_write(timeout, struct.pack('<q', -100000))
                observation = wait(233, event, 0, timeout)
                assert observation['status'] == 0x102
                assert observation['worker_iterations'] > 0 and observation['elapsed_100ns'] >= 100000
                rows.append(dict(kind=kind, action='timeout', **observation))
                self.cpu.mem_write(context+4, struct.pack('<I', 1))
                observation = wait(234, event, 0, 0, timeout)
                assert observation['status'] == 0 and observation['worker_iterations'] > 0
                self.cpu.mem_write(timeout, bytes(8))
                assert self.call(233, event, 0, timeout) == (0x102 if kind else 0)
                rows.append(dict(kind=kind, action='worker_signal', **observation))
                assert self.call(187, event) == 0
        self.cpu.mem_write(timeout, struct.pack('<q', -100000))
        observation = wait(99, 0, 0, timeout)
        assert observation['status'] == 0 and observation['worker_iterations'] > 0
        assert observation['elapsed_100ns'] >= 100000
        rows.append(dict(action='delay', **observation))
        self.cpu.mem_write(context+4, struct.pack('<I', 2))
        self.cpu.mem_write(timeout, struct.pack('<q', -1000000))
        observation = wait(233, handle, 0, timeout)
        assert observation['status'] == 0, 'worker failed to terminate/join'
        assert self.call(187, handle) == 0
        return dict(waits=rows, joined=True, blocks=self.clock_blocks)

    def periodic_dpc(self):
        timer, dpc, callback, context = 0x10001000, 0x10001100, 0x10004000, 0x10004100
        # Authored DPC records all arguments and the actual PCR IRQL. No host
        # hook increments completion or substitutes for kernel DPC dispatch.
        self.cpu.mem_write(callback, bytes.fromhex(
            '8b4c2408ff018b4424048941048b44240c8941088b44241089410c'
            '640fb60524000000894110c21000'))
        self.call(107, dpc, callback, context)
        self.call(113, timer, 1)
        assert self.call(150, timer, 0xffffffff, 0xffffffff, 3, dpc) & 255 == 0
        callbacks = []
        previous = 0
        for tick in range(1, 20):
            self.advance()
            count = self.value(context)
            if count != previous:
                assert count == previous+1
                assert self.value(context+4) == dpc and self.value(context+16) == 2
                # Original KeTimerExpiration supplies its sampled system time.
                callback_time = self.value(context+8, '<Q')
                assert callback_time == self.word(self.system_time) + (self.word(self.system_time+4) << 32)
                assert self.value(timer+3, '<B') == 1, 'periodic timer not reinserted'
                callbacks.append(dict(tick=tick, system_time=callback_time))
                previous = count
        assert len(callbacks) >= 4
        assert self.call(97, timer) & 255 == 1
        for _ in range(6):
            self.advance()
        assert self.value(context) == previous, 'canceled periodic timer called again'
        return dict(callbacks=callbacks, canceled=True)


def run(snapshot, library, image, kernel):
    ref = TimerReference(snapshot, library, image, kernel)
    try:
        now = ref.word(ref.interrupt_time)
        rows = []
        timer = 0x10001000
        for kind in (0, 1):
            for duration in (1, 9999, 10000, 10001, 100000, 330000):
                ref.call(113, timer, kind)
                relative = (-duration) & 0xffffffffffffffff
                assert ref.call(149, timer, relative & 0xffffffff, relative >> 32, 0) & 255 == 0
                start = ref.word(ref.interrupt_time)
                for tick in range(1, 40):
                    ref.advance()
                    assert ref.word(ref.interrupt_time) == start + tick * 10000
                    if ref.value(timer+4):
                        break
                else:
                    raise AssertionError('timer did not expire')
                assert ref.value(timer+3, '<B') == 0
                assert tick == 1 + duration // 10000
                assert ref.word(ref.interrupt_time) >= start + duration
                ref.cpu.mem_write(0x10001400, bytes(8))
                assert ref.call(159, timer, 0, 0, 0, 0x10001400) == 0
                assert ref.call(159, timer, 0, 0, 0, 0x10001400) == (0x102 if kind else 0)
                assert ref.call(97, timer) & 255 == 0
                rows.append(dict(type=kind, duration_100ns=duration, expiry_ticks=tick))
        dpc = ref.periodic_dpc()
        workers = ref.workers()
        return dict(snapshot_sha256=SNAPSHOT_SHA, kernel_sha256=KERNEL_SHA,
                    clock_programming=ref.programming, clock_ticks=ref.ticks,
                    initial_interrupt_time=now, cases=rows, interrupts=ref.interrupts,
                    periodic_dpc=dpc, workers=workers)
    finally:
        ref.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('snapshot', 'library', 'image', 'kernel', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--dependencies', type=Path)
    args = parser.parse_args()
    dll_directory = os.add_dll_directory(str(args.dependencies.resolve())) if args.dependencies else None
    result = run(args.snapshot, args.library, args.image, args.kernel)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(expiry_cases=len(result['cases']), interrupts=result['interrupts'],
                         dpc_callbacks=len(result['periodic_dpc']['callbacks']),
                         worker_waits=len(result['workers']['waits']), joined=result['workers']['joined'])))
