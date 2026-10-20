"""Source-bound classic CPU lighting caches for the shared native draw owner.

The original CPU setters remain authoritative. Admission describes their
cache and selector contract; it does not qualify a GPU lighting consumer.
"""
import copy


_FIELDS = ('table', 'capacity', 'head', 'material', 'stride', 'flags', 'next')
_HELPERS = {'sources', 'light_colors', 'global_colors', 'consumer'}
_FRONT_SOURCES = [111, 110, 112, 113]  # diffuse, specular, ambient, emissive
_BACK_SOURCES = [107, 106, 108, 109]
_SOURCE_VALUES = {'material': 0, 'diffuse': 1, 'specular': 2}
_CONTROLS = {'local_viewer': 104, 'color_vertex': 105}


def inspect_lighting(context, proof, codes):
    selected = proof.get('lighting_cache')
    if selected is None:
        return None
    if (not isinstance(selected, dict) or
            set(selected) != {'version', 'helpers', *_FIELDS} or
            type(selected['version']) is not int or selected['version'] != 1 or
            any(type(selected[k]) is not int or selected[k] < 0 for k in _FIELDS) or
            not isinstance(selected['helpers'], dict) or
            set(selected['helpers']) != _HELPERS or
            any(not isinstance(v, str) or v not in proof['helpers'] or v not in codes
                for v in selected['helpers'].values()) or
            len(set(selected['helpers'].values())) != len(_HELPERS)):
        raise ValueError('invalid classic lighting cache proof')
    p = selected
    layout = proof['layout']
    if ((p['stride'], p['flags'], p['next']) != (0x90, 0x68, 0x8c) or
            p['capacity'] != p['table'] + 4 or p['head'] != p['capacity'] + 4 or
            p['head'] < p['next'] or
            any(p[k] % 4 or p[k] + 4 > layout['device_bytes']
                for k in ('table', 'capacity', 'head')) or
            p['material'] % 4 or p['material'] + 136 > layout['device_bytes'] or
            not (p['head'] + 4 <= p['material'] or
                 p['material'] + 136 <= p['table'])):
        raise ValueError('unreviewed classic lighting cache extent')
    owners = {'D3DDevice_SetMaterial': 4, 'D3DDevice_SetLight': 8,
              'D3DDevice_LightEnable': 8}
    if not set(owners) <= set(proof.get('cpu_services', [])):
        raise ValueError('lighting requires its original CPU setters')

    def require(name, mnemonic, operands):
        if not any(i.mnemonic == mnemonic and i.op_str == operands
                   for i in codes[name]):
            raise ValueError('original lighting reference changed: ' + name + '/' + operands)

    for name, stack in owners.items():
        if name not in codes or {int(i.op_str or '0', 0) for i in codes[name]
                                 if i.mnemonic == 'ret'} != {stack}:
            raise ValueError('original lighting setter ABI changed: ' + name)
    for name in owners:
        # These are static-device APIs, rather than COM methods with a self arg.
        register = {'D3DDevice_SetMaterial': 'edi', 'D3DDevice_SetLight': 'ebp',
                    'D3DDevice_LightEnable': 'esi'}[name]
        require(name, 'mov', f'{register}, dword ptr [{hex(layout["device_global"])}]')
    require('D3DDevice_SetMaterial', 'add', f'edi, {hex(p["material"])}')
    require('D3DDevice_SetMaterial', 'mov', 'ecx, 0x11')
    require('D3DDevice_SetMaterial', 'rep movsd', 'dword ptr es:[edi], dword ptr [esi]')
    require('D3DDevice_SetLight', 'mov', f'edi, dword ptr [ebp + {hex(p["capacity"])}]')
    require('D3DDevice_SetLight', 'mov', f'dword ptr [ebp + {hex(p["table"])}], ebx')
    require('D3DDevice_SetLight', 'mov', f'dword ptr [ebp + {hex(p["capacity"])}], esi')
    require('D3DDevice_SetLight', 'lea', 'ebx, [ebx + ebx*8]')
    require('D3DDevice_SetLight', 'shl', 'ebx, 4')
    require('D3DDevice_SetLight', 'mov', 'ecx, 0x1a')
    require('D3DDevice_SetLight', 'mov', f'ecx, dword ptr [ebx + {hex(p["flags"])}]')
    require('D3DDevice_SetLight', 'mov', f'dword ptr [ebx + {hex(p["flags"])}], ecx')
    require('D3DDevice_LightEnable', 'cmp', f'ebx, dword ptr [esi + {hex(p["capacity"])}]')
    require('D3DDevice_LightEnable', 'mov', f'ecx, dword ptr [esi + {hex(p["table"])}]')
    require('D3DDevice_LightEnable', 'test', f'byte ptr [ecx + eax + {hex(p["flags"])}], 1')
    require('D3DDevice_LightEnable', 'mov', f'eax, dword ptr [esi + {hex(p["head"])}]')
    require('D3DDevice_LightEnable', 'lea', f'edx, [esi + {hex(p["head"] - p["next"])}]')
    require('D3DDevice_LightEnable', 'mov', f'eax, dword ptr [eax + {hex(p["next"])}]')
    require('D3DDevice_LightEnable', 'mov', f'dword ptr [ecx + {hex(p["next"])}], edx')
    require('D3DDevice_LightEnable', 'mov', f'dword ptr [esi + {hex(p["head"])}], ecx')
    light = next(row for row in proof['functions'] if row['name'] == 'D3DDevice_SetLight')
    require('D3DDevice_LightEnable', 'call', hex(light['address']))

    source = p['helpers']['sources']
    mapping = proof['render_cache']['map']

    def state_address(state):
        index = mapping[state]
        if index < 0:
            raise ValueError('lighting selector has no original cache slot')
        return layout['render_states'] + index * 4

    require(source, 'mov', f'ecx, dword ptr [{hex(state_address(105))}]')
    for state in range(106, 114):
        require(source, 'mov', f'{"eax" if state == 106 else "ecx"}, dword ptr [{hex(state_address(state))}]')
    # Missing COLOR1/COLOR2 attributes clear their respective selector bits.
    for bits, mask in ((4, 0xffffffaa), (8, 0xffffff55),
                       (0x10, 0xffffaaff), (0x20, 0xffff55ff)):
        require(source, 'test', f'ch, {hex(bits) if bits >= 10 else bits}')
        require(source, 'and', f'eax, {hex(mask)}')
    light_colors = p['helpers']['light_colors']
    global_colors = p['helpers']['global_colors']
    require(light_colors, 'lea', f'esi, [edi + {hex(p["material"])}]')
    require(light_colors, 'lea', f'esi, [edi + {hex(p["material"] + 68)}]')
    for mask in (0xc, 0x30, 0xc0):
        require(light_colors, 'test', f'dl, {hex(mask)}')
    require(light_colors, 'shr', 'edx, 8')
    require(global_colors, 'lea', f'edx, [esi + {hex(p["material"])}]')
    require(global_colors, 'lea', f'edx, [esi + {hex(p["material"] + 68)}]')
    require(global_colors, 'test', 'bl, 3')
    require(global_colors, 'test', 'bl, 0xc')
    require(global_colors, 'shr', 'ebx, 8')
    consumer = p['helpers']['consumer']
    for role, stack in (('sources', 0), ('light_colors', 16),
                        ('global_colors', 8), ('consumer', 4)):
        name = p['helpers'][role]
        if {int(i.op_str or '0', 0) for i in codes[name] if i.mnemonic == 'ret'} != {stack}:
            raise ValueError('original lighting helper ABI changed: ' + role)
        if role != 'consumer':
            require(consumer, 'call', hex(proof['helpers'][name]['address']))
    require(consumer, 'cmp', f'dword ptr [{hex(state_address(104))}], ebp')
    require(consumer, 'mov', 'esi, 0x10001')
    require(consumer, 'mov', 'dword ptr [edx], 0x40294')
    require(consumer, 'mov', 'dword ptr [edx + 4], esi')
    require(consumer, 'mov', f'edi, dword ptr [ebx + {hex(p["head"])}]')
    require(consumer, 'mov', f'edi, dword ptr [edi + {hex(p["next"])}]')
    require(consumer, 'add', 'ebx, 0x80')
    require(consumer, 'cmp', 'ebx, 0x41424')
    require(consumer, 'add', f'eax, {hex(layout["cache_fields"]["transforms"])}')

    # This capability describes the original first-load BSS defaults, not
    # a second initialization that overwrites later original CPU writes.
    start = layout['device_object']
    end = start + layout['device_bytes']
    sections = [s for s in context.image['sections'] if s['flags'] & 2 and
                s['address'] + s['raw_size'] <= start and end <= s['address'] + s['size']]
    if len(sections) != 1:
        raise ValueError('original lighting device lacks its zero-fill bootstrap')
    section = sections[0]
    result = copy.deepcopy(p)
    result.update(material_sources=list(_FRONT_SOURCES), back_material_sources=list(_BACK_SOURCES),
                  source_values=dict(_SOURCE_VALUES), controls=dict(_CONTROLS),
                  bootstrap={'section': section['index'],
                             'zero_start': section['address'] + section['raw_size'],
                             'zero_end': section['address'] + section['size']})
    return result


def emit_defines(cache):
    if (not isinstance(cache, dict) or cache.get('version') != 1 or
            set(cache) != {'version', 'helpers', 'material_sources', 'back_material_sources',
                           'source_values', 'controls', 'bootstrap', *_FIELDS} or
            cache['material_sources'] != _FRONT_SOURCES or
            cache['back_material_sources'] != _BACK_SOURCES or
            cache['source_values'] != _SOURCE_VALUES or
            cache['controls'] != _CONTROLS or
            any(type(cache[k]) is not int or cache[k] < 0 for k in _FIELDS) or
            (cache['stride'], cache['flags'], cache['next']) != (0x90, 0x68, 0x8c)):
        raise ValueError('unreviewed native lighting cache binding')
    return (['#define PILOT_D3D_LIGHTING_CACHE_VERSION 1u'] + [
        f'#define PILOT_D3D_LIGHTING_{name.upper()} 0x{cache[name]:x}u' for name in _FIELDS] + [
        f'#define PILOT_D3D_LIGHTING_{name.upper()}_STATE {state}u'
        for name, state in cache['controls'].items()])
