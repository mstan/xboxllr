#!/usr/bin/env python3
"""Qualify a bounded original prefix with independently proved behind-camera rejection.

Strictly negative-W whole strips preserve all expected owned storage because
each whole primitive has an empty W-clip result. The same retained software
early-out is bound by DLL hash/code bytes before comparisons. Other draws
use the published clamp/XY equations unchanged. Mixed-W, near-W and surviving
depth-plane clipping, complete scene/HLE, enabled noise, exact NV2A precision
and original startup/lifetime remain unqualified.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

from native_frame_inputs import require
from native_scene_clamp_reference import CLAMP_SOURCE,CLAMP_ORDER
from native_scene_rejection_reference import expected_rejection_draw,retained_software_evidence,CLIP_SOURCE
from qualify_native_scene_producer import run as original_prefix


def run(args):
    evidence = json.loads(args.clip_evidence.read_text())
    require(evidence['library_sha256'] == hashlib.sha256(args.library.read_bytes()).hexdigest() and
            evidence['clip_arithmetic'] == args.clip_arithmetic == 'fma' and
            evidence['selected_from_disassembly_before_pixels'] is True,
            'Clipping arithmetic lacks a declaration for this exact library')
    software = retained_software_evidence(args.library)
    args.diagnostics.mkdir(parents=True,exist_ok=True)
    evidence_path = args.diagnostics/'rejection-software-evidence.aot-private.json'
    evidence_path.write_text(json.dumps(software,indent=2)+'\n')
    def expectation(capture,scene,vertices,fog,targets,before,depth_arithmetic):
        return expected_rejection_draw(capture,scene,vertices,fog,targets,before,
                                       depth_arithmetic,args.clip_arithmetic)
    report = original_prefix(args,expectation=expectation,scope=__doc__)
    rejected = [row for row in report['rows'] if 'behind_camera_rejection' in row]
    report.update(clip_arithmetic=args.clip_arithmetic,
        clip_arithmetic_evidence_sha256=hashlib.sha256(args.clip_evidence.read_bytes()).hexdigest(),
        rejection_software_evidence_sha256=hashlib.sha256(evidence_path.read_bytes()).hexdigest(),
        rejection_clip_source=CLIP_SOURCE,rejected_whole_draws=[r['input'] for r in rejected],
        rejected_whole_primitives=sum(r['behind_camera_rejection']['rejected_whole_primitives'] for r in rejected),
        mixed_w_clipping_qualified=False,surviving_depth_plane_clipping_qualified=False,
        clamp_source=CLAMP_SOURCE,clamp_order=CLAMP_ORDER,rgb_channel_bound=1)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('captures','source-report','diagnostics','output','xbe','binding','library','dependencies','clip-evidence'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--last-draw',type=int,required=True)
    parser.add_argument('--depth-arithmetic',choices=('separate','fma-b-first'),required=True)
    parser.add_argument('--clip-arithmetic',choices=('fma',),required=True)
    args = parser.parse_args()
    handles = [os.add_dll_directory(str(args.dependencies.resolve()))]
    run(args)
