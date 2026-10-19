"""Original-code-qualified LTCG graphics ABI adapter; no renderer implementation.

The selected profile owns method bodies, ABI corrections and device evidence.
This adapter validates them before generating marshalling wrappers. SDK names
alone never admit a native boundary, and original bodies remain selectable.
"""
from __future__ import annotations
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
from capstone import Cs,CS_ARCH_X86,CS_MODE_32

ADAPTER='xdk-d3d8-ltcg-5849-v1'
REGISTERS={'eax','ebx','ecx','edx','esi','edi','ebp'}
INTERFACE=Path(__file__).with_name('native_profiles')/'d3d8-service-interface.json'


def argument_sources(signature):
    stack=0;sources=[]
    for p in signature['params']:
        if p['type']=='psh':
            stack+=4;sources.append(f'MEM32(g_esp+{stack}u)')
        elif p['type'] in REGISTERS:
            sources.append('g_'+p['type'])
        else:
            raise ValueError('unreviewed LTCG argument source')
    if stack!=signature['stack_bytes']:
        raise ValueError('LTCG stack arguments disagree with reviewed cleanup')
    return sources


def marshal(row,interface):
    """Map a reviewed source ABI to the shared service ABI, before any writes."""
    signature=row['signature'];sources=argument_sources(signature)
    canonical=interface.get(row['name'],dict(params=signature['params'],stack_bytes=signature['stack_bytes']))
    by_name={p['name']:s for p,s in zip(signature['params'],sources)}
    if len(by_name)!=len(sources):
        raise ValueError('ambiguous LTCG argument names')
    if row['name']=='Direct3D_CreateDevice':
        # The LTCG routine has no guest Adapter/DeviceType/window operands.
        # These are normalized host-service parameters, not observed guest args.
        by_name.update(Adapter='0u',DeviceType='1u',hFocusWindow='0u')
    args=[];registers={}
    for p in canonical['params']:
        source=by_name.get(p['name'])
        if source is None:
            # Internal helper names can differ while their complete parameter
            # order is identical. Never guess an omitted parameter.
            if len(signature['params'])!=len(canonical['params']):
                raise ValueError('missing canonical LTCG argument: '+row['name']+'/'+p['name'])
            source=sources[canonical['params'].index(p)]
        if p['type']=='psh':args.append(source)
        elif p['type'] in REGISTERS:registers[p['type']]=source
        else:raise ValueError('unknown canonical graphics argument source')
    if len(args)*4!=canonical['stack_bytes']:
        raise ValueError('canonical graphics stack layout mismatch')
    return canonical,args,registers


