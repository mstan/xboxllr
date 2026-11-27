"""Read-only XDVDFS import. Native gameplay continues to use ordinary files.

Format reference: XboxDev/extract-xiso, descriptor and directory layouts.
No executable patching, optical emulation or original content is distributed.
"""
from __future__ import annotations
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile

MAX_XBE = 256 * 1024 * 1024
SCHEMA = 'xboxllr.disc-import.v1'


class ImportCancelled(Exception):
    """An owner cancellation returns to the launcher without changing saves."""


def is_disc(path):
    return Path(path).suffix.lower() in ('.iso', '.xiso')


# Source package follows the pinned image provider; import policy stays here.
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'external/shared-cores/recomp-image-core/python'))
from recomp_image.xdvdfs import (Volume, Entry, safe_name, source_identity,
                                SECTOR, MAGIC, PARTITIONS, MAX_TABLE, MAX_ENTRIES)

class Disc(Volume):
    def __init__(self, path):
        super().__init__(path)
        try:
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
