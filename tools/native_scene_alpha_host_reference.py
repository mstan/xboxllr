#!/usr/bin/env python3
"""Independently admitted native alpha-volume pixels, including overlap.

Eight-bit nearest/even host setup is separate from the software four-bit floor.
The closed guard-band acceptance model retains projected source triangles and
scissors to the integer viewport. No target after-image supplies expectations.
"""
import argparse
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import struct

from native_scene_alpha_reference import expected_state
from native_scene_reference import read_capture,read_scene
from native_scene_pixel_reference import plane,words,binary32
from native_scene_host_precision import qualify,SPEC
from native_scene_host_pixels import GAMMA8


def predict(capture,scene,raster):
    expected_state(capture,scene)
    shader,rows=qualify(capture,raster,True)
    assert shader['shader_qualified'],shader
    width,height,_=scene['before'][0]['shape']
    vertices=[]
    for row in rows:
        row=list(map(Fraction,row));w=row[3];assert w>0 and 0<=row[2]<=w
        x=binary32(binary32(binary32(row[0]/w)+1)*Fraction(width,2))
        y=binary32(binary32(1-binary32(row[1]/w))*Fraction(height,2))
        assert -32768<x<32767 and -32768<y<32767,'Outside declared guard-band model'
        vertices.append(((round(x*256),round(y*256)),row[2]/w*16777215))
    # Flat diffuse is independently qualified per vertex. Uniform white texture
    # and MODULATE leave its alpha unchanged; no texture filtering error enters.
    alpha=Fraction(rows[0][15]);assert 0<=alpha<=1 and all(Fraction(row[15])==alpha for row in rows)
    increment=round(alpha*255);assert increment==6
    color=words(scene['before'][0]);depth=words(scene['before'][1],True)
    current=[v>>24 for v in color];covered=set();written=set();triangles=passed=rejected=near_depth=0
    direction=1 if capture['render'][74]==0x8006 else -1
    for start in range(0,len(vertices),3):
        vv=vertices[start:start+3];pts=[v[0] for v in vv]
        (ax,ay),(bx,by),(cx,cy)=pts;det=(bx-ax)*(cy-ay)-(cx-ax)*(by-ay)
        if not det or (det>0)==(capture['render'][147]==capture['render'][146]):continue
        if det<0:vv[1],vv[2]=vv[2],vv[1];pts[1],pts[2]=pts[2],pts[1]
        triangles+=1;zplane=plane(pts,[v[1] for v in vv])
        xmin=max(0,math.ceil((min(p[0] for p in pts)-128)/256));xmax=min(width-1,math.floor((max(p[0] for p in pts)-128)/256))
        ymin=max(0,math.ceil((min(p[1] for p in pts)-128)/256));ymax=min(height-1,math.floor((max(p[1] for p in pts)-128)/256))
        for y in range(ymin,ymax+1):
            for x in range(xmin,xmax+1):
                px,py=x*256+128,y*256+128
                if any((cross:=(pts[(i+1)%3][0]-p[0])*(py-p[1])-(pts[(i+1)%3][1]-p[1])*(px-p[0]))<0 or
                    (cross==0 and not (pts[(i+1)%3][1]<p[1] or (pts[(i+1)%3][1]==p[1] and pts[(i+1)%3][0]>p[0])))
                    for i,p in enumerate(pts)):continue
                index=y*width+x;covered.add(index);ideal=zplane[0]+zplane[1]*px+zplane[2]*py
                threshold=depth[index]>>8
                # Declared host acceptance, established before target outputs.
                # Mark comparisons whose result could vary across permitted
                # interpolation implementations. They still require the exact
                # independently predicted byte here; no pixel is omitted or
                # accepted merely for being near a depth boundary. Qualification
                # is for this observed host/model, not every conforming GPU.
                radius=GAMMA8*float(ideal)+.6
                if abs(float(ideal)-threshold)<=radius:near_depth+=1
                if round(ideal)>=threshold:rejected+=1;continue
                written.add(index);passed+=1;current[index]=max(0,min(255,current[index]+direction*increment))
    expected=[(v&0xffffff)|(current[i]<<24) for i,v in enumerate(color)]
    return expected,depth,dict(shader=shader,triangles=triangles,covered_pixels=len(covered),
        expected_written_pixels=len(written),passed_fragments=passed,rejected_depth_fragments=rejected,
        depth_precision_boundary_fragments=near_depth,target_pixels_checked=len(color),
        untouched_pixels_checked=len(color)-len(written),blend_equation='add' if direction==1 else 'reverse-subtract')


