import copy
import hashlib
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_audio_spatial import SPATIAL_CALLS,bind_spatial

class SpatialAdmission(unittest.TestCase):
    def setUp(self):
        self.code={};self.functions={};self.calls=[];setters={}
        def body(at,stack,target=None):
            code=(b'\xe8'+struct.pack('<i',target-at-5) if target else b'\x90')+b'\xc2'+struct.pack('<H',stack)
            self.code[at]=code;self.functions[at]=dict(start=hex(at),size=len(code))
            return dict(address=at,body=dict(sha256=hashlib.sha256(code).hexdigest(),bytes=len(code),stack_bytes=stack))
        for i,(name,stack) in enumerate(SPATIAL_CALLS.items()):
            at=0x1000+i*0x100
            voice=body(at+0x80,stack);buffer=body(at+0x40,stack,at+0x80);body(at,stack,at+0x40)
            setters[name]=dict(buffer=buffer,voice=voice)
            self.calls.append(dict(name='IDirectSoundBuffer_'+name,address=at,stack_bytes=stack))
        self.proof=dict(listener={},spatial=dict(adapter='spatial-cache-v1',setters=setters))
        self.context=type('Context',(),{})()
        self.context.matches_body=lambda f,b:(hashlib.sha256(self.code[int(f['start'],16)]).hexdigest()==b['sha256'] and f['size']==b['bytes'])
    def bind(self):
        return bind_spatial(self.context,self.proof,self.calls,self.functions,lambda at,n:self.code[at][:n])
    def test_optional_family_and_complete_actual_call_chains(self):
        self.assertEqual(len(self.bind()['setters']),13)
        self.proof.pop('spatial');self.assertIsNone(self.bind())
    def test_reject_missing_family_fields_or_listener(self):
        original=copy.deepcopy(self.proof)
        for mutate in (lambda p:p.pop('listener'),lambda p:p['spatial']['setters'].pop('SetPosition'),
                       lambda p:p['spatial'].update(adapter='future'),lambda p:p['spatial'].update(extra=True)):
            self.proof=copy.deepcopy(original);mutate(self.proof)
            with self.assertRaisesRegex(ValueError,'incomplete'):self.bind()
    def test_reject_changed_body_cleanup_and_owner_chain(self):
        original=copy.deepcopy(self.proof)
        for key,value in (('sha256','f'*64),('stack_bytes',8)):
            self.proof=copy.deepcopy(original);self.proof['spatial']['setters']['SetPosition']['voice']['body'][key]=value
            with self.assertRaises(ValueError):self.bind()
        self.proof=copy.deepcopy(original)
        self.proof['spatial']['setters']['SetPosition']['voice']=self.proof['spatial']['setters']['SetVelocity']['voice']
        with self.assertRaisesRegex(ValueError,'chain differs'):self.bind()
        self.proof=copy.deepcopy(original);self.calls[0]['stack_bytes']=4
        with self.assertRaisesRegex(ValueError,'public owner/ABI'):self.bind()

if __name__=='__main__':unittest.main()
