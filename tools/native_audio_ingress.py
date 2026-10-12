#!/usr/bin/env python3
"""Inspect a completed opt-in native audio capture and extract one PCM voice.

Checks bytes at submission against the latest caller-written ring and actual
captured loop controls. Version1 has no loop provenance and is rejected. This does
not prove consumption, uninterrupted playback, or correct speaker output.
Buffer identities are process-local capture IDs, not title addresses. Choose
the relevant lifetime using the file-open and audio timeline observations.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


HEADER = struct.Struct('<4I3Q10I')


def submission_ranges(offset, size, capacity, start, end, looping):
    """Independent byte extents of one actual feeder submission.

    A seek before the loop plays its prefix; a seek beyond its end plays the
    remaining capacity before entering the loop. Small loops can repeat within
    one chunk. Caller writes always use capacity, independently of this route.
    """
    if not looping:
        if size > capacity - offset:
            raise ValueError('finite submission wraps beyond the owned ring')
        yield offset, size
        return
    boundary = end if offset < end else capacity
    first = min(size, boundary - offset)
    yield offset, first
    remaining = size - first
    while remaining:
        count = min(remaining, end - start)
        yield start, count
        remaining -= count


def inspect(capture, log, identity, window=None):
    markers = re.findall(r'\[AUDIO INGRESS\] bytes=(\d+) overflow=(\d+)(?: window=(\d+))?', log.read_text())
    if window is not None:
        markers = [x for x in markers if x[2] == str(window)]
    if len(markers) != 1 or int(markers[0][1]):
        raise ValueError('capture must be finished with zero overflow')
    data = capture.read_bytes()
    if len(data) != int(markers[0][0]):
        raise ValueError('capture size differs from completion record')
    cursor = 0
    inventory = {}
    ring = None
    known = None
    shape = None
    submitted = bytearray()
    mismatches = 0
    pointer_owner = {}
    ended = set()
    controls = None
    last_time = None
    while cursor < len(data):
        if len(data) - cursor < HEADER.size:
            raise ValueError('partial capture header')
        magic, version, kind, header = struct.unpack_from('<4I', data, cursor)
        if version == 1:
            raise ValueError('capture lacks actual loop metadata (version1)')
        (magic, version, kind, header, time, voice, pointer, rate, channels,
         bits, capacity, offset, size, start, end, looping, playing) = HEADER.unpack_from(data, cursor)
        if (magic != 0x50414c58 or version != 2 or header != HEADER.size or
                kind not in (1, 2, 3, 4, 5) or not voice or not pointer or
                channels not in (1, 2) or bits not in (8, 16) or not 100 <= rate <= 200000 or
                not capacity or not 0 <= offset < capacity or
                size > len(data) - cursor - header or
                (last_time is not None and time < last_time)):
            raise ValueError(f'invalid record at byte {cursor}')
        last_time = time
        alignment = channels * (bits // 8)
        if (capacity % alignment or offset % alignment or size % alignment or
                not 0 <= start < end <= capacity or start % alignment or end % alignment or
                looping not in (0, 1) or playing not in (0, 1)):
            raise ValueError('invalid or unaligned actual loop metadata')
        if ((kind in (1, 3) and not 0 < size <= capacity) or
                (kind == 2 and (not 0 < size <= 2048 or not playing)) or
                (kind in (4, 5) and (size or offset))):
            raise ValueError('invalid record payload or playback state')
        if kind == 3 and (offset != 0 or size != capacity):
            raise ValueError('initial ring snapshot must cover the complete owned ring')
        payload = data[cursor + header:cursor + header + size]
        cursor += header + size
        if voice in ended:
            raise ValueError('record follows the retired buffer lifetime')
        row = inventory.setdefault(voice, dict(id=voice, pointer=pointer, first_time=time,
            last_time=time, writes=0, snapshots=0, submissions=0, submitted_bytes=0,
            controls=0, destroys=0, loop_states=[]))
        if row['pointer'] != pointer:
            raise ValueError('capture ID changed its owned buffer pointer')
        if pointer in pointer_owner and pointer_owner[pointer] != voice:
            raise ValueError('buffer pointer reused before captured lifetime retirement')
        pointer_owner[pointer] = voice
        row['last_time'] = time
        row[{1: 'writes', 2: 'submissions', 3: 'snapshots', 4: 'controls', 5: 'destroys'}[kind]] += 1
        state = dict(start=start, end=end, looping=looping, playing=playing)
        if not row['loop_states'] or row['loop_states'][-1] != state:
            row['loop_states'].append(state)
        if kind == 5:
            ended.add(voice)
            del pointer_owner[pointer]
        if kind == 2:
            row['submitted_bytes'] += size
        if voice != identity:
            continue
        if shape is None:
            shape = (rate, channels, bits, capacity)
            if bits != 16 or channels not in (1, 2) or capacity % (2 * channels):
                raise ValueError('extraction requires aligned mono/stereo PCM16')
            ring = bytearray(capacity)
            known = bytearray(capacity)
        if shape != (rate, channels, bits, capacity):
            raise ValueError('voice format changed or payload is not frame aligned')
        current_controls = (start, end, looping, playing)
        if controls is not None and current_controls != controls and kind not in (3, 4):
            raise ValueError('loop/playback controls changed without a captured control record')
        controls = current_controls
        if kind in (4, 5):
            continue
        first = min(size, capacity - offset)
        if kind in (1, 3):
            ring[offset:offset + first] = payload[:first]
            ring[:size - first] = payload[first:]
            known[offset:offset + first] = b'\1' * first
            known[:size - first] = b'\1' * (size - first)
        else:
            expected = bytearray()
            for begin, count in submission_ranges(offset, size, capacity, start, end, looping):
                if 0 in known[begin:begin + count]:
                    raise ValueError('submission contains bytes without captured caller ownership')
                expected.extend(ring[begin:begin + count])
            if payload != expected:
                mismatches += sum(a != b for a, b in zip(payload, expected))
            submitted.extend(payload)
    if shape is None or not submitted:
        raise ValueError(f'voice {identity} has no captured PCM submissions')
    report = dict(capture_sha256=hashlib.sha256(data).hexdigest(),
        capture_bytes=len(data), capture_version=2, capture_window=window, inventory=list(inventory.values()),
        selected_id=identity, rate=shape[0], channels=shape[1], bits=shape[2],
        ring_byte_mismatches=mismatches,
        submitted_frames=len(submitted) // (2 * shape[1]),
        limits=['Submission is not consumption; correlate the real cursor/output.',
                'Concatenated submissions may span seeks, stops or multiple plays.'])
    return report, submitted


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--buffer', type=int, required=True)
    parser.add_argument('--window', type=int, help='explicit diagnostic window index')
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--pcm', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists() or args.pcm.exists():
        parser.error('output paths must be new')
    report, pcm = inspect(args.capture, args.log, args.buffer, args.window)
    with args.pcm.open('xb') as out:
        out.write(pcm)
    with args.report.open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return int(report['ring_byte_mismatches'] != 0)


if __name__ == '__main__':
    raise SystemExit(main())
