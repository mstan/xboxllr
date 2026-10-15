#!/usr/bin/env python3
"""Check retained physical counter observations against original control writes.

Offline only. This does not contact a console, infer a precise hardware clock
phase, or treat an emulator's output as a replacement for physical evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from aot_inventory import parse_xbe


def pin(path):
    path = path.resolve()
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def check(probe, handoff, output):
    assert 'build' in output.resolve().parts and not output.exists()
    source = json.loads(handoff.read_text())
    pins = [pin(handoff), pin(Path(__file__))]
    for name in ('kernel', 'title', 'tool'):
        item = source[name]
        assert pin(Path(item['path'])) == item, name
        pins.append(item)
    blob = Path(source['title']['path']).read_bytes()
    sections = parse_xbe(blob)['sections']
    identities = 0
    assert len(source['title_control_lifetimes']) == 3
    for life in source['title_control_lifetimes']:
        writes = [e for e in life['title_init'] if e['address'] == 0xfe80200c]
        assert writes == [dict(address=0xfe80200c, size=4, value=0)]
        assert [e['value'] for e in life['writes'] if e['address'] == 0xfe802000] == [7, 15, 23, 31, 7]
        for row in life['source_identity']:
            address, size = int(row['address'], 16), row['bytes']
            section = next(s for s in sections if s['address'] <= address and address+size <= s['address']+s['raw_size'])
            offset = section['raw_offset']+address-section['address']
            assert hashlib.sha256(blob[offset:offset+size]).hexdigest() == row['sha256']
            identities += 1

    records = {}
    definitions = (
        ('idle-counter-reset', 'reset-idle-counter', [(0x200c, 0)]),
        ('counter-mode1', 'observe-counter-mode', [(0x2000, 15), (0x2000, 7)]),
        ('counter-modes', 'observe-counter-modes', [(0x2000, v) for v in (15, 7, 23, 7, 31, 7)]),
    )
    for name, script, expected in definitions:
        path = probe/(name+'.aot-private.json')
        data = json.loads(path.read_text())
        assert data['status'] == 'observed' and data['storage_operations'] == 0
        assert data['automatic_retry'] is False
        writes = []
        previous_end = 0
        for row in data['rows']:
            address = 0xfe800000+row['offset']
            payload = b'\xa1'+struct.pack('<I', address)+b'\xc3'
            if row['write'] is not None:
                writes.append((row['offset'], row['write']))
                payload = b'\xc7\x05'+struct.pack('<II', address, row['write'])+payload
                assert row['value'] == row['write']
            assert row['payload'] == payload.hex()
            assert re.fullmatch(r'RET [0-9a-fA-F]{1,8}', row['reply'])
            assert int(row['reply'][4:], 16) == row['value']
            assert previous_end <= row['host_begin'] <= row['host_end']
            previous_end = row['host_end']
        assert writes == expected and data['writes_may_have_been_sent'] == len(expected)
        for offset, value in ((0x3fffc, 0), (0x5fffc, 1), (0x2000, 7)):
            guards = [r for r in data['rows'] if r['phase'] in ('guard', 'final_guard') and r['offset'] == offset]
            assert len(guards) == 2 and all(r['value'] == value for r in guards)
        records[name] = data
        pins += [pin(path), pin(probe/(script+'.aot-private.py'))]

    reset = records['idle-counter-reset']
    assert [r['value'] for r in reset['rows'] if r['phase'] in ('original_zero_write', 'later_idle')] == [0, 0]
    intervals = []
    for name in ('counter-mode1', 'counter-modes'):
        data = records[name]
        for phase, mask, expected in (('guard_fe_halted', 0xe0, 0x80), ('final_fe_halted', 0xe0, 0x80),
                                      ('guard_irq_masked', 1, 0), ('final_irq_masked', 1, 0)):
            row = next(r for r in data['rows'] if r['phase'] == phase)
            assert row['value'] & mask == expected
        samples = [r for r in data['rows'] if r['phase'] == 'mode1_sample']
        assert len(samples) == 3 and samples[0]['value'] < samples[1]['value'] < samples[2]['value']
        first, last = samples[0], samples[-1]
        lower = (last['host_begin']-first['host_end'])*48000/1e9
        upper = (last['host_end']-first['host_begin'])*48000/1e9
        delta = last['value']-first['value']
        assert lower-1 <= delta <= upper+1
        intervals.append(dict(run=name, sample_delta=delta, nominal_48k_bounds=[lower, upper]))
        stopped = [r['value'] for r in data['rows'] if r['phase'] in ('stopped_sample', 'mode1_stopped')]
        assert len(stopped) == 2 and stopped[0] == stopped[1] and stopped[0] >= last['value']
    prior = [r['value'] for r in records['counter-mode1']['rows'] if r['phase'] == 'stopped_sample'][-1]
    resume = next(r for r in records['counter-modes']['rows'] if r['phase'] == 'mode1_before')
    assert resume['value'] == prior
    assert next(r for r in records['counter-modes']['rows'] if r['phase'] == 'mode1_sample')['value'] > prior
    for mode in (2, 3):
        values = [r['value'] for r in records['counter-modes']['rows']
                  if r['phase'] in (f'mode{mode}_before', f'mode{mode}_sample', f'mode{mode}_stopped')]
        assert len(values) == 6 and len(set(values)) == 1
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(dict(status='bounded-counter-observed', original_lifetimes=3,
        original_instruction_identities=identities, hardware_writes=9, storage_operations=0,
        intervals=intervals, pins=pins,
        limits='Mode1 count, stopped retention, stopped zero write and resume; modes2/3 only while FE halted. Host intervals are consistent with the original 48k sample-rate contract, not a precision frequency measurement. Sub-sample phase is nominal; active modes2/3 and nonzero/running writes remain guarded. Not full startup or title output qualification.'), indent=2)+'\n')
    print(f'PASS: original zero/mode controls, nine matching volatile writes, gate/resume/retention and two bounded 48k intervals; {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe-results', type=Path, required=True)
    parser.add_argument('--handoff-report', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    check(args.probe_results, args.handoff_report, args.output)
