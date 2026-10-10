#!/usr/bin/env python3
"""Qualify original postprocess consumers of explicitly supplied producer images.

Two self-feedback quads are isolated components. The final three rectangles
form an independent expected-image chain: the blur-mask output is sampled by
the blend consumer, rather than substituting its actual after-image. Earlier
frame-produced color/depth snapshots are supplied inputs, not qualified here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import numpy as np

from native_frame_inputs import Reader, audit, require
from native_frame_state_reference import Inputs
from native_frame_storage import draw_plan
from native_frame_state_delivery import expected_delivery
from native_frame_shader_state_reference import Prepared
from native_frame_target_reference import TargetReference, layout
from native_scene_draw_reference import replay
from native_postprocess_pixel_reference import SOURCES, rgba, pixels, quantize, compare, stencil_bytes, depth_only_view


def argb_bytes(values):
    return values[..., [2,1,0,3]].tobytes()


def run(args):
    coverage = audit(args.captures/'frame.jsonl')
    reader = Reader(args.captures/'frame-inputs.bin').read()
    inputs = Inputs(reader)
    library_sha = hashlib.sha256(args.library.read_bytes()).hexdigest()
    source = json.loads(args.source_report.read_text())
    require(source['passed'] and source['original_composed_all_draws_executed'] and
            source['inputs_sha256'] == coverage['inputs_sha256'] and
            source['library_sha256'] == library_sha, 'Wrong source composition/build')
    supplied = []
    rows = []
    args.diagnostics.mkdir(parents=True, exist_ok=True)
    args.observe_shader_state_only = True
    args.noise_disabled_fixture = True
    args.retain_targets = False
    for numbers in ([2767], [2820], [4300,4301,4302]):
        initial = {n:bytes([0xa5])*(layout(target)[2]*target['height'])
                   for n,target in reader.targets.items()}
        number = numbers[0]
        for binding in source['producer_bindings']:
            if binding['input'] != number:
                continue
            target = binding['target']
            data = (args.producers/f'input-{number}-producer-{target}.aot-private.bin').read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            require(digest == binding['live_bytes_sha256'] and len(data) == len(initial[target]),
                    'Supplied producer differs from declared snapshot')
            initial[target] = data
            supplied.append(dict(input=number,target=target,sha256=digest,
                                 scope='supplied component input; producer unqualified'))
        expected_storage = dict(initial)
        expected_images = {n:rgba(data, *layout(reader.targets[n])) for n,data in initial.items()}
        ref = TargetReference(args, reader.targets, initial, reader.root)
        written = set()
        try:
            bound = None
            for number in numbers:
                capture, scene = inputs.draw(number)
                draw = reader.draws[number-1]
                plan = draw_plan(reader.targets, draw, scene)
                scene['frame_resource_plan'] = plan
                prepared = Prepared(capture, scene)
                methods, fixture, pixel = expected_delivery(capture, scene)
                methods = {**fixture, **methods, **pixel}
                target = draw['target']
                width, height, pitch = layout(reader.targets[target])
                images = [expected_images[n] if n else None for n in plan['texture_producers']]
                wanted, margin = pixels(methods, prepared.vertices, draw['primitive'], width, height,
                                        images, expected_images[target])
                counterfactual = None
                if number == 4300:
                    producer = plan['texture_producers'][1]
                    require(producer and reader.targets[producer]['role'] == 1,
                            'Depth-view diagnostic requires the actual depth producer')
                    alternate = list(images)
                    alternate[1] = depth_only_view(expected_storage[producer],*layout(reader.targets[producer]))
                    alternate_pixels,_ = pixels(methods,prepared.vertices,draw['primitive'],width,height,
                                                 alternate,expected_images[target])
                    counterfactual = dict(scope='documented depth-only SRV equation; no native GPU run',
                        different_alpha_pixels=int(np.count_nonzero(alternate_pixels[...,3] != wanted[...,3])),
                        native_gpu_view_observed=False)
                require(pitch == width*4, 'Padded output needs explicit byte preservation')
                # Expected bytes are complete before executing or reading the
                # draw. No actual intermediate replaces the expected chain.
                expected_storage[target] = argb_bytes(wanted)
                expected_images[target] = wanted/255.
                depth = draw['depth']
                expected_storage[depth] = stencil_bytes(methods,expected_storage[depth],
                                                         *layout(reader.targets[depth]),width,height)
                if bound != plan['target_ids']:
                    ref.call('D3DDevice_SetRenderTarget', plan['target_headers'])
                    ref.kick(); ref.check(); bound = plan['target_ids']
                replay(args, Path(f'input-{number}'), ref, initialize_targets=False,
                       capture=capture, scene=scene, resource_plan=plan)
                actual = json.loads((args.diagnostics/f'input-{number}.aot-private.json').read_text())
                state = prepared.qualify(actual, library_sha)
                require(state['shader_state_components_qualified'], 'Original shader/state mismatch')
                observed = ref.images()
                actual_pixels = quantize(rgba(observed[target], width, height, pitch))
                result = compare(wanted, actual_pixels)
                retained_roots = written-{target}
                # Prior output roots are checked against their independently
                # expected bytes with the same declared interval.
                retained = []
                for n in sorted(retained_roots):
                    w,h,p = layout(reader.targets[n])
                    component = compare(quantize(expected_images[n]), quantize(rgba(observed[n],w,h,p)))
                    retained.append(dict(target=n, **component))
                row = dict(input=number, target=target, **result,
                           shader_state_components_qualified=True,
                           minimum_msb_mux_margin=margin, retained_outputs=retained,
                           depth_only_srv_counterfactual=counterfactual,
                           output_sha256=hashlib.sha256(observed[target]).hexdigest())
                row['component_pixels_qualified'] &= all(r['component_pixels_qualified'] for r in retained)
                row['depth_stencil_bytes_qualified'] = all(observed[n] == expected_storage[n]
                                                           for n,t in reader.targets.items() if t['role'])
                row['component_pixels_qualified'] &= row['depth_stencil_bytes_qualified']
                # Compare every never-written color root exactly, including
                # all bytes. All depth/stencil roots were checked above.
                written.add(target)
                row['untouched_roots_preserved'] = all(observed[n] == initial[n]
                                                       for n,t in reader.targets.items() if n not in written and not t['role'])
                row['component_pixels_qualified'] &= row['untouched_roots_preserved']
                (args.diagnostics/f'input-{number}-expected.aot-private.bin').write_bytes(expected_storage[target])
                (args.diagnostics/f'input-{number}-actual.aot-private.bin').write_bytes(observed[target])
                rows.append(row)
                print(json.dumps(row), flush=True)
        finally:
            ref.close()
    passed = bool(rows and all(r['component_pixels_qualified'] for r in rows))
    report = dict(passed=passed,rows=rows,supplied_producers=supplied,
                  inputs_sha256=coverage['inputs_sha256'],ledger_sha256=coverage['ledger_sha256'],
                  library_sha256=library_sha,source_report_sha256=hashlib.sha256(args.source_report.read_bytes()).hexdigest(),
                  compared_pixels=sum(r['compared_pixels'] for r in rows),
                  compared_channels=sum(r['compared_channels'] for r in rows),
                  expected_postprocess_intermediates_used=True, noise_disabled_fixture=True,
                  complete_scene_pixel_qualified=False,production_hle_pixels_qualified=False,
                  supplied_producers_qualified=False,exact_hardware_rounding_qualified=False,
                  sources=SOURCES,scope=__doc__)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    return passed


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('captures','producers','source-report','diagnostics','output','xbe','binding','library','dependencies'):
        parser.add_argument('--'+name,type=Path,required=True)
    args = parser.parse_args()
    handles = [os.add_dll_directory(str(args.dependencies.resolve()))]
    raise SystemExit(0 if run(args) else 1)
