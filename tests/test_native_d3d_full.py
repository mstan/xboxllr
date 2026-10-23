"""Authored full-index relationship fixtures; optional exact-image replay.

Instruction objects model source relationships without embedding game code.
The optional private fixture additionally hashes and completely decodes the
real canonical bodies before invoking the same relationship gates.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_d3d_compact as compact
import native_d3d_full as full


def instructions(start, pairs):
    return [SimpleNamespace(address=start+index*4,size=4,mnemonic=m,op_str=o)
            for index,(m,o) in enumerate(pairs)]


class FullIndexPolicyTests(unittest.TestCase):
    def setUp(self):
        self.layout=dict(device_global=0x3498,device_object=0x34a0,device_bytes=0x2490,
            vertex_constants=0x2000,texture_states=0x3000,render_states=0x3200,
            vertex_program_cache=0x1098,shader_mode=0x1918,device_references=0x934,
            dirty_flags=0x2ff8,streams=0x6000,fvf_object=0x6200,texture_mode=12,
            render_defaults=0x7000)
        self.context=SimpleNamespace(image=dict(sections=[dict(address=0x1000,size=0x20000,flags=1)]))
        self.proof=dict(classic_layout=full.POLICY,render_cache=full.mapping(),
            functions=[dict(name='D3DDevice_SetRenderState_'+name,address=0x10000+index*16)
                       for index,name in enumerate(full.COMPLEX)],
            helpers=dict(device_initializer=dict(address=0xb000),device_destructor=dict(address=0xb100)))
        self.proof['functions'] += [dict(name='D3DDevice_SetRenderStateNotInline',address=0x8000),
                                   dict(name='D3DDevice_SetRenderState_Simple',address=0x8100)]
        self.variables={name:0x3200+index*4 for names,index in [
            (('D3DRS_FogEnable','D3D_g_DeferredRenderState'),92),
            (('D3DRS_PSTextureModes','D3D_g_ComplexRenderState'),136),
            (('D3DRS_CullMode',),147),(('D3DRS_YuvEnable',),160)] for name in names}
        self.data={0x7000:struct.pack('<109I',*range(109)),
            0x7200:struct.pack('<92I',*range(92)),0x7400:struct.pack('<44I',*range(44)),
            0x7600:struct.pack('<30I',*(0x10000+index*16 for index in range(30)))}
        self.proof['full_index_tables']={name:dict(address=address,bytes=len(self.data[address]),
            sha256=hashlib.sha256(self.data[address]).hexdigest()) for name,address in
            [('render_defaults',0x7000),('simple_methods',0x7200),('deferred_dirty',0x7400),('complex_setters',0x7600)]}
        self.codes={
            'D3DDevice_SetRenderStateNotInline':instructions(0x8000,[
                ('push','esi'),('mov','esi, dword ptr [esp + 8]'),('cmp','esi, 0x5c'),('jge','0x8034'),
                ('mov','ecx, dword ptr [esi*4 + 0x7200]'),('push','edi'),
                ('mov','edi, dword ptr [esp + 0x10]'),('mov','edx, edi'),('call','0x8100'),
                ('mov','dword ptr [esi*4 + 0x3200], edi'),('pop','edi'),('pop','esi'),('ret','8'),
                ('cmp','esi, 0x88'),('jge','0x8054'),('mov','eax, dword ptr [esi*4 + 0x7290]'),
                ('or','dword ptr [0x2ff8], eax'),('mov','ecx, dword ptr [esp + 0xc]'),
                ('mov','dword ptr [esi*4 + 0x3200], ecx'),('pop','esi'),('ret','8'),
                ('mov','edx, dword ptr [esp + 0xc]'),('push','edx'),
                ('call','dword ptr [esi*4 + 0x73e0]'),('pop','esi'),('ret','8')]),
            'state_initializer':instructions(0x8200,[
                ('mov','esi, 0x39'),('mov','edi, 0x7000'),('jmp','0x8210'),('lea','ecx, [ecx]'),
                ('cmp','esi, 0x9a'),('je','0x8238'),('cmp','esi, 0x7e'),('je','0x8238'),
                ('cmp','esi, 0x7f'),('je','0x8238'),('mov','eax, dword ptr [edi]'),
                ('push','eax'),('push','esi'),('call','0x8000'),('inc','esi'),('add','edi, 4'),
                ('cmp','esi, 0xa6'),('jb','0x8210')]),
            'Direct3D_CreateDevice':instructions(0x9000,[
                ('and','eax, 0x10'),('mov','edi, 0x34a0'),('or','esi, eax'),('push','ecx'),
                ('mov','ecx, edi'),('mov','dword ptr [0x3498], edi'),('call','0xb000'),
                ('xor','eax, eax'),('mov','ecx, 0x924'),('rep stosd','dword ptr es:[edi], eax'),
                ('pop','edi'),('mov','eax, esi'),('mov','dword ptr [0x3498], 0'),('pop','esi'),('ret','0x18')]),
            'D3DDevice_Release':instructions(0xa000,[
                ('mov','edi, dword ptr [0x3498]'),('mov','eax, dword ptr [edi + 0x934]'),
                ('mov','ecx, edi'),('call','0xb100'),('mov','ecx, 0x924'),('xor','eax, eax'),
                ('mov','dword ptr [0x3498], 0'),('rep stosd','dword ptr es:[edi], eax'),('pop','edi'),('ret','')]),
            'device_destructor':instructions(0xb100,[('mov','edi, 0x6000'),('mov','ecx, 0x30')]),
            'D3DDevice_SetVertexShaderConstant1':instructions(0xc000,[('shl','ecx, 4'),('lea','ecx, [ecx + 0x2000]')]),
            'D3DDevice_SetVertexShaderConstant4':instructions(0xc100,[('shl','ecx, 4'),('lea','ecx, [ecx + 0x2000]')]),
            'D3DDevice_SetVertexShaderConstantNotInline':instructions(0xc200,[('shl','edi, 4'),('add','edi, 0x2000')]),
            'vertex_recorder':instructions(0xc300,[('shl','edi, 4'),('add','edi, 0x2000'),('lea','ecx, [ecx + edx + 0x1098]')]),
            'D3DDevice_SetShaderConstantMode':instructions(0xc400,[('mov','dword ptr [ebx + 0x1918], eax')]),
            'D3DDevice_SetTexture':instructions(0xc500,[('mov','dword ptr [ebp + esi*4 + 0xc], 0x80000000')])}

    def read(self,address,size):
        return self.data.get(address,b'')[:size]

    def cache(self,proof=None,codes=None,layout=None,variables=None):
        return compact.inspect(proof if proof is not None else self.proof,
            variables if variables is not None else self.variables,
            layout if layout is not None else self.layout,codes if codes is not None else self.codes,self.read)

    def owner(self,layout=None,codes=None):
        return full.inspect_layout(self.context,self.proof,layout if layout is not None else self.layout,
            codes if codes is not None else self.codes)

    def test_exact_identity_policy_and_independent_static_owner_pass(self):
        binding=self.cache();self.assertEqual(binding,dict(render_cache=full.mapping()))
        self.assertEqual(self.owner(),dict(version=1,policy=full.POLICY,original_texture_mode_null=0x80000000))
        self.assertIn('GUEST_RENDER_STATE_COUNT 166','\n'.join(compact.emit(binding)))

    def test_wrong_or_ambiguous_selection_and_map_reject(self):
        mutations=[lambda p:p.update(classic_layout='unknown'),lambda p:p.update(compact_layout='xdk-4134'),
            lambda p:p.pop('classic_layout'),lambda p:p['render_cache'].update(default_count=108),
            lambda p:p['render_cache'].update(default_skips=[126,127]),
            lambda p:p['render_cache']['map'].__setitem__(82,-1),
            lambda p:p['render_cache']['map'].__setitem__(0,False)]
        for mutate in mutations:
            proof=copy.deepcopy(self.proof);mutate(proof)
            with self.subTest(mutation=mutate),self.assertRaises(ValueError):self.cache(proof=proof)

    def test_each_global_boundary_and_alias_is_required(self):
        for key in self.variables:
            variables=dict(self.variables);variables[key]+=4
            with self.subTest(key=key),self.assertRaises(ValueError):self.cache(variables=variables)

    def test_table_hash_extent_and_target_identity_are_guarded(self):
        for key in self.proof['full_index_tables']:
            for field,value in [('sha256','0'*64),('bytes',4),('address',False)]:
                proof=copy.deepcopy(self.proof);proof['full_index_tables'][key][field]=value
                with self.subTest(table=key,field=field),self.assertRaises(ValueError):self.cache(proof=proof)
        proof=copy.deepcopy(self.proof);proof['functions'][1]['address']+=4
        with self.assertRaises(ValueError):self.cache(proof=proof)
        self.data[0x7400]=self.data[0x7400][:-4]
        with self.assertRaises(ValueError):self.cache()

    def test_dispatch_and_default_loop_branch_mutations_reject(self):
        for name in ('D3DDevice_SetRenderStateNotInline','state_initializer'):
            for index in range(len(self.codes[name])):
                codes=copy.deepcopy(self.codes);codes[name][index].op_str+=' changed'
                with self.subTest(name=name,index=index),self.assertRaises(ValueError):self.cache(codes=codes)
        codes=copy.deepcopy(self.codes);codes['state_initializer'][7].address+=1
        with self.assertRaises(ValueError):self.cache(codes=codes)

    def test_wrong_static_ranges_alignment_mapping_and_clear_reject(self):
        for key,value in [('device_global',0x34a0),('device_bytes',0x2494),
                ('vertex_constants',0x3000),('vertex_constants',True),('streams',0x3500),
                ('streams',0x25000),('streams',0x6001),('vertex_program_cache',0x109c),
                ('dirty_flags',0x3200),('fvf_object',0x6004)]:
            layout=dict(self.layout);layout[key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):self.owner(layout=layout)
        self.context.image['sections'][0]['flags']=0
        with self.assertRaises(ValueError):self.owner()

    def test_ambiguous_mapped_owner_rejects_even_with_one_readonly_alias(self):
        self.context.image['sections'].append(dict(address=0x2000,size=0xc00,flags=0))
        with self.assertRaises(ValueError):self.owner()

    def test_each_original_owner_or_constant_reference_is_required(self):
        for name in ('Direct3D_CreateDevice','D3DDevice_Release','device_destructor',
                'D3DDevice_SetVertexShaderConstant1','D3DDevice_SetVertexShaderConstant4',
                'D3DDevice_SetVertexShaderConstantNotInline','vertex_recorder',
                'D3DDevice_SetShaderConstantMode','D3DDevice_SetTexture'):
            codes=copy.deepcopy(self.codes);codes[name]=[]
            with self.subTest(name=name),self.assertRaises(ValueError):self.owner(codes=codes)


@unittest.skipUnless(all(os.environ.get(key) for key in
    ('XLLE_TEST_FULL_PROPOSAL','XLLE_TEST_FULL_XBE','XLLE_TEST_FULL_SOURCE','XLLE_TEST_FULL_FUNCTIONS')),
                     'private exact original/proposal not selected')
class OriginalFullIndexPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from aot_inventory import parse_xbe
        from capstone import Cs,CS_ARCH_X86,CS_MODE_32
        cls.proposal=json.loads(Path(os.environ['XLLE_TEST_FULL_PROPOSAL']).read_text())
        blob=Path(os.environ['XLLE_TEST_FULL_XBE']).read_bytes();image=parse_xbe(blob)
        if image['sha256']!=cls.proposal['xbe_sha256']:raise ValueError('wrong private XBE')
        def read(address,size):
            sections=[s for s in image['sections'] if s['address']<=address and address+size<=s['address']+s['raw_size']]
            if len(sections)!=1:raise ValueError('private original outside unique stored section')
            s=sections[0];offset=s['raw_offset']+address-s['address'];return blob[offset:offset+size]
        cls.read=staticmethod(read);cls.context=SimpleNamespace(image=image);cls.codes={}
        canonical=json.loads(Path(os.environ['XLLE_TEST_FULL_FUNCTIONS']).read_text())
        by_address={int(row['start'],16):row for row in canonical}
        if len(by_address)!=len(canonical):raise ValueError('ambiguous private canonical inventory')
        for name,row in cls.proposal['bodies'].items():
            function=by_address.get(row['address'])
            if (function is None or function['size']!=row['bytes'] or
                    int(function['end'],16)!=row['address']+row['bytes']):
                raise ValueError('private body does not match canonical extent')
            raw=read(row['address'],row['bytes'])
            if hashlib.sha256(raw).hexdigest()!=row['sha256']:raise ValueError('private body changed')
            code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(raw,row['address']))
            if not code or code[-1].address+code[-1].size!=row['address']+row['bytes']:
                raise ValueError('private body decode incomplete')
            cls.codes[name]=code
        cls.layout=dict(cls.proposal['layout'])
        # Supply discovery identities and dirty-global facts from the private
        # replay descriptor rather than deriving them from expected policy text.
        extra=json.loads(Path(os.environ['XLLE_TEST_FULL_SOURCE']).read_text())
        cls.layout['dirty_flags']=extra['dirty_flags']
        cls.variables=extra['variables']
        cls.proof=dict(classic_layout=full.POLICY,
            render_cache={k:v for k,v in cls.proposal['render_cache'].items() if k!='status'},
            full_index_tables={k:v for k,v in cls.proposal['tables'].items() if k in
                ('render_defaults','simple_methods','deferred_dirty','complex_setters')},
            functions=extra['functions'],helpers=cls.proposal['helpers'])

    def test_real_canonical_body_topologies_and_static_ranges_pass(self):
        self.assertEqual(compact.inspect(self.proof,self.variables,self.layout,self.codes,self.read),
                         dict(render_cache=full.mapping()))
        self.assertEqual(full.inspect_layout(self.context,self.proof,self.layout,self.codes)['version'],1)


if __name__=='__main__':unittest.main()
