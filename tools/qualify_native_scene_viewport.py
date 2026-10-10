#!/usr/bin/env python3
"""Extend independent original-producer prefixes through admitted XY clipping.

The original submission/target protocol is unchanged. An independently derived
homogeneous clipper supplies only expected equation geometry. Numeric graphs
must be declared from pinned DLL disassembly; exact NV2A silicon precision,
enabled noise, complete-scene/HLE pixels and startup/lifetime remain separate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

from native_frame_inputs import require
from qualify_native_scene_producer import run as original_prefix
from native_scene_viewport_reference import expected_viewport_draw,SOURCE


def run(args):
    evidence = json.loads(args.clip_evidence.read_text())
    require(evidence['library_sha256'] == hashlib.sha256(args.library.read_bytes()).hexdigest() and
            evidence['clip_arithmetic'] == args.clip_arithmetic == 'fma' and
            evidence['selected_from_disassembly_before_pixels'] is True,
            'Clipping arithmetic lacks a declaration for this exact library')
    def expectation(capture,scene,vertices,fog,targets,before,depth_arithmetic):
        return expected_viewport_draw(capture,scene,vertices,fog,targets,before,
                                      depth_arithmetic,args.clip_arithmetic)
    report = original_prefix(args,expectation=expectation,scope=__doc__)
    report.update(clip_arithmetic=args.clip_arithmetic,
                  clip_arithmetic_evidence_sha256=hashlib.sha256(args.clip_evidence.read_bytes()).hexdigest(),
                  clipping_source=SOURCE,
                  clipped_source_triangles=sum(row['clipped_source_triangles'] for row in report['rows']),
                  expanded_triangles=sum(row['expanded_triangles'] for row in report['rows']))
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('captures','source-report','diagnostics','output','xbe','binding','library','dependencies','clip-evidence'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--last-draw',type=int,required=True)
    parser.add_argument('--depth-arithmetic',choices=('separate','fma-b-first'),required=True)
    parser.add_argument('--clip-arithmetic',choices=('fma',),required=True)
    args = parser.parse_args()
    handles = [os.add_dll_directory(str(args.dependencies.resolve()))]
    run(args)
