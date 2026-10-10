#!/usr/bin/env python3
"""Qualify whole-draw empty clipping for positive W with out-of-range vertex Z.

Complete independent W/XY clip results must contain no surviving raster leaf.
Only then can fragment depth arithmetic be skipped and expected predecessors
preserved. The unchanged original whole draw/shader/state runs against that
chain. Surviving depth/mixed-W pixels, original near/zero W, full scene/HLE,
noise, silicon and startup/lifetime remain unqualified. This wrapper explicitly
requests a bounded768 prefix; the shared/default/public runner remains512.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

from native_frame_inputs import require
from native_scene_clamp_reference import CLAMP_SOURCE,CLAMP_ORDER
from native_scene_empty_depth_reference import expected_empty_depth_draw
from native_scene_mixed_clip_reference import mixed_software_evidence,CLIP_PLANES
from qualify_native_scene_producer import run as original_prefix


def run(args, prefix_limit=768, scope=__doc__):
    evidence = json.loads(args.clip_evidence.read_text())
    require(evidence['library_sha256'] == hashlib.sha256(args.library.read_bytes()).hexdigest() and
            evidence['clip_arithmetic'] == args.clip_arithmetic == 'fma' and
            evidence['selected_from_disassembly_before_pixels'] is True,
            'Clipping arithmetic lacks a declaration for this exact library')
    software = mixed_software_evidence(args.library)
    args.diagnostics.mkdir(parents=True,exist_ok=True)
    evidence_path = args.diagnostics/'empty-depth-software-evidence.aot-private.json'
    evidence_path.write_text(json.dumps(software,indent=2)+'\n')
    def expectation(capture,scene,vertices,fog,targets,before,depth_arithmetic):
        return expected_empty_depth_draw(capture,scene,vertices,fog,targets,before,
                                         depth_arithmetic,args.clip_arithmetic)
    report = original_prefix(args,expectation=expectation,scope=scope,prefix_limit=prefix_limit)
    empty_depth = [row for row in report['rows'] if row.get('positive_w_depth_domain_all_primitives_empty')]
    mixed = [row for row in report['rows'] if row.get('mixed_clip_all_primitives_empty')]
    rejected = [row for row in report['rows'] if 'behind_camera_rejection' in row]
    report.update(clip_arithmetic=args.clip_arithmetic,
        clip_arithmetic_evidence_sha256=hashlib.sha256(args.clip_evidence.read_bytes()).hexdigest(),
        empty_depth_software_evidence_sha256=hashlib.sha256(evidence_path.read_bytes()).hexdigest(),
        geometry_clip_planes=list(CLIP_PLANES),empty_positive_w_depth_draws=[r['input'] for r in empty_depth],
        empty_positive_w_depth_primitives=sum(r['source_triangles'] for r in empty_depth),
        positive_w_empty_depth_component_qualified=bool(empty_depth),
        empty_mixed_w_draws=[r['input'] for r in mixed],
        empty_mixed_w_primitives=sum(r['source_triangles'] for r in mixed),
        rejected_whole_draws=[r['input'] for r in rejected],
        rejected_whole_primitives=sum(r['behind_camera_rejection']['rejected_whole_primitives'] for r in rejected),
        surviving_mixed_w_pixels_qualified=False,surviving_depth_range_component_qualified=False,
        near_zero_original_w_qualified=False,geometric_depth_plane_clipping_qualified=False,
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
