"""Archive a pristine Windows package, never a used playtest directory.

An archive is not a legal clearance. Preserve the source, dependency notices
and original-rightsholder scope described in LICENSE-SCOPE.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
from contextlib import ExitStack
import io
import shutil
import tempfile
import zipfile

from native_release_runtime import descriptor, inside, sha

# Original private after-images are content, never redistributable source.
# Fingerprints also reject a renamed loose logo, which has no magic header.
PRIVATE_STARTUP_LOGOS = frozenset({
    'e206fb21bf7a632460c3bb97a98774ebd6470d65ad9341afbcaa87fe07cd7e2d',
    '7b6b049abaf7ebef37be861d1f5a868984e076c7d39347ac81e812d087ab7660',
})


def compose(files, output, *, title, distribution, provenance):
    """Archive a frontend plus exact nested runtime through the normal guards.

    Payloads are ordinary Paths, bytes, or binary stream factories. The caller
    admits its frozen runtime/source identity; this shared owner checks every
    resulting file, native import (including delay imports), and archive entry.
    """
    from windows_dependencies import inventory
    output = Path(output).absolute()
    if output.exists():
        raise ValueError('Choose a new archive destination')
    output.parent.mkdir(parents=True, exist_ok=True)
    folded = set()
    for name in files:
        path = PurePosixPath(name)
        if (not name or path.is_absolute() or '\\' in name or ':' in name or
                name != path.as_posix() or any(p in ('..', '.') for p in path.parts) or
                name.casefold() in folded or name.casefold() == 'release.json'):
            raise ValueError('Unsafe or colliding composition entry: ' + name)
        folded.add(name.casefold())
    with tempfile.TemporaryDirectory(prefix='.package-composition-', dir=output.parent) as temporary:
        root = Path(temporary)
        hashes = {}
        for name, payload in files.items():
            target = inside(root, name)
            target.parent.mkdir(parents=True, exist_ok=True)
            with ExitStack() as stack:
                if isinstance(payload, bytes):
                    source = stack.enter_context(io.BytesIO(payload))
                elif callable(payload):
                    source = stack.enter_context(payload())
                else:
                    payload = Path(payload)
                    if payload.is_symlink() or getattr(payload, 'is_junction', lambda: False)():
                        raise ValueError('Linked composition source')
                    source = stack.enter_context(payload.open('rb'))
                target_file = stack.enter_context(target.open('xb'))
                shutil.copyfileobj(source, target_file)
            hashes[name] = sha(target)
        native = [inside(root, name) for name in files
                  if PurePosixPath(name).suffix.lower() in ('.exe', '.dll')]
        if not native:
            raise ValueError('Composition needs a native executable')
        closure = inventory(native)
        if not closure['complete']:
            raise ValueError('Composed native dependency closure is incomplete: ' + str(closure['missing']))
        # Staging paths are private; only package-relative source names belong
        # in distributable provenance.
        for record in closure['files']:
            record['source'] = Path(record['source']).relative_to(root).as_posix()
        metadata = dict(schema='xboxllr.native-preview.v1', title=title,
            distribution=distribution, composition=provenance,
            windows_dependencies=closure, files=hashes)
        (root / 'release.json').write_text(json.dumps(metadata, indent=2)+'\n', encoding='utf-8')
        return archive(root, output)


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
                any(p.lower() in ('data', 'local', 'setup inputs') for p in path.parts[:-1]) or
                path.suffix.lower() in ('.xbe', '.iso', '.xiso', '.img', '.qcow2', '.bin', '.pak', '.rle', '.raw', '.rom')):
            raise ValueError('Private or unsafe package entry: ' + name)
        entry = inside(source, name)
        with entry.open('rb') as stream:
            if stream.read(4) == b'XBPK':
                raise ValueError('Private startup data in package: ' + name)
        if metadata['files'].get(name) in PRIVATE_STARTUP_LOGOS:
            raise ValueError('Private original startup logo in package: ' + name)
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
