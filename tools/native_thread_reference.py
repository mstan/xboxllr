#!/usr/bin/env python3
"""Execute original thread lifecycles with authored x86 worker programs.

Independent of the C/native callback bridge. No scheduler queues, kernel
callees, termination, thread objects or stack allocations are substituted.
"""
import argparse
import json
import os
import struct
from pathlib import Path
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP
from native_filesystem_reference import FilesystemReference
from native_io_device_reference import SNAPSHOT_SHA


class ThreadReference(FilesystemReference):
    def __init__(self, snapshot, library, image):
        self.entries = []
        super().__init__(snapshot, library, image)
        self.cpu.mem_write(0x10003000, bytes.fromhex('ff0520200010c20400'))
        terminate = self.base + self.word(self.export_table + 4*(258-self.ordinal_base))
        # Increment a real shared counter, then terminate through the original
        # export. INT3 is unreachable unless termination incorrectly returns.
        self.cpu.mem_write(0x10003100, bytes.fromhex('ff05202000106878563412b8') +
                           struct.pack('<I', terminate) + bytes.fromhex('ffd0cc'))

    def value(self, address):
        return struct.unpack('<I', self.cpu.mem_read(address, 4))[0]

    def block(self, cpu, address, size, user):
        super().block(cpu, address, size, user)
        if address not in (0x10003000, 0x10003100):
            return
        sp = cpu.reg_read(UC_X86_REG_ESP)
        current = self.word(0x8003da64)
        self.entries.append(dict(entry=address, return_pc=self.word(sp),
            context1=self.word(sp+4), context2=self.word(sp+8) if address == 0x10003100 else None,
            thread=current, stack_base=self.word(current+0x1c), stack_limit=self.word(current+0x20),
            tls=self.word(current+0x28), sp=sp, irql=self.word(0x8003da60)&255))

    def run(self):
        out, stats, count = 0x10002000, 0x10002200, 0x10002020
        self.cpu.mem_write(stats, struct.pack('<III', 12, 0, 0))
        assert self.call(256, stats) == 0
        baseline = (self.value(stats+4), self.value(stats+8))
        rows = []
        for lifetime in range(12):
            system = lifetime % 3 != 0
            tls_size = (64 if lifetime % 2 else 8192) if system and lifetime % 4 else 0
            if system:
                assert self.call(255, out, 32, 0 if lifetime % 2 else 0x7000,
                    tls_size, out+4, 0x13579bdf, 0x2468ace0, 1, 0, 0x10003100) == 0
            else:
                assert self.call(254, out, 0, 0x10003000, 0x13579bdf, 0) == 0
            handle = self.value(out)
            assert self.call(246, handle, 0, out+8) == 0
            thread = self.value(out+8)
            assert self.call(148, thread, 30) == 8
            if system:
                assert self.value(out+4) == self.word(thread+0x12c)
                assert self.call(231, handle, out+12) == 0 and self.value(out+12) == 1
                assert self.call(224, handle, out+12) == 0 and self.value(out+12) == 2
                assert self.call(224, handle, out+12) == 0 and self.value(out+12) == 1
            close_first = lifetime % 2 != 0
            if close_first:
                assert self.call(187, handle) == 0
            assert self.call(159, thread, 0, 0, 0, 0) == 0
            assert self.value(count) == lifetime+1 and len(self.entries) == lifetime+1
            row = self.entries[-1]
            assert row['thread'] == thread and row['irql'] == 0
            assert row['return_pc'] == (0x80020134 if system else 0x80025196)
            assert row['context1'] == 0x13579bdf
            assert not system or row['context2'] == 0x2468ace0
            assert row['stack_limit'] <= row['sp'] < row['stack_base']
            assert (row['stack_limit'] <= row['tls'] and row['tls']+tls_size <= row['stack_base']) if tls_size else not row['tls']
            assert self.word(thread+4) == 1
            assert self.word(thread+0x120) == (0x12345678 if system else 0)
            for va in range(row['stack_limit'], row['stack_base'], 4096):
                try:
                    self.physical(va)
                except ValueError as error:
                    assert str(error).startswith('absent PTE')
                else:
                    raise AssertionError('exited original stack still mapped')
            if not close_first:
                assert self.call(187, handle) == 0
            self.cpu.reg_write(UC_X86_REG_ECX, thread)
            self.call(250)
            assert self.call(256, stats) == 0
            assert (self.value(stats+4), self.value(stats+8)) == baseline
            rows.append(dict(**row, tls_bytes=tls_size, close_before_join=close_first,
                             exit_status=0x12345678 if system else 0, stack_retired=True))
        return dict(snapshot_sha256=SNAPSHOT_SHA, original_statistics=baseline, cases=rows)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('snapshot', 'library', 'image', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--dependencies', type=Path)
    args = parser.parse_args()
    dll_directory = os.add_dll_directory(str(args.dependencies.resolve())) if args.dependencies else None
    ref = ThreadReference(args.snapshot, args.library, args.image)
    try:
        result = ref.run()
    finally:
        ref.close()
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('PASS: 12 original x86 thread lifecycles, both start ABIs, TLS/stack, suspend counts, exit status, reference/close and original resource restoration')
