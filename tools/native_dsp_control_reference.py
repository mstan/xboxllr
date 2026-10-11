#!/usr/bin/env python3
"""Observe original CPU controls and a genuine VP-to-original-GP wake route.

The raw-core observer supplies explicitly diagnostic retired-cycle samples and
records unresolved B2/B0/B1 writes. It is not an admitted MCPX timer or complete
boot implementation. Original code, firmware and allocation bytes stay private.
"""
import argparse
import json
import struct
import subprocess
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP
from native_ep_startup_reference import OriginalStartup
from native_ep_directory_reference import sha
from native_ep_reference import IMAGE_SHA256


class OriginalControl(OriginalStartup):
    def _write(self, cpu, access, address, size, value, unused):
        super()._write(cpu, access, address, size, value, unused)
        if 0xfe800000 <= address < 0xfe860000:
            self.mmio[-1]['original_pc'] = cpu.reg_read(UC_X86_REG_EIP)

    def produce_control(self):
        result = self.produce_startup()
        # Real original RMW helpers, with the original IRQL scopes and ABI.
        # Initial unrelated bits are authored caller state, not reset defaults.
        self.put(0xfe801100, 0x120080)
        self.put(0xfe802000, 7)
        self.call(0x17bc43, (1,), self.owner)
        self.call(0x17bc9e, (1,), self.owner)
        assert self.get(0xfe801100) == 0x120000
        assert self.get(0xfe802000) == 15
        result.update(original_control_calls=self.calls[-2:],
            original_control_writes=self.mmio[-2:],
            voice_physical=self.get(0x17df2c),
            source_sge_virtual=self.get(0x17df48),
            source_sge_physical=self.get(0x17df4c),
            notifier_physical=self.get(0x17df3c))
        self.owned(self.get(0x17df28), 0x8000)
        self.owned(result['source_sge_virtual'], 8)
        # Owned diagnostic PCM input. Its source SGE is an authored data input,
        # not an original wave-buffer allocation or descriptor-format claim.
        pcm_virtual = self.allocate('contiguous', 4096)
        pcm = self.owned(pcm_virtual, 4096, 'contiguous')
        self.put(result['source_sge_virtual'], pcm['physical'])
        self.put(result['source_sge_virtual']+4, 0)
        samples = b''.join(struct.pack('<h', ((i*997+113*self.format_selector) % 50001)-25000)
                           for i in range(256))
        self.cpu.mem_write(pcm['virtual'], samples+bytes(4096-len(samples)))
        result.update(diagnostic_pcm=dict(pcm),
                      diagnostic_pcm_sha256=sha(samples),
                      final_registers={m['address']: m['value'] for m in self.mmio})
        return result


