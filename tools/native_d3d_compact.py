"""Reviewed classic SDK cache policies over the common semantic state ABI.

Policies describe SDK interfaces, never titles. Original body identities remain
the classic inspector's responsibility; this checks their source relationships.
"""
from __future__ import annotations
import copy
import hashlib
import re
import struct


def mapping_4134():
    return dict(count=146, map=list(range(82)) + [-1]*10 + list(range(82,117)) +
                [-1]*9 + list(range(117,139)) + [-1] + list(range(139,146)),
                default_count=89, default_skips=[116,135])


def mapping_3925():
    result=[-1]*166
    for first,last,index in ((0,81,0),(92,125,82),(136,153,116),(156,157,135),(159,165,137)):
        result[first:last+1]=range(index,index+last-first+1)
    return dict(count=144,map=result,default_count=87,default_skips=[134],guest_only_slots=[134])


COMPLEX_3925=('PSTextureModes','VertexBlend','FogColor','FillMode','BackFillMode',
    'TwoSidedLighting','NormalizeNormals','ZEnable','StencilEnable','StencilFail',
    'FrontFace','CullMode','TextureFactor','ZBias','LogicOp','EdgeAntiAlias',
    'MultiSampleAntiAlias','MultiSampleMask','MultiSampleType','ShadowFunc',
    'LineWidth','Dxt1NoiseEnable','YuvEnable','OcclusionCullEnable','StencilCullEnable',
    'RopZCmpAlwaysRead','RopZRead','DoNotCullUncompressed')


def matches_policy(mapping,expected):
    return (isinstance(mapping,dict) and mapping==expected and set(mapping)==set(expected) and
            all(type(mapping[k]) is int for k in ('count','default_count')) and
            all(type(mapping[k]) is list and all(type(i) is int for i in mapping[k])
                for k in ('map','default_skips','guest_only_slots') if k in mapping))


