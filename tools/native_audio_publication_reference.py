#!/usr/bin/env python3
"""Whole original ordinary voice allocation/publication -> maintained processing.

Prepared device/settings/IRQ context is explicit. Original callees are not
replaced. This is not an active FE queue, GP/EP output or enclosing title proof.
"""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import struct

from native_ep_directory_reference import OriginalProducer
from native_ep_reference import IMAGE_SHA256
from unicorn import UC_HOOK_MEM_READ
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP


class Publication(OriginalProducer):
    def __init__(self, blob, lifetime, ram, transaction):
        super().__init__(blob, lifetime)
        self.ram, self.transaction = ram, transaction
        self.cpu.mem_map(0xfe820000, 0x1000)
        self.cpu.mem_map(0x31000000, len(ram))
        self.cpu.hook_add(UC_HOOK_MEM_READ, self._read)

    def _read(self, cpu, access, address, size, value, unused):
        if 0xfe800000 <= address < 0xfe880000:
            assert size == 4
            cpu.mem_write(address, self.transaction('R', address, size, 0).to_bytes(size, 'little'))
        elif 0x31000000 <= address < 0x31000000+len(self.ram):
            offset = address-0x31000000
            assert offset+size <= len(self.ram)
            cpu.mem_write(address, bytes(self.ram[offset:offset+size]))

    def _write(self, cpu, access, address, size, value, unused):
        if 0xfe800000 <= address < 0xfe880000:
            self.transaction('W', address, size, value)
        elif 0x31000000 <= address < 0x31000000+len(self.ram):
            offset = address-0x31000000
            assert offset+size <= len(self.ram)
            self.ram[offset:offset+size] = value.to_bytes(size, 'little')
        else:
            super()._write(cpu, access, address, size, value, unused)


class RuntimePublication(Publication):
    """Explicit kernel pin floor; every original driver/IRQL callee executes."""
    def __init__(self, *args):
        super().__init__(*args)
        self.pins = {}

    def _instruction(self, cpu, address, size, unused):
        if address == 0x800000ad:  # MmGetPhysicalAddress must refer to a locked page.
            virtual = self.get(cpu.reg_read(UC_X86_REG_ESP)+4)
            assert any(base <= virtual < base+extent for base, extent in self.pins.items())
        if address != 0x800000af:  # MmLockUnlockBuffer(address, bytes, unlock)
            return super()._instruction(cpu, address, size, unused)
        stack = cpu.reg_read(UC_X86_REG_ESP)
        virtual, extent, unlock = args = [self.get(stack+4+i*4) for i in range(3)]
        self.owned(virtual, extent)
        assert extent and not virtual % 4096 and not extent % 4096 and unlock in (0, 1)
        if unlock:
            assert self.pins.pop(virtual) == extent
        else:
            assert virtual not in self.pins
            self.pins[virtual] = extent
        self.kernel_calls.append(dict(ordinal=175, service='pin_buffer', args=args, result=0))
        cpu.reg_write(UC_X86_REG_EAX, 0)
        cpu.reg_write(UC_X86_REG_ESP, stack+16)
        cpu.reg_write(UC_X86_REG_EIP, self.get(stack))


