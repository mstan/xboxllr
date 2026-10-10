#!/usr/bin/env python3
"""Input-only pretransformed FVF component versus original setter/bound/UP calls.

Only the captured XYZ(RHW), TEX1, optional diffuse profile is admitted. The
fixture has unit pixel scale, captured screen bias, disabled fog and RHW0/1.
State delivery, original allocation/startup and full-scene pixels stay separate.
"""
import argparse
import copy
import ctypes
from fractions import Fraction
import hashlib
import json
import math
import os
from pathlib import Path
import struct

from native_frame_inputs import Reader, audit as input_audit, require
from native_scene_shader_reference import fetch
from native_scene_pixel_reference import binary32
from native_vertex_fetch_reference import Reference, XBE_SHA, OBJECT, STREAMS

FVF_SOURCE='https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dfvf'
RCC_SOURCE='https://registry.khronos.org/OpenGL/extensions/NV/NV_vertex_program2.txt'


def declaration(fvf):
    require(fvf in (0x104,0x144),'Unsupported FVF profile')
    elements=[[2,0,0] for _ in range(16)];elements[0]=[0x42,0,0]
    if fvf&0x40:elements[3]=[0x40,0,16]
    elements[9]=[0x22,0,16+4*bool(fvf&0x40)]
    return elements


def capture(reader,number):
    require(type(number) is int and 0<number<=len(reader.draws),'Unknown FVF draw')
    draw=reader.draws[number-1];require(draw['kind']>=2,'Not a captured FVF draw')
    descriptor=list(reader.words(draw['fvf_asset'],0x114));elements=declaration(draw['shader'])
    for slot,expected in enumerate(elements):
        stream,offset,fmt,_=descriptor[5+slot*4:9+slot*4]
        require(fmt==expected[0] and (fmt==2 or [fmt,stream,offset]==expected),
                'Captured active FVF declaration disagrees with format')
    assets=[draw[k] for k in ('fvf_asset','render_asset','texture_state_asset','packed_asset')]
    assets.extend(s[2] for s in draw['streams'] if s[2])
    identity=dict(draw=draw,assets={n:reader.assets[n]['sha256'] for n in assets})
    return dict(draw=draw,elements=elements,metadata=[s[:2] for s in draw['streams']],
        streams=[reader.asset_bytes(s[2]) if s[2] else b'' for s in draw['streams']],
        render=list(reader.words(draw['render_asset'],664)),descriptor=descriptor,
        sha256=hashlib.sha256(json.dumps(identity,sort_keys=True,separators=(',',':')).encode()).hexdigest())


def expectation(value):
    d=value['draw'];require(d['shader'] in (0x104,0x144) and d['primitive'] in (6,8),
                            'Unsupported FVF submission')
    require(value['elements']==declaration(d['shader']),'Unsupported FVF declaration')
    require(value['render'][92]==0 and d['viewport'][0:2]==[0,0] and
            d['viewport'][4:]==[0,0x3f800000],'Unsupported FVF viewport/fog')
    bias=list(struct.unpack('<2f',struct.pack('<2I',*d['screen'])))
    require(all(math.isfinite(v) for v in bias),'Nonfinite screen bias')
    vertices=[]
    for index in range(d['count']):
        inputs=fetch(value,index);position=inputs[0]
        require(position[2]==0 and position[3] in (0,1),'Unqualified FVF depth/RHW')
        if position[3]==0:
            bits=struct.unpack_from('<I',value['streams'][0],index*value['metadata'][0][0]+12)[0]
            require(bits==0,'Negative zero RHW needs a separate signed-zero contract')
        # Original XDK pretransformed screen-offset contract was independently
        # qualified by native_viewport_constants_reference. RCC +0 -> +2^64
        # follows NV_vertex_program2, not a value learned from observations.
        xy=[math.trunc(binary32(position[i]+Fraction(bias[i]))*16)/16 for i in range(2)]
        w=1 if position[3] else 2**64
        diffuse=inputs[3] if d['shader']&0x40 else [1,1,1,1]
        vertices.append(xy+[0,w]+list(map(float,diffuse))+[0,0,0,0]+
                        list(map(float,inputs[9]))+[0,0,0,1]*3)
    require(vertices,'Empty FVF draw')
    return vertices,[0]*len(vertices)


