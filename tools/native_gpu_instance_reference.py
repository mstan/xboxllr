#!/usr/bin/env python3
"""Execute owned original GPU instance reservation and protection queries.

No replaced callees. The captured kernel starts with a 64KiB reservation;
compare real PTE/protection effects when the caller reduces that reservation.
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
        output = 0x10000200
        ref.cpu.mem_write(output, b'\xcd' * 8)
        top = ref.call(168, 0xffffffff, output)
        assert top == 0x83ff0000
        assert bytes(ref.cpu.mem_read(output, 8)) == struct.pack('<II', 0x10000, 0xcdcdcdcd)
        original = 0x10000
        for requested in (0x5000, 0x5000, 0x4000):
            released = top - original
            retained = top - requested
            assert ref.call(179, retained) == 0x204
            assert ref.call(168, requested, output) == top
            assert struct.unpack('<I', ref.cpu.mem_read(output, 4))[0] == 0x10000
            if requested < original:
                assert ref.call(179, released) == 0
            assert ref.call(179, retained) == 0x204
            assert ref.call(168, 0xffffffff, output) == top
            rows.append(dict(lifetime=lifetime, requested=requested, previous=original,
                             top=top, padding=0x10000, retained_protection=0x204,
                             released_protection=0 if requested < original else None))
            original = requested
        del ref
        gc.collect()
    return dict(snapshot_sha256=SNAPSHOT_SHA, cases=rows,
                scope='original query/shrink/repeat and real page release; not PRAMIN alias or growth qualification')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = run(args.snapshot)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"PASS: {len(result['cases'])} original GPU reservation cases across 3 lifetimes")
