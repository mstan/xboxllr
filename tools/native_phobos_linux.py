#!/usr/bin/env python3
"""Prepare the reviewed D3D11 renderer for native Linux/DXVK, retaining notices."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

PIN = 'bcccf8cc81d5d2becafe6138dbe81d0f9836b9a0'


def check_files(root, manifest, changes=None):
    records = {name: value['prepared'] for name, value in manifest['files'].items()}
    records.update({name: value['prepared'] for name, value in manifest.get('native_support', {}).items()})
    records.update({name: value['prepared'] for name, value in (changes or {}).items()})
    for name, expected in records.items():
        path = root / name
        if (not path.resolve().is_relative_to(root) or path.is_symlink() or
                not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected):
            raise ValueError('Changed prepared renderer file: ' + name)


def verify(output):
    output = Path(output).resolve(strict=True)
    manifest = json.loads((output / 'donor.json').read_text())
    adapter = json.loads((output / 'linux-adapter.json').read_text())
    if (manifest.get('donor') != PIN or manifest.get('backend') != 'PHOBOS' or
            adapter.get('donor') != PIN or adapter.get('schema') != 'xboxllr.phobos-linux.v1'):
        raise ValueError('Unreviewed native Linux renderer identity')
    check_files(output, manifest, adapter['changes'])
    return adapter


def prepare(source, output):
    source, output = Path(source).resolve(strict=True), Path(output).absolute()
    manifest = json.loads((source / 'donor.json').read_text())
    if manifest.get('donor') != PIN or manifest.get('backend') != 'PHOBOS':
        raise ValueError('Use the reviewed native_phobos_d3d.py output')
    check_files(source, manifest)
    if output.exists() or output.is_relative_to(source):
        raise ValueError('Choose a new, separate prepared Linux directory')
    for entry in source.rglob('*'):
        if entry.is_symlink() or getattr(entry, 'is_junction', lambda: False)():
            raise ValueError('Prepared dependency contains a link')
    shutil.copytree(source, output)
    changes = {}
    relative = 'src/d3d/d3d8_device.c'
    path = output / relative
    before = path.read_bytes()
    code = before.decode('utf-8')
    # The shared renderer owns SDL event pumping on its render thread. Keep
    # the donor's two Win32 message loops for Windows only.
    for marker in ('    /* Pump Windows messages */',
                   '    /* Pump Windows messages (same as dev_Present) */'):
        start = code.index(marker)
        end = code.index('\n\n', start)
        code = code[:start] + '#if defined(_WIN32)\n' + code[start:end] + '\n#endif' + code[end:]
    path.write_text(code, encoding='utf-8', newline='\n')
    changes[relative] = dict(source=hashlib.sha256(before).hexdigest(), prepared=hashlib.sha256(path.read_bytes()).hexdigest())
    for relative in ('src/d3d/d3d8_internal.h', 'src/d3d/d3d8_display.h', 'src/d3d/d3d8_display.c',
                     'src/d3d/rhi.c', 'src/d3d/rhi_d3d11.c'):
        path = output / relative
        before = path.read_bytes()
        code = before.decode('utf-8')
        old = '#if defined(_WIN32)'
        if old not in code:
            raise ValueError('Unreviewed renderer platform boundary: ' + relative)
        code = code.replace(old, '#if defined(_WIN32) || defined(PILOT_DXVK_NATIVE)')
        if relative == 'src/d3d/rhi_d3d11.c':
            tag = '#define D3D11_CACHE_TAG "d3d11-fxc-1"'
            if tag not in code:
                raise ValueError('Unreviewed renderer compiler-cache identity')
            code = code.replace(tag, '#define D3D11_CACHE_TAG PILOT_NATIVE_HLSL_COMPILER_ID')
        path.write_text(code, encoding='utf-8', newline='\n')
        changes[relative] = dict(source=hashlib.sha256(before).hexdigest(), prepared=hashlib.sha256(path.read_bytes()).hexdigest())
    relative = 'src/platform/xbox_winnt.h'
    path = output / relative
    before = path.read_bytes()
    path.write_bytes(b'/* xboxllr native Linux renderer: host ABI from the DXVK Native SDK. */\n'
                     b'#if defined(PILOT_DXVK_NATIVE)\n#include <windows.h>\n#else\n' + before + b'\n#endif\n')
    changes[relative] = dict(source=hashlib.sha256(before).hexdigest(), prepared=hashlib.sha256(path.read_bytes()).hexdigest())
    record = dict(schema='xboxllr.phobos-linux.v1', donor=PIN, transport='DXVK Native; ELF/POSIX/SDL', changes=changes)
    (output / 'linux-adapter.json').write_text(json.dumps(record, indent=2) + '\n')
    with (output / 'MODIFICATIONS.md').open('a', encoding='utf-8') as notice:
        notice.write('\nNative Linux: expose the existing D3D11 RHI through DXVK Native; '
                     'host types use its native SDK. OS services are supplied by xboxllr POSIX/SDL code. '
                     'No Wine runtime or Windows executable is involved. See linux-adapter.json for changed file hashes.\n')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify', type=Path)
    args = parser.parse_args()
    if args.verify:
        verify(args.verify)
        print('Native Linux renderer source hashes verified.')
    elif args.source and args.output:
        print(json.dumps(prepare(args.source, args.output), indent=2))
    else:
        parser.error('Supply --source and --output, or --verify')
