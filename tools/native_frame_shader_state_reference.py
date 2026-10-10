#!/usr/bin/env python3
"""Joint captured-input shader/state components on preowned original resources.

This combines independent vertex math and API/pixel state against the same
actual original draw. Target words remain fixture setup. Noise is disabled;
original target binding/startup, continuity, pixels and production HLE remain
separate. Disabled FVF fog coordinates are explicitly unconsumed.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import struct

from native_frame_inputs import Reader,audit as input_audit,require
from native_frame_state_reference import Inputs,selected
from native_frame_state_delivery import expected_delivery,qualify as state_qualify
from native_scene_shader_reference import expectation as program_expectation
from native_frame_fvf_reference import expectation as fvf_expectation
from native_vertex_fetch_reference import XBE_SHA


class Prepared:
    def __init__(self,capture,scene):
        self.capture=capture;self.scene=scene
        expected_delivery(capture,scene)
        self.fvf=capture['submission_kind']>=2
        if not self.fvf:
            self.vertices,self.fog,_=program_expectation(capture)
        else:
            h=capture['header']
            value=dict(draw=dict(shader=h[6],primitive=h[2],count=h[4],
                viewport=capture['viewport'],screen=scene['screen']),render=capture['render'],
                elements=capture['elements'],metadata=capture['metadata'],streams=capture['streams'])
            self.vertices,_=fvf_expectation(value)
            # The real texture-state dirty path maps TSS TEXCOORDINDEX0 to the
            # sole TEX1 declaration for each stage. Empty sampler bindings do
            # not remove these original fixed-function coordinate requests.
            for stage in range(4):
                require(capture['texture'][stage*32+28]==0,'Unmodelled FVF coordinate mapping')
            for vertex in self.vertices:
                for stage in range(1,4):vertex[12+stage*4:16+stage*4]=vertex[12:16]
            # Fog is disabled by the admitted FVF input profile. Its raw unused
            # coordinate can be reciprocal-derived and nonfinite; it supplies
            # neither an expectation nor a consumed pixel input.
            self.fog=None

    def qualify(self,actual,library_sha=None):
        state=state_qualify(self.capture,self.scene,actual,library_sha)
        require(len(actual['vertices'])==len(self.vertices),'Missing shader vertices')
        require(len(actual['fog'])==len(self.vertices),'Incomplete raw fog observation')
        failures=[];compared=0
        for n,(got,want) in enumerate(zip(actual['vertices'],self.vertices)):
            require(len(got)==28,'Incomplete shader vector')
            for lane,(a,b) in enumerate(zip(got,want)):
                require(type(a) in (int,float) and math.isfinite(a),'Nonfinite consumed vertex value')
                if a!=b:failures.append([n,lane,a,b])
                compared+=1
            if self.fog is not None:
                a=actual['fog'][n];b=self.fog[n]
                require(type(a) in (int,float) and math.isfinite(a),'Nonfinite programmable fog output')
                if a!=b:failures.append([n,'fog',a,b])
                compared+=1
        return dict(**state,vertices=len(self.vertices),vertex_compared_values=compared,
            vertex_failure_count=len(failures),vertex_first_failures=failures[:12],
            fvf_unconsumed_fog_values=len(self.vertices) if self.fvf else 0,
            shader_state_components_qualified=state['state_word_components_qualified'] and not failures)


def controls(capture,scene,actual,library_sha):
    rows=[]
    names=['baseline','wrong-vertex','missing-vertex','missing-state','changed-render',
           'wrong-route','pending-fifo','guarded','wrong-build','native-output-ignored']
    if capture['submission_kind']>=2:names.extend(('wrong-stage-coordinate','disabled-fog-ignored'))
    else:names.append('wrong-fog')
    for name in names:
        cap,sc,obs=copy.deepcopy((capture,scene,actual))
        if name=='wrong-vertex':
            word=struct.unpack('<I',struct.pack('<f',obs['vertices'][0][0]))[0]
            obs['vertices'][0][0]=struct.unpack('<f',struct.pack('<I',word^1))[0]
        elif name=='missing-vertex':obs['vertices'].pop()
        elif name=='missing-state':obs['methods'].pop('0x354')
        elif name=='changed-render':cap['render'][57]^=1
        elif name=='wrong-route':obs['submission']='substituted draw'
        elif name=='pending-fifo':obs['fifo_get']+=4
        elif name=='guarded':obs['draw_counters'][3]=1
        elif name=='wrong-build':obs['library_sha256']='changed'
        elif name=='native-output-ignored':cap['outputs']=[['changed native observation']]
        elif name=='wrong-stage-coordinate':obs['vertices'][0][16]+=1
        elif name=='disabled-fog-ignored':obs['fog']=[float('nan')]*len(obs['fog'])
        elif name=='wrong-fog':obs['fog'][0]+=1
        cap['sha256']=obs['vertex_capture_sha256']='refreshed-joint-control'
        sc['sha256']=obs['scene_capture_sha256']='refreshed-joint-control'
        want=name in ('baseline','native-output-ignored','disabled-fog-ignored')
        try:got=Prepared(cap,sc).qualify(obs,library_sha)['shader_state_components_qualified']
        except (AssertionError,ValueError):got=False
        rows.append(dict(input=cap['input_sequence'],control=name,expected_qualified=want,
                         qualified=got,control_passed=got==want))
    return rows


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output','library'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--draws',type=int,nargs='+');parser.add_argument('--all-draws',action='store_true')
    parser.add_argument('--controls',action='store_true');args=parser.parse_args()
    require(not(args.draws and args.all_draws),'Choose draw numbers or complete frame')
    coverage=input_audit(args.captures/'frame.jsonl');reader=Reader(args.captures/'frame-inputs.bin').read()
    inputs=Inputs(reader);numbers=selected(reader,args);rows=[]
    require(numbers and len(set(numbers))==len(numbers) and all(type(n) is int and 0<n<=len(reader.draws) for n in numbers),'Bad draw selection')
    library_sha=hashlib.sha256(args.library.read_bytes()).hexdigest()
    for index,n in enumerate(numbers,1):
        cap,scene=inputs.draw(n)
        try:
            prepared=Prepared(cap,scene)  # Before opening any actual observations.
            actual=json.loads((args.diagnostics/f'input-{n}.aot-private.json').read_text())
            if args.controls:rows.extend(controls(cap,scene,actual,library_sha))
            else:rows.append(prepared.qualify(actual,library_sha))
        except (AssertionError,ValueError,KeyError,IndexError) as error:
            rows.append(dict(input=n,shader_state_components_qualified=False,guard=str(error)))
        if index%256==0:print(f'Joint shader/state audit {index}/{len(numbers)}',flush=True)
    key='control_passed' if args.controls else 'shader_state_components_qualified'
    passed=bool(rows and all(r.get(key,False) for r in rows))
    report=dict(inputs_sha256=coverage['inputs_sha256'],ledger_sha256=coverage['ledger_sha256'],
        library_sha256=library_sha,xbe_sha256=XBE_SHA,rows=rows,selected_draws=len(numbers),passed=passed,
        vertices=sum(r.get('vertices',0) for r in rows),
        state_compared_words=sum(r.get('compared_words',0) for r in rows),
        vertex_compared_values=sum(r.get('vertex_compared_values',0) for r in rows),
        fvf_unconsumed_fog_values=sum(r.get('fvf_unconsumed_fog_values',0) for r in rows),
        complete_frame_shader_state_components_qualified=bool(args.all_draws and not args.controls and passed),
        full_original_state_delivery_qualified=False,complete_scene_pixel_qualified=False,
        resource_lifetime_integration_qualified=False,scope=__doc__)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(f"{'PASS' if passed else 'FAIL'}: {len(rows)} joint {'controls' if args.controls else 'draws'}, {report['vertex_compared_values']} vertex values, {report['state_compared_words']} state words")
    for row in rows:
        if not row.get(key,False):print(json.dumps(row))
    raise SystemExit(0 if passed else 1)
