#!/usr/bin/env python3
"""Prepare a private, hash-bound native title pilot from pinned Phobos tools.

Only generated CPU code and its register-model header are used at this stage.
The donor's kernel, acknowledgement threads and success stubs are not linked.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

from aot_inventory import parse_xbe

DONOR_PIN = "63f18f0334fbd8c00313c4bb565accf00627a4fd"
ROOT = Path(__file__).resolve().parent.parent


def harden_generated(directory):
    """Replace donor missing-body success with nonreturning unsupported faults."""
    counts = dict(missing_targets=0, failed_translations=0)
    for path in sorted(directory.glob("*.c")):
        code = path.read_text(encoding="utf-8")
        code, count = re.subn(
            r'void (\w+)\(void\) \{ recomp_stub_missing\((0x[0-9A-F]+u)\);[^\n]*\}',
            r'void \1(void) { RECOMP_UNIMPL("undiscovered direct target", \2); }', code)
        counts["missing_targets"] += count
        code, count = re.subn(
            r'/\* FAILED: (\w+) at (0x[0-9A-F]+) \*/\s*'
            r'void \1\(void\) \{ /\* translation failed \*/ \}',
            r'void \1(void) { RECOMP_UNIMPL("translation failed", \2u); }', code)
        counts["failed_translations"] += count
        if "stubs_unresolved" in path.name:
            # Keep only definitions and their include, not the obsolete
            # upstream comments advertising fabricated stack cleanup.
            code = '#include "recomp_funcs.h"\n' + "\n".join(
                line for line in code.splitlines() if line.startswith("void ")) + "\n"
            destination = path.with_name("recomp_unsupported_targets.c")
            destination.write_text(code, encoding="utf-8")
            path.unlink()
        else:
            path.write_text(code, encoding="utf-8")
        if re.search(r"translation failed \*/|recomp_stub_missing\(|/\*[^\n]*unhandled[^\n]*\*/", code):
            raise ValueError(f"unhandled donor lowering in {path.name}")
    # Diagnostic rings must be per-thread along with the CPU registers. The
    # donor's shared volatile counters race when bootstrap and workers overlap.
    types = directory / "recomp_types.h"
    header = types.read_text(encoding="utf-8")
    header = re.sub(r"extern volatile (uint(?:32|64)_t g_icall_(?:trace(?:_idx)?|count)\b)",
                    r"extern RECOMP_TLS volatile \1", header)
    types.write_text(header, encoding="utf-8")
    return counts


def prepare(args):
    donor = args.donor.resolve()
    git = "C:/Program Files/Git/cmd/git.exe" if sys.platform == "win32" else "git"
    pin = subprocess.check_output([git, "-C", str(donor), "rev-parse", "HEAD"], text=True).strip()
    if pin != DONOR_PIN:
        raise ValueError(f"expected donor {DONOR_PIN}, got {pin}")
    subprocess.run([git, "-C", str(donor), "diff", "--exit-code", "HEAD", "--",
                    "tools", "templates/runtime"], check=True, stdout=subprocess.DEVNULL)
    image = parse_xbe(args.xbe.read_bytes())
    out = args.output.resolve()
    # Title-derived generated C contains proprietary code; keep it out of Git.
    if "build" not in out.parts:
        raise ValueError("pilot output must be under a private build/ directory")
    out.mkdir(parents=True, exist_ok=True)
    identity = out / "identity.aot-private.json"
    if identity.exists() and json.loads(identity.read_text())["sha256"] != image["sha256"]:
        raise ValueError("output belongs to a different XBE; use a separate directory")
    (out / "gen" / "pilot_image.h").unlink(missing_ok=True)
    (out / "gen" / "pilot_media_binding.c").unlink(missing_ok=True)
    (out / "gen" / "pilot_cache_binding.c").unlink(missing_ok=True)
    (out / "gen" / "pilot_d3d_binding.c").unlink(missing_ok=True)
    (out / "gen" / "pilot_d3d_bindings.h").unlink(missing_ok=True)
    (out / "gen" / "pilot_jump_bindings.h").unlink(missing_ok=True)
    (out / "gen" / "pilot_cpu_binding.c").unlink(missing_ok=True)
    identity.write_text(json.dumps(image, indent=2) + "\n")

    def run(name, argv):
        print(name, flush=True)
        with (out / (name + ".log")).open("w", encoding="utf-8") as log:
            subprocess.run([sys.executable, *argv], cwd=donor, stdout=log,
                           stderr=subprocess.STDOUT, check=True)

    xbe = str(args.xbe.resolve())
    if not args.reuse_discovery:
        cmd = ["-m", "tools.disasm", xbe, "-o", str(out / "disasm")]
        if args.data_sections:
            cmd += ["--data-sections", args.data_sections]
        run("discover", cmd)
    extents=out/'function-extents.aot-private.json'
    extent_records=[]
    if extents.exists():
        from native_function_extent import apply_extents
        record=json.loads(extents.read_text())
        if record['sha256']!=image['sha256']: raise ValueError('function extents belong to a different image')
        extent_records=record['extents'];functions_path=out/'disasm/functions.json'
        updated=apply_extents(args.xbe.read_bytes(),image,json.loads(functions_path.read_text()),extent_records)
        functions_path.write_text(json.dumps(updated,indent=2)+'\n',newline='\n')
    run("parse", ["-m", "tools.xbe_parser", xbe, "--json", str(out / "analysis.aot-private.json")])
    run("lift", ["-m", "tools.recomp", xbe, "--all", "--split", "250",
                 "--disasm-dir", str(out / "disasm"), "--func-id-dir", str(out / "classification"),
                 "--abi-dir", str(out / "abi"), "-o", str(out / "lift"),
                 "--hle-impl", str(ROOT / "pilots/native-title/runtime.c"),
                 "--gen-dir", str(out / "gen"), "--trace-all-entries"])
    counts = harden_generated(out / "gen")
    analysis = json.loads((out / "analysis.aot-private.json").read_text())
    imports = analysis["kernel_imports"]
    header = ["/* Private input identity, regenerated from the owner's XBE. */",
              f'#define PILOT_XBE_SHA256 "{image["sha256"]}"',
              f'#define PILOT_ENTRY 0x{image["entry"]:08x}u',
              'static const struct { unsigned address, ordinal; const char *name; } pilot_imports[] = {']
    header += [f'{{ {i["thunk_addr"]}u, {i["ordinal"]}u, "{i["name"]}" }},' for i in imports]
    header += ["};"]
    (out / "gen" / "pilot_image.h").write_text("\n".join(header) + "\n")
    manifest = dict(donor=pin, sha256=image["sha256"], entry=image["entry"],
                    function_extents=extent_records,
                    data_sections=args.data_sections, **counts,
                    files={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                           for p in sorted((out / "gen").iterdir()) if p.is_file()})
    (out / "generation.aot-private.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if args.media_symbols:
        from native_media_binding import bind
        bind(args.xbe, out, args.media_symbols)
    if args.cache_symbols:
        from native_cache_binding import bind
        bind(args.xbe, out, args.cache_symbols)
    if args.d3d_symbols:
        from native_d3d_binding import bind
        bind(args.xbe, out, args.d3d_symbols)
    if args.jump_symbols:
        from native_jump_binding import bind
        bind(args.xbe, out, args.jump_symbols)
    if args.cpu_symbols:
        from native_cpu_binding import bind
        bind(args.xbe, out, args.cpu_symbols)
    if args.input_symbols:
        from native_input_binding import bind
        bind(args.xbe, out, args.input_symbols)
    print(json.dumps({k: v for k, v in manifest.items() if k != "files"}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--donor", type=Path, required=True)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--data-sections", default="")
    parser.add_argument("--reuse-discovery", action="store_true")
    parser.add_argument("--media-symbols", type=Path, help="Bind a reviewed media helper using this XDK symbol inventory")
    parser.add_argument("--cache-symbols", type=Path, help="Bind reviewed utility-drive service using this XDK symbol inventory")
    parser.add_argument("--d3d-symbols", type=Path, help="Bind reviewed graphics family using this XDK symbol inventory")
    parser.add_argument("--jump-symbols", type=Path, help="Qualify original CRT nonlocal jumps against this image inventory")
    parser.add_argument("--cpu-symbols", type=Path, help="Bind the reviewed CPU capability probe")
    parser.add_argument("--input-symbols", type=Path, help="Bind the reviewed whole XAPI input family")
    prepare(parser.parse_args())
