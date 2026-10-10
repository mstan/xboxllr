#!/usr/bin/env python3
"""Qualify a bounded1536 original scene prefix with unchanged producer equations.

Input-only preflight769..1536 admitted the existing BC1/LESS/clamp/viewport
profile and fully empty whole-negative-W, mixed-W and positive-W/out-of-range
depth clipping domains. The actual original whole draw/shader/state executes
against independently expected predecessors from the supplied root inputs.
Written RGB has the declared one-code bound; D/S/A and untouched bytes remain
exact. Surviving mixed-W/depth-boundary pixels, near-zero original W, full scene,
HLE, noise, silicon, original PFIFO rollover and startup/lifetime remain separate
gates. Existing public producer/empty-depth CLI limits stay512/768 respectively.
"""
import argparse
import os
from pathlib import Path

from qualify_native_scene_empty_depth import run as empty_prefix


def run(args):
    return empty_prefix(args,prefix_limit=1536,scope=__doc__)


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
