"""Package exact tracked source and pinned submodules, excluding private inputs.

This is a source/tooling archive, never an archive of compiled original titles.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

from check_repository_hygiene import DOCUMENTATION_IMAGES, content_findings, private_path

GIT = 'C:/Program Files/Git/cmd/git.exe' if Path('C:/Program Files/Git/cmd/git.exe').exists() else 'git'


def git(repo, *args):
    return subprocess.check_output([GIT, '-C', str(repo), *args])


def package(repo, output, *, revision='HEAD', images=None, definition=None):
    repo, output = Path(repo).resolve(strict=True), Path(output).absolute()
    if output.exists() or output.is_relative_to(repo):
        raise ValueError('Choose a new archive outside the source repository')
    approved = dict(DOCUMENTATION_IMAGES)
    approved.update(images or {})
    files, revisions = {}, {}

    def visit(source, ref, prefix='', parents=()):
        if source in parents:
            raise ValueError('Recursive submodule')
        commit = git(source, 'rev-parse', ref+'^{commit}').decode().strip()
        revisions[prefix or '.'] = commit
        entries = git(source, 'ls-tree', '-rz', commit).split(b'\0')
        blobs = []
        for entry in entries:
            if not entry:
                continue
            meta, raw_name = entry.split(b'\t', 1)
            mode, kind, oid = meta.decode().split()
            name = raw_name.decode('utf-8')
            if name.startswith('/') or '\\' in name or ':' in name or '..' in Path(name).parts:
                raise ValueError('Unsafe source path')
            relative = prefix+name
            if kind == 'commit' and mode == '160000':
                child = (source/name).resolve(strict=True)
                if not child.is_relative_to(source):
                    raise ValueError('Submodule leaves source repository')
                visit(child, oid, relative+'/', (*parents, source))
                continue
            if kind != 'blob' or mode not in ('100644', '100755'):
                raise ValueError('Unsupported source entry: '+relative)
            blobs.append((name, relative, mode, oid))
        reader = subprocess.Popen([GIT, '-C', str(source), 'cat-file', '--batch'],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        try:
            for name, relative, mode, oid in blobs:
                reader.stdin.write((oid+'\n').encode('ascii')); reader.stdin.flush()
                header = reader.stdout.readline().split()
                if len(header) != 3 or header[1] != b'blob':
                    raise ValueError('Unreadable source object')
                data = reader.stdout.read(int(header[2]))
                if reader.stdout.read(1) != b'\n':
                    raise ValueError('Truncated source object')
                digest = hashlib.sha256(data).hexdigest()
                expected = approved.get(relative, approved.get(name))
                expected_hashes = (expected,) if isinstance(expected, str) else (expected or ())
                image = digest in expected_hashes
                compiled = Path(name).suffix.lower() in ('.exe','.dll','.lib','.obj','.pdb','.a','.so','.dylib')
                if compiled or (private_path(name) and not image):
                    raise ValueError('Private source path: '+relative)
                if list(content_findings(data, allow_binary=image)):
                    raise ValueError('Private or unreviewed content: '+relative)
                files[relative] = (data, mode)
        finally:
            reader.stdin.close(); reader.stdout.close(); reader.wait()

    visit(repo, revision)
    record = dict(schema='xboxllr.source-release.v1', revisions=revisions,
        content='tracked source/tooling only; no precompiled title, original game bodies or console inputs',
        files={name: hashlib.sha256(value[0]).hexdigest() for name, value in sorted(files.items())})
    if definition:
        if definition not in files or not definition.endswith('.toml'):
            raise ValueError('Title definition is not in the source commit')
        record['title_definition'] = definition
    if 'SOURCE-RELEASE.json' in files:
        raise ValueError('Reserved release manifest path')
    files['SOURCE-RELEASE.json'] = ((json.dumps(record, indent=2)+'\n').encode(), '100644')
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name+'.partial')
    try:
        with temporary.open('xb') as stream:
            with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
                for name, (data, mode) in sorted(files.items()):
                    item = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
                    item.compress_type = zipfile.ZIP_DEFLATED
                    item.external_attr = int(mode, 8) << 16
                    archive.writestr(item, data, compresslevel=3)
        with zipfile.ZipFile(temporary) as archive:
            if set(archive.namelist()) != set(files) or archive.testzip() is not None:
                raise ValueError('Source archive verification failed')
            for name, expected in record['files'].items():
                if hashlib.sha256(archive.read(name)).hexdigest() != expected:
                    raise ValueError('Source archive hash mismatch')
        if output.exists():
            raise ValueError('Archive destination appeared during staging')
        temporary.rename(output)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    with output.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(file=output.name, sha256=digest, bytes=output.stat().st_size,
                entries=len(files), revisions=revisions)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--revision', default='HEAD')
    parser.add_argument('--definition', help='Optional tracked title-definition path')
    parser.add_argument('--image', action='append', default=[], metavar='PATH=SHA256')
    args = parser.parse_args()
    result = package(args.repo, args.output, revision=args.revision,
                     images=dict(value.rsplit('=', 1) for value in args.image), definition=args.definition)
    print(json.dumps(result, indent=2))
