#!/usr/bin/env python3
"""Bind original EP bootstrap/helpers to explicit maintained EP guards.

Original bytes remain private under build/. The actual original entry reaches
its real C5 polling loop after the resident SEN prefix and configuration. Authored
callers also execute the unchanged configuration tail and linear
builder/transfer helper; the latter stops on its unknown descriptor bit9.
These results do not claim a complete original EP boot, cadence or output pass.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from aot_inventory import parse_xbe

IMAGE_SHA256 = 'e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
PARTS = (
    (0x31be0e0, 800, 0, '816d99d2129f03f192fd0f739e7f8f4dc330cbdd4c931457058c11b3b3d6b416'),
    (0x31be400, 1532, 0x180, '55202ae3969a54a4583ff2c1534283f4654899c5128db501fec536ec1e526d74'),
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(image, processor, output):
    image, processor, output = image.resolve(), processor.resolve(), output.resolve()
    if 'build' not in output.parts:
        raise ValueError('original evidence must stay in a private build directory')
    if output.exists():
        raise ValueError('use a fresh output directory to preserve earlier evidence')
    blob = image.read_bytes()
    if hashlib.sha256(blob).hexdigest() != IMAGE_SHA256:
        raise ValueError('unreviewed original title image')
    sections = parse_xbe(blob)['sections']
    boot_data = bytearray(struct.pack('<I', 0x87) * 2048)
    source_parts = []
    output.mkdir(parents=True, exist_ok=True)
    for address, size, pc, expected_sha in PARTS:
        section = next(s for s in sections
                       if s['address'] <= address and address+size <= s['address']+s['raw_size'])
        offset = section['raw_offset']+address-section['address']
        data = blob[offset:offset+size]
        if hashlib.sha256(data).hexdigest() != expected_sha:
            raise ValueError(f'unreviewed EP source at {address:x}')
        boot_data[pc*4:pc*4+size] = data
        source_parts.append(dict(address=hex(address), bytes=size, pc=hex(pc), sha256=expected_sha))
    boot = output/'original-ep-mapped-source.aot-private.bin'
    boot.write_bytes(boot_data)
    runs = []
    for mode, guard in (('--original-entry', 'original EP startup reaches its real C5 wait: PC0031/0032 SP0'),
                        ('--original-config-tail', 'original EP configuration tail: CA0/D5=4000/D4=4000/D7=0'),
                        ('--original-bit9', 'original EP linear input: unchanged276/2E1 copy32 exact words and return')):
        run = subprocess.run([str(processor), mode, str(boot)],
                             capture_output=True, text=True, timeout=20)
        log = output/f'original-ep-{mode[2:]}.aot-private.log'
        log.write_text(run.stdout+run.stderr, newline='\n')
        passed = run.returncode == 0 and guard in run.stdout
        runs.append(dict(mode=mode, exit=run.returncode, expected_result=passed,
                         expected_guard=guard, log_sha256=sha(log)))
    record = dict(image_sha256=IMAGE_SHA256, source_parts=source_parts,
                  mapped_source_sha256=sha(boot), processor_sha256=sha(processor),
                  tool_sha256=sha(Path(__file__)), runs=runs,
                  original_boot_qualified=False, original_output_qualified=False,
                  scope='Actual original EP entry plus unchanged 02F3 configuration tail, '
                        '0276 linear builder and 02E1 transfer/control helper on the distinct EP CPU/banks.',
                  limits='Exact passive B0-B3 profile and hardware-resident SEN prefix '
                         'and an empty initial C5 clear are admitted. Hardware C4 publication '
                         'continues execution; unchanged startup returns through CA0/D5/D4 '
                         'empty-list/D7 setup and its empty event2 acknowledgement to C5 polling. '
                         'The exact linear X/P input profile is admitted from separate physical evidence. '
                         'The helper check copies32 words, preserving source RAM, GP banks and FIFO cursor. '
                         'Count32 is an offline extent regression; hardware used24. ACK/busy timing '
                         'and frame-ACK causality remain open. Full spill/refill is not implemented. '
                         'The helper caller is authored; dynamic code/table blocks are not '
                         'substituted. Full original DMA/configuration chain, resets, '
                         'EP frame admission/cadence, AC97 and full graph/output remain open.')
    report = output/'original-ep-reference.aot-private.json'
    report.write_text(json.dumps(record, indent=2)+'\n', newline='\n')
    print(json.dumps(dict(report=str(report), expected_guards=[r['expected_result'] for r in runs],
                          original_boot_qualified=False, original_output_qualified=False)))
    return all(r['expected_result'] for r in runs)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--processor-test', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    raise SystemExit(0 if check(args.xbe, args.processor_test, args.output) else 1)
