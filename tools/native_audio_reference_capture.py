#!/usr/bin/env python3
"""Verify actual reference APU DMA bytes against the preceding owned writes.

This is reference-model evidence. QPC stamps are observations, while an engine's
PIT epoch plus actual emitted frames defines its audio timeline. DMA prefetch is
not speaker retirement. Missing lifecycle/bytes and partial records fail closed.
"""
import argparse
import hashlib
import json
import math
import re
import struct
from array import array
from pathlib import Path

HEADER = struct.Struct('<4I6Q10I')


def inspect(path, output=None, log=None):
    rings = {}
    rows = []
    engine = epoch = previous_time = None
    counts = {str(k): 0 for k in range(1, 10)}
    mismatches = 0
    source = {}
    generations = {}
    with path.open('rb') as file:
        while raw := file.read(HEADER.size):
            if len(raw) != HEADER.size:
                raise ValueError('partial reference record')
            (magic, version, kind, header, qpc, ticks, eid, voice, frame, fetched,
             rate, channels, bits, capacity, offset, size, cbo, flags,
             loop_start, loop_bytes) = HEADER.unpack(raw)
            if (magic != 0x52414c58 or version != 1 or header != HEADER.size or
                    kind not in range(1, 10) or not voice or not eid or
                    channels not in (1, 2) or bits not in (8, 16) or
                    not 0 < capacity <= 1024 * 1024 or flags & ~7 or
                    not 0 <= offset < capacity or size > capacity or
                    not 0 <= cbo < capacity // (channels * bits // 8) or
                    loop_start >= capacity or not 0 < loop_bytes <= capacity - loop_start):
                raise ValueError('invalid reference record')
            if engine is None:
                engine, epoch = eid, ticks
            if eid != engine or ticks != epoch or (previous_time is not None and qpc < previous_time):
                raise ValueError('engine/epoch changed or QPC observation went backwards')
            previous_time = qpc
            payload = file.read(size)
            if len(payload) != size:
                raise ValueError('partial reference payload')
            counts[str(kind)] += 1
            if kind == 3:
                if voice in rings or offset or size != capacity:
                    raise ValueError('missing unique complete initial ring ownership')
                rings[voice] = dict(data=bytearray(payload), shape=(channels, bits, capacity),
                                    destroyed=False, writes=0, reads=0, bytes=0, rate=rate,
                                    rate_history=[],source_bytes_by_rate={},
                                    first_frame=frame, last_frame=frame, stop_frames=[], play_frames=[],
                                    fetched=fetched)
                source[voice] = bytearray()
                generations[voice] = dict(current=array('I', [0])*capacity,
                                          seen=array('i', [-1])*capacity, next=0,
                                          repeated=0, fresh=0, initialized=0)
            elif voice not in rings or rings[voice]['destroyed']:
                raise ValueError('record outside its captured live ring lifetime')
            ring = rings[voice]
            if ring['shape'] != (channels, bits, capacity):
                raise ValueError('ring format changed within a lifetime')
            if frame < ring['last_frame'] or fetched < ring['fetched']:
                raise ValueError('actual output/source counters went backwards')
            ring['last_frame'], ring['fetched'] = frame, fetched
            if not ring['rate_history'] or ring['rate_history'][-1]['rate'] != rate:
                ring['rate_history'].append(dict(frame=frame,rate=rate))
            if kind == 1:
                if not size:
                    raise ValueError('empty caller write')
                first = min(size, capacity - offset)
                ring['data'][offset:offset + first] = payload[:first]
                ring['data'][:size - first] = payload[first:]
                ring['writes'] += 1
                owner = generations[voice]
                owner['next'] += 1
                owner['current'][offset:offset+first] = array('I', [owner['next']])*first
                owner['current'][:size-first] = array('I', [owner['next']])*(size-first)
            elif kind == 2:
                if not size or offset + size > capacity:
                    raise ValueError('DMA extent outside actual contiguous owned bytes')
                expected = ring['data'][offset:offset + size]
                mismatches += sum(a != b for a, b in zip(payload, expected))
                source[voice].extend(payload)
                ring['reads'] += 1
                ring['bytes'] += size
                key=str(rate)
                ring['source_bytes_by_rate'][key]=ring['source_bytes_by_rate'].get(key,0)+size
                owner = generations[voice]
                current = owner['current'][offset:offset+size]
                seen = owner['seen'][offset:offset+size]
                repeated = sum(a == b for a,b in zip(current,seen))
                initialized = sum(a == 0 and b != 0 for a,b in zip(current,seen))
                owner['repeated'] += repeated
                owner['initialized'] += initialized
                owner['fresh'] += size-repeated-initialized
                owner['seen'][offset:offset+size] = array('i', current)
            elif kind in (4, 5, 6, 7, 8, 9):
                if size:
                    raise ValueError('control record contains an invented payload')
                if kind == 4:
                    ring['play_frames'].append(frame)
                elif kind == 5:
                    ring['stop_frames'].append(frame)
                elif kind == 6:
                    ring['destroyed'] = True
            rows.append(dict(kind=kind,voice=voice,frame=frame,qpc=qpc,cbo=cbo,
                             fetched=fetched,rate=rate,flags=flags,offset=offset,bytes=size,
                             loop_start=loop_start,loop_bytes=loop_bytes))
    if not rows:
        name = re.fullmatch(r'engine-(\d+)\.dma\.bin', path.name)
        if log is None or name is None:
            raise ValueError('empty capture without finalized engine identity')
        endings = re.findall(r'\[REFERENCE CAPTURE END\] engine=(\d+) epoch_ticks=(\d+) frames=(\d+) record_bytes=(\d+) records=(\d+) overflow_or_io_error=(\d+) bounded_limit=(\d+)', log.read_text(errors='replace'))
        endings = [e for e in endings if e[0] == name[1]]
        if len(endings) != 1 or any(int(endings[0][i]) for i in (3,4,5)):
            raise ValueError('empty capture lacks zero-record successful finalization')
        engine, epoch = map(int, endings[0][:2])
    report = dict(engine=engine,epoch_pit_ticks=epoch,record_count=len(rows),kinds=counts,
                  ring_byte_mismatches=mismatches,source_dma_is_prefetch=True,
                  capture_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                  voices=[dict(id=voice, **{k:v for k,v in ring.items() if k != 'data'})
                          for voice,ring in rings.items()])
    for voice in report['voices']:
        owner = generations[voice['id']]
        voice.update(repeated_before_another_write_bytes=owner['repeated'],
                     fresh_caller_bytes=owner['fresh'],first_initialized_read_bytes=owner['initialized'])
    if output is not None:
        samples = array('f')
        with output.open('rb') as file:
            samples.frombytes(file.read())
        if len(samples) % 6 or not all(math.isfinite(x) for x in samples):
            raise ValueError('partial or nonfinite six-channel GP output')
        report['output_frames'] = len(samples) // 6
        report['output_sha256'] = hashlib.sha256(output.read_bytes()).hexdigest()
    if log is not None:
        matches = re.findall(r'\[REFERENCE CAPTURE END\] engine=(\d+) epoch_ticks=(\d+) frames=(\d+) record_bytes=(\d+) records=(\d+) overflow_or_io_error=(\d+) bounded_limit=(\d+)', log.read_text(errors='replace'))
        matches = [tuple(map(int,m)) for m in matches if int(m[0]) == engine]
        if len(matches) != 1 or matches[0][5] or matches[0][1] != epoch or matches[0][3] != path.stat().st_size or matches[0][4] != len(rows):
            raise ValueError('capture lacks its exact zero-error finalization')
        if output is not None and matches[0][2] != report['output_frames']:
            raise ValueError('actual GP frame count differs from finalization')
        report['finalized'] = True
        report['bounded_limit'] = bool(matches[0][6])
    return report, source, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--log', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    report, _, _ = inspect(args.capture, args.output, args.log)
    with args.report.open('x') as file:
        file.write(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return int(bool(report['ring_byte_mismatches']))


if __name__ == '__main__':
    raise SystemExit(main())
