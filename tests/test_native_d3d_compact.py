"""Compact-policy admission, guarded original relationships and emission."""
import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import native_d3d_compact as compact


class CompactPolicyTests(unittest.TestCase):
    def test_existing_policy_keeps_exact_mapping_and_no_legacy_feature(self):
        layout={'render_states':0x1000}
        variables={name:0x1000+index*4 for name,index in zip(
            ('D3D_g_DeferredRenderState','D3D_g_ComplexRenderState','D3DRS_CullMode','D3DRS_YuvEnable'),
            (82,117,128,140))}
        binding=compact.inspect({'render_cache':compact.mapping_4134()},variables,layout,{},None)
        self.assertEqual(binding,{'render_cache':compact.mapping_4134()})
        self.assertNotIn('LEGACY','\n'.join(compact.emit(binding)))
        for key,value in [('compact_layout','unknown'),('render_cache',compact.mapping_3925())]:
            proof={'render_cache':compact.mapping_4134(),key:value}
            with self.assertRaises(ValueError):compact.inspect(proof,variables,layout,{},None)

    def test_emitter_cannot_invent_a_legacy_alias(self):
        binding={'render_cache':compact.mapping_3925(),'legacy_sample_type_cache':{'version':1,'guest_index':134}}
        text='\n'.join(compact.emit(binding))
        self.assertIn('65535',text)
        for mutate in (lambda b:b['render_cache']['map'].__setitem__(154,134),
                       lambda b:b['render_cache'].update(default_skips=[]),
                       lambda b:b.pop('legacy_sample_type_cache'),
                       lambda b:b['legacy_sample_type_cache'].update(guest_index=135)):
            bad=copy.deepcopy(binding);mutate(bad)
            with self.assertRaises(ValueError):compact.emit(bad)


@unittest.skipUnless(os.environ.get('XLLE_TEST_COMPACT_PROPOSAL') and os.environ.get('XLLE_TEST_COMPACT_XBE'),
                     'private exact original/proposal not selected')
class OriginalCompactPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from aot_inventory import parse_xbe
        from capstone import Cs,CS_ARCH_X86,CS_MODE_32
        p=json.loads(Path(os.environ['XLLE_TEST_COMPACT_PROPOSAL']).read_text())
        blob=Path(os.environ['XLLE_TEST_COMPACT_XBE']).read_bytes();image=parse_xbe(blob)
        if image['sha256']!=p['title_xbe_sha256']:raise ValueError('wrong private XBE')
        def read(address,size):
            s=next(s for s in image['sections'] if s['address']<=address and address+size<=s['address']+s['raw_size'])
            offset=s['raw_offset']+address-s['address'];return blob[offset:offset+size]
        cls.read=staticmethod(read);cls.codes={}
        for name,body in p['whole_body_guards'].items():
            address=int(body['address'],16);data=read(address,body['bytes'])
            if hashlib.sha256(data).hexdigest()!=body['sha256']:raise ValueError('wrong private body')
            cls.codes[name]=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data,address))
        base=int(next(r['global_address'] for r in p['decoded_reference_guards']['D3DDevice_SetRenderState_CullMode']
                      if r['global_name']=='D3DRS_CullMode'),16)-127*4
        cls.layout={'render_states':base,'render_defaults':int(p['table_guards']['render_defaults']['address'],16)}
        cls.variables={name:base+fact['dword_index']*4 for name,fact in p['global_guard_facts'].items()}
        cls.proof=dict(compact_layout='xdk-3925',render_cache=p['proposed_render_cache'],
            compact_layout_tables={name:dict(row,address=int(row['address'],16)) for name,row in p['table_guards'].items()},
            functions=[dict(name=name,address=int(row['address'],16)) for name,row in p['whole_body_guards'].items()])

    def inspect(self,proof=None,codes=None):
        return compact.inspect(proof or self.proof,self.variables,self.layout,codes or self.codes,self.read)

    def test_original_initializer_tables_and_removed_state_pass(self):
        binding=self.inspect()
        self.assertEqual(binding['render_cache']['default_count'],87)
        self.assertEqual(binding['legacy_sample_type_cache'],dict(version=1,guest_index=134))
        self.assertIn('65535','\n'.join(compact.emit(binding)))

    def test_wrong_map_default_skip_table_and_signature_reject(self):
        mutations=(lambda p:p['render_cache']['map'].__setitem__(154,134),
            lambda p:p['render_cache'].update(default_count=89),
            lambda p:p['render_cache'].update(default_skips=[]),
            lambda p:p['render_cache'].update(guest_only_slots=[]),
            lambda p:p['render_cache']['map'].__setitem__(0,False),
            lambda p:p['compact_layout_tables']['render_defaults'].update(sha256='0'*64),
            lambda p:p['compact_layout_tables']['complex_setters'].update(bytes=108),
            lambda p:next(r for r in p['functions'] if r['name']=='D3DDevice_SetRenderState_CullMode').update(address=0))
        for mutation in mutations:
            proof=copy.deepcopy(self.proof);mutation(proof)
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):self.inspect(proof)

    def test_missing_legacy_initialization_and_consumer_reject(self):
        for name in ('state_initializer','reset_buffers','D3DDevice_SetRenderTarget','D3DDevice_SetRenderState_MultiSampleType'):
            codes=dict(self.codes);codes[name]=[]
            with self.subTest(name=name),self.assertRaises(ValueError):self.inspect(codes=codes)


if __name__=='__main__':unittest.main()
