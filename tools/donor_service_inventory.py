#!/usr/bin/env python3
"""Inventory donor source/binding opportunities; never declares API correctness.

Only identifiers and source locations are recorded, not title bytes/addresses.
HLE markers are discovered in source, so absence means no named HLE layer in
that tree, not absence of a native renderer or other backend.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

GIT = "C:/Program Files/Git/cmd/git.exe"
EXPORT = re.compile(r"^[ \t]*HLE_EXPORT\(\s*([A-Za-z_]\w*)\s*\)", re.M)
ORIGINAL = re.compile(r"^[ \t]*HLE_ORIGINAL\(\s*([A-Za-z_]\w*)\s*\)", re.M)
FAMILIES = {
    "renderer_backend": "src/d3d", "graphics_adapter": "src/hle/hle_d3d8*.c",
    "audio_adapter": "src/hle/hle_dsound*.c", "audio_transport": "src/hle/audio_output*",
    "legacy_audio": "src/audio", "movies": "src/video", "movie_adapter": "src/hle/hle_xmv*.c",
    "input": "src/input", "input_adapter": "src/hle/hle_input.c",
    "kernel": "src/kernel", "apu": "src/apu", "nv2a": "src/nv2a",
    "binding_and_cpu": "tools/recomp",
}


def git(root, *args):
    return subprocess.check_output([GIT, "-C", str(root), *args], text=True).strip()


def inventory(name, root, symbols):
    if git(root, "status", "--porcelain", "--untracked-files=no"):
        raise ValueError(f"{name}: tracked donor edits would invalidate the pin")
    families = {}
    for family, pattern in FAMILIES.items():
        paths = []
        for candidate in root.glob(pattern):
            paths.extend(candidate.rglob("*") if candidate.is_dir() else [candidate])
        sources = sorted(p for p in paths if p.is_file() and p.suffix in (".c", ".h", ".cpp", ".py"))
        families[family] = {"source_files": len(sources), "source_bytes": sum(p.stat().st_size for p in sources)}
    entries = []
    for path in sorted((root / "src/hle").glob("*.c")):
        code = path.read_text(encoding="utf-8")
        originals = set(ORIGINAL.findall(code))
        for match in EXPORT.finditer(code):
            symbol = match[1]
            entries.append({"name": symbol, "source": path.relative_to(root).as_posix(),
                            "line": code.count("\n", 0, match.start()) + 1,
                            "original_body_declared_in_file": symbol in originals,
                            "blinx_exact_name_found": symbol in symbols})
    return {"name": name, "commit": git(root, "rev-parse", "HEAD"),
            "tree": git(root, "rev-parse", "HEAD^{tree}"), "families": families,
            "hle_markers": len(entries), "blinx_exact_name_matches": sum(e["blinx_exact_name_found"] for e in entries),
            "entries": entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--donor", action="append", required=True, help="NAME=PATH")
    parser.add_argument("--symbols", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    raw = args.symbols.read_bytes()
    symbols = {s["name"] for s in json.loads(raw)["symbols"] if s.get("kind") == "function"}
    donors = [inventory(name, Path(path), symbols) for name, path in (item.split("=", 1) for item in args.donor)]
    result = {"schema": 1, "symbols_sha256": hashlib.sha256(raw).hexdigest(),
              "scope": "Static source and exact-name inventory, not reachability, ABI compatibility, semantic completeness, or a percentage of game support.",
              "donors": donors}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps([{k: d[k] for k in ("name", "commit", "hle_markers", "blinx_exact_name_matches")} for d in donors]))


if __name__ == "__main__":
    main()
