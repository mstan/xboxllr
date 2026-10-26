"""Read-only XDVDFS import. Native gameplay continues to use ordinary files.

Format reference: XboxDev/extract-xiso, descriptor and directory layouts.
No executable patching, optical emulation or original content is distributed.
"""
from __future__ import annotations
from dataclasses import dataclass
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import tempfile

SECTOR = 2048
MAGIC = b'MICROSOFT*XBOX*MEDIA'
PARTITIONS = (0, 0x18300000, 0x0fd90000, 0x02080000)
MAX_TABLE = 64 * 1024 * 1024
MAX_XBE = 256 * 1024 * 1024
MAX_ENTRIES = 100000
SCHEMA = 'xboxllr.disc-import.v1'


class ImportCancelled(Exception):
    """An owner cancellation returns to the launcher without changing saves."""


def is_disc(path):
    return Path(path).suffix.lower() in ('.iso', '.xiso')


def safe_name(raw):
    try:
        name = raw.decode('cp1252')
    except UnicodeDecodeError as error:
        raise ValueError('Disc filename has an unsupported encoding') from error
    if (not name or name in ('.', '..') or name[-1] in '. ' or
            any(ord(c) < 32 or ord(c) == 127 or c in '<>:"/\\|?*' for c in name) or
            re.fullmatch(r'(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])', name.split('.')[0], re.I)):
        raise ValueError('Disc contains an unsafe filename')
    return name


def source_identity(path, value):
    # A retained cache is tied to this source file and its version, not its name
    # or a title ID. No full-ISO hashing or payload scan is repeated on launch.
    identity = dict(device=value.st_dev, file=value.st_ino, size=value.st_size,
                    modified=value.st_mtime_ns)
    # Python's Windows fstat/stat can disagree on legacy st_ctime for sparse
    # files. File ID, length and precise write time agree across both APIs.
    if os.name != 'nt':
        identity['changed'] = value.st_ctime_ns
    if not value.st_ino:
        identity['path'] = str(path)
    return identity


@dataclass(frozen=True)
class Entry:
    path: str
    offset: int
    size: int
    directory: bool


