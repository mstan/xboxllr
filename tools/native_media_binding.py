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
from native_binding import Binding

# Reviewed complete 4831 helper: no args, EAX=NTSTATUS, RET 0, callee saves,
# stack-local IO packet, closes its handle; no escaped guest state. Matching
# byte hashes are revision-specific bindings, not a general XDK signature.
REVIEWED = {("8879c24c18ccf32c675c2c1e3130c4c8e3007d1e2d70b388d3632d5a463a2f24",
             "e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249")}


def bind(xbe: Path, output: Path, symbols: Path):
    context = Binding(xbe, output, symbols)
    parents = {int(s["address"]) for s in context.symbols
               if s["name"] == "XapiInitProcess" and s["library"] == "XAPILIB"}
    matches = []
    for function in context.functions:
        digest = context.digest(function)
        if (digest, context.image["sha256"]) in REVIEWED:
            callers = {int(v, 16) for v in function["called_by"]}
            if not callers.intersection(parents):
                raise ValueError("reviewed media helper has no identified XapiInitProcess caller")
            matches.append((function, digest))
    if len(matches) != 1:
        raise ValueError(f"expected one reviewed media boundary, found {len(matches)}")
    function, digest = matches[0]
    name = function["name"]
    binding = dict(xbe_sha256=context.image["sha256"], function_sha256=digest,
                   address=function["start"], symbol=name, caller="XapiInitProcess",
                   arguments=[], result="EAX NTSTATUS", stack_pop=4,
                   preserved=["EBX", "ESI", "EDI", "EBP"],
                   policy="installed title identity/readability; no optical authentication assertion")
    wrapper = (
        '#include "recomp_types.h"\n'
        'extern void pilot_optical_verify_original(void);\n'
        'extern void pilot_verify_installed_media(void);\n'
        f'void {name}(void) {{\n'
        '#ifdef PILOT_MEDIA_INSTALLED\n'
        '    pilot_verify_installed_media();\n'
        '#else\n'
        '    pilot_optical_verify_original();\n'
        '#endif\n}\n')
    result = context.emit("media", function, "pilot_optical_verify_original", binding, wrapper)
    # The original CPU selects the same complete reviewed helper as generated
    # callers. This is factual boundary metadata, never replacement x86 bytes.
    header = context.generated / "pilot_media_cpu_binding.h"
    metadata = (
        '/* Reviewed complete installed-media selection for the original CPU. */\n'
        f'#define PILOT_MEDIA_CPU_ENTRY {function["start"]}u\n'
        f'#define PILOT_MEDIA_CPU_BYTES {function["size"]}u\n'
        f'#define PILOT_MEDIA_CPU_SHA256 "{digest}"\n')
    if header.exists() and header.read_text() != metadata:
        raise ValueError("existing original-CPU media metadata differs")
    header.write_text(metadata, encoding="utf-8", newline="\n")
    manifest = json.loads(context.manifest_path.read_text())
    manifest["files"][header.name] = hashlib.sha256(header.read_bytes()).hexdigest()
    manifest["media_cpu_binding"] = dict(address=function["start"], bytes=function["size"],
        function_sha256=digest, arguments=[], result="EAX NTSTATUS", stack_pop=4)
    context.manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--symbols", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(bind(args.xbe, args.output, args.symbols), indent=2))