def check(xbe, library, output, runtime_sge=False):
    blob = xbe.read_bytes()
    assert hashlib.sha256(blob).hexdigest() == IMAGE_SHA256
    output = output.resolve()
    assert 'build' in output.parts and not output.exists()
    output.mkdir(parents=True)
    # Source-first oracle: original default600 packs attenuation384. The
    # already established ordinary gain contract yields this fixed level.
    # Two PCM16 channels set both the stereo bit27 and samples-per-block bit16.
    expected = dict(handles=[64, 71, 255], nontrapped_reason_fields=[0,9,3], route=0x14830820, format=0x5801e0e6,
        volumes=[0x180f180f, 0xffffffff, 0xffffffff], frames=2048,
        hrtf_target=0xffff, control50=0, control70=0, pitch=0,
        output=[0.25*10**(-384/1280), -0.375*10**(-384/1280)], tolerance=0.00003)
    expected['runtime_input_sge'] = dict(slots=[0,17,2046], pages=2, descriptor_stride=8,
        writes_only_dword0=True, unlock_after_retirement=True) if runtime_sge else None
    (output/'expectations.aot-private.json').write_text(json.dumps(expected, indent=2)+'\n')
    dll = c.CDLL(str(library.resolve()))
    dll.xlle_apu_reference_create.argtypes = [c.c_void_p, c.c_uint32]
    dll.xlle_apu_reference_create.restype = c.c_void_p
    dll.xlle_apu_reference_destroy.argtypes = [c.c_void_p]
    dll.xlle_apu_reference_kernel_handoff.argtypes = [c.c_void_p]
    dll.xlle_apu_reference_error.argtypes = [c.c_void_p]
    dll.xlle_apu_reference_error.restype = c.c_char_p
    dll.xlle_apu_reference_write.argtypes = [c.c_void_p, c.c_uint32, c.c_int, c.c_uint32]
    dll.xlle_apu_reference_read.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(c.c_uint32)]
    dll.xlle_apu_reference_process.argtypes = [c.c_void_p, c.c_uint32, c.POINTER(c.c_float), c.POINTER(c.c_uint32), c.POINTER(c.c_int)]
    dll.xlle_apu_reference_frame.argtypes = [c.c_void_p, c.c_uint64]
    lives = []
    for lifetime, voice in enumerate(expected['handles']):
        ram = (c.c_ubyte*(0x40000 if runtime_sge else 0x20000))()
        instance = dll.xlle_apu_reference_create(ram, len(ram))
        assert instance
        transactions = []
        def transaction(op, address, size, value):
            assert size == 4 and address % 4 == 0
            if op == 'W':
                ok = dll.xlle_apu_reference_write(instance, address, size, value)
            else:
                observed = c.c_uint32()
                ok = dll.xlle_apu_reference_read(instance, address, c.byref(observed))
                value = observed.value
            assert ok, (hex(address), hex(value), dll.xlle_apu_reference_error(instance))
            transactions.append(dict(op=op, address=address, value=value))
            return value
        def write(off, value): return transaction('W', 0xfe800000+off, 4, value)
        def word(off): return struct.unpack_from('<I', ram, off)[0]
        def store(off, value): struct.pack_into('<I', ram, off, value)
        try:
            original = (RuntimePublication if runtime_sge else Publication)(blob, lifetime, ram, transaction)
            slot = [0, 17, 2046][lifetime] if runtime_sge else 0
            if runtime_sge:
                assert dll.xlle_apu_reference_kernel_handoff(instance)
                write(0x1100, 0x88); write(0x1138, 0x7ff)
                write(0x1510, 0); write(0x1100, 0x100f)
            obj, settings, device, counter, wfex = 0x30000000, 0x30001000, 0x30002000, 0x30003000, 0x3000a000
            # Prepared zeroed class storage and live reference-counted device.
            # The original constructors establish all voice/settings defaults.
            original.put(device, 0x1cbfbc); original.put(device+4, 1)
            original.put(settings, 0x1cbfac); original.put(settings+4, 1)
            original.cpu.mem_write(wfex, struct.pack('<HHIIHHH', 1, 2, 48000, 192000, 4, 16, 0))
            assert original.call(0x176eec, [0, wfex, 0], settings) == 0
            assert original.call(0x17a476, [device, settings], obj) == obj
            assert original.call(0x178705, [0], obj) == 0
            assert original.call(0x179283, [0], obj) == 0
            assert original.get(settings+0x20) == 600
            assert original.cpu.mem_read(obj+0x64, 1) == b'\x01'
            assert all(original.get(obj+off) == 0 for off in (0x18,0x1c,0x20,0x24,0x28,0x2c,0x30,0x34))
            # Existing device allocator and list ownership; the complete real
            # 177CE9 helper chooses the next handle and consumes its counter.
            original.put(device+0x2f8, counter); original.put(counter, 256-voice)
            for used in range(64, voice): original.put(device+0x328+used*4, 0x3000b000)
            for off in (0x728,0x730,0x738,0x740,0x748,0x750):
                original.put(device+off, device+off); original.put(device+off+4, device+off)
            original.put(0x17df38, 0x3100a000)
            original.put(0x17df28, 0x31001000)
            write(0x202c, 0x1000); write(0x2030, 0x10000 if runtime_sge else 0xf000); write(0x115c, 0xa000)
            for off in (0x2054,0x2060,0x206c): write(off, 0xffff)
            if runtime_sge:
                # Authored owned source bytes, never a substituted driver callee.
                source_va = original.allocate('contiguous', 0x2000)
                source_pa = original.owned(source_va)['physical']
                source_bytes = struct.pack('<hh', 8192, -12288)*expected['frames']
                original.cpu.mem_write(source_va, source_bytes)
                ram[source_pa:source_pa+len(source_bytes)] = source_bytes
                ram[0x10000:0x14000] = b'\xa5'*0x4000
                write(0x2000, 15)  # Runtime SE, without relabelling it idle.
                before_mapping = bytes(ram); transactions.clear()
                original.call(0x17acbf, [slot, source_va, 2])
                mapping = list(transactions)
                expected_mapping = []
                for page in range(2):
                    expected_mapping.extend([
                        dict(op='R', address=0xfe820010, value=0x80),
                        dict(op='W', address=0xfe820804, value=slot+page),
                        dict(op='W', address=0xfe820808, value=source_pa+page*4096)])
                assert mapping == expected_mapping
                expected_ram = bytearray(before_mapping)
                for page in range(2): struct.pack_into('<I', expected_ram, 0x10000+(slot+page)*8, source_pa+page*4096)
                assert bytes(ram) == expected_ram  # Includes descriptor DWORD1 and surrounding RAM.
                assert original.pins == {source_va: 8192}
            else:
                store(0xf000, 0x10000); store(0xf008, 0x11000)
                ram[0x10000:0x12000] = struct.pack('<hh', 8192, -12288)*expected['frames']
            base = 0x1000+voice*128
            store(base+0x50, 0xffffffff); store(base+0x70, 0xffffffff)
            # Declared free-running reason policy, not hardware raw-value
            # equivalence. Original mode RMW must preserve these uninterpreted
            # fields and follow exactly the same supported control path.
            mode_transactions = []
            if not runtime_sge:
                control = 0x7100f | (expected['nontrapped_reason_fields'][lifetime]<<8)
                write(0x1100, control)
                original.call(0x17bc43,[0],device+8)
                assert transaction('R',0xfe801100,4,0) == control|0x80
                original.call(0x17bc43,[1],device+8)
                assert transaction('R',0xfe801100,4,0) == control
                mode_transactions = list(transactions[-6:])
            before = bytes(ram)
            transactions.clear()
            assert original.call(0x1795f1, ecx=obj) == 0
            publication = list(transactions)
            methods = [row for row in publication if row['op'] == 'W']
            assert [row['address']-0xfe820000 for row in methods] == [
                0x2f8,0x304,0x318,0x308,0x30c,0x310,0x314,0x350,0x36c,
                0x370,0x374,0x378,0x300,0x360,0x364,0x368,0x37c,0x31c]
            assert len(publication) == 19 and publication[0] == dict(op='R',address=0xfe820010,value=0x80)
            assert int.from_bytes(original.cpu.mem_read(obj+0xc,2),'little') == voice
            assert original.get(counter) == 255-voice and original.get(device+0x328+voice*4) == obj
            assert word(base) == expected['route'] and word(base+4) == expected['format'], (hex(word(base)),hex(word(base+4)))
            assert [word(base+0x60+i*4) for i in range(3)] == expected['volumes']
            assert word(base+0x1c) == 0xffff and word(base+0x50) == word(base+0x70) == 0
            assert word(base+0x7c) == 0 and not word(base+0x54)&(1<<21)
            # Only the selected voice and its four real notifier statuses may
            # change. This compares every other byte, including authored input.
            notify = 0xa000+16*(2+voice*4)
            allowed = set(range(base,base+128)) | {notify+15+i*16 for i in range(4)}
            assert all(a == b or i in allowed for i,(a,b) in enumerate(zip(before,bytes(ram))))
            assert original.get(obj+0x68) == 0x31000000+notify
            # Re-entry takes the original already-allocated branch: no second
            # allocation, publication or counter decrement is synthesized.
            transactions.clear()
            assert original.call(0x1795f1, ecx=obj) == 0 and not transactions
            assert original.get(counter) == 255-voice
            # Explicit pre-existing source mapping, then the whole original
            # start helper owns list insertion and ACTIVE/notifier transition.
            write(0x202f8, voice); write(0x203a0, slot*4096); write(0x203dc, expected['frames']-1)
            original.call(0x179c4b, ecx=obj)
            assert word(base+0x54)&(1<<21) and ram[notify+15] == 0x80
            samples = []; count = c.c_uint32(); done = c.c_int(); bins = (c.c_float*1024)()
            for quantum in range(80):
                c.memset(bins,0,c.sizeof(bins))
                assert dll.xlle_apu_reference_process(instance, voice, bins, c.byref(count), c.byref(done)), dll.xlle_apu_reference_error(instance)
                assert all(bins[i] == 0 for i in range(64,1024))
                samples.extend((bins[i],bins[32+i]) for i in range(count.value))
                if done.value: break
            assert done.value and len(samples) == expected['frames']
            maximum = max(abs(row[channel]-expected['output'][channel]) for row in samples[512:-128] for channel in (0,1))
            assert maximum < expected['tolerance'], maximum
            assert ram[notify+15] == 1 and not word(base+0x54)&(1<<21)
            assert word(base+0x58)&0xffffff == expected['frames']-1
            # Retire through the real original list owner, then release the
            # actual software handle through the complete allocator counterpart.
            write(0x1504,1<<15); write(0x2000,8)
            assert dll.xlle_apu_reference_frame(instance,1), dll.xlle_apu_reference_error(instance)
            original.call(0x178d7c,[0],obj)
            assert transaction('R',0xfe802054,4,0) == 0xffff
            original.call(0x177d7d,[obj],device) # void; verify actual handle ownership below
            assert original.get(counter) == 256-voice and original.get(device+0x328+voice*4) == 0
            assert int.from_bytes(original.cpu.mem_read(obj+0xc,2),'little') == 0xffff
            if runtime_sge:
                # Only after actual processing, original list retirement and
                # handle release may the original counterpart unpin the source.
                original.call(0x17ad2c, [source_va, 2])
                assert not original.pins
                assert [row['ordinal'] for row in original.kernel_calls] == [175, 173, 173, 175]
                original.call(0x800000ab, [source_va])  # Explicit kernel fixture frees its allocation.
                assert all(not allocation['live'] for allocation in original.allocations)
            else:
                assert not original.allocations and not original.kernel_calls
            identity = original.source_identity()
            assert all(bytes(original.cpu.mem_read(int(r['address'],16),r['bytes'])) == original.source(int(r['address'],16),r['bytes']) for r in identity)
            lives.append(dict(voice=voice,publication=publication,calls=original.calls,mode_transactions=mode_transactions,
                output_frames=len(samples),maximum_error=maximum,source_identity=identity,
                original_handle_released=True,original_list_retired=True,transactions=transactions))
            if runtime_sge:
                lives[-1].update(input_sge_mapping=mapping, source_slot=slot,
                    kernel_services=original.kernel_calls, source_unpinned_after_retirement=True)
        except BaseException as exc:
            (output/f'failure-{lifetime}.aot-private.json').write_text(json.dumps(
                dict(error=repr(exc),voice=voice,transactions=transactions,calls=original.calls),indent=2)+'\n')
            raise
        finally:
            dll.xlle_apu_reference_destroy(instance)
    def pin(p): return dict(path=str(p.resolve()),sha256=hashlib.sha256(p.read_bytes()).hexdigest())
    result = dict(result='original-runtime-input-sge-and-consumers-passed' if runtime_sge else 'original-ordinary-publication-and-consumers-passed',lifetimes=lives,
        pins=[pin(xbe),pin(library),pin(output/'expectations.aot-private.json')],
        hardware_operations=0,playback=False,
        scope='Prepared DISPATCH_LEVEL device allocator/notifier context; whole original constructors, allocation/publication/reentry/start/unlink/handle release; maintained source/envelope/mix. Runtime mode also executes whole17ACBF pin/publish and17AD2C unpin with an explicit kernel pin floor. No busy FE timing, GP/EP output, enclosing initializer or complete title claim.')
    (output/'original-publication.aot-private.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS 3 complete original voice publications, allocation/reentry/start/retirement, and independent 2048-frame processing per lifetime' +
          ('; whole original runtime pin/publish/unpin at slots0/17/2046' if runtime_sge else ''))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','library','output'): parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--runtime-sge', action='store_true')
    check(**vars(parser.parse_args()))
