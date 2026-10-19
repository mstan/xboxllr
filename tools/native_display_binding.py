#!/usr/bin/env python3
"""Emit revision-qualified original display entries; never replace their bodies."""
import argparse
import hashlib
import json
from pathlib import Path

from native_binding import Binding


def bind(xbe, output, symbols, profile=None):
    context = Binding(xbe, output, symbols)
    proof = context.require_profile('display', profile, adapter='xdk-original-display-v1')
    if proof.get('xbe_sha256') != context.image['sha256'] or set(proof.get('entries',{})) != {'ISR','DPC','SUBMIT','ENABLE','VBLANK'}:
        raise ValueError('unreviewed original display proof')
    graphics = json.loads((output/'d3d-binding.aot-private.json').read_text())
    if graphics['xbe_sha256'] != context.image['sha256']:
        raise ValueError('display/renderer identity mismatch')
    rows = {}
    for name, entry in proof['entries'].items():
        address=entry['address']
        candidates = [f for f in context.functions if int(f['start'], 16) == address]
        if len(candidates) != 1:
            raise ValueError('missing original display entry: '+name)
        function = candidates[0]
        if context.digest(function)!=entry['sha256'] or int(function['end'],16)!=entry['end']:
            raise ValueError('unreviewed original display body: '+name)
        rows[name] = dict(address=address, sha256=context.digest(function),
                          end=int(function['end'], 16))
    # Original registration targets device+0x2550, equal to miniport+0x190.
    # Each private field is revision metadata, never a shared service constant.
    offsets = proof['offsets']
    expected={'INTERRUPT','DPC_OBJECT','CONNECTED','PMC_ENABLE','CALLBACK','EVENT','MODE','COMPLETED','FRAME','SUBMITTED'}
    if set(offsets)!=expected or type(proof.get('miniport_offset')) is not int or proof['miniport_offset']<0 or proof['miniport_offset']%4 or type(proof.get('miniport_bytes')) is not int or proof['miniport_bytes']<=0 or any(type(v) is not int or v<0 or v%4 or v+4>proof['miniport_bytes'] for v in offsets.values()):
        raise ValueError('invalid original display layout')
    binding = dict(xbe_sha256=context.image['sha256'], entries=rows, miniport_offset=proof['miniport_offset'],
                   miniport_bytes=proof['miniport_bytes'], offsets=offsets)
    header = ['#pragma once', '/* Owned original revision metadata; no replacement driver code. */',
              f'#define PILOT_DISPLAY_MINIPORT (PILOT_D3D_DEVICE_OBJECT+0x{proof["miniport_offset"]:x}u)',
              f'#define PILOT_DISPLAY_MINIPORT_BYTES 0x{proof["miniport_bytes"]:x}u']
    header += [f'#define PILOT_DISPLAY_{name} 0x{row["address"]:08x}u' for name, row in rows.items()]
    header += [f'#define PILOT_DISPLAY_{name}_OFFSET 0x{value:x}u' for name, value in offsets.items()]
    for name, label in [('D3DDevice_BlockUntilVerticalBlank', 'WAIT'),
                        ('D3DDevice_SetVerticalBlankCallback', 'REGISTER')]:
        matches = [c for c in graphics['calls'] if c['name'] == name]
        if len(matches) != 1:
            raise ValueError('missing original display boundary: '+name)
        original = 'pilot_original_d3d_'+name
        header += [f'extern void {original}(void);', f'#define PILOT_DISPLAY_{label} {original}']
    text = '\n'.join(header)+'\n'
    path = context.generated/'pilot_display_bindings.h'
    record = output/'display-binding.aot-private.json'
    if path.exists() and (path.read_text() != text or json.loads(record.read_text()) != binding):
        raise ValueError('existing display binding changed')
    if path.exists():
        return binding
    path.write_text(text, encoding='utf-8', newline='\n')
    record.write_text(json.dumps(binding, indent=2)+'\n', encoding='utf-8', newline='\n')
    context.manifest['display_binding'] = binding
    context.manifest['files'][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    context.manifest_path.write_text(json.dumps(context.manifest, indent=2)+'\n', encoding='utf-8', newline='\n')
    return binding


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'output', 'symbols'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--profile',type=Path)
    args = parser.parse_args()
    result = bind(args.xbe, args.output, args.symbols, args.profile)
    print('Verified original display entries:', len(result['entries']))
