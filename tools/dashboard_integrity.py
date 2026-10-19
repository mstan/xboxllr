"""Regenerate XIP integrity metadata in a new private, owned dashboard XBE.

Format facts and the length-prefixed SHA-1 were checked against the owner's
5960 binary and a captured fatal comparison. No retail implementation is
included. The retail XIP and section checks remain enabled. Changing header
metadata invalidates the original XBE signature; this is a local derived mod,
not a signed or redistributable dashboard.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from xbe_image import Section, Xbe, digest


XBE_SHA256 = "832862490c762a723856350142f49ded92b7ad966741eeb5b89e683006b16a0d"
ARCHIVES = {
    "default.xip": "b8611df758981d50ebce1bec835cc23388ac1ba277ca21cabea8c0eca1e47c4c",
    "mainmenu5.xip": "3c3a202cd935e7840cd7d806fee190b8d9a5ddc1d01922c5e15b97cf5d0e6812",
}
BLOCK = 65536
PAGE = 4096


def blocks(data: bytes) -> bytes:
    if not data:
        raise ValueError("empty archives cannot have an integrity record")
    return b"".join(digest(data[i:i + BLOCK]) for i in range(0, len(data), BLOCK))


def table_records(data: bytes) -> list[tuple[str, bytes]]:
    """Return exact records, including one final all-zero sentinel."""
    result = []
    names = set()
    at = 0
    while at + 132 <= len(data):
        name_bytes = data[at:at + 128]
        count = struct.unpack_from("<I", data, at + 128)[0]
        if not count:
            if any(data[at:]) or len(data) - at != 132:
                raise ValueError("invalid XIPS sentinel or trailing bytes")
            return result + [("", data[at:])]
        end = at + 132 + count * 20
        if end > len(data):
            raise ValueError("XIPS record exceeds section")
        wide = name_bytes.decode("utf-16le")
        name, separator, _ = wide.partition("\0")
        key = name.casefold()
        if not separator or not name or key in names:
            raise ValueError("invalid or duplicate XIPS record name")
        names.add(key)
        result.append((key, data[at:end]))
        at = end
    raise ValueError("missing XIPS sentinel")


def replace_records(data: bytes, archives: dict[str, tuple[bytes, bytes]]) -> bytes:
    remaining = dict(archives)
    result = []
    for name, record in table_records(data):
        if name not in remaining:
            result.append(record)
            continue
        original, replacement = remaining.pop(name)
        expected = blocks(original)
        if record[132:] != expected:
            raise ValueError(f"original archive does not match XIPS integrity: {name}")
        updated = blocks(replacement)
        result.append(record[:128] + struct.pack("<I", len(updated) // 20) + updated)
    if remaining:
        raise ValueError("archive missing from XIPS: " + ", ".join(remaining))
    return b"".join(result)


def relocate_table(data: bytes, table: bytes, reference_offset: int) -> bytes:
    """Put an enlarged XIPS on isolated pages, retaining every other section.

    reference_offset is a caller-reviewed four-byte reservation in the mapped
    header. Zero bytes alone do NOT prove free space in an arbitrary XBE. The
    public CLI is restricted to the known 5960 image and its reviewed trailing
    embedded-PE header padding at 0x1ffc.
    """
    image = Xbe(data)
    section = image.named("XIPS")
    table_records(table)
    if section.flags != 0xA:
        raise ValueError("unsupported XIPS section flags")
    if (reference_offset & 1 or reference_offset < image.table_end
            or reference_offset + 4 > image.headers or any(data[reference_offset:reference_offset + 4])):
        raise ValueError("invalid shared-page reference reservation")
    cells = {image.base + reference_offset, image.base + reference_offset + 2}
    if any(cells.intersection((s.head, s.tail)) for s in image.sections):
        raise ValueError("new reference cells already belong to another section")
    align = lambda value: (value + PAGE - 1) & -PAGE
    va = align(max(image.base + image.size, *(s.va + s.size for s in image.sections)))
    size = align(len(table))
    raw = align(len(data))
    if va + size > 0x80000000 or raw + len(table) > 0xFFFFFFFF:
        raise ValueError("relocated XIPS exceeds supported image extent")
    result = bytearray(data)
    result.extend(bytes(raw - len(result)))
    result.extend(table)
    # The head/tail counters must be exclusive: retaining the old shared cells
    # would make XeLoadSection/UnloadSection account for unrelated old pages.
    struct.pack_into("<IIII", result, section.header + 4, va, size, raw, len(table))
    struct.pack_into("<II", result, section.header + 28, *sorted(cells))
    result[section.header + 36:section.header + 56] = digest(table)
    struct.pack_into("<I", result, 0x10C, va + size - image.base)
    checked = Xbe(bytes(result))
    relocated = checked.named("XIPS")
    if any(s != checked.named(s.name) for s in image.sections if s.name != "XIPS"):
        raise ValueError("unrelated section metadata changed")
    if result[relocated.raw:relocated.raw + relocated.length] != table:
        raise ValueError("relocated XIPS readback differs")
    return bytes(result)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--mod", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    data = args.xbe.read_bytes()
    if hashlib.sha256(data).hexdigest() != XBE_SHA256:
        parser.error("XBE differs from the reviewed 5960 input")
    image = Xbe(data)
    section = image.named("XIPS")
    inputs = [args.xbe.resolve()]
    changes = {}
    archives = []
    for name, expected in ARCHIVES.items():
        original_path, modified_path = args.assets / name, args.mod / name
        original, modified = original_path.read_bytes(), modified_path.read_bytes()
        if hashlib.sha256(original).hexdigest() != expected:
            parser.error(f"original {name} differs from reviewed input")
        inputs += [original_path.resolve(), modified_path.resolve()]
        changes[name] = (original, modified)
        archives.append(dict(name=name, source_sha256=expected,
                             sha256=hashlib.sha256(modified).hexdigest(), bytes=len(modified)))
    destinations = [args.output.resolve(), args.manifest.resolve()]
    if len(set(destinations)) != 2 or any(p in inputs or p.exists() for p in destinations):
        parser.error("outputs must be distinct new paths, separate from all inputs")
    table = replace_records(data[section.raw:section.raw + section.length], changes)
    updated = relocate_table(data, table, 0x1FFC)
    relocated = Xbe(updated).named("XIPS")
    receipt = dict(schema="xboxllr.dashboard.private-integrity-mod.v1", private_guest_output=True,
                   source_xbe_sha256=XBE_SHA256, sha256=hashlib.sha256(updated).hexdigest(),
                   bytes=len(updated), archives=archives, xips_bytes=len(table),
                   xips_va=hex(relocated.va), xips_raw=hex(relocated.raw),
                   hash_algorithm="SHA1(le32(actual byte length) + bytes)",
                   original_section_checks_preserved=True, original_xbe_signature_valid=False,
                   validation="Offline digest/layout readback; retail boot and UI unverified")
    for destination, payload in ((args.output, updated),
                                 (args.manifest, (json.dumps(receipt, indent=2) + "\n").encode())):
        destination.parent.mkdir(parents=True, exist_ok=True)
        with destination.open("xb") as output:
            output.write(payload)
        if destination.read_bytes() != payload:
            raise ValueError("output readback differs")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
