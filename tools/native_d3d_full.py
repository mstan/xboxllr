"""Source-bound full-index classic cache and independent static-owner policy.

The classic inspector has already verified whole canonical body identities and
complete decode. These checks establish the particular source relationships;
an SDK label alone never selects this policy or implements a service method.
"""
from __future__ import annotations
import copy
import hashlib
import re
import struct

POLICY = 'xdk-5558-full-index'
COMPLEX = ('PSTextureModes','VertexBlend','FogColor','FillMode','BackFillMode',
    'TwoSidedLighting','NormalizeNormals','ZEnable','StencilEnable','StencilFail',
    'FrontFace','CullMode','TextureFactor','ZBias','LogicOp','EdgeAntiAlias',
    'MultiSampleAntiAlias','MultiSampleMask','MultiSampleMode','MultiSampleRenderTargetMode',
    'ShadowFunc','LineWidth','SampleAlpha','Dxt1NoiseEnable','YuvEnable',
    'OcclusionCullEnable','StencilCullEnable','RopZCmpAlwaysRead','RopZRead','DoNotCullUncompressed')


def mapping():
    return dict(count=166,map=list(range(166)),default_count=109,default_skips=[126,127,154])


def selected(proof):
    return proof.get('classic_layout') == POLICY


def _address(proof, name):
    rows=[r for r in proof['functions'] if r['name']==name]
    if len(rows)!=1 or type(rows[0]['address']) is not int or not 0<rows[0]['address']<=0xffffffff:
        raise ValueError('missing full-index source entry: '+name)
    return rows[0]['address']


def _require(codes, name, mnemonic, operands):
    matches=[i for i in codes.get(name,[]) if (i.mnemonic,i.op_str)==(mnemonic,operands)]
    if len(matches)!=1:
        raise ValueError('full-index source reference changed: '+name+'/'+operands)
    return matches[0]


def _sequence(codes, name, expected, start=None):
    code=codes.get(name,[])
    matches=[index for index in range(len(code)-len(expected)+1)
        if [(i.mnemonic,i.op_str) for i in code[index:index+len(expected)]]==expected
        and (start is None or code[index].address==start)]
    if len(matches)!=1:
        raise ValueError('full-index source topology changed: '+name)
    result=code[matches[0]:matches[0]+len(expected)]
    if any(a.address+a.size!=b.address for a,b in zip(result,result[1:])):
        raise ValueError('noncontiguous full-index source topology: '+name)
    return result


