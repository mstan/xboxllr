"""Authored startup binder contracts; original offline check is a separate CLI."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_startup_binding as n


def call(site,target):return b'\xe8'+struct.pack('<i',target-site-5)
def jump(site,target):return b'\xe9'+struct.pack('<i',target-site-5)


class Context:
    def __init__(self,directory):
        self.output=directory;self.generated=directory/'gen';self.generated.mkdir()
        self.manifest_path=directory/'generation.aot-private.json'
        self.image=dict(sha256='a'*64,sections=[dict(address=0x1000,raw_offset=0,raw_size=0x3000)])
        self.functions=[];blob=bytearray(0x3000)
        bodies={'apply':(0x1000,b'\xa1'+struct.pack('<I',0x2000)+b'\x8b\x0d'+struct.pack('<I',0x2004)+jump(0x100b,0x1100)+b'\xc3'),
                'setup':(0x1100,b'\xa3'+struct.pack('<I',0x3000)+b'\x89\x15'+struct.pack('<I',0x3004)+call(0x110b,0x1200)+b'\xc3'),
                'exchange':(0x1200,b'\xc2\x08\x00'),
                'teardown_consumer':(0x1300,call(0x1300,0x1200)+b'\xc2\x04\x00'),
                'startup_caller':(0x1400,call(0x1400,0x1000)+call(0x1405,0x1500)+b'\xc3'),
                'next_initializer':(0x1500,b'\x55\x8b\xec\x83\xec\x34\xc9\xc3')}
        proof=json.loads((Path(__file__).resolve().parents[1]/'tools/native_profiles/dashboard-5960-startup.json').read_text())
        proof['functions']={}
        for role,(address,body) in bodies.items():
            name=f'sub_{address:08X}';blob[address-0x1000:address-0x1000+len(body)]=body
            self.functions.append(dict(start=hex(address),end=hex(address+len(body)),size=len(body),name=name))
            proof['functions'][role]=dict(sha256=hashlib.sha256(body).hexdigest(),bytes=len(body))
            if n.ROLES[role] is not None:proof['functions'][role]['stack_bytes']=n.ROLES[role]
        struct.pack_into('<II',blob,0x1000,0x80000142,0x80000144)
        self.blob=bytes(blob)
        proof['layout']=dict(saved_low=0x3000,saved_high=0x3004,hardware_import=0x2000,version_import=0x2004,
            apply_return=0x1405,setup_exchange_return=0x1110,restore_exchange_return=0x1305)
        self.proof=proof
        # CRLF fixture verifies binder does not normalize unaffected code/body bytes.
        self.code=b''.join((f'/* authored */\r\nvoid {f["name"]}(void)\r\n{{\r\n    return;\r\n}}\r\n'.encode() for f in self.functions))
        (self.generated/'recomp_0000.c').write_bytes(self.code)
        self.manifest=dict(sha256=self.image['sha256'],files={'recomp_0000.c':hashlib.sha256(self.code).hexdigest()})
        self.manifest_path.write_text(json.dumps(self.manifest))
    def digest(self,function):return hashlib.sha256(n.body_bytes(self,function)).hexdigest()
    def matches_body(self,function,proof):return function['size']==proof['bytes'] and self.digest(function)==proof['sha256']
    def require_profile(self,*args,**kwargs):return self.proof


class StartupBindingTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.context=Context(Path(self.temp.name))

    def test_binding_preserves_originals_and_is_idempotent(self):
        with patch.object(n,'Binding',return_value=self.context):
            receipt=n.bind(Path('authored.xbe'),self.context.output,Path('symbols.json'))
            self.assertEqual(set(receipt['functions']),set(n.ROLES))
            code=(self.context.generated/'recomp_0000.c').read_bytes()
            expected=self.context.code
            for role in ('apply','setup','exchange'):
                function=next(f for f in self.context.functions if f['start']==hex(receipt['functions'][role]['address']))
                old=b'void '+function['name'].encode()+b'(void)'
                expected=expected.replace(old,b'void pilot_startup_original_'+function['name'].encode()+b'(void)')
            self.assertEqual(code,expected)
            self.assertIn(b'#if defined(PILOT_STARTUP_NATIVE) && PILOT_STARTUP_NATIVE',(self.context.generated/'pilot_startup_binding.c').read_bytes())
            before={p.name:(p.read_bytes(),p.stat().st_mtime_ns) for p in self.context.generated.iterdir()}
            self.assertEqual(n.bind(Path('authored.xbe'),self.context.output,Path('symbols.json')),receipt)
            self.assertEqual(before,{p.name:(p.read_bytes(),p.stat().st_mtime_ns) for p in self.context.generated.iterdir()})

    def test_bad_body_contract_and_caller_reject_before_mutation(self):
        original=copy.deepcopy(self.context.proof)
        def changed_body(p):p['functions']['exchange']['sha256']='0'*64
        def changed_cleanup(p):p['functions']['exchange']['stack_bytes']=4
        def changed_caller(p):p['layout']['apply_return']+=1
        def changed_kernel(p):p['native_contract']['export_only']=False
        def changed_consumer(p):p['layout']['restore_exchange_return']+=1
        def changed_observation(p):p['kernel_observation']['initial_descriptor']='0x00cf93000000ffff'
        for mutate in (changed_body,changed_cleanup,changed_caller,changed_kernel,changed_consumer,changed_observation):
            self.context.proof=copy.deepcopy(original);mutate(self.context.proof)
            before={p.name:p.read_bytes() for p in self.context.generated.iterdir()}
            with patch.object(n,'Binding',return_value=self.context),self.assertRaises(ValueError):
                n.bind(Path('authored.xbe'),self.context.output,Path('symbols.json'))
            self.assertEqual(before,{p.name:p.read_bytes() for p in self.context.generated.iterdir()})
            self.assertFalse((self.context.output/'startup-binding.aot-private.json').exists())

    def test_missing_generated_original_rejects_without_partial_rename(self):
        path=self.context.generated/'recomp_0000.c'
        path.write_bytes(self.context.code.replace(b'void sub_00001200',b'void renamed_unknown'))
        before=path.read_bytes()
        with patch.object(n,'Binding',return_value=self.context),self.assertRaises(ValueError):
            n.bind(Path('authored.xbe'),self.context.output,Path('symbols.json'))
        self.assertEqual(path.read_bytes(),before)

    def test_source_hash_mismatch_rejected(self):
        with self.assertRaises(ValueError):n.verify_kernel_observation(b'authored',self.context.proof['kernel_observation'])
        with self.assertRaises(ValueError):n.verify_kernel_metadata(b'authored',self.context.proof['kernel_metadata'])

    def test_swapped_or_aliased_operand_roles_rejected(self):
        original=copy.deepcopy(self.context.proof)
        for left,right,swap in (('saved_low','saved_high',True),('saved_low','saved_high',False),
                                ('hardware_import','version_import',True),('hardware_import','version_import',False)):
            self.context.proof=copy.deepcopy(original)
            layout=self.context.proof['layout']
            if swap:layout[left],layout[right]=layout[right],layout[left]
            else:layout[right]=layout[left]
            before=(self.context.generated/'recomp_0000.c').read_bytes()
            with patch.object(n,'Binding',return_value=self.context),self.assertRaises(ValueError):
                n.bind(Path('authored.xbe'),self.context.output,Path('symbols.json'))
            self.assertEqual((self.context.generated/'recomp_0000.c').read_bytes(),before)

    def test_wrong_import_ordinal_rejected(self):
        blob=bytearray(self.context.blob);struct.pack_into('<I',blob,0x1000,0x80000144)
        self.context.blob=bytes(blob)
        with self.assertRaises(ValueError):n.validate(self.context,self.context.proof)


if __name__=='__main__':unittest.main()
