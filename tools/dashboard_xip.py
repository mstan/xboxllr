"""Independent, bounded XIP member repacking for an owner's dashboard assets.

The on-disk fields are format facts checked against the owned 5960 archives.
No Theseus implementation or dashboard script bodies are included here.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct


class Xip:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < 16:
            raise ValueError("truncated XIP header")
        magic, self.start, self.files, names, extent = struct.unpack_from("<IIHHI", data)
        strings = 16 + self.files * 16 + names * 4
        if magic != 0x30504958 or not strings <= self.start <= len(data):
            raise ValueError("invalid XIP header or directory extent")
        if extent > len(data) - self.start:
            raise ValueError("XIP data extent exceeds archive")
        self.end = self.start + extent
        self.members: dict[str, tuple[str, int]] = {}
        for i in range(names):
            index, offset = struct.unpack_from("<HH", data, 16 + self.files * 16 + i * 4)
            begin = strings + offset
            end = data.find(b"\0", begin, self.start)
            if index >= self.files or begin >= self.start or end < 0:
                raise ValueError("invalid XIP member name or data index")
            name = data[begin:end].decode("ascii")
            key = name.casefold()
            if not name or key in self.members:
                raise ValueError("empty or ambiguous XIP member name")
            self.members[key] = (name, index)

    def entry(self, name: str) -> tuple[int, int, int]:
        try:
            _, index = self.members[name.casefold()]
        except KeyError as exc:
            raise ValueError(f"missing XIP member: {name}") from exc
        at = 16 + index * 16
        offset, size, kind, _ = struct.unpack_from("<IIII", self.data, at)
        # Only ordinary uncompressed file payloads can be changed. Mesh alias
        # records and other specialized entry types remain untouched.
        if kind != 0:
            raise ValueError("only ordinary XIP file members support overlays")
        begin = self.start + offset
        if begin > self.end or size > self.end - begin:
            raise ValueError("XIP member exceeds archive")
        return at, begin, size

    def read(self, name: str) -> bytes:
        _, begin, size = self.entry(name)
        return self.data[begin:begin + size]

    def replace(self, name: str, payload: bytes) -> bytes:
        self.entry(name)
        index = self.members[name.casefold()][1]
        if sum(member[1] == index for member in self.members.values()) != 1:
            raise ValueError("aliased member replacement requires an explicit alias policy")
        # Initial retail object loading consumes payloads in FILEDATA order,
        # ignoring generic/texture data offsets. Repack that physical stream;
        # changing an offset to an appended copy does not redirect the reader.
        # Type4 is a packed mesh-buffer/first-index reference, not file bytes.
        # Keep those words and the referenced IB/VB contents exactly intact.
        result = bytearray(self.data[:self.start])
        cursor = 0
        for i in range(self.files):
            entry = 16 + i * 16
            old_offset, size, kind, _ = struct.unpack_from("<IIII", self.data, entry)
            if kind == 4:
                continue
            if kind not in (0, 2, 3, 5, 6):
                raise ValueError("unsupported XIP stream member type")
            if old_offset != cursor or cursor + size > self.end - self.start:
                raise ValueError("XIP payloads are not a bounded sequential stream")
            contents = (payload if i == index else
                        self.data[self.start + cursor:self.start + cursor + size])
            offset = len(result) - self.start
            if max(offset, len(contents)) > 0xFFFFFFFF:
                raise ValueError("XIP replacement exceeds 32-bit extent")
            struct.pack_into("<II", result, entry, offset, len(contents))
            result.extend(contents)
            cursor += size
        if any(self.data[self.start + cursor:]):
            raise ValueError("nonzero data outside the XIP payload stream")
        # The original buffered reader eagerly refills after exhausting a
        # block, even when the requested member has just completed. Keep a
        # full 64-KiB read-ahead block after appended data; EOF must not turn
        # a successful member read into the retail fatal-reboot path.
        result.extend(b"\0" * ((-len(result) % 65536) + 65536))
        extent = len(result) - self.start
        if extent > 0xFFFFFFFF:
            raise ValueError("XIP repack exceeds 32-bit extent")
        struct.pack_into("<I", result, 12, extent)
        check = Xip(bytes(result))
        if check.read(name) != payload:
            raise ValueError("XIP repack readback failed")
        return bytes(result)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--member", default="default.xap")
    parser.add_argument("--extract", type=Path)
    parser.add_argument("--replace", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    data = args.archive.read_bytes()
    if hashlib.sha256(data).hexdigest() != args.expected_sha256.lower():
        parser.error("archive hash differs from the reviewed input")
    archive = Xip(data)
    if bool(args.replace) != bool(args.output):
        parser.error("replacement requires both --replace and --output")
    if not args.extract and not args.replace:
        parser.error("choose extraction or a new overlay archive")
    outputs = [p for p in (args.extract, args.output) if p]
    if len({p.resolve() for p in outputs}) != len(outputs):
        parser.error("output paths must be distinct")
    if any(p.resolve() == args.archive.resolve() or p.exists() for p in outputs):
        parser.error("outputs must be new paths, separate from the original archive")
    payloads = []
    if args.extract:
        payloads.append((args.extract, archive.read(args.member)))
    if args.output:
        payloads.append((args.output, archive.replace(args.member, args.replace.read_bytes())))
    for destination, payload in payloads:
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("xb") as output:
            output.write(payload)
        if destination.read_bytes() != payload:
            raise ValueError("output readback differs")
        print(f"{destination}: {len(payload)} bytes SHA256={hashlib.sha256(payload).hexdigest()}")


if __name__ == "__main__":
    main()
