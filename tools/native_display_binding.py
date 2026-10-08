#!/usr/bin/env python3
"""Emit revision-qualified original display entries; never replace their bodies."""
import argparse
import hashlib
import json
from pathlib import Path

from native_binding import Binding
from native_input_binding import IMAGE

ENTRIES = {'ISR': 0x13b210, 'DPC': 0x13bbc0, 'SUBMIT': 0x13b6c0,
           'ENABLE': 0x13f020, 'VBLANK': 0x13b400}


def bind(xbe, output, symbols):
    context = Binding(xbe, output, symbols)
    if context.image['sha256'] != IMAGE:
        raise ValueError('unreviewed display image')
    graphics = json.loads((output/'d3d-binding.aot-private.json').read_text())
    if graphics['xbe_sha256'] != IMAGE:
        raise ValueError('display/renderer identity mismatch')
    rows = {}
    for name, address in ENTRIES.items():
        candidates = [f for f in context.functions if int(f['start'], 16) == address]
        if len(candidates) != 1:
            raise ValueError('missing original display entry: '+name)
        function = candidates[0]
        rows[name] = dict(address=address, sha256=context.digest(function),
                          end=int(function['end'], 16))
    # Original registration targets device+0x2550, equal to miniport+0x190.
    # Each private field is revision metadata, never a shared service constant.
    offsets = {'INTERRUPT': 0x14, 'DPC_OBJECT': 0x84, 'CONNECTED': 0xa0,
               'PMC_ENABLE': 0xb4, 'CALLBACK': 0x190, 'EVENT': 0x194,
               'MODE': 0x1b4, 'COMPLETED': 0x1bc, 'FRAME': 0x1c0,
               'SUBMITTED': 0x1cc}
    binding = dict(xbe_sha256=IMAGE, entries=rows, miniport_offset=0x23c0,
                   miniport_bytes=0x83c, offsets=offsets)
    header = ['#pragma once', '/* Owned original revision metadata; no replacement driver code. */',
              '#define PILOT_DISPLAY_MINIPORT (PILOT_D3D_DEVICE_OBJECT+0x23c0u)',
              '#define PILOT_DISPLAY_MINIPORT_BYTES 0x83cu']
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
    args = parser.parse_args()
    result = bind(args.xbe, args.output, args.symbols)
    print('Verified original display entries:', len(result['entries']))
