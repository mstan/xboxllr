#!/usr/bin/env python3
"""Unchanged original headroom helpers -> maintained method state/ordinary mix.

Offline prepared-caller proof. The enclosing title initializer is not replaced
or claimed here. Spatial processing, busy FE latency and active reset retain
their separate guards. No hardware transport or speaker output is used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from native_ep_directory_reference import OriginalProducer
from native_ep_reference import IMAGE_SHA256
from unicorn import UC_HOOK_MEM_READ


def pin(path):
    path = path.resolve()
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


class HeadroomStartup(OriginalProducer):
    def __init__(self, blob, lifetime, transport):
        super().__init__(blob, lifetime)
        self.transport = transport
        self.cpu.mem_map(0xfe820000, 0x1000)
        self.cpu.hook_add(UC_HOOK_MEM_READ, self._read)

    def _read(self, cpu, access, address, size, value, unused):
        if 0xfe800000 <= address < 0xfe880000:
            assert address == 0xfe820010 and size == 4
            observed = self.transport('R', address, size, 0)
            cpu.mem_write(address, observed.to_bytes(size, 'little'))

    def _write(self, cpu, access, address, size, value, unused):
        if 0xfe800000 <= address < 0xfe880000:
            assert size == 4 and address % 4 == 0 and 0xfe820200 <= address <= 0xfe820280
            self.transport('W', address, size, value)
            return
        super()._write(cpu, access, address, size, value, unused)


def check(xbe, observer, output):
    output = output.resolve()
    assert 'build' in output.parts and not output.exists()
    blob = xbe.read_bytes()
    assert hashlib.sha256(blob).hexdigest() == IMAGE_SHA256
    output.mkdir(parents=True)
    # Expected inputs/results are fixed before device execution. The gain is
    # the independently established ordinary-submix power-of-two contract;
    # no spatial gain formula is inferred from the reference emulator.
    golden = [dict(hrtf=0, settings=[0]*32),
              dict(hrtf=0x3a, settings=list(range(32))),
              dict(hrtf=0xffffffff, settings=[255-i*7 for i in range(32)])]
    for case in golden:
        case['expected_hrtf'] = case['hrtf'] & 7
        case['expected_gains'] = [2.0**-(amount & 7) for amount in case['settings']]
    expectations = output/'expectations.aot-private.json'
    expectations.write_text(json.dumps(golden, indent=2)+'\n')
    lives = []
    for lifetime, case in enumerate(golden):
        trace = []
        with (output/f'observer-{lifetime}.aot-private.log').open('w') as error:
            child = subprocess.Popen([str(observer.resolve()), '--transport'],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=error, text=True)
            try:
                assert child.stdout.readline().strip() == 'READY'

                def request(op, address=0, size=0, value=0):
                    child.stdin.write(f'{op} {address:x} {size} {value:x}\n')
                    child.stdin.flush()
                    return child.stdout.readline().strip().split()

                def transport(op, address, size, value):
                    reply = request(op, address, size, value)
                    assert len(reply) == 2 and reply[0] == 'OK', reply
                    observed = int(reply[1], 16)
                    trace.append(dict(op=op, address=address, size=size,
                                      value=value if op == 'W' else observed))
                    return observed

                def gains():
                    result = []
                    for slot in range(32):
                        reply = request('M', slot)
                        assert len(reply) == 2 and reply[0] == 'MIX', reply
                        result.append(float(reply[1]))
                    assert result == case['expected_gains'], (result, case)
                    return result

                assert request('H') == ['HEADROOM', '0', '255']
                original = HeadroomStartup(blob, lifetime, transport)
                obj, settings = 0x30000000, 0x30003000
                original.put(obj+0xc, settings)
                original.cpu.mem_write(settings+0x14, bytes(case['settings']))
                settings_before = bytes(original.cpu.mem_read(settings, 0x100))
                assert original.call(0x17815a, [case['hrtf']], obj) == 0
                assert request('H') == ['HEADROOM', '1', str(case['expected_hrtf'])]
                for slot in range(32):
                    assert original.call(0x178191, [slot], obj) == 0
                expected_writes = [(0xfe820280, case['expected_hrtf'])]+[
                    (0xfe820200+slot*4, amount & 7) for slot, amount in enumerate(case['settings'])]
                assert [(r['address'], r['value']) for r in trace if r['op'] == 'W'] == expected_writes
                assert len(trace) == 66 and all(trace[i]['op'] == 'R' and
                    trace[i]['value'] == 0x80 and trace[i+1]['op'] == 'W' for i in range(0, 66, 2))
                ordinary = gains()
                # Subsequent HRTF changes must preserve every ordinary bin's
                # gain. These helpers still execute their actual mask/capacity.
                for amount in (0, 7):
                    assert original.call(0x17815a, [amount], obj) == 0
                    assert request('H') == ['HEADROOM', '1', str(amount)]
                    assert gains() == ordinary
                assert not original.allocations and not original.kernel_calls
                assert original.get(obj+0xc) == settings
                assert bytes(original.cpu.mem_read(settings, 0x100)) == settings_before
                identity = original.source_identity()
                assert all(bytes(original.cpu.mem_read(int(row['address'], 16), row['bytes'])) ==
                           original.source(int(row['address'], 16), row['bytes']) for row in identity)
                lives.append(dict(calls=original.calls, transactions=trace,
                    ordinary_gains=ordinary, unchanged_settings=True, kernel_calls=0,
                    allocations=0, source_identity=identity))
            finally:
                child.stdin.close()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
            assert child.returncode == 0
    result = dict(result='original-headroom-startup-group-passed', lifetimes=lives,
        pins=[pin(xbe), pin(observer), pin(expectations)], hardware_operations=0, playback=False,
        original_caller_prefix=dict(address='0x178646', bytes=0x25,
            sha256=hashlib.sha256(original.source(0x178646, 0x25)).hexdigest()),
        scope='Prepared DISPATCH_LEVEL caller/settings, whole original helpers, synchronous maintained FE capacity, ordinary mix gains/isolation; no enclosing initializer, spatial processing, busy queue, active reset or full title audio claim')
    (output/'original-headroom.aot-private.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS 3 original groups; 35 helpers and 70 MMIO transactions each; all 32 ordinary bins isolated from HRTF state')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'observer', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    check(**vars(parser.parse_args()))
