#!/usr/bin/env python3
"""Fault and independence controls for captured-input shader qualification.

Temporary copies preserve the owner's captures. Changed input hashes are
refreshed so live input mutations must fail mathematical comparisons.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile

from native_scene_reference import read_capture
from native_scene_shader_reference import audit


def run(captures,diagnostics,draws):
    rows=[]
    with tempfile.TemporaryDirectory(prefix='blinx-shader-controls-') as temporary:
        root=Path(temporary);cap=root/'captures';diag=root/'diagnostics'
        cap.mkdir();diag.mkdir()
        for number in draws:
            filename=f'draw-{number}.bin';metadata=f'draw-{number}.aot-private.json'
            capture=read_capture(captures/filename)
            constant_offset=(8+6+166+128)*4+16*12
            program_offset=constant_offset+192*16
            stream_offset=program_offset+capture['header'][5]*16+16*8
            for name in ('baseline','missing-vertex','short-vertex','missing-fog',
                         'wrong-vertex','wrong-fog','incomplete-draw','wrong-hash',
                         'vertex-input','constant-input','program-input',
                         'unsupported-program','native-raw-output-ignored'):
                draw=cap/filename;shutil.copyfile(captures/filename,draw)
                observation=json.loads((diagnostics/metadata).read_text())
                data=bytearray(draw.read_bytes())
                def change_float(offset):
                    value=struct.unpack_from('<f',data,offset)[0]
                    struct.pack_into('<f',data,offset,value+.125)
                if name=='missing-vertex':observation['vertices'].pop()
                elif name=='short-vertex':observation['vertices'][0].pop()
                elif name=='missing-fog':observation['fog'].pop()
                elif name=='wrong-vertex':observation['vertices'][0][0]+=1
                elif name=='wrong-fog':observation['fog'][0]+=1
                elif name=='incomplete-draw':observation['complete_draw_executed']=False
                elif name=='vertex-input':change_float(stream_offset)
                elif name=='constant-input':change_float(constant_offset+99*16)
                elif name in ('program-input','unsupported-program'):
                    # The first DPH is live in these three exercised programs.
                    instruction=next(i for i in range(capture['header'][5])
                                     if (capture['code'][i*4+1]>>21)&15==6)
                    offset=program_offset+instruction*16+4
                    word=struct.unpack_from('<I',data,offset)[0]
                    opcode=1 if name=='program-input' else 15
                    struct.pack_into('<I',data,offset,(word&~(15<<21))|(opcode<<21))
                elif name=='native-raw-output-ignored':
                    count=capture['header'][4]*52
                    data[-count*4:]=struct.pack('<'+'f'*count,*([123.0]*count))
                draw.write_bytes(data)
                observation['vertex_capture_sha256']=hashlib.sha256(data).hexdigest()
                if name=='wrong-hash':observation['vertex_capture_sha256']='0'*64
                (diag/metadata).write_text(json.dumps(observation))
                expected=name in ('baseline','native-raw-output-ignored')
                try:
                    result=audit(cap,diag,number)
                    qualified=result['shader_input_to_output_qualified']
                    detail=dict(failure_count=result['failure_count'])
                except AssertionError as error:
                    qualified=False;detail=dict(guard=str(error))
                rows.append(dict(draw=number,control=name,expected_qualified=expected,
                                 qualified=qualified,control_passed=qualified==expected,**detail))
    return rows


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--draws',type=int,nargs='+',default=[1,71,73])
    args=parser.parse_args()
    rows=run(args.captures,args.diagnostics,args.draws)
    args.output.write_text(json.dumps(dict(controls=rows),indent=2)+'\n')
    for row in rows:
        if not row['control_passed']:print(json.dumps(row),flush=True)
    print(f"{'PASS' if all(r['control_passed'] for r in rows) else 'FAIL'}: {len(rows)} shader fault/independence controls")
    raise SystemExit(0 if rows and all(r['control_passed'] for r in rows) else 1)