def check(image, output, observer):
    image, output, observer = image.resolve(), output.resolve(), observer.resolve()
    assert 'build' in output.parts, 'original evidence must stay private'
    blob = image.read_bytes()
    assert sha(blob) == IMAGE_SHA256, 'unreviewed original image'
    output.mkdir(parents=True, exist_ok=True)
    lives = []
    for life in range(3):
        p = OriginalControl(blob, life, life)
        data = p.produce_control()
        ram = output/f'control-ram-{life}.aot-private.bin'
        ram.write_bytes(p.physical_ram())
        ownership = output/f'control-ownership-{life}.aot-private.bin'
        ownership.write_bytes(b''.join(struct.pack('<II', a['physical'], a['extent'])
                                      for a in p.allocations if a['live']))
        registers = output/f'control-registers-{life}.aot-private.bin'
        registers.write_bytes(b''.join(struct.pack('<II', int(a), v)
                                      for a, v in sorted(data['final_registers'].items())))
        arguments = [str(observer), str(ram), str(registers), str(ownership),
                     hex(data['voice_physical']), hex(data['source_sge_physical']),
                     hex(data['notifier_physical'])]
        observations = []
        for mode in ('frames', 'disabled', 'halted', 'starved'):
            if mode == 'disabled':
                p.call(0x17bc9e, (0,), p.owner)
                assert p.get(0xfe802000) == 7
            if mode == 'halted':
                p.call(0x17bc43, (0,), p.owner)
                assert p.get(0xfe801100) == 0x120080
            selected = output/f'control-registers-{life}-{mode}.aot-private.bin'
            selected.write_bytes(b''.join(struct.pack('<II', int(a), v) for a, v in
                sorted({m['address']: m['value'] for m in p.mmio}.items())))
            capture = output/f'control-payload-{life}-{mode}.aot-private.bin'
            command = arguments.copy()
            command[2] = str(selected)
            run = subprocess.run(command+[mode, str(capture)], capture_output=True,
                                 text=True, timeout=30)
            log = output/f'control-{life}-{mode}.aot-private.log'
            log.write_text(run.stdout+run.stderr, newline='\n')
            assert run.returncode == 0 and not run.stderr, run.stdout+run.stderr
            observation = json.loads(run.stdout)
            assert observation['mode'] == mode
            assert not observation['timer_qualified'] and not observation['hardware_boot_qualified']
            if mode == 'frames':
                assert observation['actual_frames'] == 3
                assert observation['actual_original_gp_nodes'] == 24
                assert observation['actual_frame_events'] == 3
                assert observation['exact_output_words'] == 2496
                assert capture.stat().st_size == 2496*4
                mix = Path(str(capture)+'.mix-input.aot-private.bin').read_bytes()
                assert len(mix) == 3*4096
                expected = b''.join(
                    b''.join(mix[frame*4096+channel*128:frame*4096+(channel+1)*128]
                             for channel in (0, 2, 1, 4, 5, 3)) +
                    mix[frame*4096+11*128:frame*4096+31*128]
                    for frame in range(3))
                assert capture.read_bytes() == expected
                observation['capture_sha256'] = sha(capture.read_bytes())
                observation['mix_input_sha256'] = sha(mix)
            else:
                assert observation['actual_frames'] == 0 and observation['actual_frame_events'] == 0
                assert observation['actual_original_gp_nodes'] == 0
            observations.append(observation)
            if mode == 'disabled':
                p.call(0x17bc9e, (1,), p.owner)
                assert p.get(0xfe802000) == 15
            if mode == 'halted':
                p.call(0x17bc43, (1,), p.owner)
                assert p.get(0xfe801100) == 0x120000
        # The diagnostic loan is released separately from genuine driver cleanup.
        # It never becomes an original driver's refcounted wave-buffer owner.
        pcm = data['diagnostic_pcm']
        p.call(0x80000000+171, (pcm['virtual'],))
        p.destroy_startup()
        assert not any(a['live'] for a in p.allocations)
        data.update(lifetime=life, observer_runs=observations,
                    original_control_lifetime_calls=[c for c in p.calls
                        if c['address'] in ('0x17bc43', '0x17bc9e')],
                    original_control_lifetime_writes=[m for m in p.mmio
                        if m['address'] in (0xfe801100, 0xfe802000)],
                    original_destructors_release_all_allocations=True,
                    original_source_identity=p.source_identity(),
                    ram_sha256=sha(ram.read_bytes()), ownership_sha256=sha(ownership.read_bytes()),
                    registers_sha256=sha(registers.read_bytes()))
        lives.append(data)
    result = dict(issue='beads-xbox.1.113.14', tool_sha256=sha(Path(__file__).read_bytes()),
        image_sha256=IMAGE_SHA256, lifetimes=lives,
        scope='Actual original CPU FE/SE control helpers and original GP continuation after genuine maintained VP publication, MIX lease and event.',
        timer_qualified=False, hardware_boot_qualified=False, original_ep_output_qualified=False,
        limits='Private diagnostic raw-core owner; B2/B0/B1 writes are recorded, not admitted. B3 is the actual pinned core retired-cycle count, explicitly a diagnostic source, not MCPX time/configuration. Original GP and physical scratch bytes are unchanged. Voice/source data and control-entry caller are authored. No EP wake/startup/bit9, hardware reset/cadence, whole original startup/output, playback, title or Xbox operation is supplied.')
    report = output/'original-dsp-control-reference.aot-private.json'
    report.write_text(json.dumps(result, indent=2)+'\n', newline='\n')
    print(f'PASS: {len(lives)} original control/GP frame lifetimes; {report}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--observer', type=Path, required=True)
    args = parser.parse_args()
    check(args.xbe, args.output, args.observer)
