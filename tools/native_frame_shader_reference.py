#!/usr/bin/env python3
"""Independent compact-frame shader inputs versus actual original XDK draws.

No GPU/title/audio/hardware run. Fresh preowned reference resources bound each
original Load/Select/SetConstant/SetStream/Draw call, without replacing it.
FVF, full state/pixels, allocator/ring lifetime and silicon remain separate.
"""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import struct

from native_frame_inputs import Reader, audit as input_audit, require
from native_scene_reference import run_captures
from native_scene_shader_reference import expectation, MATH_SOURCE, ISA_SOURCE
from native_vertex_fetch_reference import XBE_SHA


def qualify(capture,observation):
    # This calculation completes before any observation is inspected.
    vertices,fog,_=expectation(capture)
    require(type(observation['input_sequence']) is int and
            observation['vertex_capture_sha256']==capture['sha256'] and
            observation['input_sequence']==capture['input_sequence'],'Shader input provenance mismatch')
    require(observation['complete_draw_executed'] is True and observation['input_only_capture'] is True,
            'Missing original draw or invented native output')
    counters=observation['draw_counters']
    require(len(counters)==26 and all(type(v) is int and v>=0 for v in counters) and counters[1]
            and not any(counters[3:]),'Original draw had missing/guarded device service')
    require(observation['submission']==('original DrawVerticesUP' if capture['submission_kind'] else
                                       'original DrawVertices'),'Wrong original submission route')
    require(len(observation['vertices'])==len(vertices) and len(observation['fog'])==len(fog),
            'Missing/extra vertex/fog observation')
    compared=0;failures=[]
    for index,(actual,expected) in enumerate(zip(observation['vertices'],vertices)):
        require(len(actual)==28,'Incomplete output vector')
        for lane,(got,want) in enumerate(zip(actual,expected)):
            require(type(got) in (int,float) and math.isfinite(got),'Nonfinite vertex output')
            if got!=want:failures.append([index,lane,got,want])
            compared+=1
        got=observation['fog'][index]
        require(type(got) in (int,float) and math.isfinite(got),'Nonfinite fog output')
        if got!=fog[index]:failures.append([index,'fog',got,fog[index]])
        compared+=1
    return dict(input=capture['input_sequence'],input_sha256=capture['sha256'],
                program_sha256=hashlib.sha256(bytes_from_code(capture['code'])).hexdigest(),
                vertices=len(vertices),compared_values=compared,failure_count=len(failures),
                first_failures=failures[:12],software_shader_qualified=not failures,
                complete_scene_pixel_qualified=False)


def bytes_from_code(code):
    return struct.pack('<'+'I'*len(code),*code)


def observation_for(args,number):
    paths=[folder/f'input-{number}.aot-private.json' for folder in
           [args.diagnostics]+getattr(args,'additional_diagnostics',[])]
    values=[json.loads(path.read_text()) for path in paths if path.exists()]
    require(values,'Missing original shader observation')
    require(all(value==values[0] for value in values),'Conflicting observations cannot be selected away')
    return values[0]


def selected(reader,args):
    require(not (args.draws and args.all_draws),'Choose draw numbers or all programmable draws')
    if args.draws:numbers=args.draws
    elif args.all_draws:numbers=[d['input'] for d in reader.draws if d['kind']<2]
    else:
        numbers=[];seen=set()
        for draw in reader.draws:
            if draw['kind']>=2:continue
            profile=(draw['code_asset'],draw['element_asset'],draw['kind'])
            if profile not in seen:seen.add(profile);numbers.append(draw['input'])
    require(numbers and len(set(numbers))==len(numbers),'Empty/repeated shader selection')
    return numbers


