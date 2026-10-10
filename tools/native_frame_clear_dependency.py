#!/usr/bin/env python3
"""Input-only proof that the initial clear viewport cannot affect final roots.

Bounded retained-resource fixture, noise disabled. This is a dependency proof,
not a pixel comparison, original startup/lifetime or an observed live viewport.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from native_frame_inputs import Reader,require
from native_frame_state_reference import Inputs
from native_frame_shader_state_reference import Prepared


def initial_depth(target,data,args):
    require(target['role']==1 and target['format'] in (44,45),'Unknown initial depth layout')
    require(len(args)==6 and args[:3]==[0,0,0xf3] and args[4:]==[0x3f800000,0],
            'Unknown initial clear contract')
    # Supplied native D24S8 input has depth in low24 and stencil in high8.
    # Depth1/stencil0 clearing is idempotent for every possible viewport.
    require(data==struct.pack('<I',0xffffff)*(target['width']*target['height']),
            'Initial depth/stencil differs from clear endpoint')
    return len(data)//4


def opaque_overwrite(capture,scene,vertices):
    w,h,fmt=scene['before'][0]['shape'];r=capture['render']
    require(fmt in (28,87) and w>0 and h>0,'Unknown color root')
    require(capture['header'][2]==6 and capture['header'][4]==4 and
            capture['viewport']==[0,0,w,h,0,0x3f800000],'Overwrite needs a full viewport strip')
    require(len(vertices)==4 and all(len(v)==28 and all(math.isfinite(x) for x in v) for v in vertices),
            'Incomplete independent vertex inputs')
    # Both rectangle triangles cover every pixel center, including borders;
    # their common edge belongs to exactly one under the top-left rule.
    require([v[:4] for v in vertices]==[[0,h,0,1],[0,0,0,1],[w,h,0,1],[w,0,0,1]],
            'Overwrite does not cover every color pixel')
    require(all(r[k]==v for k,v in {59:0,60:0,64:0,67:0x01010101,
        79:0,80:0,81:0,143:0,147:0,149:0,150:0}.items()),
        'Overwrite can reject fragments or consume destination color')
    require(r[144] in (0,1) and (not r[144] or r[70]==0x207),
            'Stencil can reject overwrite fragments')
    require(scene['header'][8] and scene['pixel'] is not None and len(scene['pixel'])==60 and
            scene['pixel'][54]==0x21,'Overwrite shader needs only two PROJECT2D stages')
    require(all(scene['textures'][stage] is not None and
            all(v[15+4*stage]==1 for v in vertices) for stage in (0,1)),
            'Projective sampling can discard an overwrite fragment')
    return w*h


def prior_uses(reader,writer,color):
    for draw in reader.draws:
        if draw['input']>writer['input']:continue
        require(draw['target']!=color or draw['input']==writer['input'],
                'Earlier color rendering depends on initial clear')
        for texture in draw['textures']:
            if not texture:continue
            words=reader.words(texture,reader.asset(texture)['bytes'])
            require(all(words[26+9*level]!=color for level in range(words[11])),
                    'A sampler reads initial color before its opaque overwrite')


def prove(reader):
    color,depth=reader.root;first=next(e for e in reader.events if e['type']=='clear')
    require(first['targets']==reader.root,'Initial clear is on another root')
    target=reader.targets[depth]
    depth_pixels=initial_depth(target,reader.asset_bytes(target['initial_asset']),first['args'])
    writer=next(d for d in reader.draws if d['target']==color)
    require(writer['call']>first['call'],'Overwrite precedes initial clear')
    prior_uses(reader,writer,color)
    capture,scene=Inputs(reader).draw(writer['input'])
    color_pixels=opaque_overwrite(capture,scene,Prepared(capture,scene).vertices)
    return dict(initial_clear_viewport_dependency_eliminated=True,
        initial_clear_call=first['call'],opaque_overwrite_input=writer['input'],
        idempotent_depth_pixels=depth_pixels,fully_overwritten_color_pixels=color_pixels,
        source='captured inputs and independent vertex/state equations',
        native_after_images_used=False,actual_lle_pixels_used=False,noise_disabled_fixture=True,
        live_initial_viewport_observed=False,complete_scene_pixel_qualified=False,scope=__doc__)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--captures',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();inputs=args.captures/'frame-inputs.bin';report=prove(Reader(inputs).read())
    report['inputs_sha256']=hashlib.sha256(inputs.read_bytes()).hexdigest()
    args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