def inspect_cache(proof, variables, layout, codes, read):
    from native_d3d_compact import matches_policy
    if (not selected(proof) or 'compact_layout' in proof or
            not matches_policy(proof['render_cache'],mapping())):
        raise ValueError('unreviewed full-index classic selection')
    for names,index in [(('D3DRS_FogEnable','D3D_g_DeferredRenderState'),92),
            (('D3DRS_PSTextureModes','D3D_g_ComplexRenderState'),136),
            (('D3DRS_CullMode',),147),(('D3DRS_YuvEnable',),160)]:
        if any(variables.get(name)!=layout['render_states']+index*4 for name in names):
            raise ValueError('full-index source state boundaries changed')
    tables=proof.get('full_index_tables')
    if not isinstance(tables,dict) or set(tables)!={'render_defaults','simple_methods','deferred_dirty','complex_setters'}:
        raise ValueError('incomplete full-index source tables')
    def table(name, count):
        row=tables[name];size=count*4
        if (not isinstance(row,dict) or set(row)!={'address','bytes','sha256'} or
                type(row['address']) is not int or not 0<row['address']<=0xffffffff-size or row['address']%4 or
                type(row['bytes']) is not int or row['bytes']!=size or
                not isinstance(row['sha256'],str) or not re.fullmatch('[0-9a-f]{64}',row['sha256'])):
            raise ValueError('invalid full-index source table: '+name)
        raw=read(row['address'],size)
        if len(raw)!=size or hashlib.sha256(raw).hexdigest()!=row['sha256']:
            raise ValueError('full-index source table changed: '+name)
        return raw
    table('render_defaults',109);table('simple_methods',92);table('deferred_dirty',44)
    if tables['render_defaults']['address']!=layout['render_defaults']:
        raise ValueError('full-index default table address changed')
    targets=struct.unpack('<30I',table('complex_setters',30))
    if targets!=tuple(_address(proof,'D3DDevice_SetRenderState_'+name) for name in COMPLEX):
        raise ValueError('full-index complex setter identities changed')

    generic='D3DDevice_SetRenderStateNotInline'
    code=codes.get(generic,[])
    deferred=_require(codes,generic,'cmp','esi, 0x88')
    complex_start=_require(codes,generic,'mov','edx, dword ptr [esp + 0xc]')
    dirty=layout['dirty_flags']
    if type(dirty) is not int or not 0<dirty<=0xfffffffc or dirty%4:
        raise ValueError('invalid full-index dirty flag owner')
    expected=[('push','esi'),('mov','esi, dword ptr [esp + 8]'),('cmp','esi, 0x5c'),
        ('jge',hex(deferred.address)),
        ('mov',f'ecx, dword ptr [esi*4 + {hex(tables["simple_methods"]["address"])}]'),
        ('push','edi'),('mov','edi, dword ptr [esp + 0x10]'),('mov','edx, edi'),
        ('call',hex(_address(proof,'D3DDevice_SetRenderState_Simple'))),
        ('mov',f'dword ptr [esi*4 + {hex(layout["render_states"])}], edi'),
        ('pop','edi'),('pop','esi'),('ret','8'),('cmp','esi, 0x88'),
        ('jge',hex(complex_start.address)),
        ('mov',f'eax, dword ptr [esi*4 + {hex(tables["deferred_dirty"]["address"]-92*4)}]'),
        ('or',f'dword ptr [{hex(dirty)}], eax'),('mov','ecx, dword ptr [esp + 0xc]'),
        ('mov',f'dword ptr [esi*4 + {hex(layout["render_states"])}], ecx'),
        ('pop','esi'),('ret','8'),('mov','edx, dword ptr [esp + 0xc]'),
        ('push','edx'),('call',f'dword ptr [esi*4 + {hex(tables["complex_setters"]["address"]-136*4)}]'),
        ('pop','esi'),('ret','8')]
    if [(i.mnemonic,i.op_str) for i in code]!=expected:
        raise ValueError('unreviewed full-index dispatch topology')
    _sequence(codes,generic,expected)

    initializer='state_initializer'
    loop=_require(codes,initializer,'cmp','esi, 0x9a')
    end=_require(codes,initializer,'cmp','esi, 0xa6')
    init_code=codes.get(initializer,[])
    index=init_code.index(end)
    if index<2:
        raise ValueError('missing full-index default advance')
    advance=init_code[index-2]
    expected=[('mov','esi, 0x39'),('mov',f'edi, {hex(layout["render_defaults"])}'),
        ('jmp',hex(loop.address)),('lea','ecx, [ecx]'),
        ('cmp','esi, 0x9a'),('je',hex(advance.address)),
        ('cmp','esi, 0x7e'),('je',hex(advance.address)),
        ('cmp','esi, 0x7f'),('je',hex(advance.address)),
        ('mov','eax, dword ptr [edi]'),('push','eax'),('push','esi'),
        ('call',hex(_address(proof,generic))),('inc','esi'),('add','edi, 4'),
        ('cmp','esi, 0xa6'),('jb',hex(loop.address))]
    _sequence(codes,initializer,expected)
    return dict(render_cache=copy.deepcopy(mapping()))


