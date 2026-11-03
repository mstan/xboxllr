#!/usr/bin/env python3
"""AppImage entry: immutable distribution, private persistent per-game data.

Keep the AppImage mounted for the entire launcher/game session. A per-game
writer lock protects upgrades and the running copied executable together.
"""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile


def message(title, text, *, error=True):
    print(title + ': ' + text, file=sys.stderr, flush=True)
    if os.environ.get('XBOXLLR_AUTOMATED') == '1':
        return
    try:
        sdl = ctypes.CDLL('libSDL2-2.0.so.0')
        sdl.SDL_ShowSimpleMessageBox.argtypes = [ctypes.c_uint32, ctypes.c_char_p,
                                                ctypes.c_char_p, ctypes.c_void_p]
        sdl.SDL_ShowSimpleMessageBox(0x10 if error else 0x40, title.encode(), text.encode(), None)
    except OSError:
        pass  # The terminal still contains the startup error if SDL cannot load.


def ordinary(root, relative):
    path = Path(relative)
    if path.is_absolute() or '..' in path.parts or not path.parts or ':' in relative:
        raise ValueError('Invalid application file path')
    target = root / path
    if any(p.is_symlink() for p in (target, *target.parents) if p.is_relative_to(root)):
        raise ValueError('Application files must be ordinary files: ' + relative)
    return target


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def seed(payload, root, record):
    files = record['files']
    # Verify every immutable input before replacing any managed application file.
    for name, digest in files.items():
        if name.split('/')[0] == 'data' or sha(ordinary(payload, name)) != digest:
            raise ValueError('Damaged AppImage payload: ' + name)
    for name, digest in files.items():
        source, target = ordinary(payload, name), ordinary(root, name)
        if target.is_file() and sha(target) == digest:
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        fd, candidate = tempfile.mkstemp(prefix='.appimage-', dir=target.parent)
        try:
            with os.fdopen(fd, 'wb') as output, source.open('rb') as stream:
                shutil.copyfileobj(stream, output)
                output.flush()
                os.fsync(output.fileno())
            os.chmod(candidate, source.stat().st_mode & 0o777)
            os.replace(candidate, target)
        finally:
            if os.path.exists(candidate):
                os.unlink(candidate)
    # Old managed code may be retired; unknown files and all player data survive.
    previous = ordinary(root, 'release.json')
    if previous.is_file():
        old = json.loads(previous.read_text())
        for name in set(old.get('files', {})) - set(files):
            if name.split('/')[0] != 'data':
                target = ordinary(root, name)
                if target.is_file() and sha(target) == old['files'][name]:
                    target.unlink()
    temp = root / '.release.json.new'
    ordinary(root, temp.name)
    with temp.open('w', encoding='utf-8') as output:
        json.dump(record, output, indent=2)
        output.write('\n')
    os.replace(temp, previous)


def main():
    if os.name != 'posix':
        raise ValueError('This entry point requires native Linux')
    import fcntl
    parser = argparse.ArgumentParser(description=__doc__, add_help=False)
    parser.add_argument('--payload', type=Path, required=True)
    args, forward = parser.parse_known_args()
    record = json.loads((args.payload / 'release.json').read_text())
    app_id = record['application_id']
    if not app_id.replace('-', '').isalnum() or record.get('platform') != 'linux-x86_64':
        raise ValueError('Invalid Linux application identity')
    base = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')
    if not base.is_absolute():
        raise ValueError('XDG_DATA_HOME must be an absolute path')
    root = base / 'xboxllr' / app_id
    root.mkdir(parents=True, exist_ok=True)
    ordinary(root, 'appimage.lock')
    with (root / 'appimage.lock').open('a+b') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError('This game is already open. Use its existing launcher or game window.') from error
        seed(args.payload, root, record)
        os.environ['PYTHONDONTWRITEBYTECODE'] = '1'
        sys.path.insert(0, str(root / 'tools'))
        from native_release_runtime import main as run
        sys.argv = [sys.argv[0], '--root', str(root), *forward]
        return run()


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError) as error:
        message('xboxllr', str(error))
        raise SystemExit(1)
