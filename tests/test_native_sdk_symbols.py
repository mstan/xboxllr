"""SDK attribution evidence and refusal paths from authored IA-32 fixtures."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_sdk_symbols import Image,Atlas
from native_guest_symbols import SCHEMA
from native_symbol_discovery import QualifiedFunctions
from test_aot_inventory import xbe


def arithmetic(seed=0x12345678):
    return bytes.fromhex('8b4424048b5424086bc00301d035')+struct.pack('<I',seed)+bytes.fromhex('c1c00505df9b571329d08b54240c01d0c3')


def fixture(code=None, *, text=0x11000, data=0x13000, title=1, build=5000, flags=0x4001, mutable=False, ordinal=8):
    code=arithmetic() if code is None else code
    blob=bytearray(xbe(code.ljust(0x200,b'\xcc'),entry=text))
    blob.extend(bytes(0x100))
    def put(offset,*values):struct.pack_into('<'+'I'*len(values),blob,offset,*values)
    put(0x10c,data+0x1000-0x10000)
    put(0x11c,2);put(0x304,text);put(0x208,title)
    put(0x338,3 if mutable else 2,data,0x1000,0x600,0x100,0x10390)
    blob[0x380:0x386]=b'D3DX\0\0';blob[0x390:0x397]=b'.rdata\0'
    put(0x160,1,0x101b0)
    blob[0x1b0:0x1c0]=b'D3DX8\0\0\0'+struct.pack('<4H',1,0,build,flags)
    put(0x158,(data+0x40)^0x5B6D40B6);put(0x640,0x80000000|ordinal,0)
    blob[0x600:0x604]=struct.pack('<f',0.5)
    row=dict(address=text,size=len(code),names=['AuthoredSDK'],extent_kind='pdb-length')
    document=dict(schema=SCHEMA,xbe_sha256=hashlib.sha256(blob).hexdigest(),source_sha256='a'*64,
                  identity='matching-codeview',functions=[row])
    return bytes(blob),document


class SDKSymbols(unittest.TestCase):
    def test_named_scalar_requires_independent_exact_xrefs_and_bound_object_metadata(self):
        atlas=Atlas()
        for title in (1,2):
            blob,symbols=fixture(b'\xf3\x0f\x10\x05'+struct.pack('<I',0x13000)+arithmetic(),title=title)
            layouts=dict(schema='xboxllr.guest-types.v1',xbe_sha256=symbols['xbe_sha256'],source_sha256=symbols['source_sha256'],
                types={'1':dict(id=1,tag=16,basic=8,length=4)},
                objects=[dict(address=0x13000,name='AuthoredHalf',type=1,mapped_bytes=4)])
            atlas.add(Image(blob,symbols['functions']),symbols,layouts)
        target,rows=fixture(b'\xf3\x0f\x10\x05'+struct.pack('<I',0x24000)+arithmetic(),text=0x21000,data=0x24000,title=3)
        result=atlas.match(Image(target,rows['functions']))
        self.assertEqual(result['objects'][0]['address'],0x24000)
        self.assertEqual(result['objects'][0]['size'],4)
        self.assertEqual(result['objects'][0]['names'],['AuthoredHalf'])
        self.assertNotIn('type',result['objects'][0])
        for group in atlas.document['groups'].values():
            group[1]['constants'][0]['names']=[]
        unknown=atlas.match(Image(target,rows['functions']))
        self.assertFalse(unknown['objects'])
        self.assertEqual(unknown['object_decisions'][0]['status'],'unknown')
        bad=dict(layouts,xbe_sha256='f'*64)
        with self.assertRaisesRegex(ValueError,'another image'):
            Atlas().add(Image(blob,symbols['functions']),symbols,bad)
        with self.assertRaisesRegex(ValueError,'different sources'):
            Atlas().add(Image(blob,symbols['functions']),symbols,dict(layouts,source_sha256='b'*64))

    def test_readonly_constant_relocation_and_changed_value(self):
        source,symbols=fixture(b'\xf3\x0f\x10\x05'+struct.pack('<I',0x13000)+arithmetic())
        target,rows=fixture(b'\xf3\x0f\x10\x05'+struct.pack('<I',0x24000)+arithmetic(),text=0x21000,data=0x24000,title=3)
        a,b=Image(source,symbols['functions']),Image(target,rows['functions'])
        left,right=a.fingerprint(0x11000),b.fingerprint(0x21000)
        self.assertEqual(left['signature'],right['signature'])
        self.assertEqual(left['constants'][0]['address'],0x13000)
        self.assertEqual(right['constants'][0]['address'],0x24000)
        changed=bytearray(target);changed[0x600:0x604]=struct.pack('<f',0.75)
        self.assertNotEqual(left['signature'],Image(bytes(changed),rows['functions']).fingerprint(0x21000)['signature'])

    def test_import_ordinal_is_bound_independently_of_its_thunk_address(self):
        source,rows=fixture(b'\xff\x15'+struct.pack('<I',0x13040)+arithmetic())
        target,target_rows=fixture(b'\xff\x15'+struct.pack('<I',0x24040)+arithmetic(),text=0x21000,data=0x24000,title=3)
        first=Image(source,rows['functions']).fingerprint(0x11000)
        self.assertEqual(first['signature'],Image(target,target_rows['functions']).fingerprint(0x21000)['signature'])
        self.assertEqual(first['imports'],[8])
        changed=bytearray(target);struct.pack_into('<I',changed,0x640,0x80000009)
        self.assertNotEqual(first['signature'],Image(bytes(changed),target_rows['functions']).fingerprint(0x21000)['signature'])
        segmented,seg_rows=fixture(b'\x64\xff\x15'+struct.pack('<I',0x13040)+arithmetic())
        self.assertEqual(Image(segmented,seg_rows['functions']).fingerprint(0x11000)['status'],'unknown')

    def test_external_callee_body_is_part_of_the_signature(self):
        parent=b'\xe8'+struct.pack('<i',0x80-5)+arithmetic()
        payload=parent.ljust(0x80,b'\xcc')+arithmetic(0x33334444)
        blob,rows=fixture(payload)
        rows['functions'][0]['size']=len(parent)
        rows['functions'].append(dict(address=0x11080,size=len(arithmetic()),names=['Helper'],extent_kind='pdb-length'))
        first=Image(blob,rows['functions']).fingerprint(0x11000)
        self.assertEqual(first['status'],'qualified')
        self.assertEqual(len(first['callees']),1)
        changed=bytearray(blob);changed[0x480:0x480+len(arithmetic())]=arithmetic(0x55556666)
        self.assertNotEqual(first['signature'],Image(bytes(changed),rows['functions']).fingerprint(0x11000)['signature'])
        self.assertEqual(Image(blob,rows['functions'][:1]).fingerprint(0x11000)['status'],'unknown')

    def test_unqualified_data_indirect_transfers_and_embedded_bytes_are_unknown(self):
        for prefix,mutable in [(b'\xa1'+struct.pack('<I',0x13000),True),
                               (b'\x8b\x04\x85'+struct.pack('<I',0x13000),False),(b'\xff\xd0',False)]:
            blob,rows=fixture(prefix+arithmetic(),mutable=mutable)
            self.assertEqual(Image(blob,rows['functions']).fingerprint(0x11000)['status'],'unknown')
        blob,rows=fixture(arithmetic()+b'\x12\x34')
        self.assertEqual(Image(blob,rows['functions']).fingerprint(0x11000)['status'],'unknown')

    def atlas(self, *, second_names=None, builds=(5000,5000)):
        atlas=Atlas()
        for title,build in zip((1,2),builds):
            blob,rows=fixture(title=title,build=build)
            if title==2 and second_names:rows['functions'][0]['names']=second_names
            atlas.add(Image(blob,rows['functions']),rows)
        return atlas

    def test_heldout_matching_never_uses_target_labels(self):
        blob,rows=fixture(title=3)
        rows['functions'][0]['names']=['Wrong target label']
        result=self.atlas().match(Image(blob,rows['functions']))
        self.assertEqual(result['functions'][0]['names'],['AuthoredSDK'])
        self.assertEqual(result['identity'],'qualified-sdk-signatures')
        with self.assertRaises(ValueError):QualifiedFunctions(result,blob)
        source,source_rows=fixture(title=1)
        with self.assertRaisesRegex(ValueError,'used to select'):self.atlas().match(Image(source,source_rows['functions']))
        with self.assertRaisesRegex(ValueError,'different matcher'):
            Atlas(dict(self.atlas().document,producer_sha256='0'*64))

    def test_ambiguity_versions_and_debug_context_remain_explicit(self):
        blob,rows=fixture(title=3)
        result=self.atlas(second_names=['DifferentSDK']).match(Image(blob,rows['functions']))
        self.assertFalse(result['functions'])
        self.assertIn('ambiguous',result['decisions'][0]['reason'])
        blob,rows=fixture(title=3,build=6000)
        self.assertFalse(self.atlas().match(Image(blob,rows['functions']))['functions'])
        self.assertEqual(len(self.atlas(builds=(4000,5000)).match(Image(blob,rows['functions']))['functions']),1)
        blob,rows=fixture(title=3,flags=0xc001)
        self.assertFalse(self.atlas().match(Image(blob,rows['functions']))['functions'])


if __name__=='__main__':unittest.main()
