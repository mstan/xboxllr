#!/usr/bin/env python3
"""Record original startup reset requests and bind retained physical bootstrap evidence.

The original driver runs on the existing authored caller/allocation floor.
Passive MMIO records programming, not DSP completion. Owned bytes remain private.
"""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

from unicorn.x86_const import UC_X86_REG_EIP
from native_ep_startup_reference import OriginalStartup
from native_ep_reference import IMAGE_SHA256


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def pin(path):
    path = path.resolve()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha(path.read_bytes()))


class ResetStartup(OriginalStartup):
    def __init__(self, blob, lifetime, selector):
        self.boots = []
        super().__init__(blob, lifetime, selector)

    def _write(self, cpu, access, address, size, value, unused):
        super()._write(cpu, access, address, size, value, unused)
        if 0xfe800000 <= address < 0xfe860000:
            self.mmio[-1]['pc'] = cpu.reg_read(UC_X86_REG_EIP)
        if address not in (0xfe83fffc, 0xfe85fffc) or value != 3:
            return
        ep = address == 0xfe85fffc
        table = self.get(0xfe802048 if ep else 0xfe802040)
        maximum = self.get(0xfe8020dc if ep else 0xfe8020d4)
        ram = self.physical_ram()
        assert maximum >= 1 and table & 7 == 0
        pages = [struct.unpack_from('<I', ram, table+i*8)[0] for i in range(2)]
        assert all(not a & 4095 and a+4096 <= len(ram) for a in pages)
        raw = b''.join(ram[a:a+4096] for a in pages)
        self.boots.append(dict(kind='ep' if ep else 'gp', table=table,
                              maximum=maximum, pages=pages, raw=raw))


def check(args):
    out = args.output.resolve()
    assert 'build' in out.parts
    out.mkdir(parents=True, exist_ok=True)
    blob = args.xbe.read_bytes()
    assert sha(blob) == IMAGE_SHA256
    evidence = []
    gp = args.probe_results/'qualification-20261013-resume1'
    accepted = json.loads((gp/'accepted-hardware-milestone.aot-private.json').read_text())
    evidence.append(pin(gp/'accepted-hardware-milestone.aot-private.json'))
    for name, reply in (('marker-start', 'RET 3'), ('marker-observe', 'RET d5000001')):
        path = gp/name/'observation.aot-private.json'
        row = json.loads(path.read_text())
        assert sha(path.read_bytes()) == accepted['observations'][name]['sha256']
        assert row['reply'] == reply
        payload = gp/name/'payload.bin'
        assert sha(payload.read_bytes()) == row['payload_sha256']
        evidence.extend((pin(path), pin(payload)))
    ep = args.probe_results/'ep-startup-registers-20261014-v2'
    record = json.loads((ep/'execution.aot-private.json').read_text())
    assert record['status'] == 'observed' and record['loaded_P_verified'] == 2048
    journal = [json.loads(line) for line in (ep/'execution.aot-private.jsonl').read_text().splitlines()]
    for name, reply in (('start', 'RET 3'), ('verify_p', 'RET d8000006')):
        rows = [row for row in journal if row['phase'] == name]
        assert len(rows) == 2 and rows[-1]['reply'] == reply
        payload = ep/(name+'.payload.bin')
        assert sha(payload.read_bytes()) == rows[0]['sha256']
        evidence.append(pin(payload))
    evidence.extend(pin(ep/name) for name in ('execution.aot-private.json', 'execution.aot-private.jsonl',
                                            'program.aot-private.bin', 'run.aot-private.py'))
    lives = []
    for life in range(3):
        producer = ResetStartup(blob, life, life)
        producer.produce_startup()
        controls = [row for row in producer.mmio if row['address'] >= 0xfe830000]
        for address in (0xfe83fffc, 0xfe85fffc):
            assert [row['value'] for row in controls if row['address'] == address] == [1, 3]
        boots = []
        for boot in producer.boots:
            raw = boot.pop('raw')
            path = out/f"original-{life}-{boot['kind']}.aot-private.bin"
            path.write_bytes(raw)
            boots.append(dict(**boot, image=pin(path)))
        assert [b['kind'] for b in boots] == ['gp', 'ep']
        producer.destroy_startup()
        run = None
        if args.observer:
            completed = subprocess.run([str(args.observer.resolve()), *(b['image']['path'] for b in boots)],
                                       capture_output=True, text=True, timeout=20)
            assert completed.returncode == 0, completed.stdout+completed.stderr
            log = out/f'original-{life}-bootstrap.aot-private.log'
            log.write_text(completed.stdout+completed.stderr)
            run = pin(log)
        lives.append(dict(controls=controls, bootstraps=boots, observer=run,
                          all_allocations_freed=True, instructions=producer.source_identity()))
    result = dict(title=pin(args.xbe), tool=pin(Path(__file__)), physical_evidence=evidence,
                  lifetimes=lives, observer=pin(args.observer) if args.observer else None,
                  limits=['Original driver programming with authored caller/allocation services',
                          'Retained physical programs prove bounded release/load, not exact boot latency',
                          'Held host-bank aperture, warm reset after active work and DSP cadence remain separate',
                          'Host control registers FF00/FF04/FF10/FF14/FF5C are recorded, not admitted'])
    (out/'reset-reference.aot-private.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS three original setup/destruction lifetimes; six bootstrap images; retained GP/EP physical release/load')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--probe-results', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--observer', type=Path)
    check(parser.parse_args())
