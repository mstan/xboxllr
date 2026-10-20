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


def inspect(context, proof):
    if proof.get('adapter') != ADAPTER or proof.get('stateblock') is not False:
        raise ValueError('unreviewed classic graphics adapter/features')
    if (proof.get('native_create_behavior_mask'), proof.get('original_create_driver_mask')) != (0x40, 0x10):
        raise ValueError('unreviewed classic creation policy')
    functions = {int(f['start'], 16): f for f in context.functions}
    symbols = [s for s in context.symbols if s['library'] == 'D3D8' and s['kind'] == 'function']
    rows = proof['functions']
    if (len({r['address'] for r in rows}) != len(rows) or len({r['name'] for r in rows}) != len(rows) or
            {(r['address'], r['source_name']) for r in rows} != {(s['address'], s['name']) for s in symbols}):
        raise ValueError('classic graphics inventory changed')
    def read(address, size):
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
    mapping = proof['render_cache']
    expected = list(range(82)) + [-1]*10 + list(range(82,117)) + [-1]*9 + list(range(117,139)) + [-1] + list(range(139,146))
    if (mapping.get('count') != 146 or mapping.get('map') != expected or
            mapping.get('default_count') != 89 or mapping.get('default_skips') != [116,135]):
        raise ValueError('unreviewed classic compact render-state contract')
    if (variables.get('D3D_g_DeferredRenderState') != layout['render_states']+82*4 or
            variables.get('D3D_g_ComplexRenderState') != layout['render_states']+117*4 or
            variables.get('D3DRS_CullMode') != layout['render_states']+128*4 or
            variables.get('D3DRS_YuvEnable') != layout['render_states']+140*4):
        raise ValueError('classic compact state boundaries changed')
    if layout['yuv_state'] != 160: raise ValueError('invalid semantic YUV state')
    if not 0 < layout['device_bytes'] <= 0x10000: raise ValueError('invalid classic device extent')
    if (layout['device_object']+layout['device_bytes']!=layout['device_global'] or
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
    matrices=layout['cache_fields']['transforms'];combined=layout['transform_fields']['projection_viewport']
    if not (combined+64 <= matrices or combined >= matrices+640):
        raise ValueError('classic combined transform aliases raw projection')
    facts=dict(render_defaults=list(struct.unpack('<89I',read(layout['render_defaults'],356))),
               texture_defaults=list(read(layout['texture_defaults'],32)),
               device_caps=list(struct.unpack('<53I',read(layout['device_caps'],212))),
               surface_formats=[dict(format=f,flags=read(layout['surface_formats']+f,1)[0])
                                for f in (4,5,6,7,0xc,0xe,0xf,0x12,0x1e,0x2e,0x24,0x25,0x28)])
    binding=dict(xbe_sha256=context.image['sha256'],adapter=ADAPTER,device_global=layout['device_global'],
                 device_object=layout['device_object'],vertex_shader_offset=layout['cache_fields']['vertex_object'],
                 layout=copy.deepcopy(layout),calls=calls,helpers=proof['helpers'],stateblock=False,
                 render_cache=copy.deepcopy(mapping),native_create_behavior_mask=0x40,
                 original_create_driver_mask=0x10,**facts)
    if 'command_buffer' in proof:
        p=proof['command_buffer']
        if ((p['version'],p['current_offset'],p['limit_offset'],p['flags_offset'],p['record_mask'])!=(1,0,4,8,4) or
                p['margin']!=512 or p['request_default']!=0x8000 or p['capacity_default']!=0x80000 or
                {p['reserve'],p['space'],p['kickoff']} != {'D3D_MakeRequestedSpace_8','D3DDevice_MakeSpace','CDevice_KickOff'}):
            raise ValueError('unreviewed classic inline command-buffer policy')
        binding['command_buffer']=copy.deepcopy(p)
    return binding, *emit_text(binding,interface,0x40)


def bind(context, proof):
    from native_d3d_ltcg import bind as bind_wrappers
    validated=inspect(context,proof)
    binding,header,wrapper=validated
    record=context.output/'d3d-binding.aot-private.json'
    if record.exists():
        previous=json.loads(record.read_text())
        def retained(old,new):
            if isinstance(old,dict):return isinstance(new,dict) and all(k in new and retained(v,new[k]) for k,v in old.items())
            return old==new
        if previous!=binding and previous.get('adapter')==ADAPTER and retained(previous,binding):
            interface={r['name']:r for r in json.loads(INTERFACE.read_text())}
            old_header,old_wrapper=emit_text(previous,interface,0x40)
            if (context.manifest.get('d3d_binding')!=previous or
                    (context.generated/'pilot_d3d_bindings.h').read_text()!=old_header or
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
