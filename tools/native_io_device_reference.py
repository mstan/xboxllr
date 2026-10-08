#!/usr/bin/env python3
"""Private original-kernel device lifecycle reference; no replaced callees.

Uses a hash-bound initialized RAM capture, including its real page tables,
allocator and object namespace. This does not establish kernel boot, USB,
interrupt delivery or the caller's driver callbacks. Those paths fail visibly.
The capture and generated observations must remain private.
"""
import argparse
import ctypes
import hashlib
import json
import struct
from pathlib import Path
from unicorn import (Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_ALL,
                     UC_HOOK_MEM_INVALID, UC_HOOK_INSN, UC_HOOK_INTR)
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_ESP,
                               UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_EFLAGS)

SNAPSHOT_SHA = '31ede2a0e799243b3d39033a0127147b7631950087c05306e64b4203e6ea9ed1'


class Reference:
    def __init__(self, snapshot):
        data = snapshot.read_bytes()
        if hashlib.sha256(data).hexdigest() != SNAPSHOT_SHA:
            raise ValueError('unreviewed initialized RAM capture')
        self.ram = ctypes.create_string_buffer(data, len(data))
        self.host = ctypes.addressof(self.ram)
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.cpu.mem_map(0x10000000, 0x10000)
        self.pages = {}
        self.failure = None
        self.cpu.hook_add(UC_HOOK_MEM_INVALID, self.map_page)
        self.cpu.hook_add(UC_HOOK_INSN, self.port_in, None, 1, 0, UC_X86_INS_IN)
        self.cpu.hook_add(UC_HOOK_INSN, self.port_out, None, 1, 0, UC_X86_INS_OUT)
        self.cpu.hook_add(UC_HOOK_INTR, self.interrupt)
        self.base = 0x80010000
        pe = self.word(self.base + 0x3c)
        exports = self.base + self.word(self.base + pe + 120)
        self.ordinal_base = self.word(exports + 16)
        self.export_table = self.base + self.word(exports + 28)

    def physical(self, va):
        def word(pa):
            if pa + 4 > len(self.ram):
                raise ValueError('page table outside RAM')
            return struct.unpack_from('<I', self.ram, pa)[0]
        pde = word(0xf000 + (va >> 22) * 4)
        if not pde & 1:
            raise ValueError(f'absent PDE {va:08x}')
        if pde & 128:
            return (pde & 0xffc00000) + (va & 0x3fffff)
        pte = word((pde & 0xfffff000) + ((va >> 12) & 1023) * 4)
        if not pte & 1:
            raise ValueError(f'absent PTE {va:08x}')
        return (pte & 0xfffff000) + (va & 4095)

    def map_page(self, cpu, access, address, size, value, user):
        try:
            page = address & ~4095
            pa = self.physical(page)
            if pa + 4096 > len(self.ram) or page in self.pages:
                raise ValueError(f'unsupported mapping {address:08x}')
            cpu.mem_map_ptr(page, 4096, UC_PROT_ALL, self.host + pa)
            self.pages[page] = pa
            return True
        except ValueError as exc:
            self.failure = str(exc)
            return False

    def stop(self, message):
        self.failure = message
        self.cpu.emu_stop()

    def port_in(self, cpu, port, size, user):
        self.stop(f'unmodelled port read {port:04x}')
        return 0  # Emulator callback ABI only; the call is rejected, never returned.

    def port_out(self, cpu, port, size, value, user):
        self.stop(f'unmodelled port write {port:04x}')

    def interrupt(self, cpu, number, user):
        self.stop(f'unmodelled interrupt {number}')

    def word(self, va):
        return struct.unpack_from('<I', self.ram, self.physical(va))[0]

    def call(self, ordinal, *args):
        cpu = self.cpu
        sp, end = 0x10008000, 0x10009000
        entry = self.base + self.word(self.export_table + 4 * (ordinal - self.ordinal_base))
        cpu.mem_write(sp, struct.pack('<' + 'I' * (len(args) + 1), end, *args))
        cpu.reg_write(UC_X86_REG_ESP, sp)
        cpu.reg_write(UC_X86_REG_EFLAGS, 0x202)
        self.failure = None
        cpu.emu_start(entry, end, count=1000000)
        if self.failure or cpu.reg_read(UC_X86_REG_EIP) != end:
            raise RuntimeError(self.failure or 'original call did not return')
        if cpu.reg_read(UC_X86_REG_ESP) != sp + 4 * (len(args) + 1):
            raise RuntimeError('original stdcall stack imbalance')
        # The original allocator may retire or reuse a pool virtual page.
        for page, old_pa in list(self.pages.items()):
            try:
                new_pa = self.physical(page)
            except ValueError:
                new_pa = None
            if new_pa != old_pa:
                cpu.mem_unmap(page, 4096)
                del self.pages[page]
        return cpu.reg_read(UC_X86_REG_EAX)

    def create(self, extension, kind=0x22, exclusive=0, name=None):
        name_pointer = 0
        if name is not None:
            encoded = name.encode('ascii')
            self.cpu.mem_write(0x10000400, encoded + b'\0')
            self.cpu.mem_write(0x10000300, struct.pack('<HHI', len(encoded), len(encoded) + 1, 0x10000400))
            name_pointer = 0x10000300
        self.cpu.mem_write(0x10000200, b'\xcd' * 4)
        result = self.call(65, 0x10000100, extension, name_pointer, kind, exclusive, 0x10000200)
        pointer = struct.unpack('<I', self.cpu.mem_read(0x10000200, 4))[0]
        return result, pointer


def run(snapshot):
    reference = Reference(snapshot)
    rows = []
    for extension, kind, exclusive, name in [
        (0, 0x22, 0, None), (12, 0x22, 1, None),
        (0x170, 0x3a, 0, r'\Device\XLLE_InputReference'),
        (0x170, 0x3a, 0, r'\Device\XLLE_InputReference'),
        (8, 0x22, 0, r'\MissingParent\XLLE_InputReference'),
    ]:
        result, pointer = reference.create(extension, kind, exclusive, name)
        row = dict(extension=extension, kind=kind, exclusive=exclusive, name=name,
                   status=f'{result:08x}', pointer=f'{pointer:08x}')
        if pointer:
            row['body'] = bytes(reference.cpu.mem_read(pointer, 0x48 + extension)).hex()
        rows.append(row)
    for row in rows:
        pointer = int(row['pointer'], 16)
        if pointer:
            reference.call(68, pointer)
    status, pointer = reference.create(0x170, 0x3a, 0, r'\Device\XLLE_InputReference')
    assert status == 0 and pointer, 'deleted name must be reusable'
    reference.call(68, pointer)
    live = []
    for index in range(32):
        status, pointer = reference.create(0x170, 0x3a, 0, rf'\Device\XLLE_Concurrent{index}')
        assert status == 0 and pointer
        reference.cpu.mem_write(pointer + 0x48, struct.pack('<I', 0x12345678 + index))
        live.append(pointer)
    for index, pointer in enumerate(live):
        assert bytes(reference.cpu.mem_read(pointer + 0x48, 4)) == struct.pack('<I', 0x12345678 + index)
        reference.call(68, pointer)
    status, pointer = reference.create(0x170, 0x3a, 0, r'\Device\XLLE_Concurrent0')
    assert status == 0 and pointer
    reference.call(68, pointer)
    return dict(snapshot_sha256=SNAPSHOT_SHA, rows=rows,
                delete_and_recreate=True, concurrent_devices=len(live), mapped_pages=len(reference.pages))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = run(args.snapshot)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: value for key, value in result.items() if key != 'rows'}))
