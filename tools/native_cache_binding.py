#!/usr/bin/env python3
"""Bind a reviewed complete XMountUtilityDrive and its existing error helper."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from native_binding import Binding

def bind(xbe: Path, output: Path, symbols: Path, profile=None):
    context = Binding(xbe, output, symbols)
    proof = context.require_profile('cache', profile, adapter='xapi-utility-cache-v1')
    if proof['body'].get('stack_bytes') != 4 or proof['error_helper'].get('stack_bytes') != 4:
        raise ValueError('unsupported utility cache ABI')
    function = context.symbol("XMountUtilityDrive", 4)
    error = context.symbol("XapiSetLastNTError", 4)
    digest = context.digest(function)
    if not context.matches_body(function, proof['body']):
        raise ValueError("unreviewed cache boundary/image")
    if not context.matches_body(error, proof['error_helper']):
        raise ValueError("unreviewed LastError helper")
    if error["start"] not in function["calls_to"]:
        raise ValueError("cache boundary does not call identified LastError helper")
    binding = dict(xbe_sha256=context.image["sha256"], function_sha256=digest,
                   address=function["start"], symbol=function["name"], api="XMountUtilityDrive",
                   arguments=["BOOL bFormatClean"], result="EAX BOOL; thread LastError on failure",
                   stack_pop=8, preserved=["EBX", "ESI", "EDI", "EBP"],
                   error_helper=dict(symbol=error["name"], sha256=context.digest(error)),
                   policy="persistent native cache slots and Z: namespace; no raw disk sectors")
    wrapper = ('#include "recomp_types.h"\n'
               'extern void pilot_mount_utility_original(void);\n'
               'extern void pilot_mount_utility_drive(recomp_func_t);\n'
               f'extern void {error["name"]}(void);\n'
               f'void {function["name"]}(void) {{\n'
               '#ifdef PILOT_CACHE_NATIVE\n'
               f'    pilot_mount_utility_drive({error["name"]});\n'
               '#else\n'
               '    pilot_mount_utility_original();\n'
               '#endif\n}\n')
    return context.emit("cache", function, "pilot_mount_utility_original", binding, wrapper)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xbe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--symbols", type=Path, required=True)
    parser.add_argument("--profile", type=Path)
    args = parser.parse_args()
    print(json.dumps(bind(args.xbe, args.output, args.symbols, args.profile), indent=2))
