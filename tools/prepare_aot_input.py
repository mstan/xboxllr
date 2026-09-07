#!/usr/bin/env python3
"""Extract explicitly selected first-executed blocks into a private AOT input.

The blockpack and generated JSON contain guest code. Keep them private. A captured
block is a candidate identity, not a promise that those bytes remain live later.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct


def read_blockpack(data: bytes) -> dict[tuple[int, int], bytes]:
    if data[:8] != b"XLLEBC1\0":
        raise ValueError("unsupported blockpack identity")
    result = {}
    offset = 8
    while offset < len(data):
        if len(data) - offset < 8:
            raise ValueError("truncated block header")
        pc, size = struct.unpack_from("<II", data, offset)
        offset += 8
        if not 1 <= size <= 65536 or pc + size > 0xffffffff:
            raise ValueError("block outside supported IA-32 region bounds")
        if size > len(data) - offset:
            raise ValueError("truncated block bytes")
        if (pc, size) in result:
            raise ValueError("duplicate block identity")
        result[pc, size] = data[offset:offset + size]
        offset += size
    return result


def prepare(source: Path, output: Path, selected: list[tuple[int, int]],
            expected_sha: str | None = None) -> dict:
    if not output.name.endswith(".aot-private.json"):
        raise ValueError("output must end in .aot-private.json so publication guards reject it")
    if source.resolve() == output.resolve():
        raise ValueError("source and output must be different files")
    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if expected_sha is not None and digest != expected_sha.lower():
        raise ValueError("blockpack SHA-256 differs from expected identity")
    blocks = read_blockpack(data)
    if not selected or len(set(selected)) != len(selected):
        raise ValueError("select distinct blocks explicitly")
    ordered = sorted(selected)
    for (pc, size), (next_pc, _) in zip(ordered, ordered[1:]):
        if pc + size > next_pc:
            raise ValueError("selected blocks overlap; choose an unambiguous region")
    regions = []
    for pc, size in ordered:
        code = blocks.get((pc, size))
        if code is None:
            raise ValueError(f"no first-executed bytes for block {pc:08x}:{size}")
        regions.append({"name": f"guest_{pc:08x}", "base": pc,
                        "entries": [0], "code": code.hex()})
    document = {"private_guest_input": True, "source_format": "xboxlle.blockpack.v1",
                "source_sha256": digest, "regions": regions}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    return document


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("blockpack", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--block", action="append", required=True,
                        help="explicit virtual-address:byte-size; both accept 0x notation")
    parser.add_argument("--expected-sha256")
    args = parser.parse_args()
    try:
        selected = [tuple(int(value, 0) for value in item.split(":")) for item in args.block]
        if any(len(item) != 2 for item in selected):
            raise ValueError("--block needs address:size")
        document = prepare(args.blockpack, args.output, selected, args.expected_sha256)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"private regions={len(document['regions'])} source_sha256={document['source_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
