#!/usr/bin/env python3
"""Qualify original GPU callback delivery over NV2A/PIC and the original kernel.

Private, hash-bound NTSC inputs. Reuses the display family's actual interrupt
transport. Preowned miniport state bounds this to callback delivery, not device
initialization, complete title rendering or a silicon timing oracle. No original
callee is replaced; the authored cdecl consumer records its actual observations.
"""
import argparse
import gc
import hashlib
import json
from pathlib import Path
import struct
import traceback

from aot_inventory import parse_xbe
from native_gpu_completion_reference import XBE_SHA
from native_io_device_reference import SNAPSHOT_SHA
from native_vblank_reference import DisplayReference, Hardware
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
    UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP,
    UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EFLAGS)

GPU = 0xfd000000
DEVICE = 0x10001000
MINIPORT = DEVICE + 0x23c0
CALLBACK, COUNT, RECORDS = 0x10000400, 0x10000500, 0x10004000
STACK, STOP = 0x10008000, 0x10009000
# Revision metadata, isolated from shared runtime interfaces. Inputs are hash
# checked before these addresses are used. Original bytes are never distributed.
ENTRIES = dict(insert=0x138a40, kick=0x1384b0, isr=0x13b210,
               dpc=0x13bbc0, graph=0x13b950, software_method=0x13b6c0)


