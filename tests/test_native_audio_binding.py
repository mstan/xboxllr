"""Authored-byte public audio ABI checks; no game or native execution."""
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_audio_binding import audio_object_guards, bind, public_audio_abi
from native_binding import Binding


class PublicAudioABI(unittest.TestCase):
    PUBLIC = 0x1000
    TARGET = 0x1080

    def fixture(self, public=None, target=b'\xb8\x01\x00\x00\x00\xc2\x08\x00', tail=True):
        if public is None:
            public = b'\xe9'+struct.pack('<i', self.TARGET-self.PUBLIC-5)
        context = Binding.__new__(Binding)
        context.blob = bytearray(0x200)
        context.image = {'sections': [dict(address=self.PUBLIC, raw_offset=0, raw_size=0x200)]}
        context.functions = []
        for address, body in [(self.PUBLIC, public), (self.TARGET, target)]:
            context.blob[address-self.PUBLIC:address-self.PUBLIC+len(body)] = body
            context.functions.append(dict(start=hex(address), end=hex(address+len(body)),
                                          size=len(body), name=f'sub_{address:08X}'))
        functions = {int(f['start'], 16): f for f in context.functions}
        symbol = dict(name='IDirectSoundStream_SetVolume', address=self.PUBLIC,
                      call='stdcall', stack_bytes=8)
        proof = dict(sha256=hashlib.sha256(public).hexdigest(), bytes=len(public), stack_bytes=8)
        if tail:
            proof['tail_target'] = dict(address=self.TARGET, body=dict(
                sha256=hashlib.sha256(target).hexdigest(), bytes=len(target), stack_bytes=8))
        data = lambda address, size: bytes(context.blob[address-self.PUBLIC:address-self.PUBLIC+size])
        return context, symbol, proof, functions, data

    def inspect(self, fixture):
        before = bytes(fixture[0].blob), copy.deepcopy(fixture[0].functions)
        try:
            return public_audio_abi(*fixture)
        finally:
            self.assertEqual((bytes(fixture[0].blob), fixture[0].functions), before)

    def test_legacy_ret_call_record_is_unchanged(self):
        fixture = self.fixture(public=b'\xb8\x01\x00\x00\x00\xc2\x08\x00', tail=False)
        fixture[2].pop('bytes')  # Existing RET-only profiles may omit it.
        self.assertEqual(self.inspect(fixture), dict(
            name='IDirectSoundStream_SetVolume', symbol='sub_00001000', address=self.PUBLIC,
            stack_bytes=8, sha256=fixture[2]['sha256']))

    def test_single_direct_tail_retains_public_name_and_exact_target_evidence(self):
        fixture = self.fixture()
        call = self.inspect(fixture)
        self.assertEqual(call['name'], 'IDirectSoundStream_SetVolume')
        self.assertEqual(call['address'], self.PUBLIC)
        self.assertEqual(call['symbol'], 'sub_00001000')
        self.assertEqual(call['tail_target'], dict(address=self.TARGET, bytes=8,
                         stack_bytes=8, sha256=fixture[2]['tail_target']['body']['sha256']))

    def test_jump_without_explicit_proof_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'return disagrees'):
            self.inspect(self.fixture(tail=False))

    def test_malformed_tail_proofs_are_rejected(self):
        bad = [None, [], {}, {'address': self.TARGET},
               {'address': True, 'body': {}}, {'address': -1, 'body': {}},
               {'address': 0x100000000, 'body': {}},
               {'address': self.TARGET, 'body': None}]
        for value in bad:
            fixture = self.fixture()
            fixture[2]['tail_target'] = value
            with self.subTest(proof=value), self.assertRaises(ValueError):
                self.inspect(fixture)
        for field, value in [('sha256', 'bad'), ('bytes', True), ('bytes', 0),
                             ('stack_bytes', True), ('stack_bytes', 4)]:
            fixture = self.fixture()
            fixture[2]['tail_target']['body'][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                self.inspect(fixture)
        fixture = self.fixture()
        fixture[2]['tail_target']['body']['tail_target'] = fixture[2]['tail_target'].copy()
        with self.assertRaises(ValueError):
            self.inspect(fixture)

    def test_changed_public_or_target_bytes_are_rejected(self):
        for address in (self.PUBLIC, self.TARGET):
            fixture = self.fixture()
            fixture[0].blob[address-self.PUBLIC] ^= 1
            with self.subTest(address=address), self.assertRaises(ValueError):
                self.inspect(fixture)

    def test_unmatched_destination_or_nontrivial_thunk_is_rejected(self):
        cases = [b'\xe9'+struct.pack('<i', self.TARGET-self.PUBLIC-4),
                 b'\x90\xe9'+struct.pack('<i', self.TARGET-self.PUBLIC-6),
                 b'\x0f\x84'+struct.pack('<i', self.TARGET-self.PUBLIC-6),
                 b'\xff\xe0', b'\xc2\x08\x00']
        for body in cases:
            with self.subTest(body=body.hex()), self.assertRaises(ValueError):
                self.inspect(self.fixture(public=body))

    def test_public_tail_requires_explicit_complete_extent(self):
        fixture = self.fixture()
        fixture[2].pop('bytes')
        with self.assertRaises(ValueError):
            self.inspect(fixture)

    def test_missing_ambiguous_or_changed_canonical_target_is_rejected(self):
        fixture = self.fixture()
        fixture[0].functions.pop()
        with self.assertRaises(ValueError):
            self.inspect(fixture)
        fixture = self.fixture()
        fixture[0].functions.append(copy.deepcopy(fixture[0].functions[-1]))
        with self.assertRaises(ValueError):
            self.inspect(fixture)
        for field, value in [('size', 7), ('end', hex(self.TARGET+7))]:
            fixture = self.fixture()
            fixture[0].functions[-1][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.inspect(fixture)

    def test_target_must_be_inside_unique_original_stored_section(self):
        fixture = self.fixture()
        fixture[0].image['sections'][0]['raw_size'] = 0x40
        with self.assertRaises(ValueError):
            self.inspect(fixture)
        fixture = self.fixture()
        fixture[0].image['sections'].append(fixture[0].image['sections'][0].copy())
        with self.assertRaises(ValueError):
            self.inspect(fixture)

    def test_incomplete_decode_wrong_cleanup_and_recursive_target_are_rejected(self):
        for body in (b'\xc2\x04\x00', b'\x90', b'\xc2\x08\x00\x0f',
                     b'\xe9'+struct.pack('<i', self.PUBLIC-self.TARGET-5)):
            with self.subTest(body=body.hex()), self.assertRaises(ValueError):
                self.inspect(self.fixture(target=body))

    def test_unproved_external_or_mid_instruction_branch_is_rejected(self):
        for body in (b'\x75\x10\xc2\x08\x00', b'\x75\x01\xc2\x08\x00',
                     b'\xff\xe0\xc2\x08\x00'):
            with self.subTest(body=body.hex()), self.assertRaises(ValueError):
                self.inspect(self.fixture(target=body))

    def test_proved_internal_conditional_target_path_is_accepted(self):
        call = self.inspect(self.fixture(target=b'\x85\xc0\x74\x01\x90\xc2\x08\x00'))
        self.assertEqual(call['tail_target']['stack_bytes'], 8)


class AudioObjectGuards(unittest.TestCase):
    def fixture(self, tail=True):
        context = Binding.__new__(Binding)
        context.blob = bytearray(0x200)
        context.image = {'sections': [dict(address=0x1000, raw_offset=0, raw_size=0x200)]}
        context.functions = []
        bodies = {0x1000: b'\xc2\x04\x00', 0x1020: b'\xc2\x04\x00',
                  0x1040: b'\xc2\x04\x00', 0x1060: b'\xc2\x04\x00',
                  0x1080: b'\x85\xc0\x74\x01\x90\xc2\x04\x00'}
        if tail:
            bodies[0x1060] = b'\xe9'+struct.pack('<i', 0x1080-0x1060-5)
        for address, body in bodies.items():
            context.blob[address-0x1000:address-0x1000+len(body)] = body
            context.functions.append(dict(start=hex(address), end=hex(address+len(body)),
                                          size=len(body), name=f'sub_{address:08X}'))
        functions = {int(f['start'], 16): f for f in context.functions}
        tables = dict(DEVICE=dict(entries=[0x1000, 0, 0x1020]),
                      BUFFER=dict(entries=[0x1040, 0, 0x1060]))
        proof = {}
        if tail:
            proof['object_guard_tails'] = dict(BUFFER_InnerRelease=dict(
                sha256=hashlib.sha256(bodies[0x1060]).hexdigest(), bytes=5, stack_bytes=4,
                tail_target=dict(address=0x1080, body=dict(bytes=len(bodies[0x1080]),
                    stack_bytes=4, sha256=hashlib.sha256(bodies[0x1080]).hexdigest()))))
        data = lambda address, size: bytes(context.blob[address-0x1000:address-0x1000+size])
        return context, proof, tables, functions, data

    def inspect(self, fixture):
        before = bytes(fixture[0].blob), copy.deepcopy(fixture[0].functions)
        try:
            return audio_object_guards(*fixture)
        finally:
            self.assertEqual((bytes(fixture[0].blob), fixture[0].functions), before)

    def test_default_guard_records_remain_unchanged(self):
        fixture = self.fixture(tail=False)
        guards, tails = self.inspect(fixture)
        self.assertEqual(tails, {})
        self.assertEqual(guards, [dict(name=name, address=address, bytes=3,
            sha256=hashlib.sha256(b'\xc2\x04\x00').hexdigest()) for name, address in
            [('DEVICE_Destructor', 0x1000), ('DEVICE_InnerRelease', 0x1020),
             ('BUFFER_Destructor', 0x1040), ('BUFFER_InnerRelease', 0x1060)]])

    def test_tail_proof_retains_outer_stop_address_and_extent(self):
        guards, tails = self.inspect(self.fixture())
        guard = guards[-1]
        self.assertEqual((guard['name'], guard['address'], guard['bytes']),
                         ('BUFFER_InnerRelease', 0x1060, 5))
        self.assertEqual(set(tails), {'BUFFER_InnerRelease'})
        self.assertEqual(tails[guard['name']]['address'], guard['address'])
        self.assertEqual(tails[guard['name']]['tail_target']['address'], 0x1080)

    def test_tail_without_selection_is_rejected(self):
        fixture = self.fixture()
        fixture[1].clear()
        with self.assertRaisesRegex(ValueError, 'guard cleanup'):
            self.inspect(fixture)

    def test_unknown_unused_and_malformed_selection_are_rejected(self):
        for selection in (None, [], {'Unknown': {}}, {'BUFFER_AddRef': {}},
                          {'DEVICE_InnerRelease': {}}):
            fixture = self.fixture()
            fixture[1]['object_guard_tails'] = selection
            with self.subTest(selection=selection), self.assertRaises(ValueError):
                self.inspect(fixture)
        for mutation in (lambda b: b.pop('tail_target'), lambda b: b.pop('bytes'),
                         lambda b: b.update(address=0x1060),
                         lambda b: b.update(sha256='0'*64), lambda b: b.update(bytes=4),
                         lambda b: b.update(stack_bytes=8), lambda b: b.update(stack_bytes=True),
                         lambda b: b['tail_target'].update(address=0x1081),
                         lambda b: b['tail_target']['body'].update(stack_bytes=8)):
            fixture = self.fixture()
            mutation(fixture[1]['object_guard_tails']['BUFFER_InnerRelease'])
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                self.inspect(fixture)

    def test_proof_cannot_select_another_vtable_entry_or_unused_table(self):
        fixture = self.fixture()
        fixture[2]['BUFFER']['entries'][2] = 0x1020
        with self.assertRaises(ValueError):
            self.inspect(fixture)
        fixture = self.fixture()
        fixture[2].pop('BUFFER')
        with self.assertRaises(ValueError):
            self.inspect(fixture)
        fixture = self.fixture()
        body = fixture[1]['object_guard_tails'].pop('BUFFER_InnerRelease')
        fixture[1]['object_guard_tails']['DEVICE_Destructor'] = body
        with self.assertRaises(ValueError):
            self.inspect(fixture)

    def test_ambiguous_guard_and_changed_target_are_rejected(self):
        fixture = self.fixture()
        fixture[0].functions.append(copy.deepcopy(fixture[3][0x1060]))
        with self.assertRaises(ValueError):
            self.inspect(fixture)
        fixture = self.fixture()
        fixture[0].blob[0x80] ^= 1
        with self.assertRaises(ValueError):
            self.inspect(fixture)


@unittest.skipUnless(all(os.environ.get(key) for key in
    ('XLLE_TEST_AUDIO_XBE', 'XLLE_TEST_AUDIO_OUTPUT', 'XLLE_TEST_AUDIO_SYMBOLS',
     'XLLE_TEST_AUDIO_PROFILE')), 'private exact audio inputs not selected')
class OriginalAudioObjectGuards(unittest.TestCase):
    def test_constructor_selected_tail_emits_only_an_original_teardown_stop(self):
        # Run the real binder against receipt-checked inputs. Intercept every
        # write, retaining original CPU units, manifests and wrappers untouched.
        writes = {}
        def capture(path, text, *args, **kwargs):
            writes[path.name] = text
            return len(text)
        with patch.object(Path, 'write_text', capture):
            binding = bind(*(Path(os.environ[key]) for key in
                ('XLLE_TEST_AUDIO_XBE', 'XLLE_TEST_AUDIO_OUTPUT', 'XLLE_TEST_AUDIO_SYMBOLS',
                 'XLLE_TEST_AUDIO_PROFILE')))
        header = writes['pilot_audio_bindings.h']
        wrapper = writes['pilot_audio_binding.c']
        self.assertTrue(binding['object_guard_tails'])
        for name, guard in binding['object_guard_tails'].items():
            kind, suffix = name.split('_', 1)
            slot = {'Destructor': 0, 'InnerRelease': 2}[suffix]
            self.assertEqual(guard['address'], binding['tables'][kind]['entries'][slot])
            self.assertEqual(guard['tail_target']['stack_bytes'], 4)
            self.assertIn(f'{{"{name}", {guard["address"]}u, 5u, "{guard["sha256"]}"}}', header)
            self.assertNotIn('pilot_original_audio_'+name, wrapper)
        record = json.loads(writes['audio-binding.aot-private.json'])
        self.assertEqual(record['object_guard_tails'], binding['object_guard_tails'])


if __name__ == '__main__':
    unittest.main()
