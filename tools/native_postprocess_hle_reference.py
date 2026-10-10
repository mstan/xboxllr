#!/usr/bin/env python3
"""Prepare and qualify retained-input production HLE postprocess components.

The GPU receives only original source inputs and supplied producer roots. All
expected geometry/state/pixels precede observation reads and use independent
equations. Feedback components are isolated; the final three consumers retain
actual GPU storage while their oracle chains only independently expected bytes.
Noise is disabled explicitly. Full producers/scenes, exact silicon arithmetic
and original resource startup/lifetime remain separate qualifications.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct

import numpy as np

from native_frame_inputs import Reader,audit as input_audit,require
from native_frame_state_reference import Inputs
from native_frame_storage import draw_plan
from native_frame_target_reference import layout
from native_frame_shader_state_reference import Prepared
from native_frame_state_delivery import expected_delivery
from native_postprocess_pixel_reference import rgba,pixels,quantize,compare,stencil_bytes,SOURCES

GROUPS = ((2767,),(2820,),(4300,4301,4302))


def digest(data):
    return hashlib.sha256(data).hexdigest()


def load(args):
    coverage=input_audit(args.captures/'frame.jsonl')
    reader=Reader(args.captures/'frame-inputs.bin').read()
    source=json.loads(args.source_report.read_text())
    require(source['passed'] and source['original_composed_all_draws_executed'] and
            source['inputs_sha256']==coverage['inputs_sha256'],'Wrong supplied composition frame')
    return reader,Inputs(reader),source,coverage


def initial_roots(reader,source,producers,number):
    initial={n:bytes([0xa5])*(layout(t)[2]*t['height']) for n,t in reader.targets.items()}
    require(set(initial)==set(range(1,6)),'Observer requires exactly the five declared owned roots')
    for binding in source['producer_bindings']:
        if binding['input']!=number:continue
        n=binding['target'];data=(producers/f'input-{number}-producer-{n}.aot-private.bin').read_bytes()
        require(digest(data)==binding['live_bytes_sha256'] and len(data)==len(initial[n]),
                'Supplied producer differs from its pinned input snapshot')
        initial[n]=data
    return initial


def geometry(capture,scene,reader,draw):
    plan=draw_plan(reader.targets,draw,scene);scene['frame_resource_plan']=plan
    prepared=Prepared(capture,scene)
    methods,fixture,pixel=expected_delivery(capture,scene)
    methods={**fixture,**methods,**pixel}
    require(draw['first']==0 and draw['count']==4 and draw['primitive'] in (6,8) and
            draw['up_stride'] and all(not m[1] for m in capture['metadata'][1:]),
            'Native consumer observer requires the retained four-vertex UP/stream-zero domain')
    require(scene['header'][8] and len(scene['pixel'])==60,'Native consumer requires the custom combiner')
    require(capture['render'][54]==capture['render'][136],
            'Retained pixel-program and shipping texture-mode selectors differ')
    return plan,prepared,methods


def payload(args):
    reader,inputs,source,coverage=load(args);rows=[]
    with io.BytesIO() as file:
        def put(values):file.write(struct.pack('<'+'I'*len(values),*values))
        put([0x50504c58,1,3,3])
        for numbers in GROUPS:
            initial=initial_roots(reader,source,args.producers,numbers[0]);put([len(numbers)])
            for n,t in sorted(reader.targets.items()):
                width,height,pitch=layout(t);fmt=0x2e if t['role'] else 0x12
                require(pitch==width*4,'Native observer requires compact linear roots')
                if n==1:
                    require(t['format']==28 and all(initial[n][i]==initial[n][i+2]
                            for i in range(0,len(initial[n]),4)),'Default RGBA root needs channel-symmetric supplied bytes')
                put([n,width,height,pitch,fmt,len(initial[n])]);file.write(initial[n])
            for number in numbers:
                capture,scene=inputs.draw(number);draw=reader.draws[number-1]
                plan,prepared,methods=geometry(capture,scene,reader,draw)
                put([number,draw['kind'],draw['primitive'],draw['first'],draw['count'],draw['shader'],
                     draw['program_instructions'],draw['up_stride'],draw['target'],draw['depth']])
                put(capture['viewport']);put(scene['screen']);put(capture['render']);put(capture['texture'])
                for element in capture['elements']:put(element)
                if draw['program_instructions']:file.write(capture['constants']);put(capture['code'])
                for metadata in capture['metadata']:put(metadata)
                for stream in capture['streams']:file.write(stream)
                for producer,texture in zip(plan['texture_producers'],scene['textures']):
                    require(bool(producer)==bool(texture),'Native observer needs all live textures backed by declared roots')
                    put([producer,(texture['resource'][3]>>8)&255 if texture else 0])
                rows.append(dict(input=number,vertex_input_sha256=capture['sha256'],scene_input_sha256=scene['sha256'],
                                 source_vertices=len(prepared.vertices),captured_noise=capture['render'][159],fixture_noise=0))
        data=file.getvalue()
    return data,dict(prepared=True,inputs_sha256=coverage['inputs_sha256'],rows=rows,
                     native_input_sha256=digest(data),expected_after_images_supplied=False,
                     scope=__doc__)


def require_original_payload(args):
    data,metadata=payload(args)
    require(args.native_input.read_bytes()==data,
            'Native observer payload differs from independently encoded original inputs')
    return metadata


def prepare(args):
    data,metadata=payload(args);args.native_input.write_bytes(data)
    return metadata


def audit(args):
    require_original_payload(args)  # Source binding precedes all observation reads.
    reader,inputs,source,coverage=load(args);rows=[];failures=[]
    for numbers in GROUPS:
        initial=initial_roots(reader,source,args.producers,numbers[0])
        expected_storage=dict(initial);images={n:rgba(data,*layout(reader.targets[n])) for n,data in initial.items()}
        written=set();previous_actual=None
        for number in numbers:
            capture,scene=inputs.draw(number);draw=reader.draws[number-1]
            plan,prepared,methods=geometry(capture,scene,reader,draw)
            target=draw['target'];width,height,pitch=layout(reader.targets[target])
            sampled=[images[n] if n else None for n in plan['texture_producers']]
            wanted,margin=pixels(methods,prepared.vertices,draw['primitive'],width,height,sampled,images[target])
            expected_storage[target]=wanted[...,[2,1,0,3]].tobytes();images[target]=wanted/255.
            depth=draw['depth'];expected_storage[depth]=stencil_bytes(methods,expected_storage[depth],
                *layout(reader.targets[depth]),width,height)
            # Complete independent expectation before opening any GPU observation.
            actual={};before={};lifetime_drift=0
            for lifetime in range(3):
                for n in reader.targets:
                    for phase,destination in (('before',before),('after',actual)):
                        data=(args.observations/f'native-input-{number}-pass-{lifetime}-root-{n}-{phase}.bin').read_bytes()
                        require(len(data)==len(initial[n]),'Incomplete native owned-root observation')
                        if lifetime==0:destination[n]=data
                        else:lifetime_drift+=int(destination[n]!=data)
            continuity=before==(previous_actual if previous_actual is not None else initial)
            checks=[]
            for n,t in reader.targets.items():
                if t['role'] or n not in written|{target}:
                    checks.append(dict(target=n,exact_bytes=len(actual[n]),qualified=actual[n]==expected_storage[n]))
                else:
                    result=compare(quantize(images[n]),quantize(rgba(actual[n],*layout(t))))
                    checks.append(dict(target=n,**result,qualified=result['component_pixels_qualified']))
            row=dict(input=number,roots=checks,minimum_msb_mux_margin=margin,
                     retained_gpu_predecessor_continuity=continuity,device_lifetimes=3,
                     lifetime_byte_drift=lifetime_drift,qualified=continuity and not lifetime_drift and
                     all(c['qualified'] for c in checks),expected_predecessor_bytes_used=True)
            if draw['program_instructions']:
                from native_scene_host_precision import qualify as shader_qualify
                raw=[];raster=[]
                for lifetime in range(3):
                    data=(args.observations/f'native-input-{number}-pass-{lifetime}-root-0-raw.bin').read_bytes()
                    result,_=shader_qualify(capture,data,raster=False);raw.append(result)
                    data=(args.observations/f'native-input-{number}-pass-{lifetime}-root-0-raster.bin').read_bytes()
                    result,_=shader_qualify(capture,data,raster=True);raster.append(result)
                row['raw_shader_lifetimes']=raw;row['raster_shader_lifetimes']=raster
                row['qualified']&=all(r['shader_qualified'] for r in raw+raster)
            rows.append(row)
            if not row['qualified']:failures.append(number)
            written.add(target);previous_actual=actual
    return dict(passed=not failures,failed_inputs=failures,rows=rows,
                inputs_sha256=coverage['inputs_sha256'],equation_sources=SOURCES,
                native_input_sha256=digest(args.native_input.read_bytes()),
                native_executable_sha256=digest(args.executable.read_bytes()),
                original_consumers=5,device_lifetimes=3,expected_after_image_goldens_used=False,
                complete_scene_pixel_qualified=False,original_startup_lifetime_qualified=False,
                enabled_noise_qualified=False,exact_hardware_rounding_qualified=False,scope=__doc__)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','source-report','producers','native-input','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--observations',type=Path);parser.add_argument('--executable',type=Path)
    args=parser.parse_args();require(bool(args.observations)==bool(args.executable),'Audit needs the actual executable')
    report=audit(args) if args.observations else prepare(args)
    args.output.write_text(json.dumps(report,indent=2)+'\n',newline='\n')
    print(json.dumps({k:report[k] for k in ('passed','failed_inputs') if k in report}) if args.observations else 'Prepared five native consumers; no expected outputs supplied')
    raise SystemExit(0 if report.get('passed',True) else 1)
