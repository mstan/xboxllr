#!/usr/bin/env python3
"""Bind a reviewed complete XMountUtilityDrive and its existing error helper."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from native_binding import Binding

REVIEWED = {("36718604554b7c1ad0cbdf1acf611dd46124ce447d3e425b295feb90ec74809f",
             "e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249")}


def bind(xbe: Path, output: Path, symbols: Path):
    context = Binding(xbe, output, symbols)
    function = context.symbol("XMountUtilityDrive", 4)
    error = context.symbol("XapiSetLastNTError", 4)
    digest = context.digest(function)
    if (digest, context.image["sha256"]) not in REVIEWED:
        raise ValueError("unreviewed cache boundary/image")
    if context.digest(error) != "6931e553a99293e195882b3341c126d12dbff5ff994c05a967fe338d86c0b5b2":
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
    args = parser.parse_args()
    print(json.dumps(bind(args.xbe, args.output, args.symbols), indent=2))