class CallbackReference(DisplayReference):
    def __init__(self, args, hardware):
        super().__init__(args.snapshot, hardware)
        blob = args.xbe.read_bytes()
        image = parse_xbe(blob)
        if image['sha256'] != XBE_SHA:
            raise ValueError('unreviewed NTSC image')
        self.cpu.mem_map(image['base'], (image['image_size'] + 4095) & ~4095)
        self.cpu.mem_write(image['base'], blob[:struct.unpack_from('<I', blob, 0x108)[0]])
        for section in image['sections']:
            if section['preload'] and section['raw_size']:
                offset = section['raw_offset']
                self.cpu.mem_write(section['address'], blob[offset:offset + section['raw_size']])
        thunk = struct.unpack_from('<I', blob, 0x158)[0] ^ 0x5b6d40b6
        for index in range(512):
            address = thunk + index * 4
            ordinal = self.get(address)[0]
            if not ordinal:
                break
            if ordinal & 0xffff0000 != 0x80000000:
                raise ValueError('invalid original import')
            self.put(address, self.base + self.word(
                self.export_table + 4 * ((ordinal & 0xffff) - self.ordinal_base)))
        else:
            raise ValueError('unterminated original imports')
        self.accesses = []
        self.cpu.mmio_map(GPU, 0x1000000, self.mmio_read, None, self.mmio_write, None)
        hardware.call('memory', self.host, len(self.ram))
        self.allocations = []
        self.connected = False
        self.executed = {name: 0 for name in ENTRIES}
        for name, address in ENTRIES.items():
            self.cpu.hook_add(UC_HOOK_CODE, self.record_entry, name, address, address)

    def record_entry(self, cpu, address, size, name):
        self.executed[name] += 1

    def put(self, address, *values):
        self.map_owned(address, 4 * len(values))
        self.cpu.mem_write(address, struct.pack('<' + 'I' * len(values), *values))

    def get(self, address, count=1):
        self.map_owned(address, 4 * count)
        return list(struct.unpack('<' + 'I' * count, self.cpu.mem_read(address, 4 * count)))

    def map_owned(self, address, size):
        # Host mem_read/write do not invoke Unicorn's guest fault hook. Map
        # real original-kernel PTEs for owned buffers before fixture access.
        if address < 0x80000000:
            return
        for page in range(address & ~4095, (address + size + 4095) & ~4095, 4096):
            if page not in self.pages and not self.map_page(self.cpu, 0, page, 4096, 0, None):
                raise RuntimeError(self.failure)

    def mmio_read(self, cpu, offset, size, data):
        try:
            value = self.hardware.call('read', GPU + offset, size)
            self.accesses.append(['read', offset, size, value])
            return value
        except Exception as error:
            self.stop(str(error))
            return 0  # Transaction failed; the original call must not return.

    def mmio_write(self, cpu, offset, size, value, data):
        self.accesses.append(['write', offset, size, value])
        try:
            self.hardware.call('write', GPU + offset, size, value)
        except Exception as error:
            self.stop(str(error))

    def read(self, offset):
        return self.hardware.call('read', GPU + offset, 4)

    def write(self, offset, value):
        self.hardware.call('write', GPU + offset, 4, value)

    def original(self, name, *args):
        self.put(STACK, STOP, *args)
        self.cpu.reg_write(UC_X86_REG_ESP, STACK)
        self.cpu.reg_write(UC_X86_REG_ECX, DEVICE)
        self.cpu.reg_write(UC_X86_REG_EFLAGS, 0x202)
        saved = {reg: 0xabc000 + n for n, reg in enumerate((
            UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP))}
        for reg, value in saved.items():
            self.cpu.reg_write(reg, value)
        self.failure = None
        self.cpu.emu_start(ENTRIES[name], STOP, count=1000000)
        self.check_return()
        assert self.cpu.reg_read(UC_X86_REG_ESP) == STACK + 4 * (1 + len(args))
        assert all(self.cpu.reg_read(reg) == value for reg, value in saved.items())
        return self.cpu.reg_read(UC_X86_REG_EAX)

    def check_return(self):
        if self.failure or self.cpu.reg_read(UC_X86_REG_EIP) != STOP:
            raise RuntimeError(self.failure or 'original routine did not return')

    def emit(self, method, *values):
        cursor = self.get(DEVICE)[0]
        assert cursor + 4 + 4 * len(values) <= self.ring + 4096
        self.put(cursor, (len(values) << 18) | method, *values)
        self.put(DEVICE, cursor + 4 + 4 * len(values))

    def connect(self):
        assert not self.connected
        assert self.call(98, MINIPORT + 0x14) & 255
        self.connected = True

    def disconnect(self):
        assert self.connected
        assert self.call(100, MINIPORT + 0x14) & 255
        self.connected = False

    def statistics(self):
        self.put(0x10000600, 0xdeadbeef, 36, *([0] * 8), 0xaabbccdd)
        assert self.call(181, 0x10000604) == 0
        values = self.get(0x10000600, 11)
        assert values[0] == 0xdeadbeef and values[-1] == 0xaabbccdd
        assert values[1:3] == [36, 16384]
        return values[1:-1]

    def setup(self):
        self.baseline_statistics = self.statistics()
        self.ring = self.call(165, 4096)
        assert self.ring
        self.allocations.append(self.ring)
        ring_pa = self.call(173, self.ring)
        self.semaphore = self.call(165, 4096)
        assert self.semaphore
        self.allocations.append(self.semaphore)
        self.semaphore_pa = self.call(173, self.semaphore)
        self.put(self.semaphore, 0)
        self.put(0x141b78, DEVICE)
        self.put(DEVICE, self.ring, self.ring + 4096, 0)
        self.put(DEVICE + 0x23bc, GPU + 0x800000, GPU)
        self.put(MINIPORT + 0xa0, 1)
        self.put(MINIPORT + 0xb4, 1)
        self.call(107, MINIPORT + 0x84, ENTRIES['dpc'], MINIPORT)
        self.call(109, MINIPORT + 0x14, ENTRIES['isr'], MINIPORT, 0x33, 23, 0, 0)
        self.connect()
        # Actual authored cdecl caller: context, completed semaphore, IRQL,
        # and PGRAPH puller state. Registers it must preserve are saved here.
        # It does not acknowledge an IRQ or manufacture completion.
        code = b'\x53\x56\x8b\x1d' + struct.pack('<I', COUNT)
        code += b'\xc1\xe3\x04\x8d\xb3' + struct.pack('<I', RECORDS)
        code += b'\x8b\x44\x24\x0c\x89\x06'
        code += b'\xa1' + struct.pack('<I', self.semaphore) + b'\x89\x46\x04'
        code += b'\x0f\xb6\x05' + struct.pack('<I', 0x8003da60) + b'\x89\x46\x08'
        code += b'\xa1' + struct.pack('<I', GPU + 0x400720) + b'\x89\x46\x0c'
        code += b'\xff\x05' + struct.pack('<I', COUNT) + b'\x5e\x5b\xc3'
        self.cpu.mem_write(CALLBACK, code)
        self.put(COUNT, 0)
        for offset, value in ((0x700008, 1), (0x70000c, 0x80010020),
                (0x700200, 0x97), (0x700010, 2), (0x700014, 0x80010030),
                (0x700300, 0x3d), (0x700304, len(self.ram) - 1), (0x700308, 0),
                (0x700180, 0x3d), (0x700184, len(self.ram) - 1), (0x700188, 0),
                (0x002504, 1), (0x003204, 0x100), (0x00322c, 0x18),
                (0x003240, ring_pa), (0x003244, ring_pa), (0x003220, 1),
                (0x003250, 1), (0x003200, 1), (0x000140, 1), (0x400140, 1 << 20)):
            self.write(offset, value)
        self.emit(0, 1)
        self.emit(0x01a4, 2)
        self.emit(0x1d6c, self.semaphore_pa)
        self.original('kick')
        assert self.hardware.call('vector', 0) == -1

    def deliver(self):
        assert self.hardware.call('vector', 0) == 0x33
        registers = (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                     UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI)
        saved = {reg: 0xabcdef00 + i for i, reg in enumerate(registers)}
        for reg, value in saved.items():
            self.cpu.reg_write(reg, value)
        entry = self.inject(0x33, STOP, STACK, 0x202)
        assert self.hardware.call('vector', 1) == 0x33
        self.failure = None
        self.cpu.emu_start(entry, STOP, count=1000000)
        self.check_return()
        assert all(self.cpu.reg_read(reg) == value for reg, value in saved.items())
        assert self.cpu.reg_read(UC_X86_REG_ESP) == STACK
        assert self.cpu.reg_read(UC_X86_REG_EFLAGS) == 0x202
        assert self.word(0x8003da60) & 255 == 0
        assert self.hardware.call('vector', 0) == -1
        assert not self.hardware.call('irq')
        assert self.read(0x400100) == 0 and self.read(0x400720) == 1

    def qualify(self, rows):
        self.setup()
        # Paused puller, masked device IRQ, disconnected/reconnected original
        # interrupt object, and a mixed burst all share real original packets.
        expected = []
        for case, kinds in (('puller-held', [0, 1]), ('irq-masked', [1, 0]),
                            ('disconnect-reconnect', [0, 1]), ('burst', [0, 1] * 35)):
            before = len(expected)
            entries_before = self.executed.copy()
            if case == 'puller-held':
                self.write(0x400720, 0)
            elif case == 'irq-masked':
                self.write(0x000140, 0)
            elif case == 'disconnect-reconnect':
                self.disconnect()
            for kind in kinds:
                token = len(expected) + 1
                context = (0, 0xffffffff, 0x12345678, 0x80000000)[(token - 1) % 4]
                self.emit(0x1d70, token)
                self.original('insert', kind, CALLBACK, context)
                expected.append([context, token, 2, 0])
            self.emit(0x1d70, 0xf0000000 + len(expected))
            self.original('kick')
            assert self.get(COUNT) == [before]
            if case != 'burst':
                assert self.hardware.call('vector', 0) == -1
                if case == 'puller-held':
                    assert not self.hardware.call('irq') and self.get(self.semaphore) == [0]
                    self.write(0x400720, 1)
                elif case == 'irq-masked':
                    assert self.read(0x400100) == 1 << 20
                    self.write(0x000140, 1)
                else:
                    assert self.hardware.call('irq')
                    self.connect()
            self.deliver()
            assert self.get(COUNT) == [len(expected)]
            observed = [self.get(RECORDS + i * 16, 4) for i in range(len(expected))]
            assert observed == expected, (case, observed, expected)
            final = 0xf0000000 + len(expected)
            assert self.get(self.semaphore) == [final]
            assert self.read(0x800044) == self.get(DEVICE)[0] & 0x0fffffff
            # A later ordinary kernel call and a repeated kick must not replay
            # completed callbacks or leave an interrupt/DPC pending.
            self.call(125)  # Original KeQueryInterruptTime, no arguments.
            self.original('kick')
            assert self.get(COUNT) == [len(expected)]
            assert self.hardware.call('vector', 0) == -1
            assert self.executed['isr'] > entries_before['isr']
            assert self.executed['dpc'] > entries_before['dpc']
            assert self.executed['software_method'] - entries_before['software_method'] == len(kinds)
            rows.append(dict(case=case, kinds=kinds, callbacks=observed[before:],
                             final_semaphore=final, irq_and_original_dpc=True,
                             interrupted_registers_preserved=True, no_duplicate_delivery=True))
        self.disconnect()
        for offset in (0x000140, 0x400140, 0x003220, 0x003250):
            self.write(offset, 0)
        for allocation in reversed(self.allocations):
            self.call(171, allocation)
        self.allocations.clear()
        self.final_statistics = self.statistics()
        assert self.final_statistics == self.baseline_statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'snapshot', 'library', 'functions', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    result = dict(xbe_sha256=XBE_SHA, snapshot_sha256=SNAPSHOT_SHA,
                  library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest(),
                  scope='Original InsertCallback/kick + real PFIFO/DMA/PIC/original kernel gate/ISR/DPC/IRET; preowned driver, not title startup or asynchronous draw timing',
                  lifetimes=[], error=None)
    functions = {int(row['start'], 16): row for row in json.loads(args.functions.read_text())}
    for lifetime in range(3):
        hardware = None
        ref = None
        rows = []
        lifetime_result = dict(cases=rows)
        result['lifetimes'].append(lifetime_result)
        try:
            hardware = Hardware(args.library)
            ref = CallbackReference(args, hardware)
            result['functions'] = {name: dict(address=address, sha256=hashlib.sha256(bytes(
                ref.cpu.mem_read(address, functions[address]['size']))).hexdigest())
                for name, address in ENTRIES.items()}
            ref.qualify(rows)
            lifetime_result.update(executed=ref.executed,
                baseline_statistics=ref.baseline_statistics,
                final_statistics=ref.final_statistics, original_irq_disconnected=True)
        except Exception as error:
            result.update(error=f'{type(error).__name__}: {error}', traceback=traceback.format_exc())
        finally:
            if ref is not None:
                result['mmio'] = ref.accesses
            if hardware is not None:
                hardware.close()
            ref = None
            hardware = None
            gc.collect()
        if result['error']:
            break
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(error=result['error'], lifetimes=len(result['lifetimes']),
                          cases=sum(len(row['cases']) for row in result['lifetimes']))))
    return int(result['error'] is not None)


if __name__ == '__main__':
    raise SystemExit(main())