def qualify(value,actual):
    vertices,fog=expectation(value)  # Compute before inspecting observations.
    d=value['draw'];require(actual['input_sha256']==value['sha256'] and
        type(actual['input']) is int and actual['input']==d['input'],'FVF provenance mismatch')
    require(actual['original_setter_returned'] is True and actual['original_draw_returned'] is True and
            actual['fifo_get']==actual['fifo_put'],'Incomplete original FVF calls/transport')
    require(actual['submission']==('original DrawVerticesUP' if d['kind']==3 else 'original DrawVertices'),
            'Wrong original FVF submission')
    counts=actual['draw_counters'];require(len(counts)==26 and
        all(type(v) is int and v>=0 for v in counts) and not any(counts[3:]),'Guarded FVF service')
    # QUADS have a separate LLE path and do not increment triangle counters.
    require(d['primitive']==8 or counts[1]>0,'Missing FVF triangle execution')
    require(len(actual['vertices'])==len(vertices) and len(actual['fog'])==len(fog),'Missing FVF vertices/fog')
    failures=[]
    for index,(got,want) in enumerate(zip(actual['vertices'],vertices)):
        require(len(got)==28 and all(type(v) in (float,int) and math.isfinite(v) for v in got),'Invalid FVF output')
        for lane,(a,b) in enumerate(zip(got,want)):
            if a!=b:failures.append([index,lane,a,b])
        require(type(actual['fog'][index]) in (int,float) and math.isfinite(actual['fog'][index]),'Invalid FVF fog')
        if actual['fog'][index]!=fog[index]:failures.append([index,'fog',actual['fog'][index],fog[index]])
    descriptor=actual['descriptor'];require(len(descriptor)==69,'Partial original FVF descriptor')
    for slot,expected in enumerate(value['elements']):
        stream,offset,fmt,_=descriptor[5+slot*4:9+slot*4]
        require(fmt==expected[0] and (fmt==2 or [fmt,stream,offset]==expected),'Wrong original active FVF declaration')
    require(descriptor[1]==(2|(0x400 if d['shader']&0x40 else 0)),'Wrong original FVF descriptor flags')
    return dict(input=d['input'],input_sha256=value['sha256'],vertices=len(vertices),
        compared_values=len(vertices)*29,failure_count=len(failures),first_failures=failures[:12],
        software_fvf_qualified=not failures,complete_scene_pixel_qualified=False)


def observe(args,value):
    # Retain independent expected values before constructing the reference.
    vertices,fog=expectation(value);d=value['draw'];args.diagnostics.mkdir(parents=True,exist_ok=True)
    (args.diagnostics/f'input-{d["input"]}-expected.aot-private.json').write_text(
        json.dumps(dict(vertices=vertices,fog=fog,input_sha256=value['sha256']),indent=2)+'\n')
    ref=Reference(args)
    try:
        read=ref.lib.xlle_nv2a_reference_read;read.restype=ctypes.c_uint32;read.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_int]
        counter=ref.lib.xlle_nv2a_reference_draw_count;counter.restype=ctypes.c_uint64;counter.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
        ref.put(OBJECT,1,0x10,0,0,0);ref.put(ref.device+ref.binding['stateblock']['fields']['vertex_object'],OBJECT)
        fields=ref.binding['transform_binding']['fields'];vp=d['viewport']
        ref.put(ref.device+0xa90,*vp[:4]);ref.put(ref.device+0xaa0,*vp[4:])
        ref.put(ref.device+fields['screen_offset'],*d['screen']);ref.put(ref.device+fields['depth_scale'],d['depth_scale'])
        ref.floats(ref.device+fields['pixel_scale'],1,1)
        for state in (92,143):ref.put(ref.binding['render_states']+state*4,value['render'][state])
        fvf=ref.binding['fvf_binding']['object'];ref.cpu.mem_write(fvf,bytes(0x114))
        ref.call('D3DDevice_SetVertexShader',[d['shader']])
        descriptor=ref.get(fvf,69)
        require(ref.get(ref.device+ref.binding['stateblock']['fields']['vertex_object'])[0]==fvf and
            ref.get(ref.device+ref.binding['stateblock']['fields']['vertex_shader'])[0]==d['shader'],'Original FVF binding failed')
        stride,extent=value['metadata'][0];ref.resource(OBJECT+0x1000,STREAMS)
        ref.set_stream(0,OBJECT+0x1000,stride)
        ref.cpu.mem_write(STREAMS+d['first']*stride,value['streams'][0])
        before=[counter(ref.gpu,i) for i in range(26)]
        ref.draw(d['first'],d['primitive'],d['count'],state_dirty=0x20 if d['kind']==3 else 0x50,
                 up_data=STREAMS if d['kind']==3 else None,up_stride=d['up_stride']);ref.check()
        observed=[]
        for name,width in (('vertices',28),('fog',1)):
            fn=getattr(ref.lib,'xlle_nv2a_reference_'+name);fn.restype=ctypes.c_uint32
            fn.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_float),ctypes.c_uint32]
            count=fn(ref.gpu,None,0);require(count==d['count'],'Missing original FVF output')
            data=(ctypes.c_float*(count*width))();require(fn(ref.gpu,data,count*width)==count,'Partial FVF observation')
            observed.append([list(data[i*width:(i+1)*width]) for i in range(count)] if width>1 else list(data))
        actual=dict(input=d['input'],input_sha256=value['sha256'],xbe_sha256=XBE_SHA,
            library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest(),vertices=observed[0],fog=observed[1],
            descriptor=descriptor,original_setter_returned=True,original_draw_returned=True,
            submission='original DrawVerticesUP' if d['kind']==3 else 'original DrawVertices',
            fifo_get=read(ref.gpu,0xfd800044,4),fifo_put=ref.get(ref.device)[0],
            draw_counters=[counter(ref.gpu,i)-before[i] for i in range(26)],complete_scene_pixel_qualified=False)
        (args.diagnostics/f'input-{d["input"]}.aot-private.json').write_text(json.dumps(actual,indent=2)+'\n')
        return actual
    finally:ref.close()