def inspect(context,proof):
    """Produce binding/header/wrapper without mutating the generated directory."""
    if proof.get('adapter')!=ADAPTER or proof.get('stateblock') is not False:
        raise ValueError('unreviewed LTCG graphics adapter/features')
    behavior_mask=proof.get('native_create_behavior_mask')
    driver_mask=proof.get('original_create_driver_mask')
    if type(behavior_mask) is not int or behavior_mask!=0x40 or type(driver_mask) is not int or driver_mask!=0x10:
        raise ValueError('unreviewed LTCG graphics behavior mask')
    interface={r['name']:r for r in json.loads(INTERFACE.read_text())}
    functions={int(f['start'],16):f for f in context.functions}
    symbols=[s for s in context.symbols if s['library']=='D3D8LTCG' and s['kind']=='function']
    rows=proof['functions']
    if len({r['address'] for r in rows})!=len(rows) or len({r['name'] for r in rows})!=len(rows):
        raise ValueError('duplicate LTCG graphics profile boundary')
    if {(s['address'],s['name']) for s in symbols}!={(r['address'],r['source_name']) for r in rows}:
        raise ValueError('LTCG graphics inventory differs from reviewed profile')
    def read(address,size):
        section=next((s for s in context.image['sections'] if s['address']<=address and
                      address+size<=s['address']+s['raw_size']),None)
        if section is None:raise ValueError('graphics proof outside original bytes')
        at=section['raw_offset']+address-section['address']
        return context.blob[at:at+size]
    codes={};calls=[]
    for row in rows:
        if not re.fullmatch('[A-Za-z_][A-Za-z_0-9]*',row['name']):
            raise ValueError('invalid canonical graphics name')
        symbol=next(s for s in symbols if s['address']==row['address'] and s['name']==row['source_name'])
        if {k:symbol[k] for k in ('call','params','stack_bytes')}!=row['source_signature']:
            raise ValueError('LTCG discovery signature changed: '+row['name'])
        f=functions[row['address']]
        if not context.matches_body(f,row['body']):
            raise ValueError('unreviewed LTCG graphics body: '+row['name'])
        code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(read(row['address'],f['size']),row['address']))
        if not code or code[-1].address+code[-1].size!=row['address']+f['size']:
            raise ValueError('incomplete LTCG graphics decode')
        codes[row['name']]=code
        returns={int(i.op_str,0) if i.op_str else 0 for i in code if i.mnemonic=='ret'}
        if not returns and row.get('tail_target'):
            tail=row['tail_target']
            if code[-1].mnemonic!='jmp' or code[-1].op_str!=hex(tail) or tail not in functions:
                raise ValueError('unreviewed LTCG tail boundary')
            target=functions[tail]
            target_code=Cs(CS_ARCH_X86,CS_MODE_32).disasm(read(tail,target['size']),tail)
            returns={int(i.op_str,0) if i.op_str else 0 for i in target_code if i.mnemonic=='ret'}
        if returns!={row['signature']['stack_bytes']}:
            raise ValueError('LTCG signature disagrees with original RET: '+row['name'])
        canonical,args,registers=marshal(row,interface)
        calls.append(dict(name=row['name'],symbol=f['name'],address=f['start'],
            bytes=f['size'],function_sha256=row['body']['sha256'],call='canonical',
            stack_bytes=canonical['stack_bytes'],params=canonical['params'],
            original_stack_bytes=row['signature']['stack_bytes'],original_signature=row['signature'],
            source_signature=row['source_signature'],argument_sources=args,register_sources=registers))
    for name,p in proof['helpers'].items():
        f=functions.get(p['address'])
        if f is None or not context.matches_body(f,p):
            raise ValueError('unreviewed LTCG graphics helper: '+name)
        codes[name]=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(read(p['address'],f['size']),p['address']))
    layout=proof['layout']
    def require(name,mnemonic,operands):
        if not any(i.mnemonic==mnemonic and i.op_str==operands for i in codes[name]):
            raise ValueError('unreviewed LTCG layout/reference: '+name+'/'+operands)
    require('Direct3D_CreateDevice','mov',f'dword ptr [{hex(layout["device_global"])}], {hex(layout["device_object"])}')
    require('Direct3D_CreateDevice','mov',f'ecx, {hex(layout["device_bytes"]//4)}')
    require('Direct3D_CreateDevice','and',f'eax, {hex(driver_mask)}')
    require('D3DDevice_SetShaderConstantMode','mov',f'dword ptr [ebx + {hex(layout["shader_mode"])}], eax')
    require('D3DDevice_Swap','mov',f'eax, dword ptr [esi + {hex(layout["swap_counter"])}]')
    require('D3DDevice_BlockUntilVerticalBlank','add',f'eax, {hex(layout["vblank_event"])}')
    require('D3DDevice_GetDeviceCaps','mov',f'esi, {hex(layout["device_caps"])}')
    require('D3DDevice_GetDeviceCaps','mov','ecx, 0x35')
    require('state_initializer','mov',f'edi, dword ptr [esi + {hex(layout["render_defaults"]+140)}]')
    require('state_initializer','movzx',f'ecx, byte ptr [edi + {hex(layout["texture_defaults"])}]')
    require('state_initializer','mov',f'dword ptr [esi + {hex(layout["transform_fields"]["screen_offset"])}], eax')
    require('state_initializer','mov',f'eax, {hex(layout["screen_bias"])}')
    require('reset_buffers','mov',f'dword ptr [{hex(layout["presentation_interval"])}], edx')
    require('Get2DSurfaceDesc','mov',f'al, byte ptr [ecx + {hex(layout["surface_formats"])}]')
    require('D3DDevice_SetVertexShader','mov',f'edi, {hex(layout["fvf_object"])}')
    require('D3DDevice_SetVertexShader','call',hex(proof['helpers']['fvf_decoder']['address']))
    require('D3DDevice_SetVertexShaderConstant1','lea',f'ecx, [ecx + {hex(layout["vertex_constants"])}]')
    require('vertex_recorder','lea',f'ecx, [ecx + edx + {hex(layout["vertex_program_cache"])}]')
    require('D3DDevice_LoadVertexShader','mov',f'al, byte ptr [ebp + {layout["vertex_record_flags"]}]')
    require('D3DDevice_LoadVertexShader','test',f'al, {hex(layout["vertex_record_disable"])}')
    require('D3DDevice_SelectVertexShader','mov',f'dword ptr [esi + {hex(layout["vertex_start"])}], ebx')
    require('D3DDevice_SetTexture','mov',f'eax, dword ptr [ebx + {hex(layout["resource_fence"])}]')
    require('D3DDevice_SetTexture','mov',f'dword ptr [ebx + esi*4 + {hex(layout["texture_mode"])}], 0x80000000')
    surfaces=layout.get('presentation_fields')
    if (not isinstance(surfaces,dict) or set(surfaces)!={'count','colors','depth','current_color','current_depth'}
            or any(type(v) is not int or v<0 or v%4 or v+4>layout['device_bytes'] for v in surfaces.values())
            or surfaces['colors']+8>layout['device_bytes']
            or len(set(surfaces.values())|{surfaces['colors']+4})!=6):
        raise ValueError('unreviewed presentation surface fields')
    require('reset_buffers','mov',f'dword ptr [edi + {hex(surfaces["count"])}], eax')
    require('reset_buffers','lea',f'edx, [edi + {hex(surfaces["colors"])}]')
    require('reset_buffers','lea',f'esi, [edi + {hex(surfaces["colors"]+4)}]')
    require('reset_buffers','mov',f'dword ptr [edi + {hex(surfaces["depth"])}], eax')
    count_store=next(i for i,ins in enumerate(codes['reset_buffers']) if ins.mnemonic=='mov' and
                     ins.op_str==f'dword ptr [edi + {hex(surfaces["count"])}], eax')
    if not any(i.mnemonic=='inc' and i.op_str=='eax' for i in codes['reset_buffers'][max(0,count_store-3):count_store]):
        raise ValueError('unreviewed presentation buffer count increment')
    require('device_initializer','mov',f'ecx, dword ptr [ebp + {hex(surfaces["depth"])}]')
    require('device_initializer','mov',f'edx, dword ptr [ebp + {hex(surfaces["colors"])}]')
    require('D3DDevice_SetRenderTarget','mov',f'eax, dword ptr [ebp + {hex(surfaces["current_color"])}]')
    require('D3DDevice_SetRenderTarget','mov',f'dword ptr [ebp + {hex(surfaces["current_color"])}], ebx')
    require('D3DDevice_SetRenderTarget','mov',f'dword ptr [ebp + {hex(surfaces["current_depth"])}], esi')
    variables={}
    for s in context.symbols:
        if s['library'] not in ('D3D8','D3D8LTCG') or s['kind']!='variable':continue
        if s['name'] in variables and variables[s['name']]!=s['address']:
            raise ValueError('ambiguous graphics variable')
        variables[s['name']]=s['address']
    for symbol,field in (('D3D_g_pDevice','device_global'),('D3D_g_RenderState','render_states'),
                         ('D3D_g_DeferredTextureState','texture_states'),('D3D_g_Stream','streams'),
                         ('D3DDevice__m_VBlankCallback_OFFSET','vblank_callback')):
        if variables.get(symbol)!=layout[field]:raise ValueError('unreviewed graphics variable: '+symbol)
    if variables['D3DRS_YuvEnable']!=layout['render_states']+layout['yuv_state']*4:
        raise ValueError('unreviewed YUV state index')
    cache=layout['cache_fields'];transform=layout['transform_fields']
    for field,name,mnemonic,template in (
            ('pixel_shader','D3DDevice_SetPixelShader','mov','dword ptr [ebx + {offset}], eax'),
            ('vertex_object','D3DDevice_SetVertexShader','mov','dword ptr [esi + {offset}], edi'),
            ('vertex_shader','D3DDevice_SetVertexShader','mov','dword ptr [esi + {offset}], ebx'),
            ('indices','D3DDevice_SetIndices','mov','dword ptr [esi + {offset}], ebx'),
            ('base_vertex','D3DDevice_SetIndices','mov','dword ptr [esi + {offset}], ecx'),
            ('textures','D3DDevice_SetTexture','mov','dword ptr [ebx + esi*4 + {offset}], ebp'),
            ('transforms','D3DDevice_SetTransform','lea','edi, [ecx + ebx + {offset}]'),
            ('viewport','D3DDevice_GetViewportOffsetAndScale','fild','dword ptr [eax + {offset}]')):
        require(name,mnemonic,template.format(offset=hex(cache[field])))
    # SetPixelShader publishes these three auxiliary words adjacent to the
    # proved handle. The renderer retains that observable descriptor cache.
    for offset,register in ((4,'ecx'),(8,'edx'),(12,'eax')):
        require('D3DDevice_SetPixelShader','mov',
                f'dword ptr [ebx + {hex(cache["pixel_shader"]+offset)}], {register}')
    for field,name,mnemonic,template in (
            ('projection_depth','D3DDevice_SetTransform','fstp','dword ptr [ebx + {offset}]'),
            ('depth_scale','D3D_UpdateProjectionViewportTransform','fmul','dword ptr [edx + {offset}]'),
            ('pixel_scale','D3D_UpdateProjectionViewportTransform','fmul','dword ptr [edx + {offset}]'),
            ('projection_viewport','D3D_UpdateProjectionViewportTransform','lea','ecx, [edx + {offset}]')):
        require(name,mnemonic,template.format(offset=hex(transform[field])))
    facts=dict(render_defaults=list(struct.unpack('<109I',read(layout['render_defaults'],436))),
               texture_defaults=list(read(layout['texture_defaults'],32)),
               device_caps=list(struct.unpack('<53I',read(layout['device_caps'],212))),
               surface_formats=[dict(format=f,flags=read(layout['surface_formats']+f,1)[0])
                                for f in (4,5,6,7,0xc,0xe,0xf,0x12,0x1e,0x2e,0x24,0x25,0x28)])
    binding=dict(xbe_sha256=context.image['sha256'],adapter=ADAPTER,device_global=layout['device_global'],
                 device_object=layout['device_object'],vertex_shader_offset=cache['vertex_object'],
                 layout=layout,calls=calls,helpers=proof['helpers'],stateblock=False,
                 native_create_behavior_mask=behavior_mask,original_create_driver_mask=driver_mask,**facts)
    return binding,*emit_text(binding,interface,behavior_mask)


