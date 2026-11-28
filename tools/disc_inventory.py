"""Bounded read-only Xbox disc inventories using the pinned image provider."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] /
    'external/shared-cores/recomp-image-core/python'))
from recomp_image.xdvdfs import Volume, MAGIC, PARTITIONS, SECTOR


class Disc(Volume):
    """Inventory policy; geometry and binary directory decoding are shared."""
    def __init__(self, path):
        super().__init__(path)
        try:
            candidates = [base for base in PARTITIONS
                if base + 33 * SECTOR <= self.size and
                (header := self.read(base + 32 * SECTOR, SECTOR))[:20] == MAGIC
                and header[-20:] == MAGIC]
            if len(candidates) != 1:
                raise ValueError('expected exactly one supported GDFX volume')
            self.partition = self.base
        except BaseException:
            self.file.close()
            raise

    def index(self):
        pending = [('', *self.root)]
        visited, entries, paths = set(), [], set()
        directory_bytes = 0
        while pending:
            parent, offset, size = pending.pop()
            if size == 0:
                continue
            extent = offset, size
            if extent in visited or len(visited) >= 4096:
                raise ValueError('cyclic/oversized directory graph')
            visited.add(extent)
            directory_bytes += size
            if size > 16 * 1024 * 1024 or directory_bytes > 64 * 1024 * 1024:
                raise ValueError('oversized directory data')
            for item in self.table(offset, size):
                # This inventory's original public JSON contract uses ASCII.
                item.path.encode('ascii')
                path = parent + item.path
                folded = path.casefold()
                if folded in paths or len(entries) >= 100000:
                    raise ValueError('duplicate/oversized file index')
                paths.add(folded)
                entries.append(dict(path=path, sector=(item.offset-self.base)//SECTOR,
                    bytes=item.size, directory=item.directory))
                if item.directory:
                    pending.append((path+'/', item.offset, item.size))
        self.unchanged()
        return sorted(entries, key=lambda item: item['path'].casefold())
