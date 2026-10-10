#!/usr/bin/env python3
"""Compact-frame original state replay on explicit preowned resources.

CPU texture storage and supplied root targets are fixture inputs. Sampled GPU
mips and later target observations do not supply expected methods or pixels.
The noise-disabled fixture and separate resources do not qualify enabled noise,
original allocation/startup, target continuity or full-frame pixels.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path

from native_frame_inputs import Reader, audit as input_audit, require
from native_frame_fvf_reference import capture as fvf_capture
from native_scene_draw_reference import replay


class Inputs:
    def __init__(self,reader):
        self.reader=reader;self.bytes={}

    def asset(self,number):
        if number not in self.bytes:self.bytes[number]=self.reader.asset_bytes(number)
        return self.bytes[number]

    def draw(self,number):
        r=self.reader;d=r.draws[number-1]
        if d['kind']<2:capture=r.shader_capture(number)
        else:
            value=fvf_capture(r,number)
            capture=dict(header=[0,2,d['primitive'],d['first'],d['count'],0,d['shader'],0],
                viewport=d['viewport'],render=value['render'],texture=list(r.words(d['texture_state_asset'],512)),
                elements=value['elements'],metadata=value['metadata'],streams=value['streams'],outputs=None,
                code=[],constants=None,sha256=value['sha256'],input_sequence=number,
                submission_kind=d['kind'],up_stride=d['up_stride'])
        assets=[d[k] for k in ('render_asset','texture_state_asset','packed_asset','pixel_asset') if d[k]]
        before=[];headers=[]
        for target_id in (d['target'],d['depth']):
            target=r.targets.get(target_id)
            if target:
                before.append(dict(shape=[target['width'],target['height'],target['format']],
                                   data=self.asset(target['initial_asset'])))
                headers.append(target['resource']);assets.append(target['initial_asset'])
            else:before.append(dict(shape=[0,0,0],data=b''));headers.append([0]*5)
        textures=[]
        for number in d['textures']:
            if not number:textures.append(None);continue
            w=list(r.words(number,r.asset(number)['bytes']));assets.extend([number,w[12]])
            require(not w[6] and w[13] and not w[14] and not w[16],'GPU alias/view needs an explicit storage fixture')
            assets.append(w[13]);mips=[]
            for level in range(w[11]):
                m=w[18+level*9:27+level*9];assets.append(m[7])
                # These are captured sampled inputs. Original state delivery
                # consumes the CPU storage fixture; equality/texture pixels
                # are separate qualifications and are never silently inferred.
                mips.append(dict(shape=m[1:7],data=self.asset(m[7])))
            textures.append(dict(info=w[:18],resource=list(r.words(w[12],20)),
                                 cpu=self.asset(w[13]),mips=mips))
        identity=dict(draw=d,assets={n:r.assets[n]['sha256'] for n in assets})
        scene=dict(header=[0,2,d['input'],d['primitive'],d['first'],d['count'],
            d['guest_targets'][0],d['guest_targets'][1],d['pixel_handle'],d['depth_scale']],
            color_header=headers[0],depth_header=headers[1],before=before,after=None,
            packed=list(r.words(d['packed_asset'],64)),
            pixel=list(r.words(d['pixel_asset'],240)) if d['pixel_asset'] else None,
            textures=textures,screen=d['screen'],input_only=True,
            sha256=hashlib.sha256(json.dumps(identity,sort_keys=True,separators=(',',':')).encode()).hexdigest())
        return capture,scene


def selected(reader,args):
    if args.draws:return args.draws
    if args.all_draws:return [d['input'] for d in reader.draws]
    seen=set();numbers=[]
    for d in reader.draws:
        profile=tuple(d[k] for k in ('render_asset','texture_state_asset','packed_asset','pixel_asset'))
        if profile not in seen:seen.add(profile);numbers.append(d['input'])
    return numbers


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output','xbe','binding','library','dependencies'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--draws',type=int,nargs='+');parser.add_argument('--all-draws',action='store_true')
    args=parser.parse_args();require(not(args.draws and args.all_draws),'Choose draw numbers or complete frame')
    args.observe_shader_state_only=True;args.noise_disabled_fixture=True;args.retain_targets=False
    handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    coverage=input_audit(args.captures/'frame.jsonl');reader=Reader(args.captures/'frame-inputs.bin').read()
    inputs=Inputs(reader);rows=[];numbers=selected(reader,args)
    require(numbers and len(set(numbers))==len(numbers) and all(0<n<=len(reader.draws) for n in numbers),'Bad draw selection')
    for number in numbers:
        capture,scene=inputs.draw(number)
        try:
            row=replay(args,Path(f'input-{number}'),capture=capture,scene=scene)
            row['input']=number
        except (AssertionError,ValueError) as error:
            row=dict(input=number,complete_draw_executed=False,guard=str(error))
        rows.append(row)
        print(json.dumps(dict(input=number,complete=row['complete_draw_executed'],guard=row.get('guard'))),flush=True)
    passed=all(r['complete_draw_executed'] for r in rows)
    args.output.write_text(json.dumps(dict(inputs_sha256=coverage['inputs_sha256'],draws=rows,
        selected_draws=len(rows),original_complete_draws=sum(r['complete_draw_executed'] for r in rows),
        captured_input_to_state_qualified=False,complete_scene_pixel_qualified=False,
        original_noise_enabled_scene_qualified=False,resource_lifetime_integration_qualified=False),indent=2)+'\n')
    raise SystemExit(0 if passed else 1)
