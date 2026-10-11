#!/usr/bin/env python3
"""Preserve the hash-bound original GP firmware's first guarded CPU boundary.

This diagnostic does not substitute peripheral success, original scratch layout,
hardware reset/bootstrap or a retail graph/frame/output owner. All original
bytes and executable output remain private under build/.
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
    boot = output/'original-gp-bootstrap.aot-private.bin'
    boot.write_bytes(blob[offset:offset+size])
    run = subprocess.run([str(processor), '--original-boot', str(boot)],
                         capture_output=True, text=True, timeout=20)
    log = output/'original-gp-bootstrap-guard.log'
    log.write_text(run.stdout+run.stderr, newline='\n')
    stopped = (run.returncode == 2 and
               'guarded before P/node publication' in run.stderr and
               'unqualified GP interrupt acknowledgement' in run.stderr)
    record = dict(image_sha256=IMAGE_SHA256, data_address=hex(address), data_size=size,
                  data_sha256=sha(boot), processor_sha256=sha(processor),
                  tool_sha256=sha(Path(__file__)), log_sha256=sha(log),
                  process_exit=run.returncode, expected_guard_observed=stopped,
                  original_boot_qualified=False,
                  boundary='Original entry JSR0155 reaches real FFFFC5=0FFF acknowledgement; '
                           'the maintained peripheral admits only its qualified DMA status bit80.',
                  limits='Mapped 8-KiB boot profile with exact original1484-byte prefix and '
                         'authored STOP tail; not original full scratch layout, hardware '
                         'bootstrap, whole GP/graph, cadence, FIFO or EP output.')
    (output/'original-gp-bootstrap-guard.aot-private.json').write_text(
        json.dumps(record, indent=2)+'\n', newline='\n')
    if not stopped:
        raise RuntimeError('original bootstrap boundary changed; inspect preserved private evidence')
    return dict(expected_guard_observed=True, original_boot_qualified=False,
                data_bytes=size, process_exit=run.returncode)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--processor', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.image, args.processor, args.output)))
