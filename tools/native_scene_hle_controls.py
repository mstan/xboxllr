#!/usr/bin/env python3
"""Corruption and expectation-independence controls for native scene admission.

Modify isolated in-memory inputs/observations, leaving retained captures intact.
The controls exercise omitted/stray pixels, shader and sampler inputs, depth,
masked channels and truncated readbacks. A plausible-looking no-op cannot pass.
"""
import argparse
import copy
import json
from pathlib import Path
import shutil
import struct
import tempfile

from native_scene_reference import read_capture,read_scene
from native_scene_host_pixels import audit,predict
from native_scene_host_precision import qualify
from native_scene_hle_reference import audit as corpus_audit


def run(captures,observations):
    c=read_capture(captures/'draw-2.bin');s=read_scene(captures/'scene-2.bin')
    original={k:(observations/f'native-draw-2-pass-0-{k}.bin').read_bytes() for k in ('raw','raster','color','depth')}
    before_color,before_depth,_,_,colors,_,_=predict(c,s,original['raster'])
    written=next(iter(colors));untouched=next(i for i in range(len(before_color)) if i not in colors)
    def integer_edit(data,offset,fn):
        data=bytearray(data);value=struct.unpack_from('<I',data,offset)[0]
        struct.pack_into('<I',data,offset,fn(value));return bytes(data)
    def float_edit(data,offset,fn):
        data=bytearray(data);value=struct.unpack_from('<f',data,offset)[0]
        struct.pack_into('<f',data,offset,fn(value));return bytes(data)
    names=['baseline','missing-raster-vertex','wrong-production-position','wrong-production-diffuse',
        'wrong-production-q','wrong-production-fog','wrong-raw-output','vertex-input','constant-input',
        'program-input','cull-input','bias-input','texture-input','no-op','omitted-pixel','stray-pixel',
        'wrong-rgb','wrong-depth','wrong-stencil','wrong-masked-alpha','truncated-color','truncated-depth',
        'captured-raw-output-ignored','captured-after-image-ignored']
    rows=[]
    for name in names:
        capture=copy.deepcopy(c);scene=copy.deepcopy(s);data=original.copy()
        if name=='missing-raster-vertex':data['raster']=data['raster'][:-52*4]
        elif name=='wrong-production-position':data['raster']=float_edit(data['raster'],0,lambda v:v+1)
        elif name=='wrong-production-diffuse':data['raster']=float_edit(data['raster'],12*4,lambda v:v+.5)
        elif name=='wrong-production-q':data['raster']=float_edit(data['raster'],39*4,lambda v:v+.25)
        elif name=='wrong-production-fog':data['raster']=float_edit(data['raster'],20*4,lambda v:v-.1)
        elif name=='wrong-raw-output':data['raw']=float_edit(data['raw'],0,lambda v:v+1)
        elif name=='vertex-input':capture['streams'][0]=float_edit(capture['streams'][0],0,lambda v:v+1)
        elif name=='constant-input':capture['constants']=float_edit(capture['constants'],99*16,lambda v:v+.125)
        elif name=='program-input':capture['code'][1]^=1<<6
        elif name=='cull-input':capture['render'][147]^=1
        elif name=='bias-input':capture['texture'][6]=struct.unpack('<I',struct.pack('<f',4.0))[0]
        elif name=='texture-input':
            for mip in scene['textures'][0]['mips']:mip['data']=struct.pack('<HHI',0xffff,0,0)*(len(mip['data'])//8)
            scene['textures'][0]['cpu']=b''.join(m['data'] for m in scene['textures'][0]['mips'])
        elif name=='no-op':
            data['color']=struct.pack('<'+'I'*len(before_color),*before_color)
            data['depth']=struct.pack('<'+'I'*len(before_depth),*before_depth)
        elif name=='omitted-pixel':
            data['color']=integer_edit(data['color'],written*4,lambda v:before_color[written])
            data['depth']=integer_edit(data['depth'],written*4,lambda v:before_depth[written])
        elif name=='stray-pixel':data['color']=integer_edit(data['color'],untouched*4,lambda v:v^1)
        elif name=='wrong-rgb':data['color']=integer_edit(data['color'],written*4,lambda v:v^(64<<16))
        elif name=='wrong-depth':data['depth']=integer_edit(data['depth'],written*4,lambda v:v+(128<<8))
        elif name=='wrong-stencil':data['depth']=integer_edit(data['depth'],written*4,lambda v:v^1)
        elif name=='wrong-masked-alpha':data['color']=integer_edit(data['color'],written*4,lambda v:v^(64<<24))
        elif name=='truncated-color':data['color']=data['color'][:-4]
        elif name=='truncated-depth':data['depth']=data['depth'][:-4]
        elif name=='captured-raw-output-ignored':capture['outputs']=[123.0]*len(capture['outputs'])
        elif name=='captured-after-image-ignored':
            for target in scene['after']:target['data']=b'\xcc'*len(target['data'])
        expected=name in ('baseline','captured-raw-output-ignored','captured-after-image-ignored')
        try:
            raw,_=qualify(capture,data['raw'])
            result=audit(capture,scene,data['raster'],data['color'],data['depth'])
            admitted=raw['shader_qualified'] and result['native_noise_off_draw_qualified']
            detail=dict(raw_failures=raw['failure_count'],target_failures=result['failure_count'])
        except AssertionError as error:admitted=False;detail=dict(guard=str(error))
        rows.append(dict(control=name,expected_qualified=expected,qualified=admitted,
            control_passed=admitted==expected,**detail))
    return rows


def chain_controls(captures,observations):
    rows=[]
    with tempfile.TemporaryDirectory(prefix='blinx-native-chain-controls-') as temporary:
        target=Path(temporary)
        for draw in (1,2):
            for source in observations.glob(f'native-draw-{draw}-pass-*'):shutil.copyfile(source,target/source.name)
        mutations={'chain-baseline':None,'broken-root-input':(1,0,'depth'),
            'broken-predecessor':(2,0,'color'),'before-device-lifetime-drift':(2,1,'depth')}
        for name,mutation in mutations.items():
            path=None;original=None
            if mutation:
                draw,lifetime,kind=mutation
                path=target/f'native-draw-{draw}-pass-{lifetime}-before-{kind}.bin'
                original=path.read_bytes();changed=bytearray(original);changed[0]^=1;path.write_bytes(changed)
            try:
                result=corpus_audit(captures,[1,2],target,True)
                admitted=all(r['native_noise_off_draw_qualified'] for r in result['draws']);detail={}
            except AssertionError as error:admitted=False;detail=dict(guard=str(error))
            expected=mutation is None
            rows.append(dict(control=name,expected_qualified=expected,qualified=admitted,
                control_passed=admitted==expected,**detail))
            if path is not None:path.write_bytes(original)
    return rows


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','observations','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--chain-observations',type=Path,help='Also check initial/persistent target and device-lifetime continuity')
    args=parser.parse_args();rows=run(args.captures,args.observations)
    if args.chain_observations:rows+=chain_controls(args.captures,args.chain_observations)
    args.output.write_text(json.dumps(dict(controls=rows,all_controls_passed=all(r['control_passed'] for r in rows)),indent=2)+'\n')
    for row in rows:print(json.dumps(row),flush=True)
    raise SystemExit(0 if rows and all(r['control_passed'] for r in rows) else 1)
