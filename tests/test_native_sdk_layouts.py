"""Authored SDK-shaped witnesses; no original game or SDK datasets."""
import copy
import hashlib
from pathlib import Path
import sys
import struct
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_sdk_layouts import LayoutAtlas,Unavailable,compare,shape,digest
from native_guest_types import TypeDecoder
from test_native_sdk_symbols import fixture


def reference(size=7):
    fields=[dict(name='tag',offset=0,type=dict(kind='integer',bytes=1,signed=False)),
            dict(name='value',offset=1,type=dict(kind='integer',bytes=2,signed=False)),
            dict(name='count',offset=3,type=dict(kind='integer',bytes=4,signed=False))]
    return dict(schema='xboxllr.sdk-layout-reference.v1',library='D3DX8',api=[1,0],
        abi=dict(machine='ia32',pointer_bytes=4,endianness='little',compiler_family='msvc'),
        types={'AuthoredRecord':dict(alignment=1,effective_pack=1,shape=dict(bytes=size,members=fields))})


def inputs(title=1,build=4000,root=10):
    blob,symbols=fixture(title=title,build=build)
    blob+=b'RSDS'+bytes.fromhex('00112233445566778899aabbccddeeff')+struct.pack('<I',1)+b'authored.pdb\0'
    symbols['xbe_sha256']=hashlib.sha256(blob).hexdigest()
    symbols['functions'][0]['compiland_ids']=[50]
    symbols['compilers']={'50':[dict(compiler_name='Microsoft (R) Optimizing Compiler',language=1,platform=6,
                                   frontend_major=13,backend_major=13,backend_minor=10,backend_build=3077)]}
    types={str(root):dict(id=root,tag=11,name='AuthoredRecord',length=7,dia_packed=False)}
    for n,(name,offset,size) in enumerate((('tag',0,1),('value',1,2),('count',3,4)),1):
        types[str(root+n)]=dict(id=root+n,tag=7,parent=root,name=name,field_offset=offset,data_kind=7,location=4,type=root+100+n)
        types[str(root+100+n)]=dict(id=root+100+n,tag=16,length=size,basic=7)
    layouts=dict(schema='xboxllr.guest-types.v1',xbe_sha256=symbols['xbe_sha256'],source_sha256=symbols['source_sha256'],types=types,
                 objects=[dict(name='g_record',type=root,address=0x13000,mapped_bytes=7)])
    return blob,symbols,layouts


def atlas():
    value=LayoutAtlas(reference())
    value.add(*inputs(1,4000,10));value.add(*inputs(2,5000,800))
    return value


