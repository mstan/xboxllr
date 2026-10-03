#!/usr/bin/env python3
"""Prepare explicitly selected XBE spans for private offline compilation."""
import argparse
import json
from pathlib import Path

from aot_inventory import parse_xbe, sha
from aot_compile import parse_region


def prepare(source, output, selected, expected_sha):
    if output.resolve() == source.resolve() or not output.name.endswith(".aot-private.json"):
        raise ValueError("use a distinct output with the guarded .aot-private.json suffix")
    data = source.read_bytes()
    if sha(data) != expected_sha.lower():
        raise ValueError("XBE SHA-256 differs from expected identity")
    image = parse_xbe(data)
    regions = []
    if not selected or len(set(selected)) != len(selected):
        raise ValueError("select distinct regions explicitly")
    end = 0
    for pc, size in sorted(selected):
        if not 0 < size <= 65536 or pc < end:
            raise ValueError("invalid or overlapping selected regions")
        section = next((s for s in image["sections"] if s["executable_candidate"] and
                        s["address"] <= pc and pc + size <= s["address"] + s["raw_size"]), None)
        if section is None:
            raise ValueError("region must be backed by one executable candidate section")
        offset = section["raw_offset"] + pc - section["address"]
        raw = dict(name=f"guest_{pc:08x}", base=pc, entries=[0], code=data[offset:offset + size].hex())
        parse_region(raw)  # Reject malformed reachable boundaries before writing.
        regions.append(raw)
        end = pc + size
    result = dict(private_guest_input=True, source_format="xboxlle.xbe.v1",
                  source_sha256=sha(data), regions=regions)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--region", action="append", required=True, help="address:size (0x notation accepted)")
    args = parser.parse_args()
    try:
        selected = [tuple(int(v, 0) for v in item.split(":")) for item in args.region]
        if any(len(s) != 2 for s in selected):
            raise ValueError("--region needs address:size")
        result = prepare(args.image, args.output, selected, args.expected_sha256)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"private regions={len(result['regions'])} source_sha256={result['source_sha256']}")


if __name__ == "__main__":
    main()