def run(args):
    coverage=input_audit(args.captures/'frame.jsonl');reader=Reader(args.captures/'frame-inputs.bin').read()
    numbers=selected(reader,args);rows=[];expectations=[];observations=[]
    args.diagnostics.mkdir(parents=True,exist_ok=True)
    for index,number in enumerate(numbers,1):
        capture=reader.shader_capture(number)
        try:
            # Retain expected values BEFORE executing the original draw.
            vertices,fog,_=expectation(capture)
            expected=dict(input=number,input_sha256=capture['sha256'],vertices=vertices,fog=fog,
                          complete_scene_pixel_qualified=False)
            (args.diagnostics/f'input-{number}-expected.aot-private.json').write_text(json.dumps(expected,indent=2)+'\n')
            expectations.append(dict(input=number,vertices=len(vertices),input_sha256=capture['sha256']))
            observed=run_captures(args,[(Path(f'input-{number}'),capture)])
            observations.extend(observed['draws'])
            observation=json.loads((args.diagnostics/f'input-{number}.aot-private.json').read_text())
            row=qualify(capture,observation)
        except (AssertionError,ValueError,OverflowError,KeyError,IndexError) as error:
            row=dict(input=number,software_shader_qualified=False,guard=str(error) or type(error).__name__,
                     complete_scene_pixel_qualified=False)
        rows.append(row)
        if index%32==0 or index==len(numbers):
            print(f'Original compact shader draws {index}/{len(numbers)}; qualified {sum(r["software_shader_qualified"] for r in rows)}',flush=True)
    return dict(inputs_sha256=coverage['inputs_sha256'],ledger_sha256=coverage['ledger_sha256'],
        original_observations=observations,expectations=expectations,draws=rows,
        selected_draws=len(rows),qualified_draws=sum(r['software_shader_qualified'] for r in rows),
        vertices=sum(r.get('vertices',0) for r in rows),compared_values=sum(r.get('compared_values',0) for r in rows),
        programmable_shader_samples_qualified=all(r['software_shader_qualified'] for r in rows),
        complete_programmable_frame_qualified=bool(args.all_draws and all(r['software_shader_qualified'] for r in rows)),
        complete_scene_pixel_qualified=False,resource_lifetime_integration_qualified=False,
        original_state_delivery_qualified=False,isa_source=ISA_SOURCE,math_source=MATH_SOURCE,
        scope='Supplied stream/program/constant inputs; actual original programmable bound/UP calls on fresh preowned resources; ideal nearest/even software math, not native or silicon approximations; FVF and state/pixel/lifetime remain open')


def audit_existing(args):
    coverage=input_audit(args.captures/'frame.jsonl');reader=Reader(args.captures/'frame-inputs.bin').read()
    rows=[];libraries=set();library_sha=hashlib.sha256(args.library.read_bytes()).hexdigest() if args.library else None
    for index,number in enumerate(selected(reader,args),1):
        capture=reader.shader_capture(number)
        try:
            observation=observation_for(args,number)
            require(observation['xbe_sha256']==XBE_SHA,'Observation names another original XBE')
            libraries.add(observation['library_sha256'])
            if library_sha:require(observation['library_sha256']==library_sha,
                                    'Observation comes from another LLE build')
            row=qualify(capture,observation)
        except (AssertionError,ValueError,OverflowError,KeyError,IndexError) as error:
            row=dict(input=number,software_shader_qualified=False,guard=str(error) or type(error).__name__)
        rows.append(row)
        if index%256==0:print(f'Retained compact shader audit {index}',flush=True)
    require(len(libraries)==1,'Mixed or absent original LLE reference builds')
    return dict(inputs_sha256=coverage['inputs_sha256'],ledger_sha256=coverage['ledger_sha256'],
        library_sha256=next(iter(libraries)),xbe_sha256=XBE_SHA,draws=rows,selected_draws=len(rows),
        qualified_draws=sum(r['software_shader_qualified'] for r in rows),
        vertices=sum(r.get('vertices',0) for r in rows),compared_values=sum(r.get('compared_values',0) for r in rows),
        programmable_shader_samples_qualified=all(r['software_shader_qualified'] for r in rows),
        complete_programmable_frame_qualified=bool(args.all_draws and all(r['software_shader_qualified'] for r in rows)),
        complete_scene_pixel_qualified=False,resource_lifetime_integration_qualified=False,
        original_state_delivery_qualified=False,isa_source=ISA_SOURCE,math_source=MATH_SOURCE,
        scope='Input-only independent exact software shader/fetch audit of retained actual original bound/UP calls; no new device/GPU/title/audio/hardware run; FVF/state/pixels/lifetime/silicon remain separate')