def emit_text(binding,interface,behavior_mask):
    calls=binding['calls'];layout=binding['layout'];cache=layout['cache_fields']
    names={c['name'] for c in calls};absent=sorted(set(interface)-names)
    header=['#pragma once','#include <stdint.h>','enum PilotD3DEntry {']
    header += [f'    PILOT_D3D_{c["name"]},' for c in calls]
    header += ['    PILOT_D3D_COUNT,']
    header += [f'    PILOT_D3D_{name},' for name in absent]
    header += ['};','static const struct { const char *name; uint32_t address, stack_bytes, bytes; const char *sha256; } pilot_d3d_calls[] = {']
    header += [f'{{"{c["name"]}",{c["address"]}u,{c["stack_bytes"]}u,{c["bytes"]}u,"{c["function_sha256"]}"}},' for c in calls]
    header += ['};','#define PILOT_D3D_PROFILE_5849 1','#define PILOT_D3D_STATEBLOCK_AVAILABLE 0',
               f'#define PILOT_D3D_CREATE_BEHAVIOR_MASK 0x{behavior_mask:x}u']
    if 'original_create_driver_mask' in binding:
        header += [f'#define PILOT_D3D_CREATE_DRIVER_MASK 0x{binding["original_create_driver_mask"]:x}u']
    defines={'DEVICE_GLOBAL':layout['device_global'],'DEVICE_OBJECT':layout['device_object'],
             'DEVICE_BYTES':layout['device_bytes'],'VERTEX_SHADER_OFFSET':cache['vertex_object'],
             'RENDER_STATES':layout['render_states'],'TEXTURE_STATES':layout['texture_states'],
             'VBLANK_CALLBACK_OFFSET':layout['vblank_callback'],'SHADER_CONSTANT_MODE_OFFSET':layout['shader_mode'],
             'YUV_STATE':layout['yuv_state'],'STREAMS':layout['streams'],'FVF_OBJECT':layout['fvf_object'],
             'SWAP_COUNTER':layout['swap_counter'],'PRESENT_INTERVAL':layout['presentation_interval'],
             'VERTEX_PROGRAM_CACHE':layout['vertex_program_cache'],'VERTEX_RECORD_FLAGS':layout['vertex_record_flags'],
             'VERTEX_RECORD_DISABLE':layout['vertex_record_disable'],'VERTEX_START':layout['vertex_start'],
             'VERTEX_CONSTANTS_GLOBAL':layout['vertex_constants'],'RESOURCE_FENCE':layout['resource_fence'],
             'RESOURCE_TEXTURE_MODE':layout['texture_mode'],'SCREEN_BIAS':layout['screen_bias']}
    defines.update({'TRANSFORM_'+k.upper():v for k,v in layout['transform_fields'].items()})
    defines.update({'SNAPSHOT_'+k.upper():v for k,v in cache.items()})
    if 'presentation_fields' in layout:
        header += ['#define PILOT_D3D_SURFACE_PROJECTION_VERSION 1u']
        defines.update({'PRESENTATION_'+k.upper():v for k,v in layout['presentation_fields'].items()
                        if k in ('count','colors','depth')})
        defines.update({k.upper():v for k,v in layout['presentation_fields'].items() if k.startswith('current_')})
    header += [f'#define PILOT_D3D_{name} 0x{value:x}u' for name,value in defines.items()]
    for name in ('render_defaults','texture_defaults','device_caps'):
        header += [f'static const uint32_t pilot_d3d_{name}[] = {{'+','.join(f'0x{v:x}u' for v in binding[name])+'};']
    header += ['static const struct { uint32_t format,flags; } pilot_d3d_surface_formats[] = {'+
               ','.join(f'{{{x["format"]}u,{x["flags"]}u}}' for x in binding['surface_formats'])+'};']
    wrapper=['#define RECOMP_GENERATED_CODE','#include "recomp_types.h"','#include "pilot_d3d_bindings.h"',
             'extern void pilot_d3d_dispatch_frame(unsigned,const uint32_t*,unsigned,unsigned);']
    for c in calls:
        wrapper += [f'extern void pilot_original_d3d_{c["name"]}(void);',f'void {c["symbol"]}(void) {{','#ifdef PILOT_D3D_NATIVE']
        for index,source in enumerate(c['argument_sources']):wrapper += [f'    uint32_t a{index}={source};']
        for reg,source in c['register_sources'].items():wrapper += [f'    uint32_t saved_{reg}={source};']
        if c['argument_sources']:wrapper += ['    const uint32_t args[]={'+','.join('a'+str(i) for i in range(len(c['argument_sources'])))+'};']
        wrapper += [f'    g_{reg}=saved_{reg};' for reg in c['register_sources']]
        wrapper += [f'    pilot_d3d_dispatch_frame(PILOT_D3D_{c["name"]},'+('args' if c['argument_sources'] else '0')+
                    f',{len(c["argument_sources"])}u,{c["original_stack_bytes"]}u);',
                    '#else',f'    pilot_original_d3d_{c["name"]}();','#endif','}']
    return '\n'.join(header)+'\n','\n'.join(wrapper)+'\n'


