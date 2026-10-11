#!/usr/bin/env python3
"""Run unchanged original EP DMA helpers over supported queued GP/EP jobs.

The caller/descriptors and initial consumer are authored. Original timer setup,
bit9 builders, dynamic directory and full bootstrap/output are not substituted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from aot_inventory import parse_xbe
from native_ep_reference import IMAGE_SHA256, PARTS


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(image, processor, output):
    image, processor, output = image.resolve(), processor.resolve(), output.resolve()
    if 'build' not in output.parts:
        raise ValueError('original source stays in a private build directory')
    blob = image.read_bytes()
    if hashlib.sha256(blob).hexdigest() != IMAGE_SHA256:
        raise ValueError('unreviewed original image')
    sections = parse_xbe(blob)['sections']
    boot = bytearray(struct.pack('<I', 0x87) * 2048)
    for address, size, pc, expected in PARTS:
        section = next(s for s in sections if s['address'] <= address
                       and address+size <= s['address']+s['raw_size'])
        offset = section['raw_offset']+address-section['address']
        raw = blob[offset:offset+size]
        if hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError(f'original firmware mismatch at {address:x}')
        boot[pc*4:pc*4+size] = raw
    output.mkdir(parents=True, exist_ok=True)
    source = output/'original-dma-helpers.aot-private.bin'
    source.write_bytes(boot)
    run = subprocess.run([str(processor), '--original', str(source)],
                         capture_output=True, text=True, timeout=30)
    log = output/'original-dma-lifecycle.aot-private.log'
    log.write_text(run.stdout+run.stderr, newline='\n')
    passed = (run.returncode == 0 and
              '18 actual GP/EP dynamic-code/PCM16 consumers' in run.stdout and
              'unchanged2D5/2DA/2D0 return without STOP' in run.stdout)
    record = dict(image_sha256=IMAGE_SHA256, source_parts=PARTS,
                  mapped_source_sha256=sha(source), processor_sha256=sha(processor),
                  tool_sha256=sha(Path(__file__)),
                  helper_region=dict(begin='0x2d0', end='0x2e1',
                                     sha256=hashlib.sha256(boot[0x2d0*4:0x2e1*4]).hexdigest()),
                  runs=[dict(exit=run.returncode, expected_result=passed,
                             log_sha256=sha(log))],
                  original_boot_qualified=False, original_output_qualified=False,
                  scope='Unchanged original EP 2D5 START/RUNNING, 2DA EOL/error '
                        'and 2D0 idle helpers; caller supplies known-bit P-load '
                        'and PCM16-output jobs on actual separate GP/EP CPUs.',
                  dispatch_policy='Explicit nominal one real descriptor/copy '
                                  'stage per 1, 2 or 4 retired DSP instructions.',
                  limits='No silicon DMA rate/phase, natural STOPPED bit5, '
                         'original bit9, B0..B3, graph/directory/bootstrap, '
                         'hardware reset, cadence or AC97 claim.')
    report = output/'original-dma-lifecycle-reference.aot-private.json'
    report.write_text(json.dumps(record, indent=2)+'\n', newline='\n')
    print(json.dumps(dict(report=str(report), expected_result=passed,
                         original_boot_qualified=False, original_output_qualified=False)))
    return passed


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--processor-test', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raise SystemExit(0 if check(args.xbe, args.processor_test, args.output) else 1)
