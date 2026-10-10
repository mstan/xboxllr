#!/usr/bin/env python3
"""Account for a bounded frame without promoting draw captures to pixels.

The private ledger brackets one interval between accepted swaps. It records
returned adapter calls, not GPU completion or independently correct rendering.
Pointer arguments and supplied targets remain inputs, never expected output.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct


DRAW_NAMES = {'D3DDevice_DrawVertices', 'D3DDevice_DrawVerticesUP'}
TARGET_NAMES = {'D3DDevice_SetRenderTarget', 'D3D_CommonSetRenderTarget'}


def word(value):
    if type(value) is not int or not 0 <= value <= 0xffffffff:
        raise ValueError('ledger field is not a DWORD')
    return value


def audit(path):
    raw = path.read_bytes()
    if not raw.endswith(b'\n'):
        raise ValueError('ledger has no complete final line')
    rows = [json.loads(line) for line in raw.splitlines()]
    if not 2 <= len(rows) <= 32770 or any(not isinstance(row, dict) for row in rows):
        raise ValueError('invalid ledger extent or record')
    begin, end = rows[0], rows[-1]
    if begin.get('event') != 'begin' or type(begin.get('schema')) is not int or begin['schema'] != 1:
        raise ValueError('missing schema-1 begin')
    if end.get('event') != 'end' or word(end.get('swap')) != word(begin.get('swap')) + 1:
        raise ValueError('frame lacks consecutive accepted swap boundaries')
    if not word(begin.get('width')) or not word(begin.get('height')):
        raise ValueError('invalid frame dimensions')
    word(begin.get('target')); word(begin.get('depth'))
    calls = rows[1:-1]
    names, draw_kinds = Counter(), Counter()
    draws, captures, missing, clears, target_sets, lifecycle = [], [], [], [], [], []
    for index, row in enumerate(calls, 1):
        if row.get('event') != 'call' or word(row.get('sequence')) != index:
            raise ValueError('missing, duplicate or unordered call')
        name, args = row.get('name'), row.get('args')
        if not isinstance(name, str) or not name or not isinstance(args, list) or len(args) > 64:
            raise ValueError('invalid call name or arguments')
        for arg in args:
            word(arg)
        for field in ('shader', 'target', 'depth', 'capture_sequence', 'return_eax'):
            word(row.get(field))
        names[name] += 1
        capture = row['capture_sequence']
        if name in DRAW_NAMES:
            up = name.endswith('UP')
            if len(args) != (4 if up else 3):
                raise ValueError('draw has wrong ABI argument count')
            primitive, first, count = (args[0], 0, args[1]) if up else args
            if primitive not in (5, 6, 8) or count < 3 or first + count > 0xffffffff:
                raise ValueError('invalid admitted draw range')
            if (primitive == 5 and count % 3) or (primitive == 8 and count % 4):
                raise ValueError('invalid list/quad count')
            if up and (not args[2] or not args[3]):
                raise ValueError('user-pointer draw has no data/stride')
            kind = ('programmable' if row['shader'] & 1 else 'fvf') + ('_up' if up else '_bound')
            draw_kinds[kind] += 1
            item = dict(call=index, kind=kind, primitive=primitive, first=first,
                        count=count, target=row['target'], depth=row['depth'], capture=capture)
            draws.append(item)
            if capture:
                if kind != 'programmable_bound' or capture in captures:
                    raise ValueError('capture attached to unsupported draw or reused')
                draw_path, scene_path = (path.parent / f'{prefix}-{capture}.bin' for prefix in ('draw', 'scene'))
                with draw_path.open('rb') as file:
                    header_raw = file.read(32)
                with scene_path.open('rb') as file:
                    scene_raw = file.read(40)
                if len(header_raw) != 32 or len(scene_raw) != 40:
                    raise ValueError('missing complete companion header')
                header, scene = struct.unpack('<8I', header_raw), struct.unpack('<10I', scene_raw)
                if header[:5] != (0x44534c58, 1, primitive, first, count):
                    raise ValueError('draw companion belongs to another submission')
                if scene[:8] != (0x534c4c58, 1, capture, primitive, first, count, row['target'], row['depth']):
                    raise ValueError('scene companion belongs to another submission/target')
                captures.append(capture)
            else:
                missing.append(item)
        elif capture:
            raise ValueError('capture attached to a non-draw operation')
        if name == 'D3DDevice_Clear':
            if len(args) != 6:
                raise ValueError('clear has wrong ABI argument count')
            clears.append(dict(call=index, flags=args[2], color=args[3],
                               depth_bits=args[4], stencil=args[5], target=row['target'], depth=row['depth']))
        if name in TARGET_NAMES:
            target_sets.append(index)
        if name in ('Direct3D_CreateDevice', 'D3DDevice_Reset', 'D3DDevice_Release'):
            lifecycle.append(dict(call=index, name=name))
    totals = dict(calls=len(calls), draws=len(draws), captured=len(captures),
                  clears=len(clears), target_sets=len(target_sets))
    for field, expected in totals.items():
        if word(end.get(field)) != expected:
            raise ValueError('terminal accounting disagrees with events: ' + field)
    return dict(schema=1, ledger_sha256=hashlib.sha256(raw).hexdigest(),
                frame_boundary_complete=True, start_swap=begin['swap'], end_swap=end['swap'],
                dimensions=[begin['width'], begin['height']], **totals,
                methods=dict(sorted(names.items())), draw_kinds=dict(sorted(draw_kinds.items())),
                draws_detail=draws, missing_draw_companions=missing,
                clear_operations=clears, target_switch_calls=target_sets, lifecycle_operations=lifecycle,
                companion_headers_match=True,
                complete_pixel_qualification=False,
                limitations=['Returned adapter calls do not establish correct GPU effects',
                             'Headers establish linkage only; companion contents need independent qualification',
                             'Clear effects, uncaptured draws and target/resource continuity remain to qualify'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ledger', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = audit(args.ledger)
    except (ValueError, OSError, KeyError, TypeError) as error:
        result = dict(frame_boundary_complete=False, complete_pixel_qualification=False, error=str(error))
        args.output.write_text(json.dumps(result, indent=2) + '\n')
        print('Frame accounting failed: ' + str(error))
        return 1
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ('frame_boundary_complete', 'start_swap', 'end_swap',
                     'calls', 'draws', 'captured', 'clears', 'target_sets', 'draw_kinds',
                     'complete_pixel_qualification')}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
