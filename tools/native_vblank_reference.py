#!/usr/bin/env python3
"""Execute original Blinx miniport vblank handling against maintained NV2A.

This is a bounded software/device qualification, not the complete title display
route or a hardware timing oracle. Original kernel event signaling executes;
the authored guest callback records its real arguments. No original callee is
replaced. Owned inputs and observations remain private.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import struct

from aot_inventory import parse_xbe
from native_input_binding import IMAGE
from native_io_device_reference import Reference, SNAPSHOT_SHA
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP,
                               UC_X86_REG_ESP, UC_X86_REG_EFLAGS)


class Hardware:
    def __init__(self, library):
        self.directories = []
        if os.name == 'nt':
            for directory in (library.parent, Path('C:/msys64/mingw64/bin')):
                if directory.is_dir():
                    self.directories.append(os.add_dll_directory(str(directory.resolve())))
        self.dll = ctypes.CDLL(str(library.resolve()))
        signatures = {
            'create': (ctypes.c_void_p, []),
            'destroy': (None, [ctypes.c_void_p]),
            'error': (ctypes.c_char_p, [ctypes.c_void_p]),
            'memory': (None, [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32]),
            'read': (ctypes.c_uint32, [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int]),
            'write': (None, [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]),
            'frame': (None, [ctypes.c_void_p, ctypes.c_uint64]),
            'irq': (ctypes.c_int, [ctypes.c_void_p]),
            'pic': (None, [ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint16]),
            'port': (ctypes.c_uint32, [ctypes.c_void_p, ctypes.c_uint16,
                      ctypes.c_int, ctypes.c_int, ctypes.c_uint32]),
            'field': (None, [ctypes.c_void_p, ctypes.c_int]),
            'vector': (ctypes.c_int, [ctypes.c_void_p, ctypes.c_int]),
        }
        self.functions = {}
        for name, (result, arguments) in signatures.items():
            function = getattr(self.dll, 'xlle_nv2a_reference_' + name)
            function.restype, function.argtypes = result, arguments
            self.functions[name] = function
        self.instance = self.functions['create']()
        if not self.instance:
            raise RuntimeError('NV2A reference allocation failed')

    def call(self, name, *args):
        result = self.functions[name](self.instance, *args)
        error = self.functions['error'](self.instance)
        if error:
            raise RuntimeError(error.decode())
        return result

    def close(self):
        self.functions['destroy'](self.instance)


class DisplayReference(Reference):
    def __init__(self, snapshot, hardware):
        self.hardware = hardware
        super().__init__(snapshot)
        hardware.call('pic', self.word(0x8003d88c) & 65535, self.word(0x8003d890) & 65535)
        self.enable_kernel_segments()

    def port_in(self, cpu, port, size, user):
        try:
            return self.hardware.call('port', port, size, 0, 0)
        except Exception as error:
            self.stop(str(error))
            return 0  # Rejected emulator transaction, not a returned result.

    def port_out(self, cpu, port, size, value, user):
        try:
            self.hardware.call('port', port, size, 1, value)
        except Exception as error:
            self.stop(str(error))

    def inject(self, vector, pc, stack, flags):
        if not 0x30 <= vector < 0x40:
            raise ValueError('outside connected PIC vectors')
        low = self.word(0x8003de40+(vector-0x30)*8)
        high = self.word(0x8003de44+(vector-0x30)*8)
        assert low >> 16 == 8 and high & 65535 == 0x8e00
        entry = (low & 65535) | (high & 0xffff0000)
        self.cpu.mem_write(stack-12, struct.pack('<III', pc, 8, flags))
        self.cpu.reg_write(UC_X86_REG_ESP, stack-12)
        self.cpu.reg_write(UC_X86_REG_EFLAGS, flags & ~0x20300)
        self.cpu.reg_write(UC_X86_REG_EIP, entry)
        return entry

    def interrupt(self, cpu, number, user):
        if number == 0x33:
            self.inject(number, cpu.reg_read(UC_X86_REG_EIP),
                        cpu.reg_read(UC_X86_REG_ESP), cpu.reg_read(UC_X86_REG_EFLAGS))
        else:
            super().interrupt(cpu, number, user)


def run(xbe, snapshot, library):
    blob = xbe.read_bytes()
    image = parse_xbe(blob)
    if image['sha256'] != IMAGE:
        raise ValueError('unreviewed original display image')
    hardware = Hardware(library)
    ref = DisplayReference(snapshot, hardware)
    cpu = ref.cpu
    cpu.mem_map(image['base'], (image['image_size']+4095) & ~4095)
    cpu.mem_write(image['base'], blob[:struct.unpack_from('<I', blob, 0x108)[0]])
    for section in image['sections']:
        offset, size = section['raw_offset'], section['raw_size']
        cpu.mem_write(section['address'], blob[offset:offset+size])
    thunk = struct.unpack_from('<I', blob, 0x158)[0] ^ 0x5b6d40b6
    for _ in range(512):
        ordinal = struct.unpack('<I', cpu.mem_read(thunk, 4))[0]
        if not ordinal:
            break
        if ordinal & 0xffff0000 != 0x80000000:
            raise ValueError('invalid original import')
        target = ref.base + ref.word(ref.export_table+4*((ordinal & 0xffff)-ref.ordinal_base))
        cpu.mem_write(thunk, struct.pack('<I', target))
        thunk += 4
    else:
        raise ValueError('unterminated original imports')

    def put(address, value):
        cpu.mem_write(address, struct.pack('<I', value))

    def word(address):
        return struct.unpack('<I', cpu.mem_read(address, 4))[0]

    def call(entry, *args, ecx=0):
        stack, end = 0x10008000, 0x10009000
        cpu.mem_write(stack, struct.pack('<'+'I'*(1+len(args)), end, *args))
        cpu.reg_write(UC_X86_REG_ESP, stack)
        cpu.reg_write(UC_X86_REG_ECX, ecx)
        cpu.reg_write(UC_X86_REG_EFLAGS, 0x202)
        ref.failure = None
        cpu.emu_start(entry, end, count=1000000)
        if ref.failure or cpu.reg_read(UC_X86_REG_EIP) != end:
            raise RuntimeError(ref.failure or 'original display routine did not return')
        assert cpu.reg_read(UC_X86_REG_ESP) == stack+4*(1+len(args))
        return cpu.reg_read(UC_X86_REG_EAX)

    hardware.call('memory', ref.host, len(ref.ram))
    accesses = []
    cpu.mem_map(0xfd000000, 0x1000000)

    def read_mmio(engine, access, address, size, value, user):
        try:
            result = hardware.call('read', address, size)
            engine.mem_write(address, result.to_bytes(size, 'little'))
            accesses.append(dict(write=False, address=hex(address), size=size, value=result))
        except Exception as error:
            ref.stop(str(error))

    def write_mmio(engine, access, address, size, value, user):
        try:
            hardware.call('write', address, size, value)
            accesses.append(dict(write=True, address=hex(address), size=size, value=value))
        except Exception as error:
            ref.stop(str(error))

    cpu.hook_add(UC_HOOK_MEM_READ, read_mmio, None, 0xfd000000, 0xfdffffff)
    cpu.hook_add(UC_HOOK_MEM_WRITE, write_mmio, None, 0xfd000000, 0xfdffffff)
    device, callback, out = 0x10001000, 0x10000500, 0x10000600
    miniport = device+0x23c0
    # An actual authored cdecl callback records three caller-visible words.
    code = b'\x8b\x44\x24\x04'
    for offset in (0, 4, 8):
        code += b'\x8b\x50'+bytes([offset])+b'\x89\x15'+struct.pack('<I', out+offset)
    code += b'\xff\x05'+struct.pack('<I', out+12)+b'\xc3'
    cpu.mem_write(callback, code)
    put(0x141b78, device)
    put(miniport, 0xfd000000)
    # Authored display-mode input: the original handler does not query the TV
    # encoder field port with this bit set. Mode selection and field timing
    # remain outside this first fixture; no port value is fabricated.
    put(miniport+0x1b4, 0x01000000)
    ref.call(108, miniport+0x194, 0, 0)
    call(0x1320d0, callback)
    assert word(miniport+0x190) == callback
    call(0x13f020, ecx=miniport)
    hardware.call('write', 0xfd000140, 4, 1)
    hardware.call('write', 0xfd6013d4, 1, 0x2a)
    rows = []
    try:
        for frame in range(1, 5):
            ref.call(108, miniport+0x194, 0, 0)
            hardware.call('frame', frame)
            assert hardware.call('irq')
            assert hardware.call('read', 0xfd600100, 4) == 1
            if frame == 3:
                put(miniport+0x1c4, frame)  # Original missed-swap marker.
            call(0x13b400, ecx=miniport)
            observed = list(struct.unpack('<4I', cpu.mem_read(out, 16)))
            assert observed == [frame, 0, 2 if frame >= 3 else 0, frame], observed
            assert word(miniport+0x198) == 1  # Real original event signaled.
            assert not hardware.call('irq')
            assert hardware.call('read', 0xfd600100, 4) == 0
            assert hardware.call('read', 0xfd6013d4, 1) == 0x2a
            rows.append(dict(frame=frame, callback=observed[:3], event=word(miniport+0x198)))
        call(0x1320d0, 0)
        hardware.call('frame', 5)
        call(0x13b400, ecx=miniport)
        assert word(out+12) == 4 and word(miniport+0x1c0) == 5
        # Actual PFIFO packets configure the two-surface flip counter. The
        # pusher layout is reused from tests/test_nv2a.cpp, with RAM obtained
        # from the original allocator rather than overwriting captured pages.
        allocation = ref.call(165, 4096)
        assert allocation
        physical = ref.call(173, allocation)
        packet = [0x40000, 1, 0xc0120, 0, 0, 2]
        ctypes.memmove(ref.host+physical, struct.pack('<6I', *packet), 24)
        for address, value in [(0x700008, 1), (0x70000c, 0x80010020),
                (0x700200, 0x97), (0x700180, 0x3d), (0x700184, 4095),
                (0x700188, physical), (0x002504, 1), (0x003204, 0x100),
                (0x00322c, 0x18), (0x003244, 0), (0x003220, 1),
                (0x003250, 1), (0x003200, 1), (0x800040, 24)]:
            hardware.call('write', 0xfd000000+address, 4, value)
        assert hardware.call('read', 0xfd400710, 4) == 0x20000000
        call(0x1320d0, callback)
        scanout = 0x400000
        call(0x13b6c0, ((scanout | 1) << 5) | 1, ecx=miniport)
        assert word(miniport+0x1c8) == 6 and word(miniport+0x1c4) == 7
        for frame, flags in ((6, 1), (7, 2), (8, 2)):
            hardware.call('frame', frame)
            call(0x13b400, ecx=miniport)
            observed = list(struct.unpack('<3I', cpu.mem_read(out, 12)))
            assert observed == [frame, 1, flags], observed
            rows.append(dict(frame=frame, callback=observed,
                             event=word(miniport+0x198), after_paced_swap=True))
        assert hardware.call('read', 0xfd600800, 4) == scanout
        assert hardware.call('read', 0xfd400710, 4) == 0x21000000
        # Immediate submission retires without waiting for a future edge.
        call(0x13b6c0, (((scanout+4096) | 8) << 5) | 1, ecx=miniport)
        assert word(miniport+0x1bc) == 2
        assert hardware.call('read', 0xfd600800, 4) == scanout+4096
        hardware.call('frame', 9)
        call(0x13b400, ecx=miniport)
        immediate = list(struct.unpack('<3I', cpu.mem_read(out, 12)))
        assert immediate == [9, 2, 0], immediate
        rows.append(dict(frame=9, callback=immediate, after_immediate_swap=True))
        # Connect the real original ISR and DPC, using original kernel object
        # initialization and interrupt connection, then assert actual IRQ3.
        put(miniport+0xa0, 1)
        put(miniport+0xb4, 1)
        put(miniport+0x1b4, 0)
        ref.call(107, miniport+0x84, 0x13bbc0, miniport)
        ref.call(109, miniport+0x14, 0x13b210, miniport, 0x33, 23, 0, 0)
        assert ref.call(98, miniport+0x14) & 255
        hardware.call('field', 1)
        hardware.call('frame', 10)
        assert hardware.call('vector', 0) == 0x33
        entry = ref.inject(0x33, 0x10009000, 0x10008000, 0x202)
        assert hardware.call('vector', 1) == 0x33
        ref.failure = None
        cpu.emu_start(entry, 0x10009000, count=1000000)
        if ref.failure or cpu.reg_read(UC_X86_REG_EIP) != 0x10009000:
            raise RuntimeError(ref.failure or 'original display IRQ did not return')
        irq_callback = list(struct.unpack('<3I', cpu.mem_read(out, 12)))
        assert irq_callback == [10, 2, 0], irq_callback
        assert ref.word(0x8003da60) & 255 == 0
        assert cpu.reg_read(UC_X86_REG_ESP) == 0x10008000
        assert cpu.reg_read(UC_X86_REG_EFLAGS) == 0x202
        assert hardware.call('vector', 0) == -1
        assert not hardware.call('irq')
        rows.append(dict(frame=10, callback=irq_callback, original_irq_and_dpc=True))
        assert ref.call(100, miniport+0x14) & 255
        ref.call(171, allocation)
    finally:
        hardware.close()
    return dict(xbe_sha256=IMAGE, snapshot_sha256=SNAPSHOT_SHA, cases=rows,
                unregistration=True, mmio=accesses,
                scope='Original full vblank and software-method swap handlers, callback registration/removal, event signaling, actual IRQ3/PIC/original gate/ISR/DPC/IRET, PCRTC acknowledgement and flip counter/PCRTC_START effects. Encoder field is an authored external pin input. Blocking waits, scanout pixels, field timing and full title integration remain outside this fixture.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'snapshot', 'library', 'output'):
        parser.add_argument('--'+name, required=True, type=Path)
    args = parser.parse_args()
    args.output.write_text(json.dumps(run(args.xbe, args.snapshot, args.library), indent=2)+'\n')
    print('PASS: original vblank callback/event/acknowledgement over maintained NV2A')
