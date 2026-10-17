#!/usr/bin/env python3
"""Pack retained vertex inputs for a bounded, standalone admission replay.

This exports inputs, never native after-images. Constants are declared available
by the fixture, not proven produced by a live GPU context. No pixel, numerical
shader, context LOAD or whole-title qualification follows from this preflight.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

from native_frame_inputs import Reader, require
from native_scene_shader_reference import fetch


def capture(source, output, draws=None, all_programmable=False):
    started = time.perf_counter()
    reader = Reader(source).read()
    candidates = [draw for draw in reader.draws if draw['kind'] < 2]
    if draws is not None:
        require(len(draws) == len(set(draws)) and draws, 'Duplicate/empty draw selection')
        requested = set(draws)
        selected = [draw for draw in candidates if draw['input'] in requested]
        require({draw['input'] for draw in selected} == requested,
                'A requested draw is missing or has no programmable shader')
    elif all_programmable:
        selected = candidates
    else:
        selected = []
        seen = set()
        for draw in candidates:
            key = reader.asset(draw['code_asset'])['sha256']
            if key not in seen:
                selected.append(draw)
                seen.add(key)
    require(0 < len(selected) <= 32768, 'Empty or oversized admission selection')
    require(sum(draw['count'] for draw in selected) <= 1000000,
            'Admission selection exceeds one million vertices')
    temporary = output.with_suffix(output.suffix + '.tmp')
    receipt = output.with_suffix(output.suffix + '.json')
    require(not any(path.exists() for path in (output, temporary, receipt)),
            'Replay output already exists')
    cases = []
    with temporary.open('xb') as stream:
        stream.write(struct.pack('<3I', 0x50534856, 2, len(selected)))
        for draw in selected:
            number = draw['input']
            inputs = reader.shader_capture(number)
            code = reader.asset_bytes(draw['code_asset'])
            constants = inputs['constants']
            require(len(code) == draw['program_instructions'] * 16 and len(constants) == 192 * 16,
                    'Invalid shader/constant extent')
            known = bytes(15 if element[0] != 2 else 0 for element in inputs['elements'])
            observable = bytearray([15] * 13)
            observable[5] = 1  # Fog has one observable component.
            stream.write(struct.pack('<4I', number, draw['program_instructions'], draw['count'], 1))
            stream.write(code)
            stream.write(constants)
            stream.write(known)
            stream.write(bytes([15] * 192))
            stream.write(observable)
            for vertex in range(draw['count']):
                values = fetch(inputs, vertex)
                stream.write(struct.pack('<64f', *(float(value) for row in values for value in row)))
            cases.append(dict(input=number, vertices=draw['count'],
                              program_sha256=hashlib.sha256(code).hexdigest(),
                              captured_inputs_sha256=inputs['sha256']))
    temporary.rename(output)
    result = dict(schema='xboxlle.vsh-admission-inputs.v2',
                  source=str(source.resolve()), source_sha256=reader.sha256,
                  binary_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
                  binary_bytes=output.stat().st_size, cases=cases,
                  vertices=sum(row['vertices'] for row in cases),
                  captured_constants_declared_available=True,
                  complete_scene_pixel_qualified=False,
                  seconds=time.perf_counter()-started,
                  scope='Retained-input dependency admission only; no live LOAD provenance or numerical shader/pixel proof')
    receipt.write_text(json.dumps(result, indent=2)+'\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    select = parser.add_mutually_exclusive_group()
    select.add_argument('--draws', type=int, nargs='+')
    select.add_argument('--all-programmable', action='store_true')
    parser.add_argument('--constants-available', action='store_true', required=True,
                        help='Explicitly declare captured constants available in this fixture; not live-context provenance')
    args = parser.parse_args()
    result = capture(args.source, args.output, args.draws, args.all_programmable)
    print(json.dumps({key:result[key] for key in ('binary_sha256','binary_bytes','vertices','seconds')}))


if __name__ == '__main__':
    main()
