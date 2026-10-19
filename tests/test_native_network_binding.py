"""Authored original bytes: closed network proof, retention and fail-closed bind."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_binding import Binding
from native_network_binding import bind


class NetworkBinding(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.context = c = Binding.__new__(Binding)
        c.output = self.root; c.generated = self.root/'gen'; c.generated.mkdir()
        c.manifest = {}; c.manifest_path = self.root/'generation.aot-private.json'
        c.functions, c.symbols = [], []
        c.image = dict(sha256='a'*64, sections=[dict(address=0x1000, raw_size=0x3000, raw_offset=0)])
        blob = bytearray(0x3000); sources = []
        self.proof = dict(adapter='xonline-offline-lifecycle-v1',
            globals=dict(online_owner=0x3000, online_gate=0x3004, network_gate=0x3008, network_owner=0x300c),
            consumer_range=[0x3000,0x3010], calls=[],
            callback_table=dict(address=0x2800, entries=[0x1300,0x1400,0x1500,0x1600], publisher=0x1000,
                                instruction=0x1005, owner_offset=0x264),
            untranslated_callbacks=[], set_last_error={})
        for address, body, action, refs, returns in [
            (0x1000, b'\xa1'+struct.pack('<I',0x3000)+b'\xc7\x80'+struct.pack('<II',0x264,0x2800)+b'\xc2\x04\x00', 'startup', [[0x1000,0x3000]],[4]),
            (0x1100, b'\xa1'+struct.pack('<I',0x3000)+b'\xc3', 'cleanup', [[0x1100,0x3000]],[0]),
            (0x1200, b'\xa1'+struct.pack('<I',0x300c)+b'\xc2\x08\x00', 'guard', [[0x1200,0x300c]],[8]),
            (0x1300, b'\xc2\x04\x00', 'guard', [],[4]),
            (0x1400, b'\xc2\x08\x00', 'guard', [],[8]),
            (0x1700, b'\xc2\x04\x00', None, [],[4])]:
            blob[address-0x1000:address-0x1000+len(body)] = body
            function=dict(start=f'0x{address:08X}',end=f'0x{address+len(body):08X}',size=len(body),name=f'sub_{address:08X}',section='.text')
            c.functions.append(function);sources.append(f'void {function["name"]}(void)\n{{\n    /* retained {address:x} */\n}}\n')
            proof=dict(address=address,bytes=len(body),sha256=hashlib.sha256(body).hexdigest(),returns=returns,references=refs,action=action)
            if action:self.proof['calls'].append(proof)
            else:
                self.proof['set_last_error']=dict(bytes=len(body),sha256=proof['sha256'],stack_bytes=4)
                c.symbols.append(dict(name='SetLastError',library='XAPILIB',kind='function',call='stdcall',stack_bytes=4,address=address))
        for address in [0x1500,0x1600]:
            body=b'\xc2\x04\x00';blob[address-0x1000:address-0x1000+3]=body
            self.proof['untranslated_callbacks'].append(dict(address=address,bytes=3,sha256=hashlib.sha256(body).hexdigest(),returns=[4]))
        blob[0x1800:0x1810]=struct.pack('<4I',*self.proof['callback_table']['entries'])
        c.blob=bytes(blob)
        (c.generated/'recomp_0000.c').write_text('\n'.join(sources))
        c.require_profile=lambda *a,**kw:self.proof

    def run_bind(self):
        with patch('native_network_binding.Binding',return_value=self.context):
            return bind(self.root/'original.xbe',self.root,self.root/'symbols.json')

    def assert_unchanged_rejection(self, text):
        before={p.name:p.read_bytes() for p in self.context.generated.iterdir()}
        with self.assertRaisesRegex(ValueError,text):self.run_bind()
        self.assertEqual(before,{p.name:p.read_bytes() for p in self.context.generated.iterdir()})

    def test_closed_binding_retains_original_and_is_idempotent(self):
        result=self.run_bind();before={p.name:p.read_bytes() for p in self.context.generated.iterdir()}
        self.assertEqual(len(result['calls']),5)
        source=(self.context.generated/'recomp_0000.c').read_text()
        self.assertIn('void pilot_original_network_00001000(void)',source)
        self.assertIn('/* retained 1000 */',source)
        self.assertIn('void sub_00001700(void)',source)
        wrapper=(self.context.generated/'pilot_network_binding.c').read_text()
        self.assertIn('#if defined(PILOT_NETWORK_OFFLINE) && PILOT_NETWORK_OFFLINE',wrapper)
        self.assertIn('pilot_original_network_00001000();',wrapper)
        self.assertEqual(result,self.run_bind())
        self.assertEqual(before,{p.name:p.read_bytes() for p in self.context.generated.iterdir()})

    def test_missing_consumer_rejected_before_mutation(self):
        self.proof['calls']=[v for v in self.proof['calls'] if v['address']!=0x1200]
        self.assert_unchanged_rejection('unprotected original network global')

    def test_body_return_reference_and_callback_drift(self):
        original=copy.deepcopy(self.proof)
        mutations=[lambda p:p['calls'][0].update(sha256='b'*64),
                   lambda p:p['calls'][0].update(returns=[0]),
                   lambda p:p['calls'][0].update(references=[]),
                   lambda p:p['callback_table']['entries'].__setitem__(0,0x1304),
                   lambda p:p['callback_table'].update(instruction=0x1000),
                   lambda p:p['untranslated_callbacks'][0].update(sha256='c'*64)]
        for mutation in mutations:
            self.proof=copy.deepcopy(original);mutation(self.proof)
            self.assert_unchanged_rejection('unreviewed|differs|unprotected')

    def test_new_callback_dispatch_requires_explicit_guard(self):
        (self.context.generated/'recomp_dispatch.c').write_text('{0x00001500u, callback},\n')
        self.assert_unchanged_rejection('callback dispatch')

    def test_stored_core_pointer_alias_rejected(self):
        blob=bytearray(self.context.blob);struct.pack_into('<I',blob,0x1900,0x3000);self.context.blob=bytes(blob)
        self.assert_unchanged_rejection('global literal/alias')

    def test_missing_original_body_and_changed_existing_wrapper_rejected(self):
        source=self.context.generated/'recomp_0000.c';source.write_text(source.read_text().replace('sub_00001200','other_00001200'))
        self.assert_unchanged_rejection('missing original')
        source.write_text(source.read_text().replace('other_00001200','sub_00001200'));self.run_bind()
        wrapper=self.context.generated/'pilot_network_binding.c';wrapper.write_text(wrapper.read_text()+'/*changed*/')
        self.assert_unchanged_rejection('existing network binding differs')

if __name__=='__main__':unittest.main()
