#!/usr/bin/env python3
"""Bind real original GP helper execution and preserve the next whole-boot guard.

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
    output.mkdir(parents=True, exist_ok=True)
    boot = output/'original-gp-frame-source.aot-private.bin'
    boot.write_bytes(blob[offset:offset+size])
    runs = []
    for mode in ('--original-helpers', '--original-boot'):
        run = subprocess.run([str(processor), mode, str(boot)],
                             capture_output=True, text=True, timeout=20)
        log = output/f'original-gp-frame-{mode[2:]}.aot-private.log'
        log.write_text(run.stdout+run.stderr, newline='\n')
        if mode == '--original-helpers':
            passed = run.returncode == 0 and 'original GP helpers155/D2/160' in run.stdout
        else:
            passed = (run.returncode == 2 and 'expected frame guard' in run.stderr and
                      'unqualified GP peripheral write' in run.stderr and
                      'at ffffb2' in run.stderr)
        runs.append(dict(mode=mode, exit=run.returncode, expected_result=passed,
                         log_sha256=sha(log)))
    record = dict(image_sha256=IMAGE_SHA256, data_address=hex(address), data_size=size,
                  data_sha256=sha(boot), processor_sha256=sha(processor),
                  tool_sha256=sha(Path(__file__)), runs=runs,
                  original_helpers_qualified=runs[0]['expected_result'],
                  original_boot_qualified=False,
                  boundary='Unchanged original entry reaches unowned FFFFB2=FFFFFF clock '
                           'configuration after actual startup clear, memory initialization '
                           'and DMA context setup. No clock behavior is substituted.',
                  limits='Isolated unchanged helpers155/D2/160 and actual authored-caller '
                         'yield; mapped8KiB bootstrap profile, not hardware bootstrap, '
                         'original full scratch/graph, timer producer, cadence or EP/output.')
    (output/'original-gp-frame-reference.aot-private.json').write_text(
        json.dumps(record, indent=2)+'\n', newline='\n')
    if not all(r['expected_result'] for r in runs):
        raise RuntimeError('original helper/guard changed; inspect preserved private evidence')
    return dict(original_helpers_qualified=True, original_boot_qualified=False,
                guarded_address='0xffffb2', data_bytes=size)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--processor', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.image, args.processor, args.output)))
