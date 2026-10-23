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
from itertools import combinations
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
        for name, value in dict(Adapter='0u',DeviceType='1u',hFocusWindow='0u').items():
            by_name.setdefault(name,value)
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
    # SetTransform stores the raw ten-matrix array. The projection/viewport
    # helper multiplies its projection input into a separate destination.
    # Confusing those two operands corrupts the title's queryable projection.
    matrices=layout['cache_fields']['transforms']
    combined=layout['transform_fields']['projection_viewport']
    if not (combined+64<=matrices or combined>=matrices+10*64):
        raise ValueError('projection/viewport destination overlaps raw transform cache')
    require('D3D_UpdateProjectionViewportTransform','lea',f'ecx, [edx + {hex(matrices+64)}]')
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
    presentation=proof.get('guest_presentation')
    if presentation is not None:
        required={'version','sample_types','gaussian_filter','aa_state','mask_state','filter_global',
                  'aa_enable_global','aa_mask_global','mode_global','rt_mode_global'}
        if (not isinstance(presentation,dict) or set(presentation)!=required or
                presentation['sample_types']!=[0x11,0x2222] or
                any(type(presentation[k]) is not int for k in required-{'sample_types'}) or
                presentation['version']!=1 or presentation['gaussian_filter']!=5 or
                presentation['aa_state']!=152 or presentation['mask_state']!=153):
            raise ValueError('unreviewed guest presentation contract')
        for k in ('filter_global','aa_enable_global','aa_mask_global','mode_global','rt_mode_global'):
            if presentation[k]%4 or not any(s['address']<=presentation[k] and presentation[k]+4<=s['address']+s['size'] for s in context.image['sections']):
                raise ValueError('guest presentation globals outside image')
        require('reset_buffers','test','ch, 0x30')
        require('reset_buffers','mov','esi, 5')
        require('reset_buffers','mov',f'dword ptr [{hex(presentation["filter_global"])}], esi')
        require('resolve_setup','mov',f'eax, dword ptr [{hex(presentation["filter_global"])}]')
        require('reset_buffers','mov','eax, dword ptr [ebp + 0x20]')
        require('Get2DSurfaceDesc','mov','ecx, dword ptr [edi + 4]')
        require('Get2DSurfaceDesc','cmp','ecx, dword ptr [edx + 4]')
        require('Get2DSurfaceDesc','mov','dword ptr [esi + 0x10], 0x11')
        for name,field,reg in (('D3DDevice_SetRenderState_MultiSampleAntiAlias','aa_enable_global','eax'),
                               ('D3DDevice_SetRenderState_MultiSampleMask','aa_mask_global','eax'),
                               ('D3DDevice_SetRenderState_MultiSampleMode','mode_global','ecx'),
                               ('D3DDevice_SetRenderState_MultiSampleRenderTargetMode','rt_mode_global','ecx')):
            require(name,'mov',f'dword ptr [{hex(presentation[field])}], {reg}')
        if layout['render_states']+4*presentation['aa_state']!=presentation['aa_enable_global'] or layout['render_states']+4*presentation['mask_state']!=presentation['aa_mask_global']:
            raise ValueError('guest sample state/cache role mismatch')
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
    if 'index_data_global' in layout:
        index_data=layout['index_data_global']
        if (type(index_data) is not int or index_data%4 or not any(
                s['address']<=index_data and index_data+4<=s['address']+s['size'] for s in context.image['sections'])):
            raise ValueError('unreviewed index data publication global')
        require('D3DDevice_SetIndices','mov',f'dword ptr [{hex(index_data)}], eax')
        require('D3DDevice_SetIndices','mov',f'dword ptr [{hex(index_data)}], 0')
    if 'device_references' in layout:
        offset=layout['device_references']
        if type(offset) is not int or offset%4 or offset<0 or offset+4>layout['device_bytes']:
            raise ValueError('unreviewed device reference field')
        require('D3DDevice_AddRef','mov',f'ecx, dword ptr [eax + {hex(offset)}]')
        require('D3DDevice_AddRef','mov',f'dword ptr [eax + {hex(offset)}], ecx')
        require('D3DDevice_Release','mov',f'eax, dword ptr [edi + {hex(offset)}]')
        require('D3DDevice_Release','mov',f'dword ptr [edi + {hex(offset)}], eax')
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
            ('projection_viewport','D3D_UpdateProjectionViewportTransform','add','edx, {offset}')):
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
    if presentation is not None:binding["guest_presentation"]=presentation
    if 'command_buffer' in proof:
        binding['command_buffer']=command_facts(proof['command_buffer'],calls,codes,layout,require)
    retirement={'resource_unregister','resource_free'}&set(proof['helpers'])
    if retirement:
        if retirement!={'resource_unregister','resource_free'}:raise ValueError('incomplete original resource allocator proof')
        unregister=proof['helpers']['resource_unregister']['address'];free=proof['helpers']['resource_free']['address']
        require('D3D_DestroyResource','call',hex(unregister));require('D3D_DestroyResource','call',hex(free))
        require('D3D_DestroyResource','push','0xa1800000');require('D3D_DestroyResource','push','0x24800000')
        require('resource_unregister','call',hex(free));require('resource_unregister','ret','4')
        require('resource_free','test','byte ptr [esp + 0xb], 0x80');require('resource_free','ret','8')
        binding['resource_retirement']=dict(version=1,unregister=unregister,tagged_free=free)
    return binding,*emit_text(binding,interface,behavior_mask)


