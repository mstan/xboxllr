#!/usr/bin/env python3
"""Preserve the hash-bound original GP prefix's first C4 yield continuation.

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
    output.mkdir(parents=True, exist_ok=False)
    boot = output/'original-gp-bootstrap-prefix.aot-private.bin'
    boot.write_bytes(blob[offset:offset+size])
    run = subprocess.run([str(processor), '--original-boot', str(boot)],
                         capture_output=True, text=True, timeout=20)
    log = output/'original-gp-bootstrap-continuation.aot-private.log'
    log.write_text(run.stdout+run.stderr, newline='\n')
    records = [json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
    expected = dict(boundary='first-C4-yield', pc=0x2b, power=0, yielded=True,
                    dma_nodes=0, frame_outstanding=False, ram_unchanged=True,
                    original_prefix_loaded=True)
    continued = run.returncode == 0 and records == [expected]
    record = dict(image_sha256=IMAGE_SHA256, data_address=hex(address), data_size=size,
                  data_sha256=sha(boot), processor_sha256=sha(processor),
                  tool_sha256=sha(Path(__file__)), log_sha256=sha(log),
                  process_exit=run.returncode, original_prefix_continuation_verified=continued,
                  observed_prefix=records, original_full_frame_qualified=False,
                  gp_profile_precision_deferred=True,
                  original_boot_qualified=False,
                  boundary='Unchanged original entry configures the exact supported GP '
                           'composite profile and reaches first C4 yield at PC002B. '
                           'No frame wake/resume, DMA completion or output is manufactured.',
                  limits='Mapped 8-KiB boot profile with exact original1484-byte prefix and '
                         'authored STOP tail; not original full scratch layout, hardware '
                         'bootstrap, whole GP/graph, cadence, FIFO or EP output.')
    (output/'original-gp-bootstrap-continuation.aot-private.json').write_text(
        json.dumps(record, indent=2)+'\n', newline='\n')
    if not continued:
        raise RuntimeError('original prefix did not reach the exact first-yield boundary; inspect private evidence')
    return dict(original_prefix_continuation_verified=True, boundary_pc=0x2b,
                original_full_frame_qualified=False, original_boot_qualified=False,
                data_bytes=size, process_exit=run.returncode)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--processor', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.image, args.processor, args.output)))
