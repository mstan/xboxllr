#!/usr/bin/env python3
"""Package xboxllr.exe, Python, Capstone and exact framework source as a toolkit.

Build tools/windows-build-launcher first. Inputs are local reviewed archives;
this script neither downloads executables nor changes repository visibility.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import zipfile
from source_release_package import package, git
from native_release_runtime import sha

ROOT = Path(__file__).resolve().parents[1]


def extract(archive_path, target):
    with zipfile.ZipFile(archive_path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or archive.testzip() is not None:
            raise ValueError('Invalid input archive')
        for entry in archive.infolist():
            path = PurePosixPath(entry.filename)
            if (path.is_absolute() or '\\' in entry.filename or ':' in entry.filename
                    or '..' in path.parts or (entry.external_attr >> 16) & 0o170000 == 0o120000):
                raise ValueError('Unsafe input archive entry')
        archive.extractall(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True)
    for name in ('launcher', 'embed', 'capstone-wheel', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', args.version):
        parser.error('Use a plain version identifier')
    if git(ROOT, 'status', '--porcelain').strip():
        raise ValueError('Commit toolkit sources first')
    if args.output.exists() or args.output.resolve().is_relative_to(ROOT):
        raise ValueError('Choose a new output directory outside the source tree')
    if not re.fullmatch(r'capstone-5\.0\.9-py3-none-win_amd64\.whl', args.capstone_wheel.name):
        raise ValueError('Supply the pinned Capstone 5.0.9 Windows x64 wheel')
    args.output.mkdir(parents=True)
    assets = args.output/'assets'; assets.mkdir()
    source = assets/('xboxllr-source-'+args.version+'.zip')
    source_record = package(ROOT, source)
    target = args.output/'stage'; target.mkdir()
    extract(source, target)
    python = target/'python'; python.mkdir()
    extract(args.embed, python)
    if not (python/'python.exe').is_file() or not (python/'LICENSE.txt').is_file():
        raise ValueError('Supply an official Windows embedded Python archive with license')
    paths = list(python.glob('python*._pth'))
    if len(paths) != 1:
        raise ValueError('Unsupported embedded Python layout')
    libraries = list(python.glob('python3*.zip'))
    if len(libraries) != 1:
        raise ValueError('Unsupported embedded Python standard library')
    paths[0].write_text(libraries[0].name+'\n.\nLib/site-packages\n../tools\nimport site\n', encoding='utf-8')
    site = python/'Lib/site-packages'; site.mkdir(parents=True)
    extract(args.capstone_wheel, site)
    shutil.copy2(args.launcher, target/'xboxllr.exe')
    (target/'README-FIRST.txt').write_text(
        'XboxLLR beta build toolkit (Windows x64)\n\n'
        'Open a terminal here and run: .\\xboxllr.exe --help\n'
        'Python and Capstone are bundled. MSVC, CMake and Git are not.\n'
        'Read docs/BUILD-TOOL.md and docs/DEVELOPMENT.md. Use a recursive Git\n'
        'checkout for reproducible title builds and supply your own original files.\n'
        'Blinx is the only publicly supported game. This toolkit does not make\n'
        'unprofiled Xbox games playable. Players should download BlinxRecomp instead.\n'
        'Python and Capstone retain their included licenses.\n', encoding='utf-8')
    record = dict(schema='xboxllr.toolkit-release.v1', version=args.version,
        framework_revision=git(ROOT, 'rev-parse', 'HEAD').decode().strip(),
        source_archive=source_record, embedded_python_sha256=sha(args.embed),
        capstone_wheel_sha256=sha(args.capstone_wheel), entry_point='xboxllr.exe',
        files={p.relative_to(target).as_posix(): sha(p) for p in sorted(target.rglob('*')) if p.is_file()})
    (target/'TOOLKIT-RELEASE.json').write_text(json.dumps(record,indent=2)+'\n')
    output = assets/('xboxllr-build-tool-windows-x64-'+args.version+'.zip')
    names = set(record['files']) | {'TOOLKIT-RELEASE.json'}
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=3) as archive:
        for name in sorted(names):
            path = target/name
            if name in record['files'] and sha(path) != record['files'][name]:
                raise ValueError('Toolkit changed during archive creation')
            archive.write(path, name)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != names:
            raise ValueError('Toolkit archive verification failed')
        for name, expected in record['files'].items():
            if hashlib.sha256(archive.read(name)).hexdigest() != expected:
                raise ValueError('Toolkit archive hash differs')
    records = [dict(file=output.name,sha256=sha(output),bytes=output.stat().st_size), source_record]
    (assets/'SHA256SUMS.txt').write_text(''.join(r['sha256']+'  '+r['file']+'\n' for r in records))
    (args.output/'artifacts.json').write_text(json.dumps(records,indent=2)+'\n')
    print(json.dumps(records,indent=2))


if __name__ == '__main__':
    main()
