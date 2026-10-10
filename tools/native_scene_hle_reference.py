#!/usr/bin/env python3
"""Prepare retained-input native scene fixtures without supplying after-images.

Starting targets are declared inputs. Native shader output and scene AFTER
targets never enter the replay input file. The GPU executable records actual
draw observations separately for subsequent independent qualification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from native_scene_reference import read_capture,read_scene
from native_scene_composed_reference import expected_state
from native_scene_pixel_reference import words
from native_scene_host_precision import qualify
from native_scene_host_pixels import audit as pixel_audit


def prepare(captures,numbers,path,chain=False):
    rows=[]
    with path.open('wb') as file:
        def put(values):file.write(struct.pack('<'+'I'*len(values),*values))
        put([0x534e4c58,2,len(numbers),3,int(chain)])  # three independent device lifetimes
        for sample,n in enumerate(numbers):
            c=read_capture(captures/f'draw-{n}.bin');s=read_scene(captures/f'scene-{n}.bin')
            expected_state(c,s)  # admit the independently specified closed profile
            h=c['header'];width,height,fmt=s['before'][0]['shape']
            put([n,h[2],h[3],h[4],h[5],width,height,fmt,s['before'][1]['shape'][2],s['header'][9]])
            put(c['viewport']);put(c['render']);put(c['texture'])
            for element in c['elements']:put(element)
            file.write(c['constants']);put(c['code'])
            for metadata in c['metadata']:put(metadata)
            for stream in c['streams']:file.write(stream)
            if not chain or sample==0:put(words(s['before'][0]));put(words(s['before'][1],True))
            for texture in s['textures']:
                if texture is None:put([0]*5);continue
                width,height=texture['mips'][0]['shape'][:2]
                put([1,width,height,len(texture['mips']),(texture['resource'][3]>>8)&255])
                for mip in texture['mips']:put([len(mip['data'])]);file.write(mip['data'])
            rows.append(dict(draw=n,vertex_capture_sha256=c['sha256'],scene_capture_sha256=s['sha256'],
                captured_noise_value=c['render'][159],fixture_noise_value=0,vertices=h[4]))
    return dict(draws=rows,retained_targets=chain,replay_input_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        scope='Captured inputs and supplied initial targets only; no native raw vertex or after-image expectation; noise disabled explicitly')


def audit(captures,numbers,observations,chain=False):
    rows=[];previous=None
    for n in numbers:
        c=read_capture(captures/f'draw-{n}.bin');s=read_scene(captures/f'scene-{n}.bin')
        data={kind:(observations/f'native-draw-{n}-pass-0-{kind}.bin').read_bytes()
            for kind in ('raw','raster','color','depth')}
        if chain:
            before={kind:(observations/f'native-draw-{n}-pass-0-before-{kind}.bin').read_bytes() for kind in ('color','depth')}
            expected_before=previous if previous is not None else {
                'color':s['before'][0]['data'],'depth':struct.pack('<'+'I'*(len(s['before'][1]['data'])//4),*words(s['before'][1],True))}
            assert all(before[k]==expected_before[k] for k in before),'Broken retained-target sequence'
            s['before'][0]['data']=before['color']
            s['before'][1]['data']=b''.join(struct.pack('<I',((v>>8)&0xffffff)|((v&255)<<24))
                for (v,) in struct.iter_unpack('<I',before['depth']))
        raw,_=qualify(c,data['raw'])
        result=pixel_audit(c,s,data['raster'],data['color'],data['depth'])
        for lifetime in range(3):
            path=observations/f'native-draw-{n}-pass-{lifetime}-aot-private.json'
            meta=json.loads(path.read_text())
            assert meta['draw']==n and meta['pass']==lifetime
            assert meta['primitive']==c['header'][2] and meta['first']==c['header'][3] and meta['vertices']==c['header'][4]
            assert meta['native_draw_submitted'] and meta['noise_disabled_fixture'] and meta['shader_uploads']>=1
            assert meta.get('retained_targets',False)==chain  # v1 observations were independent draws
            for kind,baseline in data.items():
                assert (observations/f'native-draw-{n}-pass-{lifetime}-{kind}.bin').read_bytes()==baseline,('Device lifetime drift',n,lifetime,kind)
            if chain:
                for kind,baseline in before.items():
                    assert (observations/f'native-draw-{n}-pass-{lifetime}-before-{kind}.bin').read_bytes()==baseline,('Before-target lifetime drift',n,lifetime,kind)
        result.update(draw=n,raw_shader=raw,device_lifetimes=3,
            vertex_capture_sha256=c['sha256'],scene_capture_sha256=s['sha256'],
            observation_sha256={k:hashlib.sha256(v).hexdigest() for k,v in data.items()})
        result['native_noise_off_draw_qualified']&=raw['shader_qualified']
        if chain:assert result['native_noise_off_draw_qualified'],'A failed prefix cannot qualify a later chained draw'
        previous={kind:data[kind] for kind in ('color','depth')}
        rows.append(result)
    assert sum(r['expected_written_pixels'] for r in rows),'All-noop inputs cannot qualify an exercised native scene corpus'
    return dict(draws=rows,retained_targets=chain,
        scope='Independently admitted retained-input noise-off native draws, including actual production shader; complete title/state/resource coverage and silicon precision remain separate')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--captures',type=Path,required=True)
    parser.add_argument('--draws',type=int,nargs='+',default=[2,3,4,5,6])
    mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--native-input',type=Path)
    mode.add_argument('--observations',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--retain-targets',action='store_true',help='Initialize targets once, then qualify the actual retained draw sequence')
    args=parser.parse_args()
    result=audit(args.captures,args.draws,args.observations,args.retain_targets) if args.observations else prepare(args.captures,args.draws,args.native_input,args.retain_targets)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    if args.observations:
        passed=sum(r['native_noise_off_draw_qualified'] for r in result['draws'])
        print(f"Native scene independent qualification: {passed}/{len(result['draws'])} draws, three device lifetimes")
        raise SystemExit(0 if passed==len(result['draws']) else 1)
    print(f"Prepared {len(result['draws'])} native scene draw inputs; no after-image or shader output golden")
