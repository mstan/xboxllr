#!/usr/bin/env python3
"""Qualify a closed prefix of original scene producers from complete-frame inputs.

The expected chain starts with explicit supplied root inputs and independent
captured clear equations. Shader/state math precedes actual original execution.
Each next draw uses expected predecessor bytes, never observed after-images.
Unclipped opaque BC1 filled strips, four-bit coverage, declared binary32 depth
arithmetic and noise-disabled software are admitted; complete scene, HLE, silicon,
enabled noise, original startup and resource lifetime remain separate gates.
The closed producer profile uses repeat/trilinear BC1, RGB diffuse [0,0.5],
saturated alpha/density-zero fog, zero specular, LESS and RGB/depth writes;
target alpha and stencil are preserved. Culled faces may be off-viewport.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

from native_frame_inputs import Reader,audit,require
from native_frame_state_reference import Inputs
from native_frame_target_reference import TargetReference,initial_storage,clear_storage
from native_frame_storage import draw_plan
from native_frame_shader_state_reference import Prepared
from native_scene_draw_reference import replay
from native_scene_producer_reference import expected_draw,compare_storage
from native_frame_composition_reference import recycle


def run(args, expectation=expected_draw, scope=__doc__, prefix_limit=512):
    require(type(prefix_limit) is int and prefix_limit in (512,768,1536), 'Unsupported bounded prefix limit')
    require(type(args.last_draw) is int and 1 <= args.last_draw <= prefix_limit,
            f'Choose a bounded prefix <={prefix_limit}')
    coverage = audit(args.captures/'frame.jsonl')
    reader = Reader(args.captures/'frame-inputs.bin').read()
    inputs = Inputs(reader)
    library_sha = hashlib.sha256(args.library.read_bytes()).hexdigest()
    source = json.loads(args.source_report.read_text())
    require(source['passed'] and source['original_composed_all_draws_executed'] and
            source['inputs_sha256'] == coverage['inputs_sha256'] and
            source['library_sha256'] == library_sha, 'Wrong source composition/build')
    expected = {n:initial_storage(t,reader.asset_bytes(t['initial_asset'])) for n,t in reader.targets.items()}
    args.observe_shader_state_only = True
    args.noise_disabled_fixture = True
    args.retain_targets = False
    args.diagnostics.mkdir(parents=True,exist_ok=True)
    ref = TargetReference(args,reader.targets,expected,reader.root)
    written = {}; rows = []; operations = []; fixture_recycles = 0
    try:
        for event in reader.events:
            if event['type'] == 'draw' and event['input'] > args.last_draw:break
            if event['type'] in ('root','bind','clear','draw') and recycle(ref):
                fixture_recycles += 1
                require(compare_storage(reader.targets,expected,ref.images(),written)['qualified'],
                        'Fixture PFIFO recycling changed independently expected storage')
            if event['type'] in ('root','bind','clear'):
                if event['type'] == 'clear':
                    require(event['args'][2] == 0xf3, 'Only full RGBA/depth/stencil clears admitted')
                    expected,_ = clear_storage(reader.targets,event['targets'],event['args'],expected)
                    for target_id in event['targets']:written.pop(target_id,None)
                    ref.call('D3DDevice_Clear',event['args'])
                else:ref.call('D3DDevice_SetRenderTarget',[ref.headers[n] for n in event['targets']])
                ref.kick();ref.check()
                check = compare_storage(reader.targets,expected,ref.images(),written)
                require(check['qualified'],'Original prefix target/clear byte mismatch')
                operations.append(dict(call=event['call'],operation=event['type'],targets=event['targets'],
                                       qualified=True,compared_pixels=check['compared_pixels']))
            elif event['type'] == 'draw':
                number = event['input'];capture,scene = inputs.draw(number)
                plan = draw_plan(reader.targets,reader.draws[number-1],scene)
                require(plan['texture_producers'] == [0]*4, 'Prior scene textures outside BC1 producer contract')
                scene['frame_resource_plan'] = plan
                prepared = Prepared(capture,scene)
                expected,changed,stats = expectation(capture,scene,prepared.vertices,prepared.fog,
                                                      reader.targets,expected,args.depth_arithmetic)
                written.setdefault(plan['target_ids'][0],set()).update(changed)
                replay(args,Path(f'input-{number}'),ref,initialize_targets=False,
                       capture=capture,scene=scene,resource_plan=plan)
                actual = json.loads((args.diagnostics/f'input-{number}.aot-private.json').read_text())
                state = prepared.qualify(actual,library_sha)
                check = compare_storage(reader.targets,expected,ref.images(),written)
                row = dict(input=number,**stats,**check,shader_state_components_qualified=state['shader_state_components_qualified'])
                row['qualified'] &= row['shader_state_components_qualified']
                rows.append(row)
                print(json.dumps({k:v for k,v in row.items() if k != 'targets'}),flush=True)
                if not row['qualified']:
                    (args.diagnostics/f'input-{number}-failed.aot-private.json').write_text(json.dumps(row,indent=2)+'\n')
                    for n,data in expected.items():(args.diagnostics/f'failed-{number}-expected-{n}.aot-private.bin').write_bytes(data)
                    for n,data in ref.images().items():(args.diagnostics/f'failed-{number}-actual-{n}.aot-private.bin').write_bytes(data)
                require(row['qualified'],'Original scene producer mismatch')
        require(len(rows) == args.last_draw and sum(r['expected_written_pixels'] for r in rows)>0,
                'Incomplete or wholly unexercised producer prefix')
        for n,data in expected.items():(args.diagnostics/f'final-expected-{n}.aot-private.bin').write_bytes(data)
        for n,data in ref.images().items():(args.diagnostics/f'final-actual-{n}.aot-private.bin').write_bytes(data)
    finally:ref.close()
    report = dict(passed=True,rows=rows,target_operations=operations,scope=scope,
                  inputs_sha256=coverage['inputs_sha256'],ledger_sha256=coverage['ledger_sha256'],
                  library_sha256=library_sha,source_report_sha256=hashlib.sha256(args.source_report.read_bytes()).hexdigest(),
                  draws=len(rows),expected_written_fragments=sum(r['expected_written_pixels'] for r in rows),
                  compared_pixels=sum(r['compared_pixels'] for r in rows),
                  expected_predecessor_bytes_used=True,observed_after_image_goldens_used=False,
                  noise_disabled_fixture=True,depth_arithmetic=args.depth_arithmetic,
                  complete_scene_pixel_qualified=False,production_hle_pixels_qualified=False,
                  exact_hardware_rounding_qualified=False,original_startup_lifetime_qualified=False)
    report.update(fixture_pushbuffer_recycles=fixture_recycles,original_ring_rollover_qualified=False)
    report['requested_prefix_limit'] = prefix_limit
    report['final_written_pixels'] = {str(n):sorted(indices) for n,indices in written.items()}
    report['expected_target_sha256'] = {str(n):hashlib.sha256(data).hexdigest() for n,data in expected.items()}
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('captures','source-report','diagnostics','output','xbe','binding','library','dependencies'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--last-draw',type=int,required=True)
    parser.add_argument('--depth-arithmetic',choices=('separate','fma-b-first'),required=True,
                        help='Select from compiler/disassembly evidence, never by observed pixels')
    args = parser.parse_args()
    handles = [os.add_dll_directory(str(args.dependencies.resolve()))]
    run(args)
