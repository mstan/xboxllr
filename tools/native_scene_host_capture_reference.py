#!/usr/bin/env python3
"""Audit retained actual GPU raw shader probes using input-derived host bounds.

Captures were made with pilot_gfx_shader_probe, not production raster_main.
This runs offline, imports no renderer and cannot qualify scene pixels.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct

from native_scene_reference import read_capture
from native_scene_host_precision import qualify


def observed(capture):
    return struct.pack('<'+'f'*len(capture['outputs']),*capture['outputs'])


def audit(captures,number):
    capture=read_capture(captures/f'draw-{number}.bin')
    result,_=qualify(capture,observed(capture),raster=False)
    return dict(**result,draw=number,vertices=capture['header'][4],
        capture_sha256=capture['sha256'],program_sha256=hashlib.sha256(
            struct.pack('<'+'I'*len(capture['code']),*capture['code'])).hexdigest(),
        production_raster_bytecode_qualified=False,complete_scene_pixel_qualified=False)


def controls(captures,numbers):
    rows=[]
    for number in numbers:
        original=read_capture(captures/f'draw-{number}.bin');data=observed(original)
        for name in ('baseline','wrong-output','short-output','extra-output',
                     'nonfinite-output','vertex-input','constant-input','program-input',
                     'unsupported-program','capture-output-field-ignored'):
            capture=copy.deepcopy(original);actual=bytearray(data)
            def change_float(blob,offset):
                value=struct.unpack_from('<f',blob,offset)[0]
                struct.pack_into('<f',blob,offset,value+1)
            if name=='wrong-output':change_float(actual,3*16)
            elif name=='short-output':actual=actual[:-4]
            elif name=='extra-output':actual+=b'\0'*4
            elif name=='nonfinite-output':struct.pack_into('<I',actual,0,0x7fc00000)
            elif name=='vertex-input':
                fmt,stream,offset=capture['elements'][0];assert fmt==0x32
                blob=bytearray(capture['streams'][stream]);change_float(blob,offset)
                capture['streams'][stream]=bytes(blob)
            elif name=='constant-input':
                blob=bytearray(capture['constants']);change_float(blob,99*16)
                capture['constants']=bytes(blob)
            elif name in ('program-input','unsupported-program'):
                index=next(i for i in range(1,len(capture['code']),4) if (capture['code'][i]>>21)&15==6)
                op=1 if name=='program-input' else 15
                capture['code'][index]=(capture['code'][index]&~(15<<21))|(op<<21)
            elif name=='capture-output-field-ignored':capture['outputs']=[123.0]*len(capture['outputs'])
            expected=name in ('baseline','capture-output-field-ignored')
            try:qualified=qualify(capture,bytes(actual),raster=False)[0]['shader_qualified']
            except AssertionError:qualified=False
            rows.append(dict(draw=number,control=name,expected_qualified=expected,
                             qualified=qualified,control_passed=qualified==expected))
    return rows


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--draws',type=int,nargs='+',required=True)
    parser.add_argument('--controls',action='store_true')
    args=parser.parse_args()
    rows=controls(args.captures,args.draws) if args.controls else [audit(args.captures,n) for n in args.draws]
    key='control_passed' if args.controls else 'shader_qualified'
    passed=bool(rows) and all(r[key] for r in rows)
    args.output.write_text(json.dumps({('controls' if args.controls else 'draws'):rows},indent=2)+'\n')
    print(f"{'PASS' if passed else 'FAIL'}: {len(rows)} {'raw host controls' if args.controls else 'raw host shader draws'}")
    for row in rows:
        if not row[key]:print(json.dumps(row))
    raise SystemExit(0 if passed else 1)
