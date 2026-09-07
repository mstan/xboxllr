#!/usr/bin/env python3
"""Validate and rank the optional executed-block census; never infer wall time."""
from __future__ import annotations

import argparse
import csv
import io
import json
from pathlib import Path


def load_census(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    first, _, table = text.partition("\n")
    if not first.startswith("# schema=xboxlle.block-census.v1 "):
        raise ValueError("unsupported or missing block census schema")
    raw = dict(item.split("=", 1) for item in first[2:].split())
    meta = {key: int(raw[key]) for key in
            ("total_insns", "fallback_insns", "idle_skipped", "native_insns", "overflow")}
    if any(value < 0 for value in meta.values()) or meta["overflow"]:
        raise ValueError("negative counters or overflow: census is incomplete")
    rows = []
    seen = set()
    for raw_row in csv.DictReader(io.StringIO(table)):
        row = {key: int(value, 0) for key, value in raw_row.items()}
        if set(row) != {"pc", "size", "entries", "charged_insns", "unretired_insns"}:
            raise ValueError("unexpected census columns")
        if (any(value < 0 for value in row.values()) or not row["size"] or
                row["pc"] + row["size"] > 1 << 32 or
                row["unretired_insns"] > row["charged_insns"]):
            raise ValueError("invalid block or counter range")
        key = row["pc"], row["size"]
        if key in seen:
            raise ValueError("duplicate block identity")
        seen.add(key)
        row["retired_insns"] = row["charged_insns"] - row["unretired_insns"]
        rows.append(row)
    if sum(r["retired_insns"] for r in rows) != meta["fallback_insns"]:
        raise ValueError("block retirement sum does not match fallback clock")
    if sum(meta[k] for k in ("fallback_insns", "idle_skipped", "native_insns")) != meta["total_insns"]:
        raise ValueError("fallback/idle/native partition does not match machine clock")
    rows.sort(key=lambda r: (-r["retired_insns"], r["pc"], r["size"]))
    return {"schema": "xboxlle.block-census-summary.v1", "counters": meta, "blocks": rows}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("census", type=Path)
    parser.add_argument("--top", type=int, default=30)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    if not 1 <= args.top <= 1000:
        parser.error("--top must be in 1..1000")
    try:
        result = load_census(args.census)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    meta = result["counters"]
    print(json.dumps(meta, sort_keys=True))
    print("pc          bytes       entries       retired   total% fallback%")
    for row in result["blocks"][:args.top]:
        retired = row["retired_insns"]
        total_pct = 100 * retired / meta["total_insns"] if meta["total_insns"] else 0
        floor_pct = 100 * retired / meta["fallback_insns"] if meta["fallback_insns"] else 0
        print(f"{row['pc']:08x} {row['size']:8d} {row['entries']:13d} {retired:13d} "
              f"{total_pct:8.3f} {floor_pct:9.3f}")
    print(f"blocks={len(result['blocks'])}; counts aggregate code versions at the same pc/size; "
          "this is instruction attribution, not wall-time attribution")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
