"""Archive a pristine Windows package, never a used playtest directory.

An archive is not a legal clearance. Preserve the source, dependency notices
and original-rightsholder scope described in LICENSE-SCOPE.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile

from native_release_runtime import descriptor, inside, sha


def archive(source: Path, output: Path):
    source, output = source.resolve(strict=True), output.absolute()
    if output.exists() or output.is_relative_to(source):
        raise ValueError('Choose a new archive outside the package')
    metadata = descriptor(source)
    names = set(metadata['files']) | {'release.json'}
    # Only the already hashed manifest is an admission list. Any generated
    # console, save, selected-input config or extra file rejects the whole tree.
    for name in names:
        path = PurePosixPath(name)
        if (path.is_absolute() or '\\' in name or ':' in name or
                any(p in ('..', '.', '') for p in path.parts) or
                any(p.casefold() in ('private-do-not-share', 'diagnostics') for p in path.parts) or
                path.parts[0].lower() in ('data', 'local', 'setup inputs') or
                path.suffix.lower() in ('.xbe', '.iso', '.xiso', '.img', '.qcow2', '.bin')):
            raise ValueError('Private or unsafe package entry: ' + name)
        inside(source, name)
    actual = set()
    for path in source.rglob('*'):
        if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
            raise ValueError('Package contains a link')
        if path.is_file():
            actual.add(path.relative_to(source).as_posix())
    if actual != names:
        raise ValueError('Package has unmanifested or missing files')
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + '.partial')
    try:
        with temporary.open('xb') as raw:
            with zipfile.ZipFile(raw, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=3) as z:
                for name in sorted(names):
                    data = inside(source, name).read_bytes()
                    expected = metadata['files'].get(name)
                    if expected and hashlib.sha256(data).hexdigest() != expected:
                        raise ValueError('Package changed during archive: ' + name)
                    entry = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
                    entry.compress_type = zipfile.ZIP_DEFLATED
                    entry.external_attr = 0o100644 << 16
                    z.writestr(entry, data, compresslevel=3)
        with zipfile.ZipFile(temporary) as z:
            if set(z.namelist()) != names or z.testzip() is not None:
                raise ValueError('Archive verification failed')
        # On Windows rename refuses an existing target; avoid replacing anything.
        if output.exists():
            raise ValueError('Archive destination appeared during staging')
        temporary.rename(output)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    return dict(file=output.name, sha256=sha(output), bytes=output.stat().st_size,
                entries=len(names), title=metadata['title']['name'],
                distribution=metadata['distribution'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(archive(args.source, args.output), indent=2))
