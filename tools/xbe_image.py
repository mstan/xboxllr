"""Immutable XBE section parsing shared by private title-data mod tools."""
from dataclasses import dataclass
import hashlib
import struct


def digest(data: bytes) -> bytes:
    return hashlib.sha1(struct.pack("<I", len(data)) + data).digest()


@dataclass(frozen=True)
class Section:
    name: str
    header: int
    flags: int
    va: int
    size: int
    raw: int
    length: int
    head: int
    tail: int


class Xbe:
    def __init__(self, data: bytes):
        if len(data) < 0x178 or data[:4] != b"XBEH":
            raise ValueError("not an XBE")
        self.data = data
        self.base, self.headers, self.size = struct.unpack_from("<III", data, 0x104)
        count, table = struct.unpack_from("<II", data, 0x11C)
        at = table - self.base
        if (not 0 < count <= 128 or not 0x178 <= self.headers <= len(data)
                or at < 0x178 or at + count * 56 > self.headers
                or self.base + self.size > 0x100000000):
            raise ValueError("invalid XBE headers")
        self.table_end = at + count * 56
        self.sections = []
        names = set()
        for i in range(count):
            header = at + i * 56
            flags, va, size, raw, length, name, refs, head, tail = struct.unpack_from("<9I", data, header)
            name -= self.base
            end = data.find(b"\0", name, self.headers) if 0 <= name < self.headers else -1
            if (end < 0 or not length <= size or raw < self.headers or raw + length > len(data)
                    or va < self.base + self.headers or va + size > self.base + self.size
                    or refs or any(p & 1 or not self.base <= p <= self.base + self.headers - 2
                                   for p in (head, tail))):
                raise ValueError("invalid XBE section or shared-page reference")
            label = data[name:end].decode("ascii")
            if not label or label in names:
                raise ValueError("empty or duplicate XBE section name")
            names.add(label)
            if digest(data[raw:raw + length]) != data[header + 36:header + 56]:
                raise ValueError(f"XBE section integrity mismatch: {label}")
            self.sections.append(Section(label, header, flags, va, size, raw, length, head, tail))
        for left, right in zip(sorted(self.sections, key=lambda s: s.va),
                               sorted(self.sections, key=lambda s: s.va)[1:]):
            if left.va + left.size > right.va:
                raise ValueError("overlapping XBE virtual sections")

    def named(self, name: str) -> Section:
        matches = [section for section in self.sections if section.name == name]
        if len(matches) != 1:
            raise ValueError(f"missing or ambiguous section: {name}")
        return matches[0]