def command_facts(p,calls,codes,layout,require):
    fields={'version','reserve','space','kickoff','request_global','capacity_global',
            'request_default','capacity_default','current_offset','limit_offset','flags_offset','record_mask','margin'}
    if not isinstance(p,dict) or set(p)!=fields or any(type(v) is not int for k,v in p.items() if k not in ('reserve','space','kickoff')):
        raise ValueError('incomplete inline command-buffer profile')
    if (p['version'],p['current_offset'],p['limit_offset'],p['flags_offset'],p['record_mask'])!=(1,0,4,8,4):
        raise ValueError('unsupported inline command-buffer layout')
    if (p['margin']<4 or p['margin']%4 or p['capacity_default']>1024*1024 or
        p['request_default']<=p['margin'] or p['request_default']%8 or
        p['capacity_default']<p['request_default'] or p['capacity_default']%4 or
        any(not 0<v<=0xfffffffc or v%4 for v in (p['request_global'],p['capacity_global']))):
        raise ValueError('invalid inline command-buffer policy')
    by_name={c['name']:c for c in calls}
    if any(type(p[k]) is not str or p[k] not in by_name for k in ('reserve','space','kickoff')):
        raise ValueError('missing original inline command-buffer boundaries')
    if len({p[k] for k in ('reserve','space','kickoff')})!=3:
        raise ValueError('aliased inline command-buffer boundaries')
    if (by_name[p['reserve']]['stack_bytes'],by_name[p['space']]['stack_bytes'],by_name[p['kickoff']]['stack_bytes'])!=(8,0,0):
        raise ValueError('unreviewed inline command-buffer ABI')
    if by_name[p['kickoff']]['register_sources']!={'ecx':'g_ecx'}:
        raise ValueError('unreviewed inline kickoff CDevice argument')
    for name in ('request','capacity'):
        require('Direct3D_CreateDevice','mov',f'dword ptr [{hex(p[name+"_global"])}], {hex(p[name+"_default"])}')
    require(p['reserve'],'mov',f'esi, dword ptr [{hex(layout["device_global"])}]')
    require(p['reserve'],'mov','edi, dword ptr [esi]')
    require(p['reserve'],'mov','dword ptr [esi + 4], eax')
    require(p['reserve'],'test','byte ptr [esi + 8], 4')
    require(p['reserve'],'add',f'eax, {hex((-p["margin"])&0xffffffff)}')
    require(p['space'],'mov',f'eax, dword ptr [{hex(p["request_global"])}]')
    require(p['space'],'shr','eax, 1')
    require(p['space'],'call',hex(int(by_name[p['reserve']]['address'],16)))
    require(p['kickoff'],'mov','eax, dword ptr [ecx]')
    return copy.deepcopy(p)


