#!/usr/bin/env python3
"""Qualify empty mixed-W clipping within a bounded original software prefix.

Every admitted mixed-W whole draw independently derives all ordered W/XY clip
results and proves no raster leaf survives. The original shader/state/whole
draw executes unchanged against independent expected predecessors. Surviving
mixed-W pixels, near/zero-W original inputs and depth-plane behavior are not
qualified; the published positive-W/clamp and negative-W equations are stable.
Complete scene/HLE, enabled noise, NV2A silicon precision and original
startup/lifetime/ring allocation remain separate qualifications.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

from native_frame_inputs import require
from native_scene_clamp_reference import CLAMP_SOURCE,CLAMP_ORDER
from native_scene_mixed_clip_reference import expected_empty_mixed_draw,mixed_software_evidence,CLIP_PLANES
from qualify_native_scene_producer import run as original_prefix


def run(args):
    evidence = json.loads(args.clip_evidence.read_text())
    require(evidence['library_sha256'] == hashlib.sha256(args.library.read_bytes()).hexdigest() and
            evidence['clip_arithmetic'] == args.clip_arithmetic == 'fma' and
            evidence['selected_from_disassembly_before_pixels'] is True,
            'Clipping arithmetic lacks a declaration for this exact library')
    software = mixed_software_evidence(args.library)
    args.diagnostics.mkdir(parents=True,exist_ok=True)
    evidence_path = args.diagnostics/'mixed-clip-software-evidence.aot-private.json'
    evidence_path.write_text(json.dumps(software,indent=2)+'\n')
    def expectation(capture,scene,vertices,fog,targets,before,depth_arithmetic):
        return expected_empty_mixed_draw(capture,scene,vertices,fog,targets,before,
                                         depth_arithmetic,args.clip_arithmetic)
    report = original_prefix(args,expectation=expectation,scope=__doc__)
    mixed = [row for row in report['rows'] if row.get('mixed_clip_all_primitives_empty')]
    rejected = [row for row in report['rows'] if 'behind_camera_rejection' in row]
    first_rejections = {}
    for row in mixed:
        for stage in row['clip_stage_proofs']:
            if stage['before_vertices'] and not stage['after_vertices']:
                first_rejections[stage['plane']] = first_rejections.get(stage['plane'],0)+1
    report.update(clip_arithmetic=args.clip_arithmetic,
        clip_arithmetic_evidence_sha256=hashlib.sha256(args.clip_evidence.read_bytes()).hexdigest(),
        mixed_software_evidence_sha256=hashlib.sha256(evidence_path.read_bytes()).hexdigest(),
        geometry_clip_planes=list(CLIP_PLANES),empty_mixed_w_draws=[r['input'] for r in mixed],
        empty_mixed_w_primitives=sum(r['source_triangles'] for r in mixed),
        homogeneous_mixed_plane_intersections=sum(r['homogeneous_plane_intersections'] for r in mixed),
        empty_mixed_first_rejecting_planes=first_rejections,
        mixed_w_empty_result_component_qualified=bool(mixed),
        rejected_whole_draws=[r['input'] for r in rejected],
        rejected_whole_primitives=sum(r['behind_camera_rejection']['rejected_whole_primitives'] for r in rejected),
        surviving_mixed_w_pixels_qualified=False,near_zero_original_w_qualified=False,
        geometric_depth_plane_clipping_qualified=False,fragment_depth_range_boundary_qualified=False,
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
