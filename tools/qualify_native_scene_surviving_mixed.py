#!/usr/bin/env python3
"""Qualify bounded original prefixes with hash-bound mixed-W draws.

The added component admits only hash-bound original inputs 1943, 1969 and 1983.
Their whole strips' W/XY clip and fan sequences are independently derived; every
surviving leaf must fit the unchanged positive-W coverage/depth/BC1 equations.
RGB has the existing one-code bound, with exact D/S/A and untouched bytes.
Other mixed survivors, near-zero W, signed/depth-boundary leaves, full scene,
HLE, noise, silicon, original rollover and startup/lifetime remain guarded.
The shared public defaults stay unchanged. This wrapper requests an explicit
stop at 1943, 1969 or 2120; it cannot bypass the shared runner's bounded admission.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

from native_frame_inputs import require
from native_scene_surviving_mixed_reference import (
    expected_bounded_surviving_draw,surviving_software_evidence)
from qualify_native_scene_producer import run as original_prefix


def run(args):
    require(type(args.last_draw) is int and args.last_draw in (1943,1969,2120),
            'This bounded component stops at original draw 1943, 1969 or 2120')
    evidence = json.loads(args.clip_evidence.read_text())
    require(evidence['library_sha256'] == hashlib.sha256(args.library.read_bytes()).hexdigest() and
            evidence['clip_arithmetic'] == args.clip_arithmetic == 'fma' and
            args.depth_arithmetic == 'fma-b-first' and
            evidence['selected_from_disassembly_before_pixels'] is True,
            'Wrong pinned clipping/depth arithmetic declaration')
    software = surviving_software_evidence(args.library,args.source)
    args.diagnostics.mkdir(parents=True,exist_ok=True)
    evidence_path = args.diagnostics/'surviving-mixed-software-evidence.aot-private.json'
    evidence_path.write_text(json.dumps(software,indent=2)+'\n')
    def expectation(capture,scene,vertices,fog,targets,before,depth_arithmetic):
        return expected_bounded_surviving_draw(capture,scene,vertices,fog,targets,before,
                                               depth_arithmetic,args.clip_arithmetic)
    report = original_prefix(args,expectation=expectation,scope=__doc__,prefix_limit=args.last_draw)
    survivors = [row for row in report['rows'] if row.get('surviving_mixed_w_component')]
    admitted = [n for n in (1943,1969,1983) if n <= args.last_draw]
    require([r['input'] for r in survivors] == admitted and
            all(r['expected_written_pixels'] > 0 for r in survivors),
            'The bounded mixed survivors were not independently exercised')
    report.update(surviving_mixed_draws=admitted,
        surviving_mixed_software_evidence_sha256=hashlib.sha256(evidence_path.read_bytes()).hexdigest(),
        clip_arithmetic=args.clip_arithmetic,surviving_mixed_w_component_qualified=True,
        surviving_depth_range_component_qualified=False,near_zero_original_w_qualified=False,
        surviving_mixed_w_pixels_qualified=False,rgb_channel_bound=1)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('captures','source-report','diagnostics','output','xbe','binding','library','dependencies','clip-evidence','source'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--last-draw',type=int,required=True)
    parser.add_argument('--depth-arithmetic',choices=('fma-b-first',),required=True)
    parser.add_argument('--clip-arithmetic',choices=('fma',),required=True)
    args = parser.parse_args()
    handles = [os.add_dll_directory(str(args.dependencies.resolve()))]
    run(args)
