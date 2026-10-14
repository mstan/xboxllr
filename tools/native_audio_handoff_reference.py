#!/usr/bin/env python3
"""Derive the pinned kernel's idle audio handoff, then exercise title controls.

Executes owned original instructions; register storage is an observation surface,
not a model of active DSP/FE completion. Original bytes and reports stay private.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP
from native_ep_startup_reference import OriginalStartup
from native_ep_reference import IMAGE_SHA256

KERNEL_SHA = '827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c'
SECTL = 0xfe802000
IEN = 0xfe801004


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def pin(path):
    path = path.resolve()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha(path.read_bytes()))


class KernelHandoff:
    def __init__(self, blob, seed):
        assert sha(blob) == KERNEL_SHA, 'unreviewed kernel image'
        self.blob = blob
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.cpu.mem_map(0x80010000, 0x70000)
        self.cpu.mem_write(0x80010000, blob)
        self.cpu.mem_map(0x30000000, 0x10000)
        self.cpu.mem_map(0xfe800000, 0x80000)
        # Explicit idle caller at DISPATCH_LEVEL, with empty software ownership.
        # No interrupt/DMA completion, callee return or instruction is replaced.
        self.cpu.mem_write(0x8003da60, b'\x02')
        self.put(SECTL, seed)
        self.events, self.calls, self.sources = [], [], {}
        self.cpu.hook_add(UC_HOOK_CODE, self.instruction)
        self.cpu.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self.memory,
                          begin=0xfe800000, end=0xfe87ffff)

    def put(self, address, value):
        self.cpu.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def get(self, address):
        return struct.unpack('<I', self.cpu.mem_read(address, 4))[0]

    def instruction(self, cpu, address, size, unused):
        assert 0x80010000 <= address < 0x80080000, hex(address)
        code = bytes(cpu.mem_read(address, size))
        assert code == self.blob[address-0x80010000:address-0x80010000+size]
        self.sources[str(address)] = dict(bytes=size, sha256=sha(code))

    def memory(self, cpu, access, address, size, value, unused):
        assert size == 4 and not address & 3
        self.events.append(dict(pc=cpu.reg_read(UC_X86_REG_EIP), address=address,
            write=access == UC_MEM_WRITE,
            value=value if access == UC_MEM_WRITE else self.get(address)))

    def call(self, address, args=()):
        stack, end = 0x30008000, 0x30009000
        self.put(stack, end)
        for i, value in enumerate(args):
            self.put(stack+4+i*4, value)
        self.cpu.reg_write(UC_X86_REG_ESP, stack)
        self.cpu.reg_write(UC_X86_REG_ECX, 0x30000000)
        regs = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)
        before = [self.cpu.reg_read(r) for r in regs]
        offset = len(self.events)
        self.cpu.emu_start(address, end, count=100000)
        assert self.cpu.reg_read(UC_X86_REG_EIP) == end, 'original call did not return'
        assert self.cpu.reg_read(UC_X86_REG_ESP) == stack+4+len(args)*4
        assert [self.cpu.reg_read(r) for r in regs] == before
        record = dict(address=address, arguments=list(args), mmio=self.events[offset:],
                      sectl=self.get(SECTL), ien=self.get(IEN), natural_return=True)
        self.calls.append(record)
        return record


def check(kernel, xbe, observation, output):
    assert 'build' in output.resolve().parts, 'original evidence must stay private'
    blob, title = kernel.read_bytes(), xbe.read_bytes()
    assert sha(blob) == KERNEL_SHA and sha(title) == IMAGE_SHA256
    physical = json.loads(observation.read_text())
    assert physical['kernel'] == '1.0.4983.6'
    captured = physical['startup_audio']
    assert captured['phase'] == 'main-entry-before-video-network'
    assert captured['sectl'] == 7
    assert captured['ien'] == 0xafa
    assert captured['gp_reset_before'] == captured['gp_reset_after'] == 0
    assert captured['ep_reset_before'] == captured['ep_reset_after'] == 1
    # Bind the retained observation's immediate evidence, without new hardware.
    for row in physical['pins']:
        assert sha(Path(row['path']).read_bytes()) == row['sha256'], row['path']
    cases = []
    for seed in (0, 7, 0x18, 0xa5a500ff):
        owner = KernelHandoff(blob, seed)
        init = owner.call(0x800654dc)
        assert [e['value'] for e in init['mmio'] if e['write'] and e['address'] == SECTL] == [0, 7]
        owner.call(0x80063ef7, (1,))
        assert owner.get(SECTL) == 15
        owner.call(0x80063ef7, (0,))
        assert owner.get(SECTL) == 7
        stop = owner.call(0x80067442, (1,))
        assert owner.get(SECTL) == captured['sectl']
        assert owner.get(IEN) == captured['ien']
        assert owner.get(0xfe83fffc) == 0 and owner.get(0xfe85fffc) == 1
        # The actual idle FE fence takes its no-work branch. Never substitute
        # successful active notification or borrow this as an active-work proof.
        assert not any(e['write'] and e['address'] in (0xfe820104, 0xfe82010c)
                       for e in stop['mmio'])
        cases.append(dict(seed=seed, calls=owner.calls, instructions=owner.sources))
    lives = []
    for selector in range(3):
        p = OriginalStartup(title, selector, selector)
        p.produce_startup()
        # This fixture loans only the independently derived handoff registers.
        # Original full title launch and active device startup remain separate.
        p.put(SECTL, cases[0]['calls'][-1]['sectl'])
        p.put(IEN, cases[0]['calls'][-1]['ien'])
        first_read, first_write = len(p.reads), len(p.mmio)
        for mode, expected in ((0, 7), (1, 15), (2, 23), (3, 31), (0, 7)):
            p.call(0x17bc9e, (mode,), p.owner)
            assert p.get(SECTL) == expected
        reads, writes = p.reads[first_read:], p.mmio[first_write:]
        assert [e['value'] for e in reads if e['address'] == SECTL] == [7, 7, 15, 23, 31]
        assert [e['value'] for e in writes if e['address'] == SECTL] == [7, 15, 23, 31, 7]
        first_ien_read, first_ien_write = len(p.reads), len(p.mmio)
        for mode, expected in ((0, 0xafa), (1, 0xafb), (0, 0xafa)):
            p.call(0x17bbd5, (mode,), p.owner)
            assert p.get(IEN) == expected
        ien_reads, ien_writes = p.reads[first_ien_read:], p.mmio[first_ien_write:]
        assert [e['value'] for e in ien_reads if e['address'] == IEN] == [0xafa, 0xafa, 0xafb]
        assert [e['value'] for e in ien_writes if e['address'] == IEN] == [0xafa, 0xafb, 0xafa]
        # The complete original title initializer subsequently replaces the
        # inherited interrupt mask. Warm application state need not equal AFA.
        first_init = len(p.mmio)
        p.call(0x17bf66, ecx=p.owner)
        assert p.get(IEN) == 0xd8 and p.get(SECTL) == 7
        title_init = p.mmio[first_init:]
        p.destroy_startup()
        assert not any(a['live'] for a in p.allocations)
        lives.append(dict(selector=selector, reads=reads, writes=writes,
                          ien_reads=ien_reads, ien_writes=ien_writes, title_init=title_init,
                          source_identity=p.source_identity(), allocations_released=True))
    output.mkdir(parents=True, exist_ok=True)
    result = dict(kernel=pin(kernel), title=pin(xbe), physical_observation=pin(observation),
        tool=pin(Path(__file__)), kernel_cases=cases, title_control_lifetimes=lives,
        handoff_sectl=7, handoff_ien=0xafa, full_boot_qualified=False, active_fence_qualified=False,
        limits='Pinned 4983 initialized idle handoff only. Kernel init/control/idle retirement and title RMW helpers execute unchanged with natural returns. Software owner/IRQL and MMIO storage are explicit fixture inputs. Physical capture is probe main entry, not Blinx entry or cold reset. No DSP reset timing, active FE completion, full title startup or connected output is established.')
    report = output/'kernel-audio-handoff.aot-private.json'
    report.write_text(json.dumps(result, indent=2)+'\n', newline='\n')
    print(f'PASS: four original kernel init/idle retirement cases; three original title control lifetimes; {report}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('kernel', 'xbe', 'observation', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    check(args.kernel, args.xbe, args.observation, args.output)
