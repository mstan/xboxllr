"""Authored title profiles exercise selection and fail-closed ABI binding.

No title binaries, proprietary generated sources, renderer or hardware needed.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_binding import Binding, has_profile_binding, select_profile, validate_body
from native_input_binding import bind as bind_input


class Profiles(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / 'title.json'
        self.profile = dict(schema='native-title-profile.v1', id='authored-second-title',
                            xbe_sha256='a' * 64,
                            bindings={'cpu': dict(adapter='x86-mmx-cdecl-v1',
                                                 body=dict(sha256='b' * 64, bytes=50))})

    def save(self):
        self.path.write_text(json.dumps(self.profile), encoding='utf-8')
        return self.path

    def test_explicit_second_title_and_supported_adapter_tuple(self):
        proof = select_profile('a' * 64, 'cpu', self.save(), adapter=('other-adapter', 'x86-mmx-cdecl-v1'))
        self.assertEqual(proof['body']['bytes'], 50)
        self.assertTrue(has_profile_binding('a' * 64, 'cpu', self.path))
        self.assertFalse(has_profile_binding('a' * 64, 'display', self.path))

    def test_default_unknown_image_cannot_inherit_blinx(self):
        with self.assertRaises(ValueError):
            select_profile('a' * 64, 'cpu', adapter='x86-mmx-cdecl-v1')

    def test_wrong_identity_schema_family_and_adapter(self):
        self.save()
        for image, family, adapter in [('c' * 64, 'cpu', 'x86-mmx-cdecl-v1'),
                                       ('a' * 64, 'input', 'xapi-gamepad-v1'),
                                       ('a' * 64, 'cpu', 'unreviewed-adapter')]:
            with self.subTest(image=image, family=family, adapter=adapter), self.assertRaises(ValueError):
                select_profile(image, family, self.path, adapter=adapter)
        for key, bad in [('schema', 'future-schema'), ('id', '../title'),
                         ('xbe_sha256', 'a' * 63), ('bindings', [])]:
            original = self.profile[key]
            self.profile[key] = bad
            with self.subTest(key=key), self.assertRaises(ValueError):
                select_profile('a' * 64, 'cpu', self.save(), adapter='x86-mmx-cdecl-v1')
            self.profile[key] = original

    def test_include_cannot_leave_framework_or_choose_code(self):
        self.profile['bindings']['cpu'] = {'include': '../outside-profile.json'}
        with self.assertRaisesRegex(ValueError, 'leaves profile/framework'):
            select_profile('a' * 64, 'cpu', self.save(), adapter='x86-mmx-cdecl-v1')
        self.profile['bindings']['cpu']['adapter'] = 'python:os.system'
        with self.assertRaisesRegex(ValueError, 'invalid.*include'):
            select_profile('a' * 64, 'cpu', self.save(), adapter='x86-mmx-cdecl-v1')

    def test_external_game_profile_can_include_local_data(self):
        self.profile['bindings']['cpu'] = {'include': 'adapters/cpu.json'}
        (self.root / 'adapters').mkdir()
        proof = dict(adapter='x86-mmx-cdecl-v1', body=dict(sha256='b' * 64, bytes=50))
        (self.root / 'adapters/cpu.json').write_text(json.dumps(proof))
        self.assertEqual(select_profile('a' * 64, 'cpu', self.save(), adapter='x86-mmx-cdecl-v1'), proof)

    def test_malformed_body_proofs(self):
        for proof in [None, {}, {'sha256': 'bad'}, {'sha256': 'b' * 64, 'bytes': True},
                      {'sha256': 'b' * 64, 'bytes': 0}, {'sha256': 'b' * 64, 'stack_bytes': 6}]:
            with self.subTest(proof=proof), self.assertRaises(ValueError):
                validate_body(proof)

    def input_fixture(self):
        context = Binding.__new__(Binding)
        context.output = self.root / 'out'
        context.generated = context.output / 'gen'
        context.generated.mkdir(parents=True)
        context.manifest_path = context.output / 'generation.aot-private.json'
        context.manifest = {}
        context.image = dict(sha256='a' * 64, sections=[dict(address=0x1000, raw_offset=0, raw_size=0x1000)])
        context.functions, context.symbols = [], []
        blob, code, calls = bytearray(0x1000), [], {}
        methods = [('XInitDevices', 8), ('XGetDevices', 4), ('XGetDeviceChanges', 12),
                   ('XInputOpen', 16), ('XInputClose', 4), ('XInputGetState', 8),
                   ('XGetDeviceEnumerationStatus', 0), ('XInputGetDeviceDescription', 4), ('SetLastError', 4)]
        for i, (name, cleanup) in enumerate(methods):
            start = 0x1000 + i * 32
            body = b'\xb8' + struct.pack('<I', i) + b'\xc2' + struct.pack('<H', cleanup)
            blob[start-0x1000:start-0x1000+len(body)] = body
            function = dict(start=f'0x{start:08X}', end=f'0x{start+len(body):08X}', size=len(body), name=f'sub_{start:08X}')
            context.functions.append(function)
            context.symbols.append(dict(name=name, library='XAPILIB', kind='function', call='stdcall',
                                        stack_bytes=8 if name == 'XInputGetDeviceDescription' else cleanup, address=start))
            proof = dict(sha256=hashlib.sha256(body).hexdigest(), bytes=len(body), stack_bytes=cleanup)
            if name == 'XInputGetDeviceDescription':
                proof['symbol_stack_bytes'] = 8
            calls[name] = proof
            code.append(f'void {function["name"]}(void)\n{{\n}}\n')
        context.blob = bytes(blob)
        for i, name in enumerate(('g_DeviceType_Gamepad', 'g_DeviceType_MU', 'g_DeviceType_IRDongle')):
            context.symbols.append(dict(name=name, library='XAPILIB', kind='variable', address=0x2000+i*16))
        (context.generated / 'recomp_0.c').write_text('\n'.join(code), encoding='utf-8')
        last = calls.pop('SetLastError')
        self.profile['bindings'] = {'input': dict(adapter='xapi-gamepad-v1', calls=calls, set_last_error=last,
                                                 layout=dict(state_bytes=22, default_polling=[1, 8, 0, 0]),
                                                 variables={s['name']: s['address'] for s in context.symbols if s['kind'] == 'variable'})}
        self.save()
        return context

    def test_subset_emits_absent_unique_out_of_range_and_no_unproved_layout(self):
        context = self.input_fixture()
        with patch('native_input_binding.Binding', return_value=context):
            record = bind_input(Path('unused'), context.output, Path('unused'), self.path)
            self.assertEqual(len(record['calls']), 8)
            header = (context.generated / 'pilot_input_bindings.h').read_text()
            self.assertIn('PILOT_INPUT_XInputGetCapabilities = PILOT_INPUT_COUNT + 1', header)
            self.assertIn('PILOT_INPUT_XInputSetState = PILOT_INPUT_COUNT + 2', header)
            self.assertIn('#define PILOT_INPUT_HAS_CAPABILITIES 0', header)
            self.assertNotIn('#define PILOT_INPUT_CAPABILITIES_BYTES', header)
            self.assertNotIn('#define PILOT_INPUT_FEEDBACK_MOTOR_OFFSET', header)
            self.assertEqual(bind_input(Path('unused'), context.output, Path('unused'), self.path), record)

    def test_mismatched_body_or_ret_never_mutates_generated_sources(self):
        for change in ('digest', 'cleanup', 'variable'):
            with self.subTest(change=change):
                # Each failure uses a clean authored tree.
                if (self.root / 'out').exists():
                    self.root = self.root / 'next'
                    self.root.mkdir()
                    self.path = self.root / 'title.json'
                context = self.input_fixture()
                before = (context.generated / 'recomp_0.c').read_bytes()
                if change == 'digest':
                    self.profile['bindings']['input']['calls']['XInputOpen']['sha256'] = 'f' * 64
                elif change == 'cleanup':
                    # Correct body hash alone does not qualify a changed RET.
                    entry = self.profile['bindings']['input']['calls']['XInputGetDeviceDescription']
                    function = context.functions[-2]
                    offset = int(function['start'], 16) - 0x1000
                    blob = bytearray(context.blob)
                    blob[offset+6] = 8
                    context.blob = bytes(blob)
                    entry['sha256'] = hashlib.sha256(context.blob[offset:offset+function['size']]).hexdigest()
                else:
                    self.profile['bindings']['input']['variables']['g_DeviceType_Gamepad'] += 4
                self.save()
                with patch('native_input_binding.Binding', return_value=context), self.assertRaises(ValueError):
                    bind_input(Path('unused'), context.output, Path('unused'), self.path)
                self.assertEqual((context.generated / 'recomp_0.c').read_bytes(), before)
                self.assertFalse((context.generated / 'pilot_input_binding.c').exists())


if __name__ == '__main__':
    unittest.main()