def inspect_layout(context, proof, layout, codes):
    """Qualify the independent constants/static device owner, without emitting."""
    if not selected(proof) or 'compact_layout' in proof:
        raise ValueError('unreviewed full-index static owner')
    def integer(key):
        value=layout[key]
        if type(value) is not int or not 0<value<=0xffffffff or value%4:
            raise ValueError('invalid full-index layout field: '+key)
        return value
    device=integer('device_object');global_=integer('device_global');size=integer('device_bytes')
    constants=integer('vertex_constants');texture=integer('texture_states');render=integer('render_states')
    program=integer('vertex_program_cache');mode=integer('shader_mode')
    if (size>0x10000 or global_+8!=device or texture+4*32*4!=render or
            render+166*4!=global_ or program+136*16!=mode or mode+4>size or
            constants+192*16>texture):
        raise ValueError('full-index independent static extents disagree')
    ranges=[('device',device,size),('device global',global_,4),('constants',constants,192*16),
        ('texture states',texture,4*32*4),('render states',render,166*4),
        ('dirty flags',integer('dirty_flags'),4),('streams',integer('streams'),16*12),
        ('FVF object',integer('fvf_object'),0x118)]
    for name,address,length in ranges:
        sections=[s for s in context.image['sections']
            if s['address']<=address and address+length<=s['address']+s['size']]
        if address+length>0x100000000 or len(sections)!=1 or not sections[0]['flags']&1:
            raise ValueError('full-index static owner outside unique writable section: '+name)
    for index,(name,address,length) in enumerate(ranges):
        if any(address<other+extent and other<address+length for _,other,extent in ranges[index+1:]):
            raise ValueError('overlapping full-index static owners: '+name)

    create='Direct3D_CreateDevice';release='D3DDevice_Release'
    _require(codes,create,'and','eax, 0x10')
    _sequence(codes,create,[('mov',f'edi, {hex(device)}'),('or','esi, eax'),
        ('push','ecx'),('mov','ecx, edi'),('mov',f'dword ptr [{hex(global_)}], edi')])
    initializer=proof['helpers']['device_initializer']['address']
    _require(codes,create,'call',hex(initializer))
    clear=_sequence(codes,create,[('xor','eax, eax'),('mov',f'ecx, {hex(size//4)}'),
        ('rep stosd','dword ptr es:[edi], eax'),('pop','edi'),('mov','eax, esi'),
        ('mov',f'dword ptr [{hex(global_)}], 0'),('pop','esi'),('ret','0x18')])
    if clear[0].address<_require(codes,create,'call',hex(initializer)).address:
        raise ValueError('full-index failure clear precedes creation')
    destructor=proof['helpers']['device_destructor']['address']
    _sequence(codes,release,[('mov','ecx, edi'),('call',hex(destructor)),
        ('mov',f'ecx, {hex(size//4)}'),('xor','eax, eax'),
        ('mov',f'dword ptr [{hex(global_)}], 0'),('rep stosd','dword ptr es:[edi], eax'),
        ('pop','edi'),('ret','')])
    _require(codes,release,'mov',f'edi, dword ptr [{hex(global_)}]')
    _require(codes,release,'mov',f'eax, dword ptr [edi + {hex(layout["device_references"])}]')
    _require(codes,'device_destructor','mov',f'edi, {hex(layout["streams"])}')
    _require(codes,'device_destructor','mov','ecx, 0x30')
    for name in ('D3DDevice_SetVertexShaderConstant1','D3DDevice_SetVertexShaderConstant4'):
        _sequence(codes,name,[('shl','ecx, 4'),('lea',f'ecx, [ecx + {hex(constants)}]')])
    _sequence(codes,'D3DDevice_SetVertexShaderConstantNotInline',
        [('shl','edi, 4'),('add',f'edi, {hex(constants)}')])
    _sequence(codes,'vertex_recorder',[('shl','edi, 4'),('add',f'edi, {hex(constants)}')])
    _require(codes,'vertex_recorder','lea',f'ecx, [ecx + edx + {hex(program)}]')
    _require(codes,'D3DDevice_SetShaderConstantMode','mov',f'dword ptr [ebx + {hex(mode)}], eax')
    _require(codes,'D3DDevice_SetTexture','mov',f'dword ptr [ebp + esi*4 + {hex(layout["texture_mode"])}], 0x80000000')
    # Source relationships are retained in the binding receipt; no renderer or
    # unsupported-method semantics change is selected by these metadata facts.
    return dict(version=1,policy=POLICY,original_texture_mode_null=0x80000000)