class Disc:
    def __init__(self, path):
        self.path = Path(path).resolve(strict=True)
        self.file = self.path.open('rb')
        try:
            info = os.fstat(self.file.fileno())
            if not stat.S_ISREG(info.st_mode):
                raise ValueError('Choose an Xbox ISO or XISO file')
            self.identity = source_identity(self.path, info)
            self.size = info.st_size
            self.base = None
            for base in PARTITIONS:
                if base + 33 * SECTOR > self.size:
                    continue
                header = self.read(base + 32 * SECTOR, SECTOR)
                if header[:20] == MAGIC and header[-20:] == MAGIC:
                    self.base = base
                    root_sector, root_size = struct.unpack_from('<II', header, 20)
                    self.root = (base + root_sector * SECTOR, root_size)
                    self.extent(*self.root)
                    if not 14 <= root_size <= MAX_TABLE:
                        raise ValueError('Xbox disc root directory is invalid')
                    break
            if self.base is None:
                raise ValueError('Not an Xbox XDVDFS disc image. Select an Xbox ISO/XISO or extracted XBE.')
            roots = self.table(*self.root)
            matches = [e for e in roots if e.path.casefold() == 'default.xbe' and not e.directory]
            if len(matches) != 1 or not 0x178 <= matches[0].size <= MAX_XBE:
                raise ValueError('Xbox disc has no valid root default.xbe')
            self.xbe_entry = matches[0]
            self.xbe = self.read(self.xbe_entry.offset, self.xbe_entry.size)
            self.digest = hashlib.sha256(self.xbe).hexdigest()
        except BaseException:
            self.file.close()
            raise

    def __enter__(self): return self
    def __exit__(self, *_): self.file.close()

    def extent(self, offset, size):
        if offset < 0 or size < 0 or offset > self.size or size > self.size - offset:
            raise ValueError('Xbox disc contains a truncated or out-of-bounds extent')

    def read(self, offset, size):
        self.extent(offset, size)
        self.file.seek(offset)
        data = self.file.read(size)
        if len(data) != size:
            raise ValueError('Xbox disc is truncated')
        return data

    def unchanged(self):
        if source_identity(self.path, os.fstat(self.file.fileno())) != self.identity:
            raise ValueError('Disc image changed during import. Its partial import was discarded.')
        # Also detect replacement of the pathname while the original handle is open.
        if source_identity(self.path, self.path.stat()) != self.identity:
            raise ValueError('Disc image was replaced during import')

    def table(self, offset, size):
        if not 14 <= size <= MAX_TABLE:
            raise ValueError('Xbox disc directory table is invalid')
        data = self.read(offset, size)
        pending, seen, names, intervals, result = [0], set(), set(), [], []
        while pending:
            at = pending.pop()
            if at in seen or at + 14 > size or len(seen) >= MAX_ENTRIES:
                raise ValueError('Xbox disc directory has a cycle or invalid node')
            seen.add(at)
            left, right, sector, length, attributes, n = struct.unpack_from('<HHIIBB', data, at)
            end = at + 14 + n
            if not n or end > size:
                raise ValueError('Xbox disc directory record is truncated')
            name = safe_name(data[at + 14:end])
            if name.casefold() in names:
                raise ValueError('Xbox disc contains colliding filenames')
            names.add(name.casefold());intervals.append((at, end))
            start = self.base + sector * SECTOR
            self.extent(start, length)
            result.append(Entry(name, start, length, bool(attributes & 0x10)))
            for child in (left, right):
                if child:
                    pending.append(child * 4)
        intervals.sort()
        if any(b[0] < a[1] for a, b in zip(intervals, intervals[1:])):
            raise ValueError('Xbox disc directory records overlap')
        return result

    def entries(self, check=lambda: None):
        pending = [('', *self.root, 0)]
        visited, result, extents = set(), [], [(self.root[0], self.root[0] + self.root[1])]
        while pending:
            prefix, offset, size, depth = pending.pop()
            check()
            if depth > 32 or (offset, size) in visited:
                raise ValueError('Xbox disc has a directory cycle or excessive depth')
            visited.add((offset, size))
            for e in self.table(offset, size):
                path = prefix + e.path
                if len(path) > 30000 or len(result) >= MAX_ENTRIES:
                    raise ValueError('Xbox disc exceeds import path or entry limits')
                result.append(Entry(path, e.offset, e.size, e.directory))
                if e.size:
                    extents.append((e.offset, e.offset + e.size))
                if e.directory and e.size:
                    pending.append((path + '/', e.offset, e.size, depth + 1))
        extents.sort()
        if any(b[0] < a[1] for a, b in zip(extents, extents[1:])):
            raise ValueError('Xbox disc file or directory extents overlap')
        self.unchanged()
        return result


def image_digest(path):
    """Fingerprint only default.xbe, including when it lives inside an image."""
    if is_disc(path):
        with Disc(path) as disc:
            return disc.digest
    with Path(path).open('rb') as file:
        if os.fstat(file.fileno()).st_size > MAX_XBE:
            raise ValueError('Choose a supported Xbox XBE or ISO/XISO')
        return hashlib.file_digest(file, 'sha256').hexdigest()


def ordinary(path):
    value = path.lstat()
    if stat.S_ISLNK(value.st_mode) or getattr(value, 'st_file_attributes', 0) & 0x400:
        raise ValueError('Linked import-cache paths are not supported')
    return value


def remove_partial(path, store):
    if path.parent != store or not path.name.startswith('.import-'):
        raise ValueError('Invalid partial import cleanup path')
    # Only our new, marked staging trees can be removed. Do not follow links
    # or clear protected attributes if unexpected contents appear.
    marker = path / '.import-owner.json'
    ordinary(path);ordinary(marker)
    if json.loads(marker.read_text()).get('schema') != SCHEMA:
        raise ValueError('Unrecognized partial import was preserved')
    for parent, directories, files in os.walk(path, followlinks=False):
        for name in (*directories, *files):
            if name == '.git':
                raise ValueError('Unexpected repository in partial import was preserved')
            value = ordinary(Path(parent) / name)
            if getattr(value, 'st_file_attributes', 0) & 7:
                raise ValueError('Protected partial import contents were preserved')
    shutil.rmtree(path)


