#!/usr/bin/env python3
"""Execute the original stopped initializer against maintained APU transport.

No hardware access. Physical observations are retained inputs, not emulator
goldens. Prepared caller objects are loaned; this is not whole-title startup.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from native_ep_startup_reference import OriginalStartup
from native_ep_reference import IMAGE_SHA256
from native_audio_handoff_reference import KernelHandoff


def pin(path):
    path = path.resolve()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())


class Initializer(OriginalStartup):
    transport = None
    def _read(self, cpu, access, address, size, value, unused):
        if self.transport and 0xfe800000 <= address < 0xfe860000:
            self.put(address, self.transport('R', address, size, 0))
        super()._read(cpu, access, address, size, value, unused)

    def _write(self, cpu, access, address, size, value, unused):
        if self.transport and 0xfe800000 <= address < 0xfe860000:
            self.transport('W', address, size, value)
        super()._write(cpu, access, address, size, value, unused)


def physical(probe):
    pins = []
    observations = {}
    for name, script in (('idle-configuration', 'write-idle-configuration'),
                         ('fectl-startup', 'observe-fectl-startup')):
        path = probe/(name+'.aot-private.json')
        data = json.loads(path.read_text())
        assert data['status'] == 'observed' and data['storage_operations'] == 0
        assert data['automatic_retry'] is False
        writes = []
        for row in data['rows']:
            address = 0xfe800000+row['offset']
            payload = b'\xa1'+struct.pack('<I', address)+b'\xc3'
            if row['write'] is not None:
                payload = b'\xc7\x05'+struct.pack('<II', address, row['write'])+payload
                writes.append(row)
            assert row['payload'] == payload.hex()
            assert re.fullmatch(r'RET [0-9a-fA-F]{1,8}', row['reply'])
            assert int(row['reply'][4:], 16) == row['value']
        assert data['writes_may_have_been_sent'] == len(writes)
        before = {r['offset']:r['value'] for r in data['rows'] if r['phase'] == 'guard'}
        after = {r['offset']:r['value'] for r in data['rows'] if r['phase'] == 'final_guard'}
        assert before[0x3fffc] == 0 and before[0x5fffc] == 1 and before[0x2000] == 7
        assert before[0x1100] & 0xe0 == 0x80 and before[0x1000] == 0 and before[0x1004] & 1 == 0
        assert all(after[off] == value for off, value in before.items() if off != 0x1100)
        if name == 'idle-configuration':
            assert len(writes) == 23 and len({r['offset'] for r in writes}) == 23
            assert all(r['write'] == r['value'] for r in writes) and before == after
        else:
            assert [(r['write'],r['value']) for r in writes] == [(0x88,0x70088),(8,0x70008),(0x100f,0x7100f),(0x7138f,0x7138f)]
            assert all(r['offset'] == 0x1100 for r in writes)
        observations[name] = writes
        pins += [pin(path), pin(probe/(script+'.aot-private.py'))]
    return observations, pins


def check(xbe, observer, probe, handoff, output):
    assert 'build' in output.resolve().parts and not output.exists()
    output.mkdir(parents=True)
    blob = xbe.read_bytes()
    assert hashlib.sha256(blob).hexdigest() == IMAGE_SHA256
    observed, pins = physical(probe)
    retained = json.loads(handoff.read_text())
    for key in ('kernel','physical_observation','tool'):
        item=retained[key];assert pin(Path(item['path']))==item;pins.append(item)
    entry=json.loads(Path(retained['physical_observation']['path']).read_text())
    for item in entry['pins']:
        actual=pin(Path(item['path']));assert actual['sha256']==item['sha256'];pins.append(actual)
    assert entry['startup_audio']['phase']=='main-entry-before-video-network'
    assert entry['startup_audio']['fectl']==0x71f8f
    kernel=KernelHandoff(Path(retained['kernel']['path']).read_bytes(),7)
    kernel.call(0x800654dc);kernel.call(0x80067442,(1,))
    assert kernel.get(0xfe801100)==0x108f
    assert (entry['startup_audio']['fectl']&~0x70f00)==kernel.get(0xfe801100)
    pins.append(pin(handoff))
    lives = []
    for lifetime in range(3):
        original = Initializer(blob, lifetime, lifetime)
        original.produce_startup()  # explicit prepared objects; no live device transport yet
        rows = []
        with (output/f'observer-{lifetime}.aot-private.log').open('w') as log:
            with subprocess.Popen([str(observer.resolve()), '--transport'], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=log, text=True, bufsize=1) as process:
                assert process.stdout.readline().strip() == 'READY'
                def transport(op, address, size, value):
                    process.stdin.write(f'{op} {address:x} {size} {value:x}\n'); process.stdin.flush()
                    line = process.stdout.readline().strip()
                    assert re.fullmatch(r'OK [0-9a-fA-F]{8}', line), line
                    result = int(line[3:], 16)
                    rows.append(dict(operation=op,address=address,size=size,value=value,observed=result))
                    return result
                original.transport = transport
                assert transport('R',0xfe801100,4,0)==0x71f8f
                # Actual original mode0 helper preserves the captured status
                # and trap reason before the complete initializer replaces it.
                original.call(0x17bc43,(0,),original.owner)
                assert transport('R',0xfe801100,4,0)==0x71f8f
                original.call(0x17bf66, ecx=original.owner)
                for reg in observed['idle-configuration']:
                    assert transport('R',0xfe800000+reg['offset'],4,0) == reg['value']
                assert transport('R',0xfe801100,4,0) == 0x7100f
                assert transport('R',0xfe802000,4,0) == 7
                assert transport('R',0xfe80200c,4,0) == 0
                assert transport('R',0xfe801004,4,0) == 0xd8
                assert transport('R',0xfe801510,4,0) == 0
                original.transport = None
                process.stdin.close();assert process.wait(timeout=10) == 0
        writes = [r for r in rows if r['operation'] == 'W']
        for reg in observed['idle-configuration']:
            assert any(r['address']==0xfe800000+reg['offset'] and r['value']==reg['write'] for r in writes)
        controls = [r for r in writes if r['address']==0xfe801100][-3:]
        assert [(r['value'] & ~0x70000,r['observed']) for r in controls] == [(0x88,0x70088),(8,0x70008),(0x100f,0x7100f)]
        original.destroy_startup()
        assert not any(a['live'] for a in original.allocations)
        lives.append(dict(lifetime=lifetime,transactions=rows,source_identity=original.source_identity(),
                          prepared_objects_released=True,natural_initializer_return=True))
    report = output/'original-initialization.aot-private.json'
    report.write_text(json.dumps(dict(status='stopped-initialization-transport-observed',
        pins=pins+[pin(xbe),pin(observer),pin(Path(__file__))],lifetimes=lives,
        hardware_writes=27,hardware_access_in_this_tool=False,
        kernel_handoff=dict(physical_fectl=entry['startup_audio']['fectl'],kernel_calls=kernel.calls,kernel_source_identity=kernel.sources),
        limits='Prepared original objects and an initialized-kernel device are explicit caller inputs. Actual initializer instructions and RMW reads use the maintained register owner. Physical evidence covers stopped latched configuration and the three FE controls, not active field effects, DSP scheduling, active FE completion or connected title output.'),indent=2)+'\n')
    print(f'PASS: three original initializer returns,23 physical configuration fields and live FE RMW/status transport; {report}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','observer','probe-results','handoff-report','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args = parser.parse_args()
    check(args.xbe,args.observer,args.probe_results,args.handoff_report,args.output)