def emit_text(binding,interface,behavior_mask):
    calls=binding['calls'];layout=binding['layout'];cache=layout['cache_fields']
    names={c['name'] for c in calls}
    required=set(interface)
    absent=sorted(required-names)
    header=['#pragma once','#include <stdint.h>','enum PilotD3DEntry {']
    header += [f'    PILOT_D3D_{c["name"]},' for c in calls]
    header += ['    PILOT_D3D_COUNT,']
    header += [f'    PILOT_D3D_{name},' for name in absent]
    header += ['};','static const struct { const char *name; uint32_t address, stack_bytes, bytes; const char *sha256; } pilot_d3d_calls[] = {']
    header += [f'{{"{c["name"]}",{c["address"]}u,{c["stack_bytes"]}u,{c["bytes"]}u,"{c["function_sha256"]}"}},' for c in calls]
    header += ['};']
    if 'immediate' in binding:
        from native_d3d_immediate import emit_defines
        header += emit_defines(binding['immediate'])
    if 'float_beta_fvf' in binding:
        if binding['float_beta_fvf']!={'version':1,'max_components':4,'ignored_mask':0x1000}:
            raise ValueError('unreviewed float-beta FVF descriptor capability')
        header += ['#define PILOT_D3D_FVF_FLOAT_BETA_VERSION 1u']
    if 'lighting_cache' in binding:
        from native_d3d_lighting import emit_defines
        header += emit_defines(binding['lighting_cache'])
    if binding.get('cpu_services'):
        by_name={c['name']:c for c in calls};selected=binding['cpu_services'];indices={name:i for i,name in enumerate(selected)}
        header += ['#define PILOT_D3D_NATIVE_CPU_SERVICE_VERSION 1',
                   f'#define PILOT_D3D_NATIVE_CPU_SERVICE_COUNT {len(selected)}u']
        header += [f'extern void pilot_original_d3d_{name}(void);' for name in selected]
        header += ['static const struct {void (*function)(void);unsigned stack_bytes;} pilot_d3d_native_cpu_services[]={']
        header += [f'{{pilot_original_d3d_{name},{by_name[name]["original_stack_bytes"]}u}},' for name in selected]
        header += ['};','static const int16_t pilot_d3d_native_cpu_service_index[]={'+','.join(str(indices.get(c['name'],-1)) for c in calls)+'};']
    profile_define='PILOT_D3D_PROFILE_CLASSIC' if binding['adapter']=='xdk-d3d8-classic-cache-v1' else 'PILOT_D3D_PROFILE_5849'
    header += [f'#define {profile_define} 1','#define PILOT_D3D_STATEBLOCK_AVAILABLE 0',
               f'#define PILOT_D3D_CREATE_BEHAVIOR_MASK 0x{behavior_mask:x}u']
    if 'render_cache' in binding:
        from native_d3d_compact import emit as emit_compact
        header += emit_compact(binding)
    if 'packed_pixel_constants' in layout:
        header += [f'#define PILOT_D3D_PIXEL_PACKED_CONSTANTS 0x{layout["packed_pixel_constants"]:x}u']
    if 'gamma_cache' in layout:
        header += ['#define PILOT_D3D_GAMMA_CACHE_VERSION 1']
        header += [f'#define PILOT_D3D_GAMMA_{name.upper()} 0x{value:x}u' for name,value in layout['gamma_cache'].items()]
    if 'gamma_setter' in layout:
        header += ['#define PILOT_D3D_GAMMA_SETTER_VERSION 1']
    if 'tile_cache' in layout:
        header += ['#define PILOT_D3D_TILE_CACHE_VERSION 1']
        header += [f'#define PILOT_D3D_TILE_{name.upper()} {value}u' for name,value in layout['tile_cache'].items()]
    if 'sample_count' in layout:
        header += [f'#define PILOT_D3D_SAMPLE_COUNT_OFFSET {layout["sample_count"]}u']
    if 'guest_presentation' in binding:
        header += ['#define PILOT_D3D_GUEST_PRESENTATION_VERSION 1u',
                   f'#define PILOT_D3D_SAMPLE_AA_STATE {binding["guest_presentation"]["aa_state"]}u',
                   f'#define PILOT_D3D_SAMPLE_MASK_STATE {binding["guest_presentation"]["mask_state"]}u']
    if 'resource_retirement' in binding:
        retirement=binding['resource_retirement']
        header += [f'#define PILOT_D3D_RESOURCE_RETIRE_VERSION {retirement["version"]}u',
                   f'#define PILOT_D3D_RESOURCE_UNREGISTER 0x{retirement["unregister"]:x}u',
                   f'#define PILOT_D3D_RESOURCE_TAGGED_FREE 0x{retirement["tagged_free"]:x}u']
    if 'original_create_driver_mask' in binding:

        header += [f'#define PILOT_D3D_CREATE_DRIVER_MASK 0x{binding["original_create_driver_mask"]:x}u']
    if 'command_buffer' in binding:
        for name,value in binding['command_buffer'].items():
            if name in ('reserve','space','kickoff'):
                header += [f'#define PILOT_D3D_COMMANDS_{name.upper()}_ENTRY PILOT_D3D_{value}']
            else:header += [f'#define PILOT_D3D_COMMANDS_{name.upper()} 0x{value:x}u']
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
    if 'index_data_global' in layout:defines['INDEX_DATA_GLOBAL']=layout['index_data_global']
    if 'device_references' in layout:defines['DEVICE_REFERENCES']=layout['device_references']
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