def prepare_game(root, data, image, progress=None):
    """Return the admitted extracted XBE; no source file or save is modified."""
    from native_revisions import select
    from xbe_identity import parse_identity, unsupported_identity_message
    image = Path(image).resolve(strict=True)
    if not is_disc(image):
        select(data.get('_catalog', data), image_digest(image), image=image)
        return image
    with Disc(image) as disc, ExitStack() as leases:
        catalog = data.get('_catalog', data)
        try:
            select(catalog, disc.digest)
        except ValueError:
            raise ValueError(unsupported_identity_message(parse_identity(disc.xbe), catalog['title'])) from None
        from native_release_runtime import inside, session_lock
        store = inside(Path(root), 'data/discs');store.mkdir(parents=True, exist_ok=True)
        leases.enter_context(session_lock(store))
        key = hashlib.sha256(json.dumps([disc.identity, disc.digest], sort_keys=True).encode()).hexdigest()
        destination = inside(Path(root), 'data/discs/' + key)
        if destination.exists():
            ordinary(destination)
            receipt = destination / 'import.json';ordinary(receipt)
            if receipt.stat().st_size > 32 * 1024 * 1024:
                raise ValueError('Disc import cache metadata is invalid')
            record = json.loads(receipt.read_text())
            if (record.get('schema') != SCHEMA or record.get('source') != disc.identity or
                    record.get('xbe_sha256') != disc.digest or not isinstance(record.get('files'), dict) or
                    not isinstance(record.get('directories'), list)):
                raise ValueError('Disc import cache metadata changed; existing data was preserved')
            content = inside(destination, 'files');ordinary(content)
            for name in record['directories']:
                if not stat.S_ISDIR(ordinary(inside(content, name)).st_mode):
                    raise ValueError('Imported game directories are incomplete')
            for name, size in record['files'].items():
                target = inside(content, name)
                info = ordinary(target)
                if not stat.S_ISREG(info.st_mode) or info.st_size != size:
                    raise ValueError('Imported game files are incomplete; existing data was preserved')
            target = inside(content, disc.xbe_entry.path)
            if image_digest(target) != disc.digest:
                raise ValueError('Imported Xbox executable changed; existing data was preserved')
            disc.unchanged()
            return target
        # The caller owns the package's session lease. Clean imports left by a
        # terminated prior attempt before allocating another whole game copy.
        for partial in store.glob('.import-*'):
            if partial.is_dir() and (partial / '.import-owner.json').is_file():
                remove_partial(partial, store)
        if progress is None:
            from native_disc_progress import Progress
            progress = Progress(catalog['title'].get('name', 'Xbox game'))
        with progress:
            entries = disc.entries(progress.check)
            total = sum(e.size for e in entries if not e.directory)
            if shutil.disk_usage(store).free < total + 64 * 1024 * 1024:
                raise ValueError(f'Import needs {total / 1024**3:.2f} GiB of free space beside this game build.')
            candidate = Path(tempfile.mkdtemp(prefix='.import-', dir=store))
            (candidate / '.import-owner.json').write_text(json.dumps(dict(schema=SCHEMA)))
            content = candidate / 'files';content.mkdir()
            done = 0
            try:
                for e in entries:
                    if e.directory:
                        (content / e.path).mkdir(parents=True, exist_ok=True)
                for e in sorted((e for e in entries if not e.directory), key=lambda e: e.offset):
                    progress.check()
                    target = content / e.path
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with target.open('xb') as output:
                        for offset in range(0, e.size, 4 * 1024 * 1024):
                            chunk = disc.read(e.offset + offset, min(4 * 1024 * 1024, e.size - offset))
                            output.write(chunk);done += len(chunk)
                            progress.update(done, total, e.path)
                disc.unchanged()
                if image_digest(content / disc.xbe_entry.path) != disc.digest:
                    raise ValueError('Imported executable verification failed')
                record = dict(schema=SCHEMA, source=disc.identity, xbe_sha256=disc.digest,
                              files={e.path:e.size for e in entries if not e.directory},
                              directories=[e.path for e in entries if e.directory])
                with (candidate / 'import.json').open('x', encoding='utf-8') as file:
                    json.dump(record, file, sort_keys=True);file.flush();os.fsync(file.fileno())
                progress.check()
                candidate.rename(destination)
                return destination / 'files' / disc.xbe_entry.path
            finally:
                if candidate.exists():
                    remove_partial(candidate, store)
