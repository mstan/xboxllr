#!/usr/bin/env python3
"""Prepare private, unchanged AOT ISR/DPC bodies for the original AV fixture.

The normal generated image, generation inventory and display binding must
already exist. Output contains owner-supplied code and must remain private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from aot_inventory import parse_xbe


def require(condition, message):
    if not condition:
        raise ValueError(message)


def prepare(generated, xbe, output):
    generated, xbe, output = (p.resolve() for p in (generated, xbe, output))
    manifest_path = generated.parent / 'generation.aot-private.json'
    inventory_path = generated.parent / 'disasm/functions.json'
    binding_path = generated.parent / 'display-binding.aot-private.json'
    manifest = json.loads(manifest_path.read_text())
    binding = json.loads(binding_path.read_text())
    inventory = {int(r['start'], 16): r for r in json.loads(inventory_path.read_text())}
    blob = xbe.read_bytes()
    image = parse_xbe(blob)
    require(image['sha256'] == manifest['sha256'] == binding['xbe_sha256'],
            'original image, generation and display binding differ')
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    chunks, loaded, where, pins = [], {}, {}, {}
    for name in manifest['files']:
        if not re.fullmatch(r'recomp_[0-9]{4}\.c', name):
            continue
        with (generated / name).open('rb') as source:
            header = source.read(350).decode('ascii', 'replace')
        extent = re.search(r'Functions: \d+ \(0x([0-9A-Fa-f]+) - 0x([0-9A-Fa-f]+)\)', header)
        require(extent, f'missing chunk range: {name}')
        chunks.append((int(extent[1], 16), int(extent[2], 16), name))

    def body(name):
        address = int(name[4:], 16)
        choices = [n for lo, hi, n in chunks if lo <= address <= hi]
        require(len(choices) == 1, f'ambiguous or missing original body: {name}')
        chunk = choices[0]
        if chunk not in loaded:
            path = generated / chunk
            raw = path.read_bytes()
            sha = hashlib.sha256(raw).hexdigest()
            require(sha == manifest['files'][chunk], f'changed generated source: {chunk}')
            pins[str(path)] = sha
            loaded[chunk] = {m[1].decode(): m[0] for m in re.finditer(
                rb'^void (\w+)\(void\)\r?\n\{\r?\n.*?^\}', raw, re.M | re.S)}
        where[name] = chunk
        return loaded[chunk][name]

    pending = [f"sub_{binding['entries'][entry]['address']:08X}" for entry in ('ISR', 'DPC')]
    bodies, calls = {}, {}
    while pending:
        name = pending.pop()
        if name in bodies:
            continue
        code = body(name)
        require(not re.search(rb'\bpilot_original_\w+\b', code), f'unhandled renamed callee: {name}')
        bodies[name] = code
        callees = {n.decode() for n in re.findall(rb'\b(sub_[0-9A-F]{8})\b', code)} - {name}
        calls[name] = sorted(callees)
        pending.extend(callees - bodies.keys())
        require(len(bodies) <= 128, 'callback closure exceeds the reviewed bounded extraction')
    rows = []
    for name in sorted(bodies):
        address = int(name[4:], 16)
        record = inventory[address]
        end = int(record['end'], 16)
        require(end > address and end - address == record['size'], f'invalid original extent: {name}')
        section = next(s for s in image['sections'] if s['address'] <= address < end <= s['address'] + s['raw_size'])
        offset = section['raw_offset'] + address - section['address']
        original = blob[offset:offset + end - address]
        require(len(original) == end - address, f'truncated original body: {name}')
        rows.append(dict(name=name, address=address, end=end, source_chunk=where[name],
                         aot_body_sha256=hashlib.sha256(bodies[name]).hexdigest(),
                         xbe_bytes_sha256=hashlib.sha256(original).hexdigest(), direct_callees=calls[name]))
    for entry in ('ISR', 'DPC', 'VBLANK'):
        bound = binding['entries'][entry]
        row = next(r for r in rows if r['address'] == bound['address'])
        require(row['xbe_bytes_sha256'] == bound['sha256'], f'changed display body: {entry}')
    header = b'/* PRIVATE retained generated callback closure; original bodies unchanged. */\n#define RECOMP_GENERATED_CODE\n#include "recomp_funcs.h"\n#include <math.h>\n\n'
    lookup = ['recomp_func_t pilot_gpu_av_original_lookup(uint32_t va)', '{', '    switch(va) {']
    lookup += [f'    case 0x{n[4:]}u: return {n};' for n in sorted(bodies)]
    lookup += ['    default: return NULL;', '    }', '}', '']
    data = header + b''.join(bodies[n] + b'\n\n' for n in sorted(bodies)) + '\n'.join(lookup).encode()
    for name, code in bodies.items():
        require(data.count(code) == 1, f'body not copied exactly once: {name}')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    for path in (xbe, manifest_path, inventory_path, binding_path, generated / 'recomp_types.h',
                 generated / 'recomp_funcs.h', output, Path(__file__).resolve()):
        pins[str(path)] = digest(path)
    report = dict(qualified=False, generated_bodies_modified=False, replacement_stubs=False,
                  count=len(rows), rows=rows, pins=pins)
    output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated', type=Path, required=True)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = prepare(args.generated, args.xbe, args.output)
    print(json.dumps(dict(functions=report['count'], output=str(args.output))))
