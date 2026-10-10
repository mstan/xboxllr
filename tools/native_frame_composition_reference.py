#!/usr/bin/env python3
"""Execute original frame draws with retained producer-backed target storage.

Independent vertex/state components still run against the same actual calls.
Clear viewport remains an explicit full-target fixture; startup/allocation,
original ring rollover, exact feedback caching and complete pixels are separate.
Captured sampled GPU bytes and after-images never become replay texture inputs.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path

from native_frame_inputs import Reader,audit as input_audit,require
from native_frame_state_reference import Inputs,selected
from native_frame_target_reference import TargetReference,initial_storage
from native_frame_storage import draw_plan
from native_frame_shader_state_reference import Prepared
from native_scene_draw_reference import replay
from native_vertex_fetch_reference import PUSH


def recycle(ref):
    """Fixture recycling via one ordinary PFIFO jump after the FIFO is drained.

    Does not replace a guest emitter or claim original allocation/ring rollover.
    Keeps all PGRAPH state and target bytes. Original calls have room to emit.
    """
    cursor=ref.get(ref.device)[0]
    require(ref.read(ref.gpu,0xfd800044,4)==cursor,'Cannot recycle pending original work')
    require(PUSH<=cursor<PUSH+0x100000,'Pushbuffer outside preowned fixture')
    if cursor<PUSH+0xc0000:return False
    ref.put(cursor,0x20000000|PUSH);ref.put(ref.device,PUSH);ref.kick()
    require(ref.read(ref.gpu,0xfd800044,4)==PUSH,'Fixture PFIFO jump did not complete')
    return True


def compose(args):
    coverage=input_audit(args.captures/'frame.jsonl');reader=Reader(args.captures/'frame-inputs.bin').read()
    inputs=Inputs(reader);numbers=selected(reader,args);wanted=set(numbers)
    require(numbers and len(wanted)==len(numbers) and all(0<n<=len(reader.draws) for n in numbers),'Bad composition selection')
    args.observe_shader_state_only=True;args.noise_disabled_fixture=True;args.retain_targets=False
    args.diagnostics.mkdir(parents=True,exist_ok=True)
    initial={n:initial_storage(t,reader.asset_bytes(t['initial_asset'])) for n,t in reader.targets.items()}
    ref=TargetReference(args,reader.targets,initial,reader.root);rows=[];target_calls=[];producers=[];recycles=0
    library_sha=hashlib.sha256(args.library.read_bytes()).hexdigest()
    try:
        for event in reader.events:
            if event['type'] in ('root','bind','clear'):
                recycles+=recycle(ref)
                if event['type']=='clear':ref.call('D3DDevice_Clear',event['args'])
                else:ref.call('D3DDevice_SetRenderTarget',[ref.headers[n] for n in event['targets']])
                ref.kick();ref.check()
                require(ref.read(ref.gpu,0xfd800044,4)==ref.get(ref.device)[0],'Original target/clear FIFO pending')
                target_calls.append(dict(call=event['call'],event=event['type'],targets=event['targets'],original_returned=True))
            elif event['type']=='draw' and event['input'] in wanted:
                recycles+=recycle(ref);n=event['input'];draw=reader.draws[n-1]
                capture,scene=inputs.draw(n);plan=draw_plan(reader.targets,draw,scene);scene['frame_resource_plan']=plan
                prepared=Prepared(capture,scene)  # Independent math before execution/readback.
                seen=set()
                for stage,producer in enumerate(plan['texture_producers']):
                    if not producer:continue
                    address=plan['texture_addresses'][stage];data=bytes(ref.cpu.mem_read(address,ref.sizes[producer]))
                    if producer not in seen:
                        (args.diagnostics/f'input-{n}-producer-{producer}.aot-private.bin').write_bytes(data);seen.add(producer)
                    producers.append(dict(input=n,stage=stage,target=producer,address=address,
                        live_bytes_sha256=hashlib.sha256(data).hexdigest(),shared_with_output=producer in plan['target_ids']))
                result=replay(args,Path(f'input-{n}'),ref,initialize_targets=False,capture=capture,scene=scene,resource_plan=plan)
                require(result['complete_draw_executed'],'Incomplete composed original draw')
                actual=json.loads((args.diagnostics/f'input-{n}.aot-private.json').read_text())
                row=prepared.qualify(actual,library_sha);row['call']=event['call'];rows.append(row)
                if not row['shader_state_components_qualified']:print(json.dumps(row),flush=True)
                require(row['shader_state_components_qualified'],'Composed original shader/state mismatch')
                if len(rows)%64==0 or plan['texture_producers']!=[0]*4:
                    print(json.dumps(dict(input=n,completed=len(rows),selected=len(numbers),producer_roots=plan['texture_producers'],fixture_recycles=recycles)),flush=True)
        for n,data in ref.images().items():(args.diagnostics/f'final-target-{n}.aot-private.bin').write_bytes(data)
    finally:ref.close()
    require(len(rows)==len(numbers),'Incomplete composition selection')
    passed=bool(rows and all(r['shader_state_components_qualified'] for r in rows))
    report=dict(passed=passed,inputs_sha256=coverage['inputs_sha256'],library_sha256=library_sha,
        selected_draws=len(numbers),draws=rows,target_calls=target_calls,producer_bindings=producers,
        fixture_pushbuffer_recycles=recycles,vertices=sum(r['vertices'] for r in rows),
        vertex_compared_values=sum(r['vertex_compared_values'] for r in rows),
        state_compared_words=sum(r['compared_words'] for r in rows),
        original_composed_all_draws_executed=bool(args.all_draws and passed),
        noise_disabled_fixture=True,initial_clear_viewport_fixture=True,
        complete_frame_target_continuity_qualified=False,complete_scene_pixel_qualified=False,
        original_ring_startup_qualified=False,resource_lifetime_integration_qualified=False,scope=__doc__)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(f"{'PASS' if passed else 'FAIL'}: {len(rows)} composed original shader/state draws, {len(producers)} producer bindings",flush=True)
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output','xbe','binding','library','dependencies'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--draws',type=int,nargs='+');parser.add_argument('--all-draws',action='store_true')
    args=parser.parse_args();require(not(args.draws and args.all_draws),'Select samples or the complete frame')
    handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    raise SystemExit(0 if compose(args)['passed'] else 1)
