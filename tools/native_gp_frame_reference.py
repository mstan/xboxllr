#!/usr/bin/env python3
"""Bind real original GP helper execution and record the bounded prefix continuation.

All original firmware bytes and execution reports remain private under build/.
Helper qualification uses authored callers of unchanged original functions;
it does not qualify full firmware boot, hardware cadence, timers or output.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from aot_inventory import parse_xbe

IMAGE_SHA256 = 'e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(image, processor, output):
    image, processor, output = image.resolve(), processor.resolve(), output.resolve()
    if 'build' not in output.parts:
        raise ValueError('original evidence must stay in a private build directory')
    blob = image.read_bytes()
    if hashlib.sha256(blob).hexdigest() != IMAGE_SHA256:
        raise ValueError('unreviewed original title image')
    address, size = 0x17d7e8, 0x5cc
    section = next(s for s in parse_xbe(blob)['sections']
                   if s['address'] <= address and address+size <= s['address']+s['raw_size'])
    offset = section['raw_offset']+address-section['address']
    output.mkdir(parents=True, exist_ok=False)
    boot = output/'original-gp-frame-prefix.aot-private.bin'
    boot.write_bytes(blob[offset:offset+size])
    runs = []
    for mode in ('--original-helpers', '--original-boot'):
        run = subprocess.run([str(processor), mode, str(boot)],
                             capture_output=True, text=True, timeout=20)
        log = output/f'original-gp-frame-current-{mode[2:]}.aot-private.log'
        log.write_text(run.stdout+run.stderr, newline='\n')
        if mode == '--original-helpers':
            passed = run.returncode == 0 and 'original GP helpers155/D2/160' in run.stdout
        else:
            records = [json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
            expected = dict(boundary='first-C4-yield', pc=0x2b, power=0, yielded=True,
                            dma_nodes=0, frame_outstanding=False, ram_unchanged=True,
                            original_prefix_loaded=True)
            passed = run.returncode == 0 and records == [expected]
        runs.append(dict(mode=mode, exit=run.returncode, expected_result=passed,
                         log_sha256=sha(log)))
    record = dict(image_sha256=IMAGE_SHA256, data_address=hex(address), data_size=size,
                  data_sha256=sha(boot), processor_sha256=sha(processor),
                  tool_sha256=sha(Path(__file__)), runs=runs,
                  original_helpers_qualified=runs[0]['expected_result'],
                  original_prefix_continuation_verified=runs[1]['expected_result'],
                  observed_prefix=records, original_full_frame_qualified=False,
                  gp_profile_precision_deferred=True,
                  original_boot_qualified=False,
                  boundary='Unchanged original entry reaches first C4 yield PC002B '
                           'after the exact supported GP composite profile. '
                           'No original frame wake/resume or output is fabricated.',
                  limits='Isolated unchanged helpers155/D2/160 and actual authored-caller '
                         'yield; mapped8KiB bootstrap profile, not hardware bootstrap, '
                         'original full scratch/graph, precise profiling clock, cadence or EP/output.')
    (output/'original-gp-frame-continuation.aot-private.json').write_text(
        json.dumps(record, indent=2)+'\n', newline='\n')
    if not all(r['expected_result'] for r in runs):
        raise RuntimeError('original helper/prefix boundary changed; inspect preserved private evidence')
    return dict(original_helpers_qualified=True, original_boot_qualified=False,
                original_prefix_continuation_verified=True, original_full_frame_qualified=False,
                boundary_pc=0x2b, data_bytes=size)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--processor', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.image, args.processor, args.output)))
