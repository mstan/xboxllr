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
import struct
from native_binding import Binding

IMAGE = "e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249"
CREATE = "4111b2a1ca25c2bf8e4cc1cad05ab2142ec17d48b54f2f7457951977d5bce564"
DEFAULTS = "ab0f752eebe3bb6ce6296909b6f90b604f58eafe877032f64006f6c33f21ccb8"
CAPS = "a2e598588a3b18aff4ac64b396716e58ca211384f86f5ef757c7ce3aec3caa22"
VBLANK = "3671f3eb0feef60a9001693f28fbd102d209e1c072a2a486aa34f60429a7eaf0"
STATEBLOCK = "9a3cc11b88c617614da8725cfb996ab8c30ad3ae9e5477b3ae9734753afc5a31"
CREATE_STATEBLOCK = "63f71d88c14fae20c1ca8bcee60ab04a308b8bae93ae75d56527d44881cc2dce"


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
    def data(address,size):
        section=next(s for s in context.image["sections"] if s["address"]<=address and address+size<=s["address"]+s["raw_size"])
        offset=section["raw_offset"]+address-section["address"]
        return context.blob[offset:offset+size]
    # The reviewed initializer reads 109 DWORD defaults for RS57..165 and a
    # 32-byte texture-stage table. Keep these title bytes in private output.
    defaults=[f for f in context.functions if f['size']==210 and context.digest(f)==DEFAULTS]
    if len(defaults)!=1: raise ValueError("missing reviewed state initializer")
    start=int(defaults[0]['start'],16)
    init=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(start,210),start))
    rs_tables=[int(i.op_str.split(', ')[1],0) for i in init if i.mnemonic=='mov' and i.op_str.startswith('ebx, 0x')]
    ts_tables=[int(m[1],16) for i in init if i.mnemonic=='movzx' and (m:=re.fullmatch(r'edx, byte ptr \[edi \+ (0x[0-9a-f]+)\]',i.op_str))]
    if len(rs_tables)!=1 or len(ts_tables)!=1: raise ValueError("unreviewed state table references")
    if variables['D3DRS_CullMode']!=variables['D3D_g_RenderState']+147*4:
        raise ValueError("unreviewed state index layout")
    state_defaults=dict(render_states=variables['D3D_g_RenderState'],texture_states=variables['D3D_g_DeferredTextureState'],
        initializer_sha256=DEFAULTS,render_defaults=list(struct.unpack('<109I',data(rs_tables[0],436))),
        texture_defaults=list(data(ts_tables[0],32)))
    mode=next(s for s in identified if s['name']=='D3DDevice_SetShaderConstantMode')
    mode_code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(mode['address'],functions[mode['address']]['size']),mode['address']))
    mode_fields=[int(m[1],16) for i in mode_code if i.mnemonic=='mov' and
                 (m:=re.fullmatch(r'dword ptr \[ebx \+ (0x[0-9a-f]+)\], eax',i.op_str))]
    if len(mode_fields)!=1: raise ValueError('unreviewed shader mode field')
    state_defaults['shader_constant_mode_offset']=mode_fields[0]
    # Reviewed programmable-shader load/select path. Cache offsets stay in the
    # revision binding; native code owns GPU programs and never emits packets.
    load=next(s for s in identified if s['name']=='D3DDevice_LoadVertexShader')
    select=next(s for s in identified if s['name']=='D3DDevice_SelectVertexShader')
    load_code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(load['address'],functions[load['address']]['size']),load['address']))
    select_code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(select['address'],functions[select['address']]['size']),select['address']))
    record_helpers=[int(i.op_str,0) for i in load_code if i.mnemonic=='call']
    if len(record_helpers)!=2 or not any(i.mnemonic=='test' and i.op_str=='al, 0x10' for i in load_code):
        raise ValueError('unreviewed vertex load cache policy')
    record_helper=functions[record_helpers[0]]
    record_code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(record_helpers[0],record_helper['size']),record_helpers[0]))
    program_offsets=[int(m[1],16) for i in record_code if i.mnemonic=='lea' and
                     (m:=re.fullmatch(r'edx, \[ecx \+ esi \+ (0x[0-9a-f]+)\]',i.op_str))]
    start_offsets=[int(m[1],16) for i in select_code if i.mnemonic=='mov' and
                   (m:=re.fullmatch(r'dword ptr \[esi \+ (0x[0-9a-f]+)\], ecx',i.op_str))]
    flag_offsets=[int(m[1],0) for i in load_code if i.mnemonic=='mov' and
                  (m:=re.fullmatch(r'al, byte ptr \[ebx \+ (0x[0-9a-f]+|[0-9]+)\]',i.op_str))]
    if len(program_offsets)!=1 or len(start_offsets)!=1 or len(flag_offsets)!=1:
        raise ValueError('unreviewed vertex cache layout')
    state_defaults['vertex_binding']=dict(program_cache=program_offsets[0],start=start_offsets[0],
        record_flags=flag_offsets[0],record_disable=0x10,recorder_sha256=context.digest(record_helper))
    fvf_set=next(s for s in identified if s['name']=='D3DDevice_SetVertexShader')
    fvf_code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(fvf_set['address'],functions[fvf_set['address']]['size']),fvf_set['address']))
    fvf_objects=[int(i.op_str.split(', ')[1],0) for i in fvf_code if i.mnemonic=='mov' and i.op_str.startswith('edi, 0x')]
    fvf_calls=[int(i.op_str,0) for i in fvf_code if i.mnemonic=='call']
    if len(fvf_objects)!=1 or len(fvf_calls)<1:raise ValueError('unreviewed FVF descriptor binding')
    state_defaults['fvf_binding']=dict(object=fvf_objects[0],decoder=fvf_calls[0],
        decoder_sha256=context.digest(functions[fvf_calls[0]]))
    swap=next(s for s in identified if s['name']=='D3DDevice_Swap')
    swap_code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(swap['address'],functions[swap['address']]['size']),swap['address']))
    swap_fields=[int(m[1],16) for i in swap_code if i.mnemonic=='mov' and
        (m:=re.fullmatch(r'eax, dword ptr \[esi \+ (0x[0-9a-f]+)\]',i.op_str))]
    if not swap_fields or swap_code[-1].mnemonic!='ret':raise ValueError('unreviewed swap counter return')
    state_defaults['swap_binding']=dict(counter=swap_fields[-1],setter_sha256=context.digest(functions[swap['address']]))
    # Original resource binders use a separate four-bit count at Common[22:19].
    # Retired binding fences and texture-type caches belong to this revision.
    texture_bind=next(s for s in identified if s['name']=='D3DDevice_SetTexture')
    texture_code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(texture_bind['address'],functions[texture_bind['address']]['size']),texture_bind['address']))
    fences=[int(m[1],0) for i in texture_code if i.mnemonic=='mov' and
        (m:=re.fullmatch(r'ecx, dword ptr \[ebp \+ (0x[0-9a-f]+|[0-9]+)\]',i.op_str))]
    modes=[int(m[1],0) for i in texture_code if i.mnemonic=='mov' and
        (m:=re.fullmatch(r'dword ptr \[ebp \+ esi\*4 \+ (0x[0-9a-f]+|[0-9]+)\], 0x80000000',i.op_str))]
    if len(fences)!=1 or len(modes)!=1:raise ValueError('unreviewed resource binding cache layout')
    state_defaults['resource_binding']=dict(fence=fences[0],texture_mode=modes[0])
    transform=next(s for s in identified if s['name']=='D3DDevice_SetTransform')
    screen=next(s for s in identified if s['name']=='D3DDevice_SetScreenSpaceOffset')
    screen_code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(screen['address'],functions[screen['address']]['size']),screen['address']))
    bias=[int(m[1],16) for i in screen_code if i.mnemonic=='fld' and
          (m:=re.fullmatch(r'dword ptr \[(0x[0-9a-f]+)\]',i.op_str))]
    if len(bias)!=2 or bias[0]!=bias[1]:raise ValueError('unreviewed screen-space bias')
    state_defaults['transform_binding']=dict(setter_sha256=context.digest(functions[transform['address']]),
        fields=dict(projection_depth=0x504,depth_scale=0x510,pixel_scale=0x518,
                    projection_viewport=0x530,screen_offset=0xaa8),
        screen_bias=struct.unpack('<I',data(bias[0],4))[0])
    caps=next(s for s in identified if s['name']=='D3DDevice_GetDeviceCaps')
    if caps['stack_bytes']!=4 or context.digest(functions[caps['address']])!=CAPS:
        raise ValueError('unreviewed capability query')
    caps_code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(caps['address'],23),caps['address']))
    caps_tables=[int(i.op_str.split(', ')[1],0) for i in caps_code if i.mnemonic=='mov' and i.op_str.startswith('esi, 0x')]
    if len(caps_tables)!=1 or not any(i.mnemonic=='mov' and i.op_str=='ecx, 0x35' for i in caps_code):
        raise ValueError('unreviewed capability table copy')
    state_defaults['device_caps']=list(struct.unpack('<53I',data(caps_tables[0],53*4)))
    vblank=next(s for s in identified if s['name']=='D3DDevice_BlockUntilVerticalBlank')
    if vblank['stack_bytes']!=0 or context.digest(functions[vblank['address']])!=VBLANK:
        raise ValueError('unreviewed vertical-blank wait')
    state_defaults['vblank_wait']=dict(sha256=VBLANK,contract='clear notification event; nonalertable indefinite wait; no GPU submission')
    callback=next(s for s in identified if s['name']=='D3DDevice_SetVerticalBlankCallback')
    callback_code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(callback['address'],functions[callback['address']]['size']),callback['address']))
    offsets=[int(m[1],16) for i in callback_code if i.mnemonic=='mov' and
        (m:=re.fullmatch(r'dword ptr \[ecx \+ (0x[0-9a-f]+)\], eax',i.op_str))]
    if len(callback_code)!=4 or len(offsets)!=1 or callback_code[-1].mnemonic!='ret' or callback_code[-1].op_str!='4':
        raise ValueError('unreviewed vblank callback registration')
    state_defaults['vblank_callback_offset']=offsets[0]
    # Get2DSurfaceDesc derives usage from its format-capability table even for
    # sampled texture surfaces. Keep the reviewed image's facts in the binding.
    desc=next(s for s in identified if s['name']=='Get2DSurfaceDesc')
    desc_code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(desc['address'],functions[desc['address']]['size']),desc['address']))
    format_tables=[int(m[1],16) for i in desc_code if i.mnemonic=='mov' and
        (m:=re.fullmatch(r'al, byte ptr \[edx \+ (0x[0-9a-f]+)\]',i.op_str))]
    if len(format_tables)!=1: raise ValueError('unreviewed surface format table')
    state_defaults['surface_formats']=[dict(format=x,flags=data(format_tables[0]+x,1)[0])
        for x in (4,5,6,7,0xc,0xe,0xf,0x12,0x1e,0x2e,0x24,0x25)]
    # CPU snapshot layout from the complete reviewed recorder, independently
    # checked against original-x86 execution. These offsets belong to this
    # revision's binding, never to shared native service code.
    record=next(s for s in identified if s['name']=='D3D_RecordStateBlock')
    create_block=next(s for s in identified if s['name']=='D3DDevice_CreateStateBlock')
    if context.digest(functions[record['address']])!=STATEBLOCK or context.digest(functions[create_block['address']])!=CREATE_STATEBLOCK:
        raise ValueError('unreviewed state-block layout')
    block_code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(create_block['address'],299),create_block['address']))
    block_tables=[int(m[1],16) for i in block_code if i.mnemonic=='movzx' and
        (m:=re.fullmatch(r'(?:ecx|edx), byte ptr \[eax \+ (0x[0-9a-f]+)\]',i.op_str))]
    if len(block_tables)!=4: raise ValueError('unreviewed state-block membership tables')
    state_defaults['stateblock']=dict(recorder_sha256=STATEBLOCK,create_sha256=CREATE_STATEBLOCK,
        streams=variables['D3D_g_Stream'],
        fields=dict(textures=0xb38,pixel_shader=0x370,vertex_shader=0x384,vertex_object=0x380,
                    indices=0x38c,base_vertex=0x1c,pixel_constants=0xb58,vertex_constants=0xc58,
                    transforms=0x810,viewport=0xa90,materials=0xab0,lights=0x390,light_count=0x394),
        pixel_render=list(data(block_tables[0],32)),pixel_texture=list(data(block_tables[1],27)),
        vertex_render=list(data(block_tables[2],33)),vertex_texture=list(data(block_tables[3],2)))
    calls=[]
    for symbol in identified:
        if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*",symbol["name"]): raise ValueError("invalid symbol")
        original=functions[symbol["address"]]
        calls.append(dict(name=symbol["name"],symbol=original["name"],address=original["start"],
                          function_sha256=context.digest(original),call=symbol["call"],
                          stack_bytes=symbol["stack_bytes"],params=symbol["params"]))
    binding=dict(xbe_sha256=IMAGE,device_global=variables["D3D_g_pDevice"],device_object=devices[0],
                 vertex_shader_offset=variables["D3DDevice__m_VertexShader_OFFSET"],calls=calls,**state_defaults)
    header=['#pragma once', '#include <stdint.h>', 'enum PilotD3DEntry {']
    header += [f'    PILOT_D3D_{c["name"]},' for c in calls]
    header += ['    PILOT_D3D_COUNT', '};', 'static const struct { const char *name; uint32_t address, stack_bytes; } pilot_d3d_calls[] = {']
    header += [f'    {{"{c["name"]}", {c["address"]}u, {c["stack_bytes"]}u}},' for c in calls]
    header += ['};',f'#define PILOT_D3D_DEVICE_GLOBAL 0x{binding["device_global"]:x}u',
               f'#define PILOT_D3D_DEVICE_OBJECT 0x{binding["device_object"]:x}u',
               f'#define PILOT_D3D_VERTEX_SHADER_OFFSET {binding["vertex_shader_offset"]}u']
    header += [f'#define PILOT_D3D_RENDER_STATES 0x{binding["render_states"]:x}u',
               f'#define PILOT_D3D_TEXTURE_STATES 0x{binding["texture_states"]:x}u',
               f'#define PILOT_D3D_VBLANK_CALLBACK_OFFSET {binding["vblank_callback_offset"]}u',
               f'#define PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET 0x{binding["shader_constant_mode_offset"]:x}u',
               'static const uint32_t pilot_d3d_render_defaults[] = {'+', '.join(f'0x{x:x}u' for x in binding['render_defaults'])+'};',
               'static const uint32_t pilot_d3d_texture_defaults[] = {'+', '.join(str(x)+'u' for x in binding['texture_defaults'])+'};']
    header += ['static const uint32_t pilot_d3d_device_caps[] = {'+', '.join(f'0x{x:x}u' for x in binding['device_caps'])+'};']
    header += ['static const struct { uint32_t format,flags; } pilot_d3d_surface_formats[] = {'+
        ', '.join(f'{{0x{x["format"]:x}u,0x{x["flags"]:x}u}}' for x in binding['surface_formats'])+'};']
    block=binding['stateblock']
    header += [f'#define PILOT_D3D_STREAMS 0x{block["streams"]:x}u']
    header += [f'#define PILOT_D3D_FVF_OBJECT 0x{binding["fvf_binding"]["object"]:x}u']
    header += [f'#define PILOT_D3D_SWAP_COUNTER 0x{binding["swap_binding"]["counter"]:x}u']
    header += [f'#define PILOT_D3D_VERTEX_{name.upper()} {binding["vertex_binding"][name]}u'
               for name in ('program_cache','start','record_flags','record_disable')]
    header += [f'#define PILOT_D3D_RESOURCE_{name.upper()} {offset}u' for name,offset in binding['resource_binding'].items()]
    header += [f'#define PILOT_D3D_TRANSFORM_{name.upper()} {offset}u' for name,offset in binding['transform_binding']['fields'].items()]
    header += [f'#define PILOT_D3D_SCREEN_BIAS 0x{binding["transform_binding"]["screen_bias"]:x}u']
    header += [f'#define PILOT_D3D_SNAPSHOT_{name.upper()} {offset}u' for name,offset in block['fields'].items()]
    header += ['static const uint8_t pilot_d3d_block_'+name+'[] = {'+', '.join(str(x) for x in block[name])+'};'
               for name in ('pixel_render','pixel_texture','vertex_render','vertex_texture')]
    wrapper=['#include "pilot_d3d_bindings.h"', 'extern void pilot_d3d_dispatch(unsigned);']
    for c in calls:
        wrapper += [f'extern void pilot_original_d3d_{c["name"]}(void);',f'void {c["symbol"]}(void) {{',
                    '#ifdef PILOT_D3D_NATIVE', f'    pilot_d3d_dispatch(PILOT_D3D_{c["name"]});',
                    '#else', f'    pilot_original_d3d_{c["name"]}();', '#endif', '}']
    generated=context.generated; record=output/"d3d-binding.aot-private.json"
    texts={"pilot_d3d_bindings.h":"\n".join(header)+"\n", "pilot_d3d_binding.c":"\n".join(wrapper)+"\n"}
    if (generated/"pilot_d3d_binding.c").exists():
        old=json.loads(record.read_text())
        if any(k not in binding or (binding[k][:len(v)]!=v if k=='surface_formats' else binding[k]!=v) for k,v in old.items()) or (generated/'pilot_d3d_binding.c').read_text()!=texts['pilot_d3d_binding.c']:
            raise ValueError("existing graphics binding differs")
        if old==binding and (generated/'pilot_d3d_bindings.h').read_text()==texts['pilot_d3d_bindings.h']:
            return binding
        # Binding already renamed original bodies; add verified state metadata
        # without relifting the title or changing its preserved CPU bodies.
        (generated/'pilot_d3d_bindings.h').write_text(texts['pilot_d3d_bindings.h'],newline='\n')
        record.write_text(json.dumps(binding,indent=2)+'\n',newline='\n')
        context.manifest['d3d_binding']=binding
        context.manifest['files']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(generated.iterdir()) if p.is_file()}
        context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',newline='\n')
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