def inspect(proof,variables,layout,codes,read):
    if 'classic_layout' in proof:
        from native_d3d_full import inspect_cache
        return inspect_cache(proof,variables,layout,codes,read)
    version=proof.get('compact_layout','xdk-4134')
    if version not in ('xdk-4134','xdk-3925'):
        raise ValueError('unknown classic compact layout policy')
    expected=mapping_4134() if version=='xdk-4134' else mapping_3925()
    mapping=proof['render_cache']
    if not matches_policy(mapping,expected):
        raise ValueError('unreviewed classic compact render-state contract')
    indices=(82,117,128,140) if version=='xdk-4134' else (82,116,127,138)
    names=('D3D_g_DeferredRenderState','D3D_g_ComplexRenderState','D3DRS_CullMode','D3DRS_YuvEnable')
    if any(variables.get(name)!=layout['render_states']+index*4 for name,index in zip(names,indices)):
        raise ValueError('classic compact state boundaries changed')
    result=dict(render_cache=copy.deepcopy(mapping))
    if version=='xdk-4134':
        return result

    def require(name,mnemonic,operands,unique=True):
        matches=[i for i in codes.get(name,[]) if (i.mnemonic,i.op_str)==(mnemonic,operands)]
        if not matches or (unique and len(matches)!=1):raise ValueError('3925 compact source reference changed: '+name+'/'+operands)
        return matches[0]
    def address(name):
        rows=[r for r in proof['functions'] if r['name']==name]
        if len(rows)!=1 or type(rows[0]['address']) is not int:raise ValueError('missing 3925 setter: '+name)
        return rows[0]['address']
    generic='D3DDevice_SetRenderStateNotInline'
    require(generic,'cmp','esi, 0x52');require(generic,'cmp','esi, 0x74')
    require(generic,'mov',f'dword ptr [esi*4 + {hex(layout["render_states"])}], edi')
    tables=proof.get('compact_layout_tables',{})
    if set(tables)!={'render_defaults','simple_methods','deferred_dirty','complex_setters'}:
        raise ValueError('incomplete 3925 compact source tables')
    def table(name,size):
        p=tables[name]
        if (set(p)!={'address','bytes','sha256'} or type(p['address']) is not int or
                p['address']<0 or p['address']%4 or type(p['bytes']) is not int or p['bytes']!=size or
                not isinstance(p['sha256'],str) or not re.fullmatch('[0-9a-f]{64}',p['sha256'])):
            raise ValueError('invalid 3925 compact table: '+name)
        data=read(p['address'],size)
        if hashlib.sha256(data).hexdigest()!=p['sha256']:raise ValueError('3925 compact table changed: '+name)
        return data
    table('render_defaults',87*4)
    if tables['render_defaults']['address']!=layout['render_defaults']:
        raise ValueError('3925 default table layout changed')
    table('simple_methods',82*4);table('deferred_dirty',34*4)
    require(generic,'mov',f'ecx, dword ptr [esi*4 + {hex(tables["simple_methods"]["address"])}]')
    deferred=tables['deferred_dirty']['address']-82*4
    require(generic,'mov',f'eax, dword ptr [esi*4 + {hex(deferred)}]')
    require('D3DDevice_SetRenderState_Deferred','mov',f'eax, dword ptr [ecx*4 + {hex(deferred)}]')
    require('D3DDevice_SetRenderState_Deferred','mov',f'dword ptr [ecx*4 + {hex(layout["render_states"])}], edx')
    targets=struct.unpack('<28I',table('complex_setters',28*4))
    if targets!=tuple(address('D3DDevice_SetRenderState_'+name) for name in COMPLEX_3925):
        raise ValueError('3925 compact complex setter identities changed')
    dispatch=tables['complex_setters']['address']-116*4
    require(generic,'call',f'dword ptr [esi*4 + {hex(dispatch)}]')

    require('state_initializer','mov','edi, 0x39')
    require('state_initializer','mov',f'ebx, {hex(layout["render_defaults"])}')
    skip=require('state_initializer','cmp','esi, 0x134')
    advance=require('state_initializer','add','esi, 4')
    require('state_initializer','je',hex(advance.address))
    increment=require('state_initializer','inc','edi',unique=False)
    if increment.address!=advance.address+advance.size:raise ValueError('3925 default loop advance changed')
    require('state_initializer','add','ebx, 4')
    require('state_initializer','cmp','esi, 0x15c');require('state_initializer','jb',hex(skip.address))
    require('state_initializer','call',hex(address(generic)),unique=False)
    # This legacy slot is not a semantic alias for the later MultiSampleMode.
    # Creation/reset initializes it separately and the default loop skips it.
    slot=layout['render_states']+134*4
    require('reset_buffers','mov','eax, dword ptr [esi + 0x10]')
    require('reset_buffers','and','eax, 0xffff');require('reset_buffers','and','edi, 0x3000')
    stores=[i for i in codes.get('reset_buffers',[]) if (i.mnemonic,i.op_str)==('mov',f'dword ptr [{hex(slot)}], eax')]
    if len(stores)!=2:raise ValueError('3925 legacy sample initialization changed')
    fallback=require('reset_buffers','mov','eax, 0x11')
    following=[i for i in codes['reset_buffers'] if i.address==stores[1].address+stores[1].size]
    if (not following or not (stores[0].address<fallback.address<stores[1].address)):
        raise ValueError('3925 legacy sample normalization changed')
    require('reset_buffers','jne',hex(following[0].address))
    require('D3DDevice_SetRenderState_MultiSampleType','mov',f'dword ptr [{hex(slot)}], ecx')
    require('D3DDevice_SetRenderState_MultiSampleType','call',hex(address('D3DDevice_SetRenderTarget')))
    require('D3DDevice_SetRenderTarget','mov',f'ecx, dword ptr [{hex(slot)}]')
    require('D3DDevice_SetRenderTarget','test','ch, 0x30')
    result['legacy_sample_type_cache']=dict(version=1,guest_index=134)
    return result


def emit(binding):
    mapping=binding['render_cache'];legacy=binding.get('legacy_sample_type_cache')
    if mapping.get('guest_only_slots'):
        if (not matches_policy(mapping,mapping_3925()) or legacy!=dict(version=1,guest_index=134) or
                any(type(v) is not int for v in legacy.values())):
            raise ValueError('unreviewed legacy sample cache emission')
    elif legacy is not None:
        raise ValueError('legacy sample cache has no reviewed guest-only slot')
    inverse=[65535 if i in mapping.get('guest_only_slots',[]) else 0 for i in range(mapping['count'])]
    for state,index in enumerate(mapping['map']):
        if index>=0:inverse[index]=state
    lines=['#define PILOT_D3D_RENDER_STATE_MAP_VERSION 1',
        f'#define PILOT_D3D_GUEST_RENDER_STATE_COUNT {mapping["count"]}',
        f'#define PILOT_D3D_RENDER_DEFAULT_COUNT {mapping["default_count"]}',
        'static const int16_t pilot_d3d_render_state_map[166]={'+','.join(map(str,mapping['map']))+'};',
        'static const uint16_t pilot_d3d_render_state_inverse[]={'+','.join(map(str,inverse))+'};',
        'static const uint8_t pilot_d3d_render_default_skips[]={'+','.join(str(int(i+57 in mapping['default_skips'])) for i in range(mapping['default_count']))+'};']
    if legacy:
        lines+=['#define PILOT_D3D_LEGACY_SAMPLE_TYPE_CACHE_VERSION 1',
                '#define PILOT_D3D_LEGACY_SAMPLE_TYPE_INDEX 134u']
    return lines
