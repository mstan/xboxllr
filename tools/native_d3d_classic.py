"""Exact-image classic XDK ABI/layout admission for the shared native renderer.

No game or original bytes are embedded here. Profiles retain complete body
identities, corrected ABI evidence, decoded layout references and cache facts.
Unsupported service methods still guard in the renderer.
"""
from __future__ import annotations
import copy
import json
import re
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from native_d3d_ltcg import INTERFACE, marshal, emit_text

ADAPTER = 'xdk-d3d8-classic-cache-v1'
CPU_SERVICES={'D3D_GetAdapterModeCount','D3D_EnumAdapterModes','CMiniport_GetDisplayCapabilities',
              'D3D_GetDeviceCaps','D3D_CheckDeviceFormat','D3DDevice_SetLight','D3DDevice_LightEnable',
              'D3DDevice_SetMaterial','D3DDevice_GetGammaRamp','D3D_AllocContiguousMemory','D3DResource_Register',
              'D3DDevice_GetTile','D3DSurface_GetDesc','Get2DSurfaceDesc','D3DResource_GetType'}


def inspect(context, proof):
    if proof.get('adapter') != ADAPTER or proof.get('stateblock') is not False:
        raise ValueError('unreviewed classic graphics adapter/features')
    if (proof.get('native_create_behavior_mask'), proof.get('original_create_driver_mask')) != (0x40, 0x10):
        raise ValueError('unreviewed classic creation policy')
    functions = {int(f['start'], 16): f for f in context.functions}
    if 'classic_layout' in proof and len(functions)!=len(context.functions):
        raise ValueError('ambiguous full-index canonical function inventory')
    symbols = [s for s in context.symbols if s['library'] == 'D3D8' and s['kind'] == 'function']
    rows = proof['functions']
    if (len({r['address'] for r in rows}) != len(rows) or len({r['name'] for r in rows}) != len(rows) or
            {(r['address'], r['source_name']) for r in rows} != {(s['address'], s['name']) for s in symbols}):
        raise ValueError('classic graphics inventory changed')
    def read(address, size):
        if 'classic_layout' in proof:
            sections=[s for s in context.image['sections'] if s['address']<=address and
                      address+size<=s['address']+s['raw_size']]
            if len(sections)!=1:raise ValueError('full-index proof outside unique original bytes')
            offset=sections[0]['raw_offset']+address-sections[0]['address']
            raw=context.blob[offset:offset+size]
            if len(raw)!=size:raise ValueError('truncated full-index original proof')
            return raw
        section = next((s for s in context.image['sections'] if s['address'] <= address and
                        address + size <= s['address'] + s['raw_size']), None)
        if section is None: raise ValueError('classic proof outside original bytes')
        offset = section['raw_offset'] + address - section['address']
        return context.blob[offset:offset+size]
    codes = {}
    def decode(name, address, body):
        f = functions.get(address)
        if f is None or not context.matches_body(f, body):
            raise ValueError('unreviewed classic original body: ' + name)
        code = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(read(address, f['size']), address))
        if not code or code[-1].address + code[-1].size != address + f['size']:
            raise ValueError('incomplete classic original decode: ' + name)
        codes[name] = code
        return f, code
    interface = {r['name']: r for r in json.loads(INTERFACE.read_text())}
    calls = []
    for row in rows:
        if not re.fullmatch('[A-Za-z_][A-Za-z_0-9]*', row['name']):
            raise ValueError('invalid classic service name')
        symbol = next(s for s in symbols if s['address'] == row['address'])
        if {k: symbol[k] for k in ('call', 'params', 'stack_bytes')} != row['source_signature']:
            raise ValueError('classic SDK discovery signature changed')
        f, code = decode(row['name'], row['address'], row['body'])
        returns = {int(i.op_str or '0', 0) for i in code if i.mnemonic == 'ret'}
        if not returns and row.get('tail_target'):
            target = row['tail_target']
            if code[-1].mnemonic != 'jmp' or code[-1].op_str != hex(target['address']):
                raise ValueError('classic tail target changed')
            _, tail_code = decode(row['name'] + '_tail', target['address'], target['body'])
            returns = {int(i.op_str or '0', 0) for i in tail_code if i.mnemonic == 'ret'}
        if returns != {row['signature']['stack_bytes']}:
            raise ValueError('classic ABI disagrees with original RET: ' + row['name'])
        canonical, args, registers = marshal(row, interface)
        calls.append(dict(name=row['name'], symbol=f['name'], address=f['start'], bytes=f['size'],
                          function_sha256=row['body']['sha256'], call='canonical',
                          stack_bytes=canonical['stack_bytes'], params=canonical['params'],
                          original_stack_bytes=row['signature']['stack_bytes'], original_signature=row['signature'],
                          source_signature=row['source_signature'], argument_sources=args, register_sources=registers))
    for name, helper in proof['helpers'].items(): decode(name, helper['address'], helper)
    layout = proof['layout']
    if 'sample_count' in layout:
        sample=layout['sample_count']
        if (type(sample) is not int or sample<0 or sample%4 or sample+4>layout['device_bytes'] or
                not any(i.mnemonic=='mov' and i.op_str==f'eax, dword ptr [eax + {hex(sample)}]'
                        for i in codes['Get2DSurfaceDesc']) or 'surface_dimensions' not in proof['helpers']):
            raise ValueError('unreviewed original surface description cache')
    if 'tile_cache' in layout:
        tile=layout['tile_cache']
        if (set(tile)!={'offset','count','stride'} or tile['count']!=8 or tile['stride']!=24 or
                type(tile['offset']) is not int or tile['offset']<0 or tile['offset']%4 or
                tile['offset']+tile['count']*tile['stride']>layout['device_bytes']):
            raise ValueError('unreviewed classic tile cache layout')
        if not any(i.mnemonic=='lea' and i.op_str==f'esi, [ecx + eax*8 + {hex(tile["offset"])}]'
                   for i in codes['D3DDevice_GetTile']):
            raise ValueError('classic tile getter cache reference changed')
        if not any(i.mnemonic=='lea' and i.op_str==f'edi, [ebx + ecx*8 + {hex(tile["offset"])}]'
                   for i in codes['D3DDevice_SetTile']):
            raise ValueError('classic tile setter cache reference changed')
    def field(path):
        value = layout
        for key in path.split('.'): value = value[key]
        if type(value) is not int or value < 0: raise ValueError('invalid classic layout field')
        return value
    seen = set()
    for ref in proof['references']:
        operands = ref['operands']
        if 'field' in ref:
            value=field(ref['field'])
            if 'relative_to' in ref:value-=field(ref['relative_to'])
            if '{value}' not in operands:raise ValueError('layout reference has no field operand')
            operands = operands.format(value=str(value) if ref.get('decimal') else hex(value))
            seen.add(ref['field'])
        if not any(i.mnemonic == ref['mnemonic'] and i.op_str == operands for i in codes[ref['function']]):
            raise ValueError('classic original reference changed: ' + ref['function'] + '/' + operands)
    required = {'device_global','device_object','shader_mode','swap_counter','vblank_callback',
                'device_caps','render_defaults','texture_defaults','surface_formats','fvf_object',
                'vertex_program_cache','vertex_constants','vertex_start','vertex_record_flags',
                'resource_fence','texture_mode','packed_pixel_constants'}
    required |= {'cache_fields.'+k for k in ('pixel_shader','vertex_object','vertex_shader','indices','base_vertex','textures','transforms','viewport')}
    required |= {'transform_fields.'+k for k in ('projection_depth','depth_scale','pixel_scale','projection_viewport','screen_offset')}
    required |= {'presentation_fields.'+k for k in ('count','colors','depth','current_color','current_depth')}
    if not required <= seen: raise ValueError('missing decoded classic layout references: ' + str(sorted(required-seen)))
    variables = {s['name']: s['address'] for s in context.symbols if s['library']=='D3D8' and s['kind']=='variable'}
    for name, path in {'D3D_g_pDevice':'device_global','D3D_g_Stream':'streams',
                       'D3D_g_RenderState':'render_states','D3D_g_DeferredTextureState':'texture_states',
                       'D3DDevice__m_VBlankCallback_OFFSET':'vblank_callback'}.items():
        if variables.get(name) != field(path): raise ValueError('classic SDK variable changed: ' + name)
    from native_d3d_compact import inspect as inspect_compact
    compact = inspect_compact(proof,variables,layout,codes,read)
    if layout['yuv_state'] != 160: raise ValueError('invalid semantic YUV state')
    if not 0 < layout['device_bytes'] <= 0x10000: raise ValueError('invalid classic device extent')
    if 'classic_layout' in proof:
        from native_d3d_full import inspect_layout
        compact['full_index_layout']=inspect_layout(context,proof,layout,codes)
    elif (layout['device_object']+layout['device_bytes']!=layout['device_global'] or
            layout['vertex_constants']-layout['device_object']+192*16!=layout['vertex_program_cache'] or
            layout['vertex_program_cache']+136*16!=layout['shader_mode']):
        raise ValueError('classic static device/cache extents disagree')
    for group in ('cache_fields','transform_fields','presentation_fields'):
        if any(type(v) is not int or v < 0 or v%4 or v+4 > layout['device_bytes'] for v in layout[group].values()):
            raise ValueError('classic cache field outside device')
    if 'gamma_cache' in layout:
        gamma=layout['gamma_cache']
        if (set(gamma)!={'ramps','pending','current'} or gamma['ramps']+1536!=gamma['pending'] or
                gamma['pending']+8!=gamma['current'] or gamma['current']+4>layout['device_bytes'] or
                not {'gamma_cache.ramps','gamma_cache.current'}<=seen or 'gamma_initializer' not in codes):
            raise ValueError('unreviewed classic gamma cache')
    if 'gamma_setter' in layout:
        setter=layout['gamma_setter'];gamma=layout.get('gamma_cache')
        if (gamma is None or set(setter)!={'miniport','deferred_flags_mask'} or
                type(setter['miniport']) is not int or setter['miniport']<0 or
                setter['miniport']>gamma['ramps'] or type(setter['deferred_flags_mask']) is not int or
                setter['deferred_flags_mask']!=1):
            raise ValueError('unreviewed classic gamma setter policy')
        required=(('lea',f'edx, [eax + {hex(setter["miniport"])}]'),
                  ('mov',f'eax, dword ptr [eax + {hex(layout["swap_counter"])}]'),
                  ('lea',f'ebx, [ecx + edx + {hex(gamma["ramps"]-setter["miniport"])}]'),
                  ('mov',f'dword ptr [edx + {hex(gamma["current"]-setter["miniport"])}], eax'),
                  ('mov',f'dword ptr [edx + eax*4 + {hex(gamma["pending"]-setter["miniport"])}], 1'),
                  ('test','byte ptr [esp + 0x10], 2'))
        for mnemonic,operands in required:
            if not any(i.mnemonic==mnemonic and i.op_str==operands for i in codes['D3DDevice_SetGammaRamp']):
                raise ValueError('original gamma setter layout changed')
    matrices=layout['cache_fields']['transforms'];combined=layout['transform_fields']['projection_viewport']
    if not (combined+64 <= matrices or combined >= matrices+640):
        raise ValueError('classic combined transform aliases raw projection')
    default_count=compact['render_cache']['default_count']
    facts=dict(render_defaults=list(struct.unpack(f'<{default_count}I',read(layout['render_defaults'],default_count*4))),
               texture_defaults=list(read(layout['texture_defaults'],32)),
               device_caps=list(struct.unpack('<53I',read(layout['device_caps'],212))),
               surface_formats=[dict(format=f,flags=read(layout['surface_formats']+f,1)[0])
                                for f in (4,5,6,7,0xc,0xe,0xf,0x12,0x1e,0x2e,0x24,0x25,0x28,0,0x11)])
    binding=dict(xbe_sha256=context.image['sha256'],adapter=ADAPTER,device_global=layout['device_global'],
                 device_object=layout['device_object'],vertex_shader_offset=layout['cache_fields']['vertex_object'],
                 layout=copy.deepcopy(layout),calls=calls,helpers=proof['helpers'],stateblock=False,
                 **compact,native_create_behavior_mask=0x40,
                 original_create_driver_mask=0x10,**facts)
    if 'cpu_services' in proof:
        selected=proof['cpu_services'];by_name={c['name']:c for c in calls}
        if not isinstance(selected,list) or len(set(selected))!=len(selected) or not set(selected)<=CPU_SERVICES:
            raise ValueError('unreviewed original CPU service selection')
        for name in selected:
            call=by_name.get(name)
            if (call is None or call['register_sources'] or call['stack_bytes']!=call['original_stack_bytes'] or
                    any(p['type']!='psh' for p in call['original_signature']['params'])):
                raise ValueError('original CPU service cannot borrow this caller ABI')
        if 'D3DDevice_GetGammaRamp' in selected and 'gamma_cache' not in layout:
            raise ValueError('original gamma getter requires its initialized cache proof')
        if {'D3DSurface_GetDesc','Get2DSurfaceDesc'}&set(selected) and 'sample_count' not in layout:
            raise ValueError('original surface description requires its sample cache proof')
        binding['cpu_services']=list(selected)
    if 'command_buffer' in proof:
        p=proof['command_buffer']
        if ((p['version'],p['current_offset'],p['limit_offset'],p['flags_offset'],p['record_mask'])!=(1,0,4,8,4) or
                p['margin']!=512 or p['request_default']!=0x8000 or p['capacity_default']!=0x80000 or
                {p['reserve'],p['space'],p['kickoff']} != {'D3D_MakeRequestedSpace_8','D3DDevice_MakeSpace','CDevice_KickOff'}):
            raise ValueError('unreviewed classic inline command-buffer policy')
        binding['command_buffer']=copy.deepcopy(p)
    if type(proof.get('immediate',False)) is not bool:
        raise ValueError('invalid immediate graphics profile admission')
    if proof.get('immediate'):
        from native_d3d_immediate import inspect as inspect_immediate
        helper=proof['helpers'].get('immediate_flush')
        if helper is None:raise ValueError('immediate Begin requires its original state-flush helper')
        for name in ('D3DDevice_Begin','D3DDevice_End','D3DDevice_SetVertexData2f','D3DDevice_SetVertexData4f'):
            if not any(i.mnemonic=='mov' and i.op_str==f'esi, dword ptr [{hex(layout["device_global"])}]'
                       for i in codes.get(name,())):
                raise ValueError('immediate entry has a different device owner: '+name)
        binding['immediate']=inspect_immediate(codes,interface,[c['name'] for c in calls],
            flush_helper_address=helper['address'],enabled=True)
    if type(proof.get('float_beta_fvf',False)) is not bool:
        raise ValueError('invalid float-beta FVF profile admission')
    if proof.get('float_beta_fvf'):
        # The full helper is already body-hash checked. Verify the float-beta
        # descriptor/offset calculation rather than importing desktop index
        # semantics for the otherwise ignored bit1000.
        decoder={(i.mnemonic,i.op_str) for i in codes['fvf_decoder']}
        required={('and','esi, 0xe'),('lea','edx, [esi - 6]'),('shr','edx, 1'),
            ('inc','edx'),('mov','ecx, edx'),('shl','ecx, 4'),('or','ecx, 2'),
            ('mov','dword ptr [eax + 0x28], 0xc'),('mov','dword ptr [eax + 0x2c], ecx'),
            ('lea','edx, [edx*4 + 0xc]')}
        if not required<=decoder:raise ValueError('unreviewed original float-beta FVF decoder')
        binding['float_beta_fvf']={'version':1,'max_components':4,'ignored_mask':0x1000}
    if 'lighting_cache' in proof:
        from native_d3d_lighting import inspect_lighting
        binding['lighting_cache']=inspect_lighting(context,proof,codes)
    return binding, *emit_text(binding,interface,0x40)


