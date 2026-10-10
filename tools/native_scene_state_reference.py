#!/usr/bin/env python3
"""Independent active API state and shader delivery for closed scene fixtures.

This admits two fixed initial driver caches, not arbitrary packed state. The
original setters execute with that cache and their delivered words must match
API-derived equations. Pixels, texture contents, target continuity, enabled
noise and allocator/startup remain separate gates.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile

from native_scene_reference import read_capture,read_scene
from native_scene_composed_reference import expected_state,PACKED_PROFILES
from native_scene_shader_reference import audit as shader_audit


def audit(captures,diagnostics,number,packed_profile):
    capture=read_capture(captures/f'draw-{number}.bin')
    scene=read_scene(captures/f'scene-{number}.bin')
    expected=expected_state(capture,scene,packed_profile)
    shader=shader_audit(captures,diagnostics,number)
    actual=json.loads((diagnostics/f'draw-{number}.aot-private.json').read_text())
    assert actual['complete_draw_executed'] and actual['noise_disabled_fixture']
    assert actual['scene_capture_sha256']==scene['sha256'],'Scene provenance mismatch'
    failures=[[hex(k),actual['methods'].get(hex(k)),v] for k,v in expected.items()
              if actual['methods'].get(hex(k))!=v]
    return dict(draw=number,packed_profile=packed_profile,state_methods_compared=len(expected),
        state_failure_count=len(failures),state_first_failures=failures[:12],
        shader_compared_lanes=shader['compared_lanes'],shader_failure_count=shader['failure_count'],
        captured_input_to_shader_state_qualified=bool(not failures and shader['shader_input_to_output_qualified']),
        complete_scene_pixel_qualified=False,original_noise_enabled_scene_qualified=False,
        resource_lifetime_integration_qualified=False)


def controls(captures,diagnostics,numbers,packed_profile):
    rows=[]
    with tempfile.TemporaryDirectory(prefix='blinx-state-controls-') as temporary:
        root=Path(temporary);cap=root/'captures';diag=root/'diagnostics'
        cap.mkdir();diag.mkdir()
        for number in numbers:
            for name in ('baseline','wrong-cull','wrong-combiner','wrong-sampler',
                         'missing-method','unsupported-render','unknown-cache',
                         'incomplete-draw','noise-enabled','native-after-ignored'):
                for prefix in ('draw','scene'):
                    shutil.copyfile(captures/f'{prefix}-{number}.bin',cap/f'{prefix}-{number}.bin')
                observation=json.loads((diagnostics/f'draw-{number}.aot-private.json').read_text())
                if name in ('wrong-cull','wrong-combiner','wrong-sampler'):
                    key={'wrong-cull':'0x39c','wrong-combiner':'0xac0','wrong-sampler':'0x1b14'}[name]
                    observation['methods'][key]^=1
                elif name=='missing-method':observation['methods'].pop('0x1b04')
                elif name=='incomplete-draw':observation['complete_draw_executed']=False
                elif name=='noise-enabled':observation['noise_disabled_fixture']=False
                elif name in ('unsupported-render','unknown-cache','native-after-ignored'):
                    prefix='draw' if name=='unsupported-render' else 'scene'
                    path=cap/f'{prefix}-{number}.bin';data=bytearray(path.read_bytes())
                    offset=(8+6+60)*4 if name=='unsupported-render' else ((10+5+5+60)*4 if name=='unknown-cache' else len(data)-4)
                    word=struct.unpack_from('<I',data,offset)[0]
                    struct.pack_into('<I',data,offset,word^1);path.write_bytes(data)
                # Deliberate refresh makes semantic controls independent of stale hashes.
                for prefix,key in (('draw','vertex_capture_sha256'),('scene','scene_capture_sha256')):
                    observation[key]=hashlib.sha256((cap/f'{prefix}-{number}.bin').read_bytes()).hexdigest()
                (diag/f'draw-{number}.aot-private.json').write_text(json.dumps(observation))
                expected=name in ('baseline','native-after-ignored')
                try:
                    qualified=audit(cap,diag,number,packed_profile)['captured_input_to_shader_state_qualified']
                except AssertionError:
                    qualified=False
                rows.append(dict(draw=number,control=name,expected_qualified=expected,
                                 qualified=qualified,control_passed=qualified==expected))
    return rows


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--draws',type=int,nargs='+',required=True)
    parser.add_argument('--packed-profile',choices=tuple(PACKED_PROFILES),default='firstworld')
    parser.add_argument('--controls',action='store_true',help='Run temporary-copy rejection and independence controls instead')
    args=parser.parse_args()
    if args.controls:
        rows=controls(args.captures,args.diagnostics,args.draws,args.packed_profile)
        passed=all(r['control_passed'] for r in rows);report=dict(controls=rows)
    else:
        rows=[audit(args.captures,args.diagnostics,n,args.packed_profile) for n in args.draws]
        passed=all(r['captured_input_to_shader_state_qualified'] for r in rows);report=dict(draws=rows)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(f"{'PASS' if rows and passed else 'FAIL'}: {len(rows)} {'state controls' if args.controls else 'shader/state draws'}")
    for row in rows:
        if not row.get('control_passed',row.get('captured_input_to_shader_state_qualified')):print(json.dumps(row))
    raise SystemExit(0 if rows and passed else 1)
