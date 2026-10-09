#!/usr/bin/env python3
"""Create a separate private FATX HDD and install hash-checked title files.

Uses the externally prepared pyfatx tool only for asset preparation. No donor
filesystem implementation is linked into the native title. Existing images
are never overwritten, and the input content tree is read-only.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import time


def sha(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def empty_image(path, size):
    with path.open('xb') as image:
        if os.name == 'nt':
            import msvcrt
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.DeviceIoControl.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_uint32,
                ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
            returned = ctypes.c_uint32()
            if not kernel.DeviceIoControl(msvcrt.get_osfhandle(image.fileno()),
                0x900c4, None, 0, None, 0, ctypes.byref(returned), None):
                raise ctypes.WinError(ctypes.get_last_error())
        image.truncate(size)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--content', type=Path, required=True)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--title-id', required=True)
    parser.add_argument('--xbe-sha256', required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--preparation-source', type=Path, required=True,
                        help='external FATX source directory, retained privately for provenance')
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9A-Fa-f]{8}', args.title_id):
        raise ValueError('title ID must have eight hexadecimal digits')
    if args.image.exists() or args.manifest.exists():
        raise FileExistsError('choose new image and manifest paths')
    content = args.content.resolve(strict=True)
    if sha(content/'default.xbe') != args.xbe_sha256.lower():
        raise ValueError('installed XBE does not match the selected title revision')
    records = []
    seen = set()
    for source in sorted(content.rglob('*')):
        if source.is_symlink():
            raise ValueError('title installation does not follow symlinks')
        relative = source.relative_to(content)
        for part in relative.parts:
            encoded = part.encode('ascii')
            if not 1 <= len(encoded) <= 42 or any(c in part for c in '\\/:*?"<>|'):
                raise ValueError(f'unsupported FATX component: {part}')
        key = relative.as_posix().casefold()
        if key in seen:
            raise ValueError(f'case-colliding title path: {relative}')
        seen.add(key)
        if source.is_file():
            records.append(dict(path=relative.as_posix(), bytes=source.stat().st_size,
                                sha256=sha(source)))
    from pyfatx import Fatx
    started = time.monotonic()
    empty_image(args.image, 8*1024**3)
    Fatx.format(str(args.image.resolve()))
    volume = Fatx(str(args.image.resolve()), drive='e')
    destination = '/Titles/'+args.title_id.upper()
    volume.mkdir('/Titles')
    volume.mkdir(destination)
    directories = {'.'}
    for index, record in enumerate(records):
        relative = Path(record['path'])
        for parent in reversed(relative.parents):
            name = parent.as_posix()
            if name not in directories:
                volume.mkdir(destination+'/'+name)
                directories.add(name)
        data = (content/relative).read_bytes()
        if len(data) != record['bytes'] or hashlib.sha256(data).hexdigest() != record['sha256']:
            raise ValueError(f'content changed during installation: {relative}')
        try:
            volume.write(destination+'/'+record['path'], data)
        except Exception as exc:
            raise RuntimeError(f'FATX write failed at file {index+1}: {relative}') from exc
        if index % 200 == 0:
            print(f'Installed {index+1}/{len(records)} files', flush=True)
    # Close/reopen the actual image before checking every stored file.
    del volume
    volume = Fatx(str(args.image.resolve()), drive='e')
    for record in records:
        data = volume.read(destination+'/'+record['path'])
        if len(data) != record['bytes'] or hashlib.sha256(data).hexdigest() != record['sha256']:
            raise ValueError(f'FATX readback mismatch: {record["path"]}')
    del volume
    result = dict(title_id=args.title_id.upper(), xbe_sha256=args.xbe_sha256.lower(),
        image=str(args.image.resolve()), image_bytes=args.image.stat().st_size,
        image_sha256=sha(args.image), installed_path='\\Device\\Harddisk0\\Partition1'+destination.replace('/','\\'),
        content_bytes=sum(row['bytes'] for row in records), files=records,
        donor='mborgerson/fatx baed16e3a2d62fc247f2bd6a3ed5deffe75da150 (external preparation only)',
        preparation_directory_source_sha256=sha(args.preparation_source/'fatx_dir.c'),
        elapsed=round(time.monotonic()-started,3), readback_verified=True)
    args.manifest.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(f'Installed and verified {len(records)} files; {result["content_bytes"]} bytes',flush=True)


if __name__ == '__main__':
    main()