def bind(context,proof,*,inspector=None):
    if inspector is None:inspector=inspect
    binding,header,wrapper=inspector(context,proof)
    generated=context.generated;record=context.output/'d3d-binding.aot-private.json'
    texts={'pilot_d3d_bindings.h':header,'pilot_d3d_binding.c':wrapper}
    if (generated/'pilot_d3d_binding.c').exists():
        if 'resource_retirement' in binding:
            previous=copy.deepcopy(binding);previous.pop('resource_retirement')
            for name in ('resource_unregister','resource_free'):previous['helpers'].pop(name)
            prior_header='\n'.join(line for line in header.splitlines() if not line.startswith((
                '#define PILOT_D3D_RESOURCE_RETIRE_VERSION ','#define PILOT_D3D_RESOURCE_UNREGISTER ',
                '#define PILOT_D3D_RESOURCE_TAGGED_FREE ')))+'\n'
            actual=json.loads(record.read_text())
            if (actual==previous and context.manifest.get('d3d_binding')==actual and
                    (generated/'pilot_d3d_binding.c').read_text()==wrapper and
                    (generated/'pilot_d3d_bindings.h').read_text()==prior_header):
                (generated/'pilot_d3d_bindings.h').write_text(header,encoding='utf-8',newline='\n')
                record.write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8',newline='\n')
                context.manifest['d3d_binding']=binding
                context.manifest['files']['pilot_d3d_bindings.h']=hashlib.sha256((generated/'pilot_d3d_bindings.h').read_bytes()).hexdigest()
                context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
                return binding
        # Correct only the exact old input-as-output alias. inspect() has
        # proved the new destination from the unchanged original helper.
        if ('transforms' in binding['layout'].get('cache_fields',{}) and
                'projection_viewport' in binding['layout'].get('transform_fields',{})):
            previous=copy.deepcopy(binding)
            previous['layout']['transform_fields']['projection_viewport']=binding['layout']['cache_fields']['transforms']+64
            current_define=f'#define PILOT_D3D_TRANSFORM_PROJECTION_VIEWPORT 0x{binding["layout"]["transform_fields"]["projection_viewport"]:x}u\n'
            old_define=f'#define PILOT_D3D_TRANSFORM_PROJECTION_VIEWPORT 0x{previous["layout"]["transform_fields"]["projection_viewport"]:x}u\n'
            previous_header=header.replace(current_define,old_define)
            actual=json.loads(record.read_text())
            if (actual==previous and actual!=binding and context.manifest.get('d3d_binding')==actual and
                    header.count(current_define)==1 and (generated/'pilot_d3d_binding.c').read_text()==wrapper and
                    (generated/'pilot_d3d_bindings.h').read_text()==previous_header):
                (generated/'pilot_d3d_bindings.h').write_text(header,encoding='utf-8',newline='\n')
                record.write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8',newline='\n')
                context.manifest['d3d_binding']=binding
                context.manifest['files']['pilot_d3d_bindings.h']=hashlib.sha256((generated/'pilot_d3d_bindings.h').read_bytes()).hexdigest()
                context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
                return binding
        additions={'index_data_global':'INDEX_DATA_GLOBAL','device_references':'DEVICE_REFERENCES'}
        available=[key for key in additions if key in binding['layout']]
        for count in range(1,len(available)+1):
            for missing in combinations(available,count):
                previous=copy.deepcopy(binding)
                for key in missing:previous['layout'].pop(key)
                removed=tuple('#define PILOT_D3D_'+additions[key]+' ' for key in missing)
                previous_header='\n'.join(line for line in header.splitlines() if not line.startswith(removed))+'\n'
                actual=json.loads(record.read_text())
                if (actual==previous and context.manifest.get('d3d_binding')==actual and
                        (generated/'pilot_d3d_binding.c').read_text()==wrapper and
                        (generated/'pilot_d3d_bindings.h').read_text()==previous_header):
                    (generated/'pilot_d3d_bindings.h').write_text(header,encoding='utf-8',newline='\n')
                    record.write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8',newline='\n')
                    context.manifest['d3d_binding']=binding
                    context.manifest['files']['pilot_d3d_bindings.h']=hashlib.sha256((generated/'pilot_d3d_bindings.h').read_bytes()).hexdigest()
                    context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
                    return binding
        if 'command_buffer' in binding:
            previous=copy.deepcopy(binding);previous.pop('command_buffer')
            actual=json.loads(record.read_text())
            previous_header='\n'.join(line for line in header.splitlines()
                                      if not line.startswith('#define PILOT_D3D_COMMANDS_'))+'\n'
            if (actual==previous and context.manifest.get('d3d_binding')==actual and
                (generated/'pilot_d3d_binding.c').read_text()==wrapper and
                (generated/'pilot_d3d_bindings.h').read_text()==previous_header):
                (generated/'pilot_d3d_bindings.h').write_text(header,encoding='utf-8',newline='\n')
                record.write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8',newline='\n')
                context.manifest['d3d_binding']=binding
                context.manifest['files']['pilot_d3d_bindings.h']=hashlib.sha256((generated/'pilot_d3d_bindings.h').read_bytes()).hexdigest()
                context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
                return binding
        previous_presentation=copy.deepcopy(binding)
        previous_presentation.pop('guest_presentation',None)
        previous_presentation.get('helpers',{}).pop('resolve_setup',None)
        prior_binding=copy.deepcopy(previous_presentation)
        prior_binding.pop('native_create_behavior_mask',None);prior_binding.pop('original_create_driver_mask',None)
        prior_binding['layout'].pop('presentation_fields',None)
        actual_record=json.loads(record.read_text())
        if (actual_record not in (binding,previous_presentation,prior_binding) or context.manifest.get('d3d_binding')!=actual_record
                or (generated/'pilot_d3d_binding.c').read_text()!=wrapper):
            raise ValueError('existing LTCG graphics binding differs')
        old_header=(generated/'pilot_d3d_bindings.h').read_text()
        if old_header==header and actual_record==binding:return binding
        presentation_defines={'PILOT_D3D_GUEST_PRESENTATION_VERSION','PILOT_D3D_SAMPLE_AA_STATE','PILOT_D3D_SAMPLE_MASK_STATE'}
        preceding_header='\n'.join(line for line in header.splitlines() if not
            (line.startswith('#define ') and line.split()[1] in presentation_defines))+'\n'
        if actual_record==previous_presentation and old_header==preceding_header:
            (generated/'pilot_d3d_bindings.h').write_text(header,encoding='utf-8',newline='\n')
            record.write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8',newline='\n')
            context.manifest['d3d_binding']=binding
            context.manifest['files']['pilot_d3d_bindings.h']=hashlib.sha256((generated/'pilot_d3d_bindings.h').read_bytes()).hexdigest()
            context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
            return binding
        # Only the exact previous body/layout record can gain the newly proved
        # fields and public policy. Wrapper and retained bodies stay identical.
        new_defines=presentation_defines|{'PILOT_D3D_CREATE_DRIVER_MASK','PILOT_D3D_SURFACE_PROJECTION_VERSION',
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
