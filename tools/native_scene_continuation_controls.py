#!/usr/bin/env python3
"""Fault controls at independently changed producer-continuation pixels.

Both predecessor and continuation expected roots are bound to independently
derived passing reports. Their differences select the exercised pixel; actual
after-images are used only as observations and mutated copies, never goldens.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import numpy as np

from native_frame_inputs import Reader,require
from native_scene_producer_reference import compare_storage


def bound_expected(report,diagnostics,targets):
    roots = {n:(diagnostics/f'final-expected-{n}.aot-private.bin').read_bytes() for n in targets}
    require(all(hashlib.sha256(data).hexdigest() == report['expected_target_sha256'][str(n)]
                for n,data in roots.items()), 'Changed independent expected root')
    return roots


def run(args):
    report = json.loads(args.report.read_text())
    predecessor = json.loads(args.predecessor_report.read_text())
    for item in (report,predecessor):
        require(item['passed'] and item['expected_predecessor_bytes_used'] and
                not item['observed_after_image_goldens_used'], 'Expected-chain report required')
    require(predecessor['draws'] < report['draws'] and
            predecessor['inputs_sha256'] == report['inputs_sha256'] and
            predecessor['library_sha256'] == report['library_sha256'], 'Unrelated continuation')
    reader = Reader(args.captures/'frame-inputs.bin').read()
    require(hashlib.sha256(reader.path.read_bytes()).hexdigest() == report['inputs_sha256'],
            'Changed retained input frame')
    expected = bound_expected(report,args.diagnostics,reader.targets)
    prior = bound_expected(predecessor,args.predecessor_diagnostics,reader.targets)
    actual = {n:(args.diagnostics/f'final-actual-{n}.aot-private.bin').read_bytes() for n in reader.targets}
    written = {int(n):set(indices) for n,indices in report['final_written_pixels'].items()}
    selected = {}
    differences = {}
    for n,target in reader.targets.items():
        old = np.frombuffer(prior[n],dtype='<u4')
        new = np.frombuffer(expected[n],dtype='<u4')
        require(old.size == new.size,'Changed root extent in continuation')
        changed = np.flatnonzero(old != new)
        differences[n] = int(changed.size)
        if target['role']:
            candidates = [int(i) for i in changed if (int(old[i]) >> 8) != (int(new[i]) >> 8)]
            if candidates and 'depth' not in selected:selected['depth'] = (n,candidates[0])
        else:
            candidates = [int(i) for i in changed if int(i) in written.get(n,set()) and
                any(abs(((int(old[i]) >> s)&255)-((int(new[i]) >> s)&255)) > 1 for s in (0,8,16))]
            if candidates and 'color' not in selected:selected['color'] = (n,candidates[0])
    require(set(selected) == {'color','depth'},'No independently changed continuation color/depth pixel')
    color,index = selected['color'];depth,zindex = selected['depth']
    rows = []
    for name in ('baseline','one-code-new-rgb','two-code-new-rgb','masked-new-alpha',
                 'missing-new-color-pixel','missing-new-depth-pixel','wrong-new-depth',
                 'wrong-new-stencil','missing-continuation-color-root','missing-continuation-depth-root'):
        mutated = dict(actual)
        target,pixel = color,index
        word = None
        if name in ('one-code-new-rgb','two-code-new-rgb'):
            word = struct.unpack_from('<I',expected[color],index*4)[0]
            value = word&255;delta = 1 if name == 'one-code-new-rgb' else 2
            value += delta if value <= 255-delta else -delta
            word = (word&0xffffff00)|value
        elif name == 'masked-new-alpha':
            word = struct.unpack_from('<I',actual[color],index*4)[0]^(1 << 24)
        elif name == 'missing-new-color-pixel':
            word = struct.unpack_from('<I',prior[color],index*4)[0]
        elif name in ('missing-new-depth-pixel','wrong-new-depth','wrong-new-stencil'):
            target,pixel = depth,zindex
            data = prior[depth] if name == 'missing-new-depth-pixel' else actual[depth]
            word = struct.unpack_from('<I',data,pixel*4)[0]
            if name == 'wrong-new-depth':word ^= 1 << 8
            elif name == 'wrong-new-stencil':word ^= 1
        elif name == 'missing-continuation-color-root':mutated[color] = prior[color]
        elif name == 'missing-continuation-depth-root':mutated[depth] = prior[depth]
        if word is not None:
            data = bytearray(mutated[target]);struct.pack_into('<I',data,pixel*4,word)
            mutated[target] = bytes(data)
        wanted = name in ('baseline','one-code-new-rgb')
        got = compare_storage(reader.targets,expected,mutated,written)['qualified']
        rows.append(dict(control=name,expected_qualified=wanted,qualified=got,
                         control_passed=got == wanted))
    result = dict(passed=all(row['control_passed'] for row in rows),controls=rows,
        predecessor_report_sha256=hashlib.sha256(args.predecessor_report.read_bytes()).hexdigest(),
        report_sha256=hashlib.sha256(args.report.read_bytes()).hexdigest(),
        independently_changed_root_pixels=differences,selected_independent_pixels=selected,
        actual_after_images_used_as_expected=False)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(passed=result['passed'],controls=len(rows),selected_independent_pixels=selected)),flush=True)
    return result['passed']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('captures','report','diagnostics','predecessor-report','predecessor-diagnostics','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    raise SystemExit(0 if run(parser.parse_args()) else 1)
