"""Archive receipt-verified prepared native dependencies, never title outputs.

Project source and title generation recipes ship separately at the recorded
framework pin. This supplement preserves the actual modified dependency files.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

from check_repository_hygiene import content_findings


def digest(data):
    return hashlib.sha256(data).hexdigest()


def package(renderer, ui, dsp, output):
    renderer, ui, dsp = (Path(p).resolve(strict=True) for p in (renderer, ui, dsp))
    output = Path(output).absolute()
    if output.exists() or any(output.is_relative_to(p) for p in (renderer, ui, dsp)):
        raise ValueError('Choose a new archive outside the prepared sources')
    files = {}

    def add(root, prefix, name, expected=None, *, artwork=False):
        relative = PurePosixPath(name)
        if (relative.is_absolute() or '\\' in name or ':' in name or
                any(p in ('..', '.', '') for p in relative.parts)):
            raise ValueError('Unsafe dependency path')
        path = root.joinpath(*relative.parts)
        if (not path.resolve(strict=True).is_relative_to(root) or
                any(p.is_symlink() or getattr(p, 'is_junction', lambda: False)()
                    for p in (path, *path.parents) if p.is_relative_to(root))):
            raise ValueError('Dependency source leaves its root')
        if path.suffix.lower() in ('.exe','.dll','.lib','.obj','.pdb','.a','.so',
                                  '.dylib','.xbe','.iso','.rom','.img','.qcow2','.bin'):
            raise ValueError('Binary or original input in dependency source: '+name)
        data = path.read_bytes()
        if expected is not None and digest(data) != expected:
            raise ValueError('Prepared source differs from its receipt: '+name)
        if list(content_findings(data, allow_binary=artwork)):
            raise ValueError('Private or unreviewed dependency content: '+name)
        files[prefix+'/'+name] = data

    def extras(root, prefix, names):
        for name in names:
            path = root / name
            if path.is_dir():
                for item in sorted(path.rglob('*')):
                    if item.is_file():
                        add(root, prefix, item.relative_to(root).as_posix())
            else:
                add(root, prefix, name)

    r = json.loads((renderer/'donor.json').read_text(encoding='utf-8'))
    for name, hashes in r['files'].items():
        add(renderer, 'renderer', name, hashes['prepared'])
    for name, expected in r['nv2a_reference']['files'].items():
        add(renderer, 'renderer', 'nv2a-reference/'+name, expected)
    extras(renderer, 'renderer', ['LICENSE','LICENSE.upstream-MIT','NOTICE',
                                 'MODIFICATIONS.md','LICENSES','pilot_d3d_checks.h','donor.json'])

    u = json.loads((ui/'xboxlle-ui.json').read_text(encoding='utf-8'))
    for name, expected in u['files'].items():
        # Upstream launcher sample ROMs and tests are unrelated to this build.
        if PurePosixPath(name).parts[0] in ('test_data','tests','tools'):
            continue
        artwork = name.startswith('assets/')
        add(ui, 'launcher-ui', name, expected, artwork=artwork)
    add(ui, 'launcher-ui', 'xboxlle-ui.json')

    d = json.loads((dsp/'donor.json').read_text(encoding='utf-8'))
    for name, expected in d['sources'].items():
        add(dsp, 'dsp', name, d.get('guarded', {}).get(name, expected))
    extras(dsp, 'dsp', ['third_party','donor.json'])

    files['README-SOURCES.md'] = b'''# Windows native dependency source

This supplement contains the actual prepared renderer, launcher UI and DSP
source with their retained copyrights, licenses, modification records and
preparation receipts. The accompanying project source kit supplies the CMake
build files, native host/services, CPU helpers, adapters and preparation tools.
See docs/DEVELOPMENT.md in that source kit for toolchains and generation/builds.

Point d3d_source and dsp_source in the title's private paths file to renderer/
and dsp/ here. Build the locked DSP FFI as documented. launcher-ui/ preserves
the patched frontend source used by the executable, while the project's
native_launcher_ui.py records how to prepare it from its pinned upstream.
The renderer's historical private-pilot label is provenance, not an additional
restriction replacing the GPL. Each component retains its own license terms.

Original game/Dashboard function bodies are generated from the user's supported
original title input. This supplement contains no original title or console
dumps. The launcher dependency's unrelated sample ROM/test_data are excluded.
Use the project source kit at the exact release revision and retain its
RIGHTS-NOTICE.md; this supplement grants no original-game rights or GPL exception.
General-purpose tools and unmodified dependencies can be obtained from the
documented pinned repositories and locked package manifests.
'''
    record = dict(schema='xboxllr.native-dependency-source.v1',
        renderer_revision=r['donor'], ui_revision=u['revision'],
        dsp_revision=d['revision'],
        files={name:digest(data) for name,data in sorted(files.items())})
    files['DEPENDENCY-SOURCES.json'] = (json.dumps(record, indent=2)+'\n').encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name+'.partial')
    try:
        with temporary.open('xb') as stream:
            with zipfile.ZipFile(stream,'w',compression=zipfile.ZIP_DEFLATED) as archive:
                for name,data in sorted(files.items()):
                    entry=zipfile.ZipInfo(name,(2026,1,1,0,0,0))
                    entry.compress_type=zipfile.ZIP_DEFLATED
                    entry.external_attr=0o100644 << 16
                    archive.writestr(entry,data,compresslevel=3)
        with zipfile.ZipFile(temporary) as archive:
            if archive.testzip() is not None or set(archive.namelist()) != set(files):
                raise ValueError('Dependency source ZIP verification failed')
            for name,expected in record['files'].items():
                if digest(archive.read(name)) != expected:
                    raise ValueError('Dependency source hash mismatch')
        if output.exists():
            raise ValueError('Archive destination appeared during staging')
        temporary.rename(output)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    return dict(file=output.name, sha256=digest(output.read_bytes()),
                bytes=output.stat().st_size, entries=len(files),
                renderer_revision=r['donor'],ui_revision=u['revision'],dsp_revision=d['revision'])


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('renderer','ui','dsp','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    print(json.dumps(package(args.renderer,args.ui,args.dsp,args.output),indent=2))
