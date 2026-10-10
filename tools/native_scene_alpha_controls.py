#!/usr/bin/env python3
"""Independent alpha-volume gate corruption and expectation controls.

Only in-memory inputs/observations change. Mutations exercise the arithmetic
and pixel gate, not stale provenance. Captured raw/after data stay diagnostics.
"""
import argparse
import copy
import json
from pathlib import Path
import struct

from native_scene_reference import read_capture,read_scene
from native_scene_alpha_reference import expected_state,expected_targets
from native_scene_alpha_host_reference import compare as host_compare
from native_scene_shader_reference import expectation
from native_scene_pixel_reference import words


def edit(data,index,kind,transform):
    result=bytearray(data);value=struct.unpack_from('<'+kind,result,index*4)[0]
    struct.pack_into('<'+kind,result,index*4,transform(value));return bytes(result)


def software_compare(c,s,data,meta,arithmetic):
    methods=expected_state(c,s);vertices,fog,_=expectation(c)
    if vertices!=meta['vertices'] or fog!=meta['fog']:return False
    if any(meta['methods'].get(hex(k))!=v for k,v in methods.items()):return False
    color,depth,_=expected_targets(c,s,arithmetic)
    return all(data[k]==struct.pack('<'+'I'*len(v),*v) for k,v in (('color',color),('depth',depth)))


def run(captures,observations,native,arithmetic,only=None):
    c=read_capture(captures/'draw-1.bin');s=read_scene(captures/'scene-1.bin')
    prefix='native-draw-1-pass-0' if native else 'draw-1'
    data={k:(observations/f'{prefix}-{k}.bin').read_bytes() for k in (('raw','raster','color','depth') if native else ('color','depth'))}
    meta={} if native else json.loads((observations/'draw-1.aot-private.json').read_text())
    before=words(s['before'][0]);actual=[v[0] for v in struct.iter_unpack('<I',data['color'])]
    written=next(i for i,v in enumerate(actual) if v!=before[i]);untouched=next(i for i,v in enumerate(actual) if v==before[i])
    names=['baseline','no-op','omitted-alpha','stray-alpha','wrong-alpha','wrong-rgb','wrong-depth','wrong-stencil',
        'truncated-color','truncated-depth','vertex-input','constant-input','program-input','cull-input',
        'blend-equation-input','write-mask-input','depth-write-input','texture-input',
        'captured-raw-output-ignored','captured-after-image-ignored']
    names+=['wrong-raw-output','wrong-raster-position','wrong-raster-alpha','truncated-raster'] if native else ['wrong-transformed-position','wrong-transformed-alpha','wrong-setter-method']
    if only:
        assert set(only)<=set(names)
        names=[name for name in names if name in only]
    results=[]
    for name in names:
        cc=copy.deepcopy(c);ss=copy.deepcopy(s);dd=data.copy();mm=copy.deepcopy(meta)
        if name=='no-op':dd['color']=struct.pack('<'+'I'*len(before),*before)
        elif name=='omitted-alpha':dd['color']=edit(dd['color'],written,'I',lambda v:before[written])
        elif name=='stray-alpha':dd['color']=edit(dd['color'],untouched,'I',lambda v:v^(6<<24))
        elif name=='wrong-alpha':dd['color']=edit(dd['color'],written,'I',lambda v:v^(16<<24))
        elif name=='wrong-rgb':dd['color']=edit(dd['color'],written,'I',lambda v:v^1)
        elif name=='wrong-depth':dd['depth']=edit(dd['depth'],written,'I',lambda v:v^(16<<8))
        elif name=='wrong-stencil':dd['depth']=edit(dd['depth'],written,'I',lambda v:v^1)
        elif name=='truncated-color':dd['color']=dd['color'][:-4]
        elif name=='truncated-depth':dd['depth']=dd['depth'][:-4]
        elif name=='vertex-input':cc['streams'][0]=edit(cc['streams'][0],0,'f',lambda v:v+10)
        elif name=='constant-input':cc['constants']=edit(cc['constants'],59*4,'f',lambda v:v+10)
        elif name=='program-input':cc['code'][1]=(cc['code'][1]&~(15<<21))|(1<<21)  # DPH -> MOV, not equivalent DP4 with input W=1
        elif name=='cull-input':cc['render'][147]=0x901
        elif name=='blend-equation-input':cc['render'][74]=0x800b
        elif name=='write-mask-input':cc['render'][67]=0x1010101
        elif name=='depth-write-input':cc['render'][64]=1
        elif name=='texture-input':ss['textures'][0]['mips'][0]['data']=bytes(len(ss['textures'][0]['mips'][0]['data']))
        elif name=='captured-raw-output-ignored':cc['outputs']=[123.0]*len(cc['outputs'])
        elif name=='captured-after-image-ignored':
            for target in ss['after']:target['data']=b'\xcc'*len(target['data'])
        elif name=='wrong-raw-output':dd['raw']=edit(dd['raw'],0,'f',lambda v:v+128)
        elif name=='wrong-raster-position':dd['raster']=edit(dd['raster'],0,'f',lambda v:v+1)
        elif name=='wrong-raster-alpha':dd['raster']=edit(dd['raster'],15,'f',lambda v:v+.5)
        elif name=='truncated-raster':dd['raster']=dd['raster'][:-208]
        elif name=='wrong-transformed-position':mm['vertices'][0][0]+=1
        elif name=='wrong-transformed-alpha':mm['vertices'][0][7]+=.5
        elif name=='wrong-setter-method':mm['methods']['0x350']=0x800b
        want=name in ('baseline','captured-raw-output-ignored','captured-after-image-ignored')
        try:
            passed=host_compare(cc,ss,dd)['native_alpha_overlay_qualified'] if native else software_compare(cc,ss,dd,mm,arithmetic)
        except (AssertionError,ValueError,IndexError,KeyError,ZeroDivisionError,struct.error):passed=False
        row=dict(route='native' if native else 'original-XDK/LLE',control=name,gate_passed=passed,expected_pass=want,control_passed=passed==want)
        results.append(row);print(json.dumps(row),flush=True)
    return results


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','observations','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--native',action='store_true');p.add_argument('--depth-arithmetic',choices=('separate','fma-b-first'),default='fma-b-first')
    p.add_argument('--only-controls',nargs='+',help='Focused rerun after correcting a specific control')
    args=p.parse_args();rows=run(args.captures,args.observations,args.native,args.depth_arithmetic,args.only_controls)
    args.output.write_text(json.dumps(dict(controls=rows,all_controls_passed=all(r['control_passed'] for r in rows)),indent=2)+'\n')
    raise SystemExit(0 if all(r['control_passed'] for r in rows) else 1)
