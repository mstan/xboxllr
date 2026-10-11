#!/usr/bin/env python3
"""Execute the original EP scratch/directory producer over owned offline RAM.

Original driver callees are never replaced. Only the listed Xbox kernel memory
services have an explicit authored allocation/physical-address floor. Firmware,
timer configuration and unknown DMA bit9 are not replaced or qualified here.
Original bytes and RAM captures remain under a private build directory.
"""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

from aot_inventory import parse_xbe
from native_ep_reference import IMAGE_SHA256
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
    UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP,
    UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_GDTR, UC_X86_REG_CS,
    UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_FS)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class OriginalProducer:
    """One x86 CPU, real original resource/heap/copy code, bounded kernel RAM."""
    RAM_BYTES = 0x400000
    KERNEL = {15: ('pool_allocate', 2), 17: ('pool_free', 1),
              23: ('pool_size', 1), 166: ('contiguous_allocate', 5),
              171: ('contiguous_free', 1), 173: ('physical_address', 1),
              180: ('contiguous_size', 1)}
    BLOCKS = ((0x31bea00, 0x2f2), (0x31bf5c8, 0x862),
              (0x31c1750, 0x652), (0x31c3098, 0x6d3),
              (0x31c4be8, 0xbc), (0x31c4ed8, 0xc9))

    def __init__(self, blob, lifetime, fail_contiguous=0):
        self.blob, self.sections = blob, parse_xbe(blob)['sections']
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.cpu.mem_map(0x10000, 0x3300000)
        self.cpu.mem_map(0x20000000, 0x100000)
        self.cpu.mem_map(0x21000000, 0x400000)
        self.cpu.mem_map(0x30000000, 0x10000)
        self.cpu.mem_map(0x80000000, 0x1000)
        self.cpu.mem_map(0xfe800000, 0x10000)
        for section in self.sections:
            if section['raw_size']:
                self.cpu.mem_write(section['address'], blob[section['raw_offset']:
                    section['raw_offset']+section['raw_size']])
        self.stack, self.end, self.pcr = 0x30008000, 0x30009000, 0x30006000
        gdt = 0x30005000
        descriptor = 0x00c093000000ffff | ((self.pcr & 0xffffff) << 16) | ((self.pcr >> 24) << 56)
        self.cpu.mem_write(gdt, struct.pack('<QQQQ', 0, 0x00cf9b000000ffff,
                                           0x00cf93000000ffff, descriptor))
        self.cpu.reg_write(UC_X86_REG_GDTR, (0, gdt, 31, 0))
        for reg, value in ((UC_X86_REG_CS, 8), (UC_X86_REG_SS, 16),
                           (UC_X86_REG_DS, 16), (UC_X86_REG_ES, 16), (UC_X86_REG_FS, 24)):
            self.cpu.reg_write(reg, value)
        # Explicit DISPATCH_LEVEL caller context. Actual original IRQL scopes
        # then take their own documented no-raise/no-lower path.
        self.cpu.mem_write(self.pcr+0x24, b'\x02')
        self.pool_next, self.physical_next = 0x20000000, 0x20000+lifetime*0x8000
        self.contiguous_next = 0x21000000+lifetime*0x8000
        self.allocations, self.kernel_calls, self.calls, self.visits, self.mmio = [], [], [], {}, []
        self.fail_contiguous, self.contiguous_requests = fail_contiguous, 0
        self.executed_sources = {}
        self.cpu.hook_add(UC_HOOK_CODE, self._instruction)
        self.cpu.hook_add(UC_HOOK_MEM_WRITE, self._write)

    def source(self, address, size):
        section = next(s for s in self.sections if s['address'] <= address
                       and address+size <= s['address']+s['size'])
        off = section['raw_offset']+address-section['address']
        raw = max(0, min(size, section['raw_size']-(address-section['address'])))
        return self.blob[off:off+raw]+bytes(size-raw)

    def put(self, address, value):
        self.cpu.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def get(self, address):
        return struct.unpack('<I', self.cpu.mem_read(address, 4))[0]

    def owned(self, address, size=1, kind=None):
        allocation = next((a for a in self.allocations if a['live'] and
                           a['virtual'] <= address and address+size <= a['virtual']+a['extent']
                           and (kind is None or a['kind'] == kind)), None)
        if allocation is None:
            raise AssertionError(f'unowned {kind} RAM {address:x}+{size:x}')
        return allocation

    def allocate(self, kind, requested, alignment=0x1000, protection=4):
        if requested <= 0 or requested > 0x100000:
            raise AssertionError('unsupported allocation extent')
        alignment = max(0x1000, alignment) if kind == 'contiguous' else 16
        if alignment & (alignment-1) or protection != 4:
            raise AssertionError('unsupported allocation policy')
        # Address alignment and the page-owned extent are separate. An aligned
        # allocation request does not acquire unrequested extra alignment pages.
        extent = (requested+0xfff) & -0x1000 if kind == 'contiguous' else (requested+15) & -16
        if kind == 'pool':
            virtual = (self.pool_next+15) & -16
            self.pool_next = virtual+extent+16
            physical = 0x300000+(virtual-0x20000000)
            assert self.pool_next <= 0x20100000
        else:
            virtual = (self.contiguous_next+alignment-1) & -alignment
            physical = (self.physical_next+alignment-1) & -alignment
            self.contiguous_next, self.physical_next = virtual+extent+0x1000, physical+extent+0x1000
            assert self.physical_next < 0x300000
        allocation = dict(kind=kind, virtual=virtual, physical=physical,
                          requested=requested, extent=extent, live=True)
        self.allocations.append(allocation)
        self.cpu.mem_write(virtual, b'\xa5'*extent)
        return virtual

    def _instruction(self, cpu, address, size, unused):
        if 0x10000 <= address < 0x3300000:
            self.executed_sources[address] = bytes(cpu.mem_read(address, size))
        if address in (0x17c031, 0x17cd84, 0x17cbd4, 0x17b1ac, 0x17b2b1,
                       0x17bd77, 0x17bd00, 0x17cc83, 0x177a48, 0x17b25f,
                       0x17adc9, 0x17ae4d, 0x17785e):
            self.visits[hex(address)] = self.visits.get(hex(address), 0)+1
        if not 0x80000000 <= address < 0x80001000:
            return
        ordinal = address-0x80000000
        if ordinal not in self.KERNEL:
            raise AssertionError(f'unsupported kernel ordinal {ordinal}')
        name, count = self.KERNEL[ordinal]
        stack = cpu.reg_read(UC_X86_REG_ESP)
        args = [self.get(stack+4+i*4) for i in range(count)]
        result = 0
        if ordinal == 15:
            result = self.allocate('pool', args[0])
        elif ordinal == 166:
            assert args[1:3] == [0, 0xffffffff]
            self.contiguous_requests += 1
            if self.contiguous_requests != self.fail_contiguous:
                result = self.allocate('contiguous', args[0], args[3], args[4])
        elif ordinal in (23, 180):
            allocation = self.owned(args[0], kind='pool' if ordinal == 23 else 'contiguous')
            assert allocation['virtual'] == args[0]
            result = allocation['extent']
        elif ordinal == 173:
            allocation = self.owned(args[0])
            result = allocation['physical']+args[0]-allocation['virtual']
        else:
            allocation = self.owned(args[0], kind='pool' if ordinal == 17 else 'contiguous')
            assert args[0] == allocation['virtual']
            allocation['live'] = False
        self.kernel_calls.append(dict(ordinal=ordinal, service=name, args=args, result=result))
        cpu.reg_write(UC_X86_REG_EAX, result)
        cpu.reg_write(UC_X86_REG_ESP, stack+4+count*4)
        cpu.reg_write(UC_X86_REG_EIP, self.get(stack))

    def _write(self, cpu, access, address, size, value, unused):
        if 0x20000000 <= address < 0x21400000:
            self.owned(address, size)
        if 0xfe800000 <= address < 0xfe810000:
            assert size == 4 and address in (0xfe802040, 0xfe802048, 0xfe8020d4, 0xfe8020dc)
            self.mmio.append(dict(address=address, size=size, value=value))

    def call(self, address, args=(), ecx=0):
        sentinels = [(UC_X86_REG_EBX, 0x11223344), (UC_X86_REG_EBP, 0x22334455),
                     (UC_X86_REG_ESI, 0x33445566), (UC_X86_REG_EDI, 0x44556677)]
        self.put(self.stack, self.end)
        for i, value in enumerate(args):
            self.put(self.stack+4+i*4, value)
        self.put(self.stack+4+len(args)*4, 0x76543210)
        for reg, value in sentinels:
            self.cpu.reg_write(reg, value)
        self.cpu.reg_write(UC_X86_REG_ECX, ecx)
        self.cpu.reg_write(UC_X86_REG_ESP, self.stack)
        self.cpu.emu_start(address, self.end, count=500000)
        assert self.cpu.reg_read(UC_X86_REG_EIP) == self.end, f'bounded original call stopped {address:x}'
        assert self.cpu.reg_read(UC_X86_REG_ESP) == self.stack+4+len(args)*4
        assert self.get(self.stack+4+len(args)*4) == 0x76543210
        assert all(self.cpu.reg_read(reg) == value for reg, value in sentinels)
        assert self.cpu.mem_read(self.pcr+0x24, 1) == b'\x02'
        result = self.cpu.reg_read(UC_X86_REG_EAX)
        self.calls.append(dict(address=hex(address), args=list(args), ecx=ecx, result=result,
                               preserved_stack_and_callee_registers=True))
        return result

    def produce(self):
        for initializer in (0x177784, 0x17778f, 0x17b14e, 0x17b159):
            self.call(initializer)
        resource_owner, gp_owner, ep_owner = 0x30000000, 0x30001000, 0x30002000
        self.cpu.mem_write(resource_owner, bytes(0x100))
        self.cpu.mem_write(gp_owner, bytes(0x100))
        self.cpu.mem_write(ep_owner, bytes(0x100))
        assert self.call(0x17c031, ecx=resource_owner) == 0
        self.call(0x17785e, ecx=gp_owner)
        self.call(0x17cd84, (gp_owner,), ep_owner)
        scratch = self.get(0x17dfd8)
        scratch_size = self.get(0x17dfe0)
        scratch_physical = self.get(0x17dfdc)
        assert scratch_size == 0x13000
        self.owned(scratch, scratch_size, 'contiguous')
        actual = bytes(self.cpu.mem_read(scratch, scratch_size))
        assert actual[:0x320] == self.source(0x31be0e0, 0x320)
        assert actual[0x600:0xbfc] == self.source(0x31be400, 0x5fc)
        pointer = 0x300
        directory = [0x24, 6]
        blocks = []
        for source, words in self.BLOCKS:
            raw = self.source(source, words*4)
            assert actual[pointer*4:(pointer+words)*4] == raw
            directory += [pointer, words]
            blocks.append(dict(source=hex(source), scratch_word=pointer, words=words, sha256=sha(raw)))
            pointer += words
        actual_directory = list(struct.unpack_from('<36I', actual, 0x400))
        known = dict(enumerate(directory))
        known.update({20:pointer, 21:0x18, 22:0, 23:0, 25:0, 27:0,
                      14:(scratch_size-0x3000)//4, 15:0x600,
                      34:pointer+0x18, 35:0x2000})
        assert all(actual_directory[i] == v for i,v in known.items()), (actual_directory, known)
        directory = actual_directory
        assert actual[pointer*4:pointer*4+0x60] == self.source(0x31c5200, 0x60)
        assert self.get(ep_owner+8) == scratch+pointer*4
        assert self.get(ep_owner+4) == gp_owner
        assert self.visits['0x17cbd4'] == 6
        assert self.visits['0x17b2b1'] == 11  # GPprefix plus EPprefix/helper/6blocks/table/directory
        table = self.get(0x17dfe8)
        table_physical = self.get(0x17dfec)
        table_size = self.get(0x17dff0)
        assert table_size == 0x98
        assert self.get(0xfe802048) == table_physical and self.get(0xfe8020dc) == 19
        for page in range(19):
            assert self.get(table+page*8) == scratch_physical+page*0x1000
            assert self.get(table+page*8+4) == 0
        # Exact original producer bytes stay intact; no helper/firmware patch.
        for start, size in ((0x17c031, 0x180), (0x17cd84, 0x15c), (0x17cbd4, 0x9e),
                            (0x31be0e0, 0x320), (0x31be400, 0x5fc)):
            assert bytes(self.cpu.mem_read(start, size)) == self.source(start, size)
        return dict(scratch_virtual=scratch, scratch_physical=scratch_physical,
                    scratch_bytes=scratch_size, table_physical=table_physical,
                    table_entries=19, directory=directory,
                    directory_defined_slots=sorted(known),
                    directory_unspecified_slots=[i for i in range(36) if i not in known], blocks=blocks,
                    scratch_sha256=sha(actual), calls=self.calls, visits=self.visits,
                    kernel_calls=self.kernel_calls, mmio=self.mmio,
                    constant_table_virtual_zero_tail_bytes=20)

    def physical_ram(self):
        ram = bytearray(b'\xa5'*self.RAM_BYTES)
        for allocation in self.allocations:
            if allocation['live']:
                physical, size = allocation['physical'], allocation['extent']
                ram[physical:physical+size] = self.cpu.mem_read(allocation['virtual'], size)
        return ram

    def destroy(self):
        self.call(0x17cd67, ecx=0x30002000)
        self.call(0x177a73, ecx=0x30001000)
        self.call(0x17bec1, ecx=0x30000000)
        assert not any(a['live'] for a in self.allocations), self.allocations
        assert self.get(0x17ddcc) == 0
        assert all(self.get(a) == 0 for a in (0x17d79c, 0x17d7a0, 0x17d7a4, 0x17d7a8))

    def source_identity(self):
        assert all(self.source(address, len(raw)) == raw
                   for address, raw in self.executed_sources.items())
        return [dict(address=hex(address), bytes=len(raw), sha256=sha(raw))
                for address, raw in sorted(self.executed_sources.items())]