class SDKLayouts(unittest.TestCase):
    def test_real_rebinding_uses_target_addresses_and_fresh_type_namespace(self):
        blob,symbols,layouts=inputs(3,6000,900)
        result=atlas().transfer(blob,symbols,layouts)
        self.assertEqual(result['identity'],'qualified-sdk-layouts')
        self.assertEqual(shape(result,'AuthoredRecord'),reference()['types']['AuthoredRecord']['shape'])
        self.assertEqual(result['objects'][0]['address'],0x13000)
        self.assertNotEqual(result['objects'][0]['type'],900)
        root=result['types'][str(result['objects'][0]['type'])]
        self.assertEqual(root['alignment'],1)
        member=next(t for t in result['types'].values() if t.get('name')=='value')
        self.assertEqual(TypeDecoder(result,result['xbe_sha256']).scalar(member['type'],b'\x34\x12')['value'],'4660')
        self.assertFalse(set(map(int,result['types']))&set(map(int,layouts['types'])))

    def test_target_size_offset_signedness_and_name_ambiguity_reject(self):
        for change in ('size','offset','signed','ambiguous'):
            blob,symbols,layouts=inputs(3,6000,900)
            if change=='size':layouts['types']['900']['length']=9
            if change=='offset':layouts['types']['902']['field_offset']=2
            if change=='signed':layouts['types']['1002']['basic']=6
            if change=='ambiguous':
                other=copy.deepcopy(layouts['types']['900']);other.update(id=2000,length=9);layouts['types']['2000']=other
                for key in ('901','902','903'):
                    member=copy.deepcopy(layouts['types'][key]);member.update(id=int(key)+1100,parent=2000);layouts['types'][str(member['id'])]=member
            result=atlas().transfer(blob,symbols,layouts)
            self.assertEqual(result['decisions'][0]['status'],'unavailable',change)
            self.assertFalse(result['objects'])

    def test_dia_packed_flag_cannot_qualify_ambiguous_alignment(self):
        ref=reference(8)
        for flag in (False,True):
            blob,symbols,layouts=inputs();layouts['types']['10'].update(length=8,dia_packed=flag)
            decisions=LayoutAtlas(ref).add(blob,symbols,layouts)
            self.assertEqual(decisions[0]['status'],'unavailable')
            self.assertIn('packing/alignment',decisions[0]['reason'])

    def test_independent_titles_image_source_machine_and_producer_are_required(self):
        value=LayoutAtlas(reference());value.add(*inputs())
        self.assertEqual(value.transfer(*inputs(3))['decisions'][0]['status'],'unavailable')
        with self.assertRaisesRegex(ValueError,'target was used'):atlas().transfer(*inputs())
        blob,symbols,layouts=inputs(3);layouts['xbe_sha256']='f'*64
        with self.assertRaises(ValueError):atlas().transfer(blob,symbols,layouts)
        blob,symbols,layouts=inputs(3);layouts['source_sha256']='b'*64
        with self.assertRaisesRegex(ValueError,'different PDB'):atlas().transfer(blob,symbols,layouts)
        blob,symbols,layouts=inputs(3);symbols['compilers']['50'][0]['platform']=0xd0
        with self.assertRaisesRegex(Unavailable,'compiler ABI'):atlas().transfer(blob,symbols,layouts)
        broken=copy.deepcopy(atlas().document);broken['producer_sha256']='0'*64
        with self.assertRaises(ValueError):LayoutAtlas(document=broken)

    def test_unmapped_or_pointer_global_cannot_be_reinterpreted_as_an_object(self):
        blob,symbols,layouts=inputs(3)
        layouts['objects'][0]['address']=0xdead0000
        self.assertFalse(atlas().transfer(blob,symbols,layouts)['objects'])
        layouts['types']['3000']=dict(id=3000,tag=14,length=4,type=10)
        layouts['objects'][0].update(address=0x13000,type=3000)
        self.assertFalse(atlas().transfer(blob,symbols,layouts)['objects'])

    def test_same_name_incomplete_object_type_needs_its_own_layout_witness(self):
        blob,symbols,layouts=inputs(3)
        layouts['types']['3000']=dict(id=3000,tag=11,name='AuthoredRecord',length=7)
        layouts['objects'][0]['type']=3000
        result=atlas().transfer(blob,symbols,layouts)
        self.assertEqual(result['decisions'][0]['status'],'qualified')
        self.assertFalse(result['objects'])
        layouts['types']['3000']['unmodified_type']=10
        self.assertEqual(len(atlas().transfer(blob,symbols,layouts)['objects']),1)
        layouts['types']['3000']['unmodified_type']=3000
        self.assertFalse(atlas().transfer(blob,symbols,layouts)['objects'])
        layouts['types']['3000'].update(unmodified_type=10,length=8)
        self.assertFalse(atlas().transfer(blob,symbols,layouts)['objects'])

    def test_actual_size_difference_is_reported_without_guessing_nested_layout(self):
        left=inputs()[2];right=copy.deepcopy(left);right['types']['10']['length']=9
        self.assertEqual(compare(left,right,'AuthoredRecord')['status'],'incompatible')
        right['types'].clear()
        self.assertEqual(compare(left,right,'AuthoredRecord')['status'],'unavailable')

    def test_signed_bitfield_positions_survive_transfer_and_changed_positions_reject(self):
        ref=reference();members=ref['types']['AuthoredRecord']['shape']['members']
        members[0].update(name='low',bit_position=0,bit_width=3);members[0]['type']['signed']=True
        members.append(dict(name='high',offset=0,type=dict(kind='integer',bytes=1,signed=False),bit_position=3,bit_width=5))
        members.sort(key=lambda m:(m['offset'],m['name']))
        def bits(title):
            blob,symbols,layouts=inputs(title)
            layouts['types']['11'].update(name='low',location=6,storage_offset=0,bit_position=0,bit_width=3,type=500)
            layouts['types']['500']=dict(id=500,tag=16,length=1,basic=6)
            layouts['types']['14']=dict(id=14,parent=10,tag=7,name='high',field_offset=0,data_kind=7,location=6,
                                       storage_offset=0,bit_position=3,bit_width=5,type=111)
            return blob,symbols,layouts
        value=LayoutAtlas(ref);value.add(*bits(1));value.add(*bits(2));target=bits(3)
        result=value.transfer(*target);decoder=TypeDecoder(result,result['xbe_sha256'])
        low=next(t for t in result['types'].values() if t.get('name')=='low')
        high=next(t for t in result['types'].values() if t.get('name')=='high')
        self.assertEqual(decoder.bitfield(low['id'],b'\xfd'+bytes(6))['value'],'-3')
        self.assertEqual(decoder.bitfield(high['id'],b'\xfd'+bytes(6))['value'],'31')
        target[2]['types']['14']['bit_position']=2
        self.assertEqual(value.transfer(*target)['decisions'][0]['status'],'unavailable')

    def test_array_count_pointer_width_and_nonmatching_symbol_authority(self):
        ref=reference();ref['types']['AuthoredRecord']['shape']['members'][2]['type']=dict(
            kind='array',bytes=4,count=4,element=dict(kind='integer',bytes=1,signed=False))
        def arrays(title):
            blob,symbols,layouts=inputs(title)
            layouts['types']['113']=dict(id=113,tag=15,length=4,count=4,type=111)
            return blob,symbols,layouts
        value=LayoutAtlas(ref);value.add(*arrays(1));value.add(*arrays(2));target=arrays(3)
        result=value.transfer(*target)
        self.assertTrue(any(t.get('tag')==15 and t['count']==4 for t in result['types'].values()))
        target[2]['types']['113']['count']=3
        self.assertEqual(value.transfer(*target)['decisions'][0]['status'],'unavailable')
        blob,symbols,layouts=inputs(3);symbols['identity']='qualified-sdk-signatures'
        with self.assertRaisesRegex(ValueError,'matching-CodeView'):
            atlas().transfer(blob,symbols,layouts)
        bad=reference();bad['abi']['pointer_bytes']=8
        with self.assertRaisesRegex(Unavailable,'reference ABI'):
            LayoutAtlas(bad).add(*inputs())

    def test_image_bound_compiler_proof_qualifies_known_alignment_and_rejects_conflicts(self):
        ref=reference(8);ref['types']['AuthoredRecord'].update(alignment=4,effective_pack=1)
        def measured(title):
            blob,symbols,layouts=inputs(title);layouts['types']['10']['length']=8
            temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup)
            artifact=Path(temporary.name)/'authored.bin';artifact.write_bytes(blob)
            declaration=Path(temporary.name)/'authored.cpp';declaration.write_text('authored layout proof fixture')
            proof=dict(schema='xboxllr.sdk-abi-proof.v1',xbe_sha256=layouts['xbe_sha256'],pdb_sha256=layouts['source_sha256'],
                compiler_record_sha256=digest(symbols['compilers']['50'][0]),artifact_sha256=hashlib.sha256(blob).hexdigest(),
                artifact_path=str(artifact),declaration_path=str(declaration),declaration_sha256=hashlib.sha256(declaration.read_bytes()).hexdigest(),
                types={'AuthoredRecord':dict(shape_sha256=digest(ref['types']['AuthoredRecord']['shape']),alignment=4,effective_pack=1)})
            return blob,symbols,layouts,proof
        value=LayoutAtlas(ref);value.add(*measured(1));value.add(*measured(2));target=measured(3)
        result=value.transfer(*target)
        self.assertEqual(result['decisions'][0]['status'],'qualified')
        self.assertEqual(next(t for t in result['types'].values() if t.get('tag')==11)['alignment'],4)
        for key in ('xbe_sha256','pdb_sha256','compiler_record_sha256','artifact_sha256'):
            wrong=copy.deepcopy(target[3]);wrong[key]='f'*64 if key!='artifact_sha256' else ''
            self.assertEqual(value.transfer(*target[:3],wrong)['decisions'][0]['status'],'unavailable')
        wrong=copy.deepcopy(target[3]);wrong['types']['AuthoredRecord']['alignment']=1
        self.assertEqual(value.transfer(*target[:3],wrong)['decisions'][0]['status'],'unavailable')
        self.assertEqual(value.transfer(*target[:3])['decisions'][0]['status'],'unavailable')
        wrong=copy.deepcopy(target[3]);wrong['artifact_sha256']='f'*64
        self.assertEqual(value.transfer(*target[:3],wrong)['decisions'][0]['status'],'unavailable')

    def test_boolean_and_integer_fields_are_not_interchangeable(self):
        blob,symbols,layouts=inputs(3)
        layouts['types']['111']['basic']=10
        self.assertEqual(atlas().transfer(blob,symbols,layouts)['decisions'][0]['status'],'unavailable')


if __name__=='__main__':unittest.main()