def compare(capture,scene,data):
    raw,_=qualify(capture,data['raw']);color,depth,stats=predict(capture,scene,data['raster'])
    failures=[]
    for kind,want in (('color',color),('depth',depth)):
        assert len(data[kind])==len(want)*4
        for i,((got,),value) in enumerate(zip(struct.iter_unpack('<I',data[kind]),want)):
            if got!=value:failures.append([kind,i,hex(got),hex(value)])
    return dict(**stats,raw_shader=raw,failure_count=len(failures),first_failures=failures[:12],
        native_alpha_overlay_qualified=not failures and raw['shader_qualified'],
        depth_bound_unambiguous=not stats['depth_precision_boundary_fragments'])


def audit(captures,observations,numbers,chain=False):
    results=[];previous=None
    for n in numbers:
        c=read_capture(captures/f'draw-{n}.bin');s=read_scene(captures/f'scene-{n}.bin')
        data={kind:(observations/f'native-draw-{n}-pass-0-{kind}.bin').read_bytes() for kind in ('raw','raster','color','depth')}
        if chain:
            before={kind:(observations/f'native-draw-{n}-pass-0-before-{kind}.bin').read_bytes() for kind in ('color','depth')}
            root={'color':s['before'][0]['data'],'depth':struct.pack('<'+'I'*(len(s['before'][1]['data'])//4),*words(s['before'][1],True))}
            assert all(before[k]==(previous or root)[k] for k in before),'Broken retained-target sequence'
            s['before'][0]['data']=before['color']
            s['before'][1]['data']=b''.join(struct.pack('<I',((v>>8)&0xffffff)|((v&255)<<24)) for (v,) in struct.iter_unpack('<I',before['depth']))
        result=compare(c,s,data)
        for lifetime in range(3):
            meta=json.loads((observations/f'native-draw-{n}-pass-{lifetime}-aot-private.json').read_text())
            assert meta['draw']==n and meta['pass']==lifetime and meta['primitive']==5
            assert meta['first']==c['header'][3] and meta['vertices']==c['header'][4]
            assert meta['native_draw_submitted'] and meta['noise_disabled_fixture'] and meta['shader_uploads']>=1
            assert meta['retained_targets']==chain
            for kind,baseline in data.items():
                assert (observations/f'native-draw-{n}-pass-{lifetime}-{kind}.bin').read_bytes()==baseline,'Device lifetime drift'
            if chain:
                for kind,baseline in before.items():
                    assert (observations/f'native-draw-{n}-pass-{lifetime}-before-{kind}.bin').read_bytes()==baseline,'Before-target lifetime drift'
        results.append(dict(draw=n,**result,device_lifetimes=3,
            vertex_capture_sha256=c['sha256'],scene_capture_sha256=s['sha256'],
            observation_sha256={k:hashlib.sha256(v).hexdigest() for k,v in data.items()}))
        if chain:assert result['native_alpha_overlay_qualified'],'Failed prefix cannot qualify later chained draws'
        previous={kind:data[kind] for kind in ('color','depth')}
    assert sum(r['expected_written_pixels'] for r in results),'All-noop corpus'
    return dict(draws=results,retained_targets=chain,host_spec_source=SPEC,
        scope='Constant-alpha white-texture volume; binary32 viewport/eight-bit guard-band setup and exact alpha/RGB/depth preservation on observed host. Near-depth results are checked exactly; cross-device interpolation precision remains separate')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','observations','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--draws',type=int,nargs='+',default=list(range(1,33)));p.add_argument('--retain-targets',action='store_true')
    args=p.parse_args();result=audit(args.captures,args.observations,args.draws,args.retain_targets)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    for r in result['draws']:print(json.dumps(r),flush=True)
    raise SystemExit(0 if all(r['native_alpha_overlay_qualified'] for r in result['draws']) else 1)
