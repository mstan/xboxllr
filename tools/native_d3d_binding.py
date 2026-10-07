#!/usr/bin/env python3
"""Bind the reviewed original NTSC graphics family; unknown methods stop.

Only the qualified subset is dispatched natively. Every identified D3D8 body
retains an ORIGINAL build selection, avoiding accidental guest-driver fallback
after the native device has been created. Revision data stays in private output.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
from native_binding import Binding

IMAGE = "e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249"
CREATE = "4111b2a1ca25c2bf8e4cc1cad05ab2142ec17d48b54f2f7457951977d5bce564"


def bind(xbe: Path, output: Path, symbols: Path):
    context=Binding(xbe,output,symbols)
    if context.image["sha256"] != IMAGE:
        raise ValueError("unreviewed graphics image")
    functions={int(f["start"],16):f for f in context.functions}
    identified=[s for s in context.symbols if s["library"]=="D3D8" and s["kind"]=="function"]
    variables={s["name"]:s["address"] for s in context.symbols if s["library"]=="D3D8" and s["kind"]=="variable"}
    create=next(s for s in identified if s["name"]=="Direct3D_CreateDevice")
    if create["stack_bytes"]!=24 or context.digest(functions[create["address"]])!=CREATE:
        raise ValueError("unreviewed graphics creation boundary")
    # Original complete routine publishes a statically allocated CDevice using
    # MOV EDI,imm / MOV [g_pDevice],EDI. Validate these decoded references rather
    # than embedding their values in a reusable service or guessing a layout.
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    f=functions[create["address"]]
    section=next(s for s in context.image["sections"] if s["address"]<=create["address"]<s["address"]+s["raw_size"])
    offset=section["raw_offset"]+create["address"]-section["address"]
    instructions=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(context.blob[offset:offset+f["size"]],create["address"]))
    devices=[int(i.op_str.split(", ")[1],0) for i in instructions if i.mnemonic=="mov" and i.op_str.startswith("edi, 0x")]
    if len(devices)!=1 or not any(i.mnemonic=="mov" and i.op_str==f'dword ptr [0x{variables["D3D_g_pDevice"]:x}], edi' for i in instructions):
        raise ValueError("unreviewed device/global relationship")
    calls=[]
    for symbol in identified:
        if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*",symbol["name"]): raise ValueError("invalid symbol")
        original=functions[symbol["address"]]
        calls.append(dict(name=symbol["name"],symbol=original["name"],address=original["start"],
                          function_sha256=context.digest(original),call=symbol["call"],
                          stack_bytes=symbol["stack_bytes"],params=symbol["params"]))
    binding=dict(xbe_sha256=IMAGE,device_global=variables["D3D_g_pDevice"],device_object=devices[0],
                 vertex_shader_offset=variables["D3DDevice__m_VertexShader_OFFSET"],calls=calls)
    header=['#pragma once', '#include <stdint.h>', 'enum PilotD3DEntry {']
    header += [f'    PILOT_D3D_{c["name"]},' for c in calls]
    header += ['    PILOT_D3D_COUNT', '};', 'static const struct { const char *name; uint32_t address, stack_bytes; } pilot_d3d_calls[] = {']
    header += [f'    {{"{c["name"]}", {c["address"]}u, {c["stack_bytes"]}u}},' for c in calls]
    header += ['};',f'#define PILOT_D3D_DEVICE_GLOBAL 0x{binding["device_global"]:x}u',
               f'#define PILOT_D3D_DEVICE_OBJECT 0x{binding["device_object"]:x}u',
               f'#define PILOT_D3D_VERTEX_SHADER_OFFSET {binding["vertex_shader_offset"]}u']
    wrapper=['#include "pilot_d3d_bindings.h"', 'extern void pilot_d3d_dispatch(unsigned);']
    for c in calls:
        wrapper += [f'extern void pilot_original_d3d_{c["name"]}(void);',f'void {c["symbol"]}(void) {{',
                    '#ifdef PILOT_D3D_NATIVE', f'    pilot_d3d_dispatch(PILOT_D3D_{c["name"]});',
                    '#else', f'    pilot_original_d3d_{c["name"]}();', '#endif', '}']
    generated=context.generated; record=output/"d3d-binding.aot-private.json"
    texts={"pilot_d3d_bindings.h":"\n".join(header)+"\n", "pilot_d3d_binding.c":"\n".join(wrapper)+"\n"}
    if (generated/"pilot_d3d_binding.c").exists():
        if json.loads(record.read_text())!=binding or any((generated/n).read_text()!=t for n,t in texts.items()):
            raise ValueError("existing graphics binding differs")
        return binding
    replacements={c["symbol"]:f'pilot_original_d3d_{c["name"]}' for c in calls}
    found=set(); edits={}
    pattern=re.compile(r'void (sub_[0-9A-Fa-f]{8})\(void\)\n\{')
    def rename(match):
        name=match[1]
        if name not in replacements: return match[0]
        if name in found: raise ValueError("duplicate graphics body")
        found.add(name); return f'void {replacements[name]}(void)\n{{'
    for path in generated.glob("recomp_*.c"):
        original=path.read_text(encoding="utf-8"); updated=pattern.sub(rename,original)
        if original!=updated: edits[path]=updated
    if found!=set(replacements): raise ValueError("missing graphics bodies")
    for path,code in edits.items(): path.write_text(code,encoding="utf-8",newline="\n")
    for name,code in texts.items(): (generated/name).write_text(code,encoding="utf-8",newline="\n")
    record.write_text(json.dumps(binding,indent=2)+"\n",newline="\n")
    context.manifest["d3d_binding"]=binding
    context.manifest["files"]={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(generated.iterdir()) if p.is_file()}
    context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+"\n",newline="\n")
    return binding


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xbe",type=Path,required=True); parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--symbols",type=Path,required=True); args=parser.parse_args()
    result=bind(args.xbe,args.output,args.symbols)
    print(json.dumps(dict(xbe_sha256=result["xbe_sha256"],identified=len(result["calls"]),device_global=result["device_global"],device_object=result["device_object"])))