def check(image, output, observer=None):
    image, output = image.resolve(), output.resolve()
    if 'build' not in output.parts:
        raise ValueError('original evidence must stay under private build/')
    blob = image.read_bytes()
    assert sha(blob) == IMAGE_SHA256, 'unreviewed original image'
    output.mkdir(parents=True, exist_ok=True)
    records = []
    for lifetime in range(3):
        producer = OriginalProducer(blob, lifetime)
        result = producer.produce()
        ram = output/f'original-directory-ram-{lifetime}.aot-private.bin'
        ram.write_bytes(producer.physical_ram())
        ownership = output/f'original-directory-ownership-{lifetime}.aot-private.bin'
        ownership.write_bytes(b''.join(struct.pack('<II', a['physical'], a['extent'])
                                      for a in producer.allocations if a['live']))
        result.update(lifetime=lifetime, ram_sha256=sha(ram.read_bytes()))
        result['owned_allocations'] = [dict(a) for a in producer.allocations]
        result['ownership_sha256'] = sha(ownership.read_bytes())
        if observer:
            run = subprocess.run([str(observer.resolve()), str(ram),
                hex(result['table_physical']), str(result['table_entries']), str(ownership)],
                capture_output=True, text=True, timeout=30)
            log = output/f'original-directory-observer-{lifetime}.aot-private.log'
            log.write_text(run.stdout+run.stderr, newline='\n')
            result.update(observer_exit=run.returncode, observer_log_sha256=sha(log.read_bytes()))
            assert run.returncode == 0, run.stdout+run.stderr
        producer.destroy()
        result['original_destructors_release_all_allocations'] = True
        result['executed_original_instructions'] = producer.source_identity()
        records.append(result)
    failures = []
    for failed_request in (1, 8, 16):
        producer = OriginalProducer(blob, 0, fail_contiguous=failed_request)
        for initializer in (0x177784, 0x17778f, 0x17b14e, 0x17b159):
            producer.call(initializer)
        result = producer.call(0x17c031, ecx=0x30000000)
        assert result == 0x8007000e
        assert not producer.mmio
        producer.destroy()
        failures.append(dict(failed_contiguous_request=failed_request,
                             original_result=result,
                             original_destructors_release_all_allocations=True,
                             calls=producer.calls, kernel_calls=producer.kernel_calls,
                             executed_original_instructions=producer.source_identity()))
    report = dict(image_sha256=IMAGE_SHA256, tool_sha256=sha(Path(__file__).read_bytes()),
                  observer_sha256=sha(observer.read_bytes()) if observer else None,
                  original_dispatcher_boundaries_observed=bool(observer),
                  lifetimes=records, resource_failure_lifetimes=failures, original_boot_qualified=False,
                  original_output_qualified=False,
                  scope='Unchanged original resource/heap/GP/EP scratch producer and six selectors; '
                        'kernel RAM services have explicit authored offline ownership.',
                  limits='Unknown bit9/timer/cadence/hardware bootstrap/AC97 are guarded; '
                         'no physical-console or audible output operation.')
    path = output/'original-ep-directory-reference.aot-private.json'
    path.write_text(json.dumps(report, indent=2)+'\n', newline='\n')
    print(f'PASS: {len(records)} original EP directory producer lifetimes; {path}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--observer', type=Path)
    args = parser.parse_args()
    check(args.xbe, args.output, args.observer)