def controls(value,observation):
    rows=[]
    for name in ('baseline','wrong-output','changed-input','wrong-bias','missing-vertex','missing-fog',
                 'wrong-provenance','wrong-route','setter-not-returned','draw-not-returned','pending-fifo',
                 'guarded-service','wrong-declaration','nonfinite','unqualified-rhw'):
        v=copy.deepcopy(value);a=copy.deepcopy(observation)
        if name=='wrong-output':a['vertices'][0][0]+=1
        elif name=='changed-input':v['streams'][0]=struct.pack('<f',42)+v['streams'][0][4:]
        elif name=='wrong-bias':v['draw']['screen']=[0,0]
        elif name=='missing-vertex':a['vertices'].pop()
        elif name=='missing-fog':a['fog'].pop()
        elif name=='wrong-provenance':a['input_sha256']='different'
        elif name=='wrong-route':a['submission']='substituted'
        elif name=='setter-not-returned':a['original_setter_returned']=False
        elif name=='draw-not-returned':a['original_draw_returned']=False
        elif name=='pending-fifo':a['fifo_get']+=4
        elif name=='guarded-service':a['draw_counters'][25]=1
        elif name=='wrong-declaration':a['descriptor'][7]=2
        elif name=='nonfinite':a['fog'][0]=float('nan')
        elif name=='unqualified-rhw':v['streams'][0]=v['streams'][0][:12]+struct.pack('<f',2)+v['streams'][0][16:]
        if name in ('changed-input','wrong-bias','unqualified-rhw'):
            v['sha256']=a['input_sha256']='mutated-input'
        try:passed=qualify(v,a)['software_fvf_qualified']
        except (AssertionError,ValueError,IndexError):passed=False
        rows.append(dict(input=v['draw']['input'],control=name,control_passed=passed==(name=='baseline')))
    return rows


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output'):parser.add_argument('--'+name,type=Path,required=True)
    for name in ('xbe','binding','library','dependencies'):parser.add_argument('--'+name,type=Path)
    parser.add_argument('--audit-existing',action='store_true');parser.add_argument('--controls',action='store_true')
    args=parser.parse_args();require(not(args.controls and args.audit_existing),'Choose controls or retained audit')
    coverage=input_audit(args.captures/'frame.jsonl');reader=Reader(args.captures/'frame-inputs.bin').read()
    numbers=[d['input'] for d in reader.draws if d['kind']>=2];require(numbers,'No FVF draws')
    if not(args.audit_existing or args.controls):
        require(all(getattr(args,n) for n in ('xbe','binding','library','dependencies')),'Original FVF replay inputs missing')
        handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    rows=[];libraries=set()
    for number in numbers:
        value=capture(reader,number)
        actual=json.loads((args.diagnostics/f'input-{number}.aot-private.json').read_text()) if args.audit_existing or args.controls else observe(args,value)
        require(actual['xbe_sha256']==XBE_SHA,'Another original XBE')
        libraries.add(actual['library_sha256'])
        if args.library:require(actual['library_sha256']==hashlib.sha256(args.library.read_bytes()).hexdigest(),'Another reference build')
        rows.extend(controls(value,actual) if args.controls else [qualify(value,actual)])
    require(len(libraries)==1,'Mixed FVF reference builds')
    key='control_passed' if args.controls else 'software_fvf_qualified';passed=all(r[key] for r in rows)
    report=dict(inputs_sha256=coverage['inputs_sha256'],rows=rows,passed=passed,
        complete_fvf_frame_component_qualified=bool(passed and not args.controls),
        complete_scene_pixel_qualified=False,original_state_delivery_qualified=False,
        resource_lifetime_integration_qualified=False,fvf_source=FVF_SOURCE,rcc_source=RCC_SOURCE,
        xbe_sha256=XBE_SHA,library_sha256=next(iter(libraries)))
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(passed=passed,rows=len(rows),vertices=sum(r.get('vertices',0) for r in rows),
        values=sum(r.get('compared_values',0) for r in rows),complete_scene_pixel_qualified=False)),flush=True)
    raise SystemExit(0 if passed else 1)
