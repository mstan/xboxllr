#!/usr/bin/env python3
"""Qualify a bounded producer prefix with source-derived sampled precision.

Only draw2129 receives the reviewed sampled-set component. Existing defaults,
nominal equations and tolerances remain unchanged. Actual source, input and
loaded-host-CRT proof precede original reference creation. The older full-frame
state report is retained as provenance; each new original row is revalidated.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
sys.path.insert(0,'tools')
from native_frame_inputs import require
from qualify_native_scene_producer import run as original_prefix

import native_scene_sampled_reference as sampler


def run(args):
    require(type(args.last_draw) is int and args.last_draw==2129,'This reviewed component stops at original2129 only')
    require(args.depth_arithmetic=='fma-b-first' and args.clip_arithmetic=='fma',
        'Wrong reviewed clip/depth graph')
    # This code order is intentional: the read-only loader guard executes
    # BEFORE any original renderer/reference is created by the shared runner.
    software,loaded_handle=sampler.verify_for_replay(args.library,args.source,args.captures)
    def expectation(capture,scene,vertices,fog,targets,before,depth_arithmetic):
        return sampler.expected_sampled_bounded_draw(capture,scene,vertices,fog,targets,before,
            depth_arithmetic,args.clip_arithmetic)
    report=original_prefix(args,expectation=expectation,scope=__doc__,prefix_limit=2129,
        source_transition=sampler.validate_source_transition)
    sampler.record_current_prefix_state(report)
    exercised=[row for row in report['rows'] if row.get('sampled_precision_component')]
    require(len(exercised)==1 and exercised[0]['input']==2129 and
        exercised[0]['expected_written_pixels']==582,'Bound2129 sampler was not exercised')
    report.update(sampled_precision_draws=[2129],sampler_software_evidence=software,
        rgb_channel_bound=1,complete_scene_pixel_qualified=False,production_hle_pixels_qualified=False)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','source-report','diagnostics','output','xbe','binding','library','dependencies','source'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--last-draw',type=int,required=True)
    parser.add_argument('--depth-arithmetic',choices=('fma-b-first',),required=True)
    parser.add_argument('--clip-arithmetic',choices=('fma',),required=True)
    args=parser.parse_args()
    handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    run(args)
