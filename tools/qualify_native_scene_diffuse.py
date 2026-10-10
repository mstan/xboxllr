#!/usr/bin/env python3
"""Qualify original viewport-clipped BC1 producers with diffuse at most 3/4.

Independent input-derived shader/state, geometry, expected predecessors,
texture/combiner equations and exact depth/stencil; written RGB remains within
one code, all other bytes exact. Complete scene, HLE, enabled noise, silicon
precision and startup/lifetime remain separate gates.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

from native_frame_inputs import require
from native_scene_diffuse_reference import expected_diffuse_draw,codec_error_bound,DIFFUSE_LIMIT,SOURCES
from qualify_native_scene_producer import run as original_prefix


def run(args):
    evidence = json.loads(args.clip_evidence.read_text())
    require(evidence['library_sha256'] == hashlib.sha256(args.library.read_bytes()).hexdigest() and
            evidence['clip_arithmetic'] == args.clip_arithmetic == 'fma' and
            evidence['selected_from_disassembly_before_pixels'] is True,
            'Clipping arithmetic lacks a declaration for this exact library')
    bounds = codec_error_bound()  # Declare the proof before original execution.
    def expectation(capture,scene,vertices,fog,targets,before,depth_arithmetic):
        return expected_diffuse_draw(capture,scene,vertices,fog,targets,before,
                                     depth_arithmetic,args.clip_arithmetic)
    report = original_prefix(args,expectation=expectation,scope=__doc__)
    report.update(clip_arithmetic=args.clip_arithmetic,
                  clip_arithmetic_evidence_sha256=hashlib.sha256(args.clip_evidence.read_bytes()).hexdigest(),
                  numeric_sources=SOURCES,diffuse_ceiling=str(DIFFUSE_LIMIT),
                  codec_error_codes={str(k):str(v) for k,v in bounds.items()},
                  maximum_prequantization_codec_error=str(max(bounds.values())*DIFFUSE_LIMIT),
                  rgb_channel_bound=1,
                  clipped_source_triangles=sum(r['clipped_source_triangles'] for r in report['rows']))
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