def bind(context, proof):
    from native_d3d_ltcg import bind as bind_wrappers
    validated=inspect(context,proof)
    binding,header,wrapper=validated
    record=context.output/'d3d-binding.aot-private.json'
    # Full preparation deliberately removes generated wrappers before binding
    # freshly translated bodies. Its retained record is historical evidence,
    # not a request to upgrade wrapper files which no longer exist.
    if record.exists() and (context.generated/'pilot_d3d_binding.c').exists():
        previous=json.loads(record.read_text())
        def retained(old,new):
            if isinstance(old,dict):return isinstance(new,dict) and all(k in new and retained(v,new[k]) for k,v in old.items())
            if isinstance(old,list):return isinstance(new,list) and len(old)<=len(new) and all(retained(v,new[i]) for i,v in enumerate(old))
            return old==new
        if previous!=binding and previous.get('adapter')==ADAPTER and retained(previous,binding):
            interface={r['name']:r for r in json.loads(INTERFACE.read_text())}
            old_header,old_wrapper=emit_text(previous,interface,0x40)
            old_headers=[old_header]
            if not any(c['name']=='D3DDevice_CreateIndexBuffer2' for c in previous['calls']):
                # Older canonical inventories omitted this optional enum even
                # though cross-profile contract fixtures refer to it. Only an
                # absent entry after COUNT may be added without an ABI change.
                old_headers.append(old_header.replace('    PILOT_D3D_D3DDevice_CreateIndexBuffer2,\n',''))
            if (context.manifest.get('d3d_binding')!=previous or
                    (context.generated/'pilot_d3d_bindings.h').read_text() not in old_headers or
                    (context.generated/'pilot_d3d_binding.c').read_text()!=old_wrapper):
                raise ValueError('existing classic source/metadata drifted before additive proof')
            # Only independently inspected additive original facts can upgrade
            # a prior exact header. Original bodies and existing facts survive.
            for name,text in (('pilot_d3d_bindings.h',header),('pilot_d3d_binding.c',wrapper)):
                path=context.generated/name
                if path.read_text()!=text:path.write_text(text,encoding='utf-8',newline='\n')
            record.write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8',newline='\n')
            context.manifest['d3d_binding']=binding
            import hashlib
            for name in ('pilot_d3d_bindings.h','pilot_d3d_binding.c'):
                context.manifest['files'][name]=hashlib.sha256((context.generated/name).read_bytes()).hexdigest()
            context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
            return binding
    return bind_wrappers(context,proof,inspector=lambda *_:validated)
