#!/usr/bin/env python3
"""Execute original AV queries over the owned hash-bound kernel capture.

No replaced callees or encoder/device state supplied. This qualifies these
software queries only; it does not establish video output or SMBus behavior.
"""
import argparse
import gc
import json
import struct
from pathlib import Path
from native_io_device_reference import Reference, SNAPSHOT_SHA


def run(snapshot):
    rows = []
    for lifetime in range(3):
        ref = Reference(snapshot)
        ref.enable_kernel_segments()
        for option, expected in ((6, 0x004e0101), (13, 0), (16, 0), (17, 0), (6, 0x004e0101)):
            ref.cpu.mem_write(0x10000200, struct.pack('<III', 0x11223344, 0xcdcdcdcd, 0x55667788))
            ref.call(2, 0, option, 0, 0x10000204)
            before, value, after = struct.unpack('<III', ref.cpu.mem_read(0x10000200, 12))
            assert (before, value, after) == (0x11223344, expected, 0x55667788)
            rows.append(dict(lifetime=lifetime, option=option, output=value, guards_preserved=True))
        del ref
        gc.collect()
    return dict(snapshot_sha256=SNAPSHOT_SHA, cases=rows,
                scope='original capabilities/current-mode/encoder-type/table-version query ABI; no device qualification')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = run(args.snapshot)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"PASS: {len(result['cases'])} original AV queries across three lifetimes")