def bind(context,proof):
    binding,header,wrapper=inspect(context,proof)
    generated=context.generated;record=context.output/'d3d-binding.aot-private.json'
    texts={'pilot_d3d_bindings.h':header,'pilot_d3d_binding.c':wrapper}
    if (generated/'pilot_d3d_binding.c').exists():
        prior_binding=copy.deepcopy(binding)
        prior_binding.pop('native_create_behavior_mask',None);prior_binding.pop('original_create_driver_mask',None)
        prior_binding['layout'].pop('presentation_fields',None)
        actual_record=json.loads(record.read_text())
        if (actual_record not in (binding,prior_binding) or context.manifest.get('d3d_binding')!=actual_record
                or (generated/'pilot_d3d_binding.c').read_text()!=wrapper):
            raise ValueError('existing LTCG graphics binding differs')
        old_header=(generated/'pilot_d3d_bindings.h').read_text()
        if old_header==header and actual_record==binding:return binding
        # Only the exact previous body/layout record can gain the newly proved
        # fields and public policy. Wrapper and retained bodies stay identical.
        new_defines={'PILOT_D3D_CREATE_DRIVER_MASK','PILOT_D3D_SURFACE_PROJECTION_VERSION',
                     'PILOT_D3D_PRESENTATION_COUNT','PILOT_D3D_PRESENTATION_COLORS',
                     'PILOT_D3D_PRESENTATION_DEPTH','PILOT_D3D_CURRENT_COLOR','PILOT_D3D_CURRENT_DEPTH'}
        prior='\n'.join(line for line in header.splitlines() if not
                        (line.startswith('#define ') and line.split()[1] in new_defines))+'\n'
        prior=prior.replace('#define PILOT_D3D_CREATE_BEHAVIOR_MASK 0x40u\n',
                            '#define PILOT_D3D_CREATE_BEHAVIOR_MASK 0x10u\n')
        delta=binding['layout']['vertex_constants']-binding['layout']['device_object']
        marker='static const uint32_t pilot_d3d_render_defaults[]'
        initial=prior.replace('#define PILOT_D3D_CREATE_BEHAVIOR_MASK 0x10u\n','')
        initial=initial.replace(marker,f'#define PILOT_D3D_SNAPSHOT_VERTEX_CONSTANTS 0x{delta:x}u\n'+marker,1)
        if actual_record!=prior_binding or old_header not in (prior,initial):
            raise ValueError('existing LTCG graphics header differs')
        (generated/'pilot_d3d_bindings.h').write_text(header,encoding='utf-8',newline='\n')
        record.write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8',newline='\n')
        context.manifest['d3d_binding']=binding
        context.manifest['files']['pilot_d3d_bindings.h']=hashlib.sha256((generated/'pilot_d3d_bindings.h').read_bytes()).hexdigest()
        context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
        return binding
    replacements={c['symbol']:'pilot_original_d3d_'+c['name'] for c in binding['calls']}
    found=set();edits={}
    def replace(m):
        if m[1] not in replacements:return m[0]
        if m[1] in found:raise ValueError('duplicate LTCG graphics body')
        found.add(m[1]);return 'void '+replacements[m[1]]+'(void)\n{'
    for path in generated.glob('recomp_*.c'):
        text=path.read_text(encoding='utf-8');updated=re.sub(r'void (sub_[0-9A-Fa-f]{8})\(void\)\n\{',replace,text)
        if text!=updated:edits[path]=updated
    if found!=set(replacements):raise ValueError('missing LTCG graphics bodies')
    for path,text in edits.items():path.write_text(text,encoding='utf-8',newline='\n')
    for name,text in texts.items():(generated/name).write_text(text,encoding='utf-8',newline='\n')
    record.write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8',newline='\n')
    context.manifest['d3d_binding']=binding
    context.manifest['files']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(generated.iterdir()) if p.is_file()}
    context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
    return binding
