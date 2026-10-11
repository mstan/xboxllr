#!/usr/bin/env python3
"""Bind unchanged original GP linear helpers to real maintained SGE transport.

Original bytes stay private under build/. Authored callers isolate the original
DB/EB builders, 0126 transfer/EOL consumer and 005C control/parameter chain;
they do not admit the unknown timer setup or claim full firmware boot/output.
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
    boot = output/'original-gp-linear-source.aot-private.bin'
    boot.write_bytes(blob[offset:offset+size])
    runs = []
    for mode in ('--original-chain', '--original-program-guard'):
        run = subprocess.run([str(processor), mode, str(boot)],
                             capture_output=True, text=True, timeout=20)
        log = output/f'original-gp-linear-{mode[2:]}.aot-private.log'
        log.write_text(run.stdout+run.stderr, newline='\n')
        if mode == '--original-chain':
            passed = run.returncode == 0 and 'original GP linear DB/EB/0126/005C' in run.stdout
        else:
            passed = (run.returncode == 2 and 'expected linear executable guard before P/node publication' in run.stderr)
        runs.append(dict(mode=mode, exit=run.returncode, expected_result=passed,
                         log_sha256=sha(log)))
    record = dict(image_sha256=IMAGE_SHA256, data_address=hex(address), data_size=size,
                  data_sha256=sha(boot), processor_sha256=sha(processor),
                  tool_sha256=sha(Path(__file__)), runs=runs,
                  original_helpers_qualified=runs[0]['expected_result'],
                  original_boot_qualified=False,
                  scope='Unchanged DB/EB/0126/005C helpers move actual scratch controls, '
                        'parameters and samples through real SGE/input/output and consume actual EOL.',
                  limits='Isolated authored callers; unknown B0..B3 setup/producer remains '
                         'guarded. No in-run P publication, bit9 reload, linear offset writeback, '
                         'full graph/boot, hardware reset/cadence or EP/output qualification.')
    (output/'original-gp-linear-reference.aot-private.json').write_text(
        json.dumps(record, indent=2)+'\n', newline='\n')
    if not all(r['expected_result'] for r in runs):
        raise RuntimeError('original linear helper/guard changed; inspect preserved private evidence')
    return dict(original_helpers_qualified=True, original_boot_qualified=False,
                helper_lifetimes=3, actual_helper_transfers=15, data_bytes=size)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--processor', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.image, args.processor, args.output)))
