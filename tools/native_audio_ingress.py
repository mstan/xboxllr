#!/usr/bin/env python3
"""Inspect a completed opt-in native audio capture and extract one PCM voice.

Checks bytes at submission against the latest caller-written ring. This does
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


HEADER = struct.Struct('<4I3Q6I')


def inspect(capture, log, identity):
    markers = re.findall(r'\[AUDIO INGRESS\] bytes=(\d+) overflow=(\d+)', log.read_text())
    if len(markers) != 1 or int(markers[0][1]):
        raise ValueError('capture must be finished with zero overflow')
    data = capture.read_bytes()
    if len(data) != int(markers[0][0]):
        raise ValueError('capture size differs from completion record')
    cursor = 0
    inventory = {}
    ring = None
    shape = None
    submitted = bytearray()
    mismatches = 0
    while cursor < len(data):
        if len(data) - cursor < HEADER.size:
            raise ValueError('partial capture header')
        (magic, version, kind, header, time, voice, pointer, rate, channels,
         bits, capacity, offset, size) = HEADER.unpack_from(data, cursor)
        if (magic != 0x50414c58 or version != 1 or header != HEADER.size or
                kind not in (1, 2) or not 0 < size <= capacity or
                not 0 <= offset < capacity or size > len(data) - cursor - header):
            raise ValueError(f'invalid record at byte {cursor}')
        payload = data[cursor + header:cursor + header + size]
        cursor += header + size
        row = inventory.setdefault(voice, dict(id=voice, first_time=time,
            last_time=time, writes=0, submissions=0, submitted_bytes=0))
        row['last_time'] = time
        row['writes' if kind == 1 else 'submissions'] += 1
        if kind == 2:
            row['submitted_bytes'] += size
        if voice != identity:
            continue
        if shape is None:
            shape = (rate, channels, bits, capacity)
            if bits != 16 or channels not in (1, 2) or capacity % (2 * channels):
                raise ValueError('extraction requires aligned mono/stereo PCM16')
            ring = bytearray(capacity)  # Native PCM16 buffers initialize to zero.
        if shape != (rate, channels, bits, capacity) or size % (2 * channels):
            raise ValueError('voice format changed or payload is not frame aligned')
        first = min(size, capacity - offset)
        if kind == 1:
            ring[offset:offset + first] = payload[:first]
            ring[:size - first] = payload[first:]
        else:
            expected = ring[offset:offset + first] + ring[:size - first]
            if payload != expected:
                mismatches += sum(a != b for a, b in zip(payload, expected))
            submitted.extend(payload)
    if shape is None or not submitted:
        raise ValueError(f'voice {identity} has no captured PCM submissions')
    report = dict(capture_sha256=hashlib.sha256(data).hexdigest(),
        capture_bytes=len(data), inventory=list(inventory.values()),
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
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--pcm', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists() or args.pcm.exists():
        parser.error('output paths must be new')
    report, pcm = inspect(args.capture, args.log, args.buffer)
    with args.pcm.open('xb') as out:
        out.write(pcm)
    with args.report.open('x') as out:
        out.write(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return int(report['ring_byte_mismatches'] != 0)


if __name__ == '__main__':
    raise SystemExit(main())
