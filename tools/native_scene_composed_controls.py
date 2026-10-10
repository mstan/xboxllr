#!/usr/bin/env python3
"""Fault/independence controls for the retained-input scene composition gate.

Operate on temporary copies of one private draw, never the owner's captures.
Input mutations refresh provenance deliberately: rejection must come from
independent math/state/pixel checks, not merely a stale capture hash.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile

from native_scene_reference import read_capture,read_scene
from native_scene_shader_reference import expectation
from native_scene_composed_reference import audit,expected_state
from native_scene_pixel_reference import expected_targets


def run(captures,diagnostics,arithmetic):
    number=2;draw_name='draw-2.bin';scene_name='scene-2.bin'
    metadata_name='draw-2.aot-private.json'
    c=read_capture(captures/draw_name);s=read_scene(captures/scene_name)
    original=json.loads((diagnostics/metadata_name).read_text())
    vertices,fog,_=expectation(c)
    inputs={**original,'vertices':vertices,'fog':fog,
        'methods':{hex(k):v for k,v in expected_state(c,s).items()}}
    before_color,before_depth,colors,_,_,_=expected_targets(c,s,inputs,arithmetic)
    written=next(iter(colors));untouched=next(i for i in range(len(before_color)) if i not in colors)
    # Offsets belong to the independently documented version1 capture format.
    render_offset=8*4+6*4
    texture_offset=render_offset+166*4
    constant_offset=texture_offset+128*4+16*12
    program_offset=constant_offset+192*16
    stream_offset=program_offset+c['header'][5]*16+16*8
    def word_edit(path,offset,transform):
        data=bytearray(path.read_bytes());word=struct.unpack_from('<I',data,offset)[0]
        struct.pack_into('<I',data,offset,transform(word));path.write_bytes(data)
    def float_edit(path,offset,transform):
        data=bytearray(path.read_bytes());value=struct.unpack_from('<f',data,offset)[0]
        struct.pack_into('<f',data,offset,transform(value));path.write_bytes(data)
    rows=[]
    with tempfile.TemporaryDirectory(prefix='blinx-scene-controls-') as temporary:
        root=Path(temporary);cap=root/'captures';diag=root/'diagnostics'
        cap.mkdir();diag.mkdir()
        names=['baseline','missing-vertex','short-vertex','missing-fog','wrong-vertex','wrong-fog',
            'wrong-cull-method','wrong-filter-method','wrong-combiner-method','noise-enabled',
            'vertex-input','constant-input','program-input','cull-api-input','bias-api-input',
            'unsupported-alpha-test','no-op','omitted-pixel','stray-pixel','wrong-rgb',
            'wrong-depth','wrong-stencil','wrong-masked-alpha','truncated-target',
            'native-vertex-output-ignored','native-after-image-ignored']
        for name in names:
            for source in (captures/draw_name,captures/scene_name):shutil.copyfile(source,cap/source.name)
            for suffix in ('aot-private.json','color.bin','depth.bin'):
                source=diagnostics/f'draw-{number}-{suffix}' if suffix!='aot-private.json' else diagnostics/metadata_name
                shutil.copyfile(source,diag/source.name)
            observation=json.loads((diag/metadata_name).read_text())
            draw=cap/draw_name;scene=cap/scene_name
            color=diag/'draw-2-color.bin';depth=diag/'draw-2-depth.bin'
            if name=='missing-vertex':observation['vertices'].pop()
            elif name=='short-vertex':observation['vertices'][0].pop()
            elif name=='missing-fog':observation['fog'].pop()
            elif name=='wrong-vertex':observation['vertices'][0][0]+=1
            elif name=='wrong-fog':observation['fog'][0]+=1
            elif name=='wrong-cull-method':observation['methods']['0x39c']^=1
            elif name=='wrong-filter-method':observation['methods']['0x1b14']^=1<<16
            elif name=='wrong-combiner-method':observation['methods']['0xac0']^=1<<24
            elif name=='noise-enabled':observation['noise_disabled_fixture']=False
            elif name=='vertex-input':float_edit(draw,stream_offset,lambda v:v+1)
            elif name=='constant-input':float_edit(draw,constant_offset+99*16,lambda v:v+.125)
            elif name=='program-input':word_edit(draw,program_offset+4,lambda v:v^(1<<6))
            elif name=='cull-api-input':word_edit(draw,render_offset+147*4,lambda v:v^1)
            elif name=='bias-api-input':float_edit(draw,texture_offset+6*4,lambda v:v+.5)
            elif name=='unsupported-alpha-test':word_edit(draw,render_offset+60*4,lambda v:1)
            elif name=='no-op':
                color.write_bytes(struct.pack('<'+'I'*len(before_color),*before_color))
                depth.write_bytes(struct.pack('<'+'I'*len(before_depth),*before_depth))
            elif name=='omitted-pixel':
                word_edit(color,written*4,lambda v:before_color[written])
                word_edit(depth,written*4,lambda v:before_depth[written])
            elif name=='stray-pixel':word_edit(color,untouched*4,lambda v:v^1)
            elif name=='wrong-rgb':word_edit(color,written*4,lambda v:v^(16<<16))
            elif name=='wrong-depth':word_edit(depth,written*4,lambda v:v+(16<<8))
            elif name=='wrong-stencil':word_edit(depth,written*4,lambda v:v^1)
            elif name=='wrong-masked-alpha':word_edit(color,written*4,lambda v:v^(64<<24))
            elif name=='truncated-target':color.write_bytes(color.read_bytes()[:-4])
            elif name=='native-vertex-output-ignored':
                data=bytearray(draw.read_bytes());start=len(data)-c['header'][4]*52*4
                data[start:]=struct.pack('<'+'f'*(c['header'][4]*52),*([123.0]*(c['header'][4]*52)))
                draw.write_bytes(data)
            elif name=='native-after-image-ignored':
                # Last word is the native AFTER depth target, outside inputs.
                word_edit(scene,len(scene.read_bytes())-4,lambda v:v^0x10203040)
            # Updating hashes prevents the controls from relying on provenance
            # rejection instead of the computation being exercised.
            observation['vertex_capture_sha256']=hashlib.sha256(draw.read_bytes()).hexdigest()
            observation['scene_capture_sha256']=hashlib.sha256(scene.read_bytes()).hexdigest()
            (diag/metadata_name).write_text(json.dumps(observation))
            expected=name in ('baseline','native-vertex-output-ignored','native-after-image-ignored')
            detail={}
            try:
                result=audit(cap,diag,number,arithmetic)
                qualified=result['captured_input_to_fixture_pixel_qualified']
                detail={k:result[k] for k in ('failure_count','shader_failure_count','state_failure_count')}
            except AssertionError as error:
                qualified=False;detail={'guard':str(error)}
            rows.append(dict(control=name,expected_qualified=expected,qualified=qualified,
                control_passed=qualified==expected,**detail))
    return rows


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--depth-arithmetic',required=True,choices=('separate','fma-b-first'))
    args=parser.parse_args();rows=run(args.captures,args.diagnostics,args.depth_arithmetic)
    args.output.write_text(json.dumps(dict(controls=rows,all_controls_passed=all(r['control_passed'] for r in rows)),indent=2)+'\n')
    for row in rows:print(json.dumps(row),flush=True)
    raise SystemExit(0 if rows and all(r['control_passed'] for r in rows) else 1)