def controls(reader,diagnostics,numbers):
    rows=[]
    for number in numbers:
        original=reader.shader_capture(number)
        observation=json.loads((diagnostics/f'input-{number}.aot-private.json').read_text())
        for name in ('baseline','changed-output','missing-vertex','missing-fog','nonfinite','wrong-provenance',
                     'incomplete-draw','guarded-service','wrong-route','changed-live-input','missing-stream',
                     'changed-constants','unsupported-opcode','native-output-ignored'):
            capture=copy.deepcopy(original);actual=copy.deepcopy(observation)
            if name=='changed-output':actual['vertices'][0][0]+=64
            elif name=='missing-vertex':actual['vertices'].pop()
            elif name=='missing-fog':actual['fog'].pop()
            elif name=='nonfinite':actual['vertices'][0][0]=float('nan')
            elif name=='wrong-provenance':actual['vertex_capture_sha256']='0'*64
            elif name=='incomplete-draw':actual['complete_draw_executed']=False
            elif name=='guarded-service':actual['draw_counters'][3]=1
            elif name=='wrong-route':actual['submission']='substituted draw'
            elif name=='native-output-ignored':capture['outputs']=[1e30]*(capture['header'][4]*52)
            elif name in ('changed-constants','unsupported-opcode'):
                if name=='unsupported-opcode':capture['code'][1]=(capture['code'][1]&~(15<<21))|(15<<21)
                else:
                    capture['constants']=struct.pack('<768f',*[v+4 for (v,) in struct.iter_unpack('<f',capture['constants'])])
                capture['sha256']=hashlib.sha256(name.encode()+bytes_from_code(capture['code'])).hexdigest()
                actual['vertex_capture_sha256']=capture['sha256']
            elif name in ('changed-live-input','missing-stream'):
                stream=next(i for i,data in enumerate(capture['streams']) if data)
                if name=='missing-stream':capture['streams'][stream]=b''
                else:
                    # Mutate actual finite input values and refresh provenance;
                    # an unchanged old output cannot qualify different inputs.
                    for fmt,source,offset in capture['elements']:
                        if fmt not in (0x12,0x22,0x32,0x42):continue
                        data=bytearray(capture['streams'][source])
                        for lane in range(fmt>>4):
                            value=struct.unpack_from('<f',data,offset+lane*4)[0]
                            struct.pack_into('<f',data,offset+lane*4,value+4)
                        capture['streams'][source]=bytes(data)
                capture['sha256']=hashlib.sha256(name.encode()+bytes_from_code(capture['code'])).hexdigest()
                actual['vertex_capture_sha256']=capture['sha256']
            try:passed=qualify(capture,actual)['software_shader_qualified']
            except (AssertionError,ValueError,OverflowError,KeyError,IndexError):passed=False
            rows.append(dict(input=number,control=name,qualified=passed,
                             control_passed=passed==(name in ('baseline','native-output-ignored'))))
    return dict(controls=rows,control_passed=all(r['control_passed'] for r in rows),
                complete_scene_pixel_qualified=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output'):parser.add_argument('--'+name,type=Path,required=True)
    for name in ('xbe','binding','library','dependencies'):parser.add_argument('--'+name,type=Path)
    parser.add_argument('--draws',type=int,nargs='+');parser.add_argument('--all-draws',action='store_true')
    parser.add_argument('--audit-existing',action='store_true')
    parser.add_argument('--additional-diagnostics',type=Path,nargs='+',default=[])
    parser.add_argument('--controls',action='store_true');args=parser.parse_args()
    require(not(args.controls and args.audit_existing),'Choose controls or retained audit')
    if args.controls:
        input_audit(args.captures/'frame.jsonl');reader=Reader(args.captures/'frame-inputs.bin').read()
        result=controls(reader,args.diagnostics,selected(reader,args));passed=result['control_passed']
    elif args.audit_existing:
        result=audit_existing(args);passed=result['programmable_shader_samples_qualified']
    else:
        require(all(getattr(args,name) for name in ('xbe','binding','library','dependencies')),
                'Original replay requires XBE, binding, library and dependencies')
        handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
        result=run(args);passed=result['programmable_shader_samples_qualified']
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k in ('selected_draws','qualified_draws','vertices',
                     'compared_values','programmable_shader_samples_qualified','complete_programmable_frame_qualified',
                     'control_passed','complete_scene_pixel_qualified')}),flush=True)
    raise SystemExit(0 if passed else 1)
