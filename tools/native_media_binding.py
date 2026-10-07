#!/usr/bin/env python3
"""Bind a reviewed XAPI media boundary; never guess an address in a new image.

Only hashes/metadata are committed. The original routine remains generated in
the private build, selected by PILOT_MEDIA=OPTICAL. No proprietary bytes here.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
from aot_inventory import parse_xbe

# Reviewed complete 4831 helper: no args, EAX=NTSTATUS, RET 0, callee saves,
# stack-local IO packet, closes its handle; no escaped guest state. Matching
# byte hashes are revision-specific bindings, not a general XDK signature.
REVIEWED = {("8879c24c18ccf32c675c2c1e3130c4c8e3007d1e2d70b388d3632d5a463a2f24",
             "e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249")}


def bind(xbe: Path, output: Path, symbols: Path):
    blob = xbe.read_bytes()
    image = parse_xbe(blob)
    manifest_path = output / "generation.aot-private.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest["sha256"] != image["sha256"]:
        raise ValueError("generated code belongs to a different XBE")
    generated = output / "gen"
    for name, digest in manifest["files"].items():
        if hashlib.sha256((generated / name).read_bytes()).hexdigest() != digest:
            raise ValueError(f"generated artifact changed: {name}")
    functions = json.loads((output / "disasm/functions.json").read_text())
    parents = {int(s["address"]) for s in json.loads(symbols.read_text())["symbols"]
               if s["name"] == "XapiInitProcess" and s["library"] == "XAPILIB"}
    matches = []
    for function in functions:
        start, end = int(function["start"], 16), int(function["end"], 16)
        if end - start != function["size"]:
            raise ValueError("inconsistent function extent")
        for section in image["sections"]:
            if section["address"] <= start < end <= section["address"] + section["raw_size"]:
                offset = section["raw_offset"] + start - section["address"]
                digest = hashlib.sha256(blob[offset:offset + end - start]).hexdigest()
                if (digest, image["sha256"]) in REVIEWED:
                    callers = {int(v, 16) for v in function["called_by"]}
                    if not callers.intersection(parents):
                        raise ValueError("reviewed media helper has no identified XapiInitProcess caller")
                    matches.append((function, digest))
    if len(matches) != 1:
        raise ValueError(f"expected one reviewed media boundary, found {len(matches)}")
    function, digest = matches[0]
    name = function["name"]
    if not re.fullmatch(r"sub_[0-9A-Fa-f]{8}", name):
        raise ValueError("invalid generated function identifier")
    binding = dict(xbe_sha256=image["sha256"], function_sha256=digest,
                   address=function["start"], symbol=name, caller="XapiInitProcess",
                   arguments=[], result="EAX NTSTATUS", stack_pop=4,
                   preserved=["EBX", "ESI", "EDI", "EBP"],
                   policy="installed title identity/readability; no optical authentication assertion")
    wrapper = generated / "pilot_media_binding.c"
    if wrapper.exists():
        if json.loads((output / "media-binding.aot-private.json").read_text()) != binding:
            raise ValueError("existing binding differs")
        return binding
    needle = f"void {name}(void)\n{{"
    found = [(p, p.read_text(encoding="utf-8")) for p in generated.glob("recomp_*.c")]
    found = [(p, code) for p, code in found if needle in code]
    if len(found) != 1 or found[0][1].count(needle) != 1:
        raise ValueError("expected exactly one generated function body")
    path, code = found[0]
    path.write_text(code.replace(needle, "void pilot_optical_verify_original(void)\n{", 1), encoding="utf-8")
    wrapper.write_text(
        '#include "recomp_types.h"\n'
        'extern void pilot_optical_verify_original(void);\n'
        'extern void pilot_verify_installed_media(void);\n'
        f'void {name}(void) {{\n'
        '#ifdef PILOT_MEDIA_INSTALLED\n'
        '    pilot_verify_installed_media();\n'
        '#else\n'
        '    pilot_optical_verify_original();\n'
        '#endif\n}\n', encoding="utf-8")
    (output / "media-binding.aot-private.json").write_text(json.dumps(binding, indent=2) + "\n")
    manifest["media_binding"] = binding
    manifest["files"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted(generated.iterdir()) if p.is_file()}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    return binding


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--symbols", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(bind(args.xbe, args.output, args.symbols), indent=2))
