#!/usr/bin/env python3
"""Fault controls against independently expected retained-producer bytes.

Mutate copies in memory. Actual after-images never replace expected images;
the expected targets must hash-match the passing independently derived report.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from native_frame_inputs import Reader,require
from native_frame_state_reference import Inputs
from native_frame_storage import draw_plan
from native_frame_shader_state_reference import controls as state_controls
from native_scene_producer_reference import compare_storage


def run(args):
    report = json.loads(args.report.read_text())
    require(report['passed'] and report['expected_predecessor_bytes_used'] and
            not report['observed_after_image_goldens_used'], 'Not an independently expected passing report')
    reader = Reader(args.captures/'frame-inputs.bin').read()
    require(hashlib.sha256(reader.path.read_bytes()).hexdigest() == report['inputs_sha256'],
            'Controls use another retained input frame')
    expected = {n:(args.diagnostics/f'final-expected-{n}.aot-private.bin').read_bytes() for n in reader.targets}
    actual = {n:(args.diagnostics/f'final-actual-{n}.aot-private.bin').read_bytes() for n in reader.targets}
    require(all(hashlib.sha256(v).hexdigest() == report['expected_target_sha256'][str(n)]
                for n,v in expected.items()), 'Changed independent target expectation')
    written = {int(n):set(indices) for n,indices in report['final_written_pixels'].items()}
    target_id = next(n for n,indices in written.items() if indices)
    changed = min(written[target_id])
    untouched = next(i for i in range(reader.targets[target_id]['width']*reader.targets[target_id]['height'])
                     if i not in written[target_id])
    depth = next(n for n,t in reader.targets.items() if t['role'])
    other = next(n for n,t in reader.targets.items() if not t['role'] and n != target_id)
    rows = []
    for name in ('baseline','one-code-rgb','wrong-written-rgb','masked-alpha','untouched-scene-color',
                 'unwritten-other-root','wrong-depth','wrong-stencil','missing-root','truncated-root'):
        mutated = dict(actual)
        selected = target_id;index = changed;mask = 0
        if name == 'one-code-rgb':mask = 1
        elif name == 'wrong-written-rgb':mask = 16
        elif name == 'masked-alpha':mask = 1 << 24
        elif name == 'untouched-scene-color':index = untouched;mask = 1
        elif name == 'unwritten-other-root':selected = other;index = 0;mask = 1
        elif name == 'wrong-depth':selected = depth;index = 0;mask = 1 << 8
        elif name == 'wrong-stencil':selected = depth;index = 0;mask = 1
        elif name == 'missing-root':mutated.pop(depth)
        elif name == 'truncated-root':mutated[depth] = mutated[depth][:-4]
        if mask:
            data = bytearray(mutated[selected]);word = struct.unpack_from('<I',data,index*4)[0]
            struct.pack_into('<I',data,index*4,word ^ mask);mutated[selected] = bytes(data)
        want = name in ('baseline','one-code-rgb')
        try:got = compare_storage(reader.targets,expected,mutated,written)['qualified']
        except ValueError:got = False
        rows.append(dict(control=name,expected_qualified=want,qualified=got,control_passed=got == want))
    capture,scene = Inputs(reader).draw(3)
    scene['frame_resource_plan'] = draw_plan(reader.targets,reader.draws[2],scene)
    observation = json.loads((args.diagnostics/'input-3.aot-private.json').read_text())
    joint = state_controls(capture,scene,observation,report['library_sha256'])
    result = dict(passed=all(r['control_passed'] for r in rows+joint),pixel_controls=rows,
                  joint_shader_state_controls=joint,report_sha256=hashlib.sha256(args.report.read_bytes()).hexdigest(),
                  expected_after_image_used=False)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(passed=result['passed'],pixel_controls=len(rows),joint_controls=len(joint))),flush=True)
    return result['passed']


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('captures','report','diagnostics','output'):parser.add_argument('--'+name,type=Path,required=True)
    raise SystemExit(0 if run(parser.parse_args()) else 1)
