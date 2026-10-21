import copy
import hashlib
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_audio_spatial_math import (ROOT_ABIS, DEFAULT_SIZES,
                                      bind_spatial_math, spatial_math_header, retains_spatial_math)


class SpatialMathAdmission(unittest.TestCase):
    def setUp(self):
        self.code, self.functions = {}, {}
        helpers, roots = [], {}
        self.helper = 0x3000
        def add(address, stack, prefix=b''):
            code = prefix + b'\xc2' + struct.pack('<H', stack)
            self.code[address] = code
            self.functions[address] = dict(start=hex(address), size=len(code))
            entry = dict(address=address, body=dict(bytes=len(code), stack_bytes=stack,
                         sha256=hashlib.sha256(code).hexdigest()))
            helpers.append(entry)
            return entry
        for i, (name, stack) in enumerate(ROOT_ABIS.items()):
            address = 0x1000 + i * 0x100
            roots[name] = address
            add(address, stack, b'\xe8' + struct.pack('<i', self.helper - address - 5))
        add(self.helper, 0)
        defaults = {}
        for i, (name, size) in enumerate(DEFAULT_SIZES.items()):
            address = 0x4000 + i * 0x100
            self.code[address] = bytes(size)
            defaults[name] = dict(address=address, bytes=size,
                                 sha256=hashlib.sha256(bytes(size)).hexdigest())
        self.proof = dict(listener={}, spatial={}, spatial_math=dict(
            adapter='original-native-spatial-math-v1', roots=roots,
            helpers=helpers, defaults=defaults, indirect=[]))
        self.context = type('Context', (), {})()
        self.context.matches_body = lambda f, b: (
            f['size'] == b['bytes'] and
            hashlib.sha256(self.code[int(f['start'], 16)]).hexdigest() == b['sha256'])

    def bind(self):
        return bind_spatial_math(self.context, self.proof, self.functions,
                                 lambda a, n: self.code[a][:n])

    def replace(self, address, code, stack):
        self.code[address] = code
        self.functions[address]['size'] = len(code)
        entry = next(e for e in self.proof['spatial_math']['helpers'] if e['address'] == address)
        entry['body'] = dict(bytes=len(code), stack_bytes=stack,
                             sha256=hashlib.sha256(code).hexdigest())

    def test_optional_and_complete_native_closure(self):
        selected = self.bind()
        self.assertEqual(len(selected['roots']), 17)
        header = spatial_math_header(selected, lambda a, n: self.code[a][:n])
        self.assertIn('#define PILOT_AUDIO_SPATIAL_MATH_VERSION 1u', header)
        self.assertFalse(any('OUTPUT_VERSION' in line for line in header))
        self.proof.pop('spatial_math')
        self.assertIsNone(self.bind())

    def test_incremental_proof_preserves_existing_roots_and_data(self):
        new=self.proof['spatial_math'];old=copy.deepcopy(new)
        old['roots'].pop('frequency_pitch');old['defaults'].pop('mix_pairs')
        self.assertTrue(retains_spatial_math(old,new))
        for key,name in (('roots','gain'),('defaults','source')):
            changed=copy.deepcopy(new);changed[key][name]=0
            self.assertFalse(retains_spatial_math(old,changed))
        changed=copy.deepcopy(new);changed['helpers'].pop()
        self.assertFalse(retains_spatial_math(old,changed))

    def test_missing_contract_and_wrong_root_abi(self):
        original = copy.deepcopy(self.proof)
        for mutate in (lambda p: p.pop('listener'), lambda p: p.pop('spatial'),
                       lambda p: p['spatial_math']['roots'].pop('gain'),
                       lambda p: p['spatial_math']['defaults'].pop('source')):
            self.proof = copy.deepcopy(original)
            mutate(self.proof)
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                self.bind()
        self.proof = copy.deepcopy(original)
        self.proof['spatial_math']['roots']['gain'] = self.helper
        with self.assertRaisesRegex(ValueError, 'ABI'):
            self.bind()

    def test_unclosed_and_unreachable_dependencies(self):
        original = copy.deepcopy(self.proof)
        self.proof['spatial_math']['helpers'].pop()
        with self.assertRaisesRegex(ValueError, 'unclosed'):
            self.bind()
        self.proof = copy.deepcopy(original)
        for name, address in self.proof['spatial_math']['roots'].items():
            self.replace(address, b'\xc2' + struct.pack('<H', ROOT_ABIS[name]), ROOT_ABIS[name])
        with self.assertRaisesRegex(ValueError, 'unreachable'):
            self.bind()

    def test_external_machine_effects_and_indirect_calls(self):
        for prefix in (b'\xff\xd0', b'\xe4\x80', b'\xfa', b'\x0f\x31',
                       b'\x64\xa1\x00\x00\x00\x00'):
            self.setUp()
            self.replace(self.helper, prefix + b'\xc3', 0)
            with self.assertRaisesRegex(ValueError, 'indirect|machine effects'):
                self.bind()

    def test_changed_cleanup_alias_and_external_conditional_target(self):
        self.replace(self.helper, b'\xc2\x04\x00', 0)
        with self.assertRaisesRegex(ValueError, 'cleanup'):
            self.bind()
        self.setUp()
        roots = self.proof['spatial_math']['roots']
        roots['gain'] = roots['pitch']
        with self.assertRaisesRegex(ValueError, 'aliased'):
            self.bind()
        self.setUp()
        self.replace(self.helper, b'\x0f\x85' + struct.pack('<i', 0x3500 - self.helper - 6) + b'\xc3', 0)
        with self.assertRaisesRegex(ValueError, 'unclosed'):
            self.bind()

    def test_changed_bytes_and_defaults_fail_closed(self):
        self.code[self.helper] = b'\x90' * len(self.code[self.helper])
        with self.assertRaisesRegex(ValueError, 'changed'):
            self.bind()
        self.setUp()
        address = self.proof['spatial_math']['defaults']['source']['address']
        self.code[address] = b'\xff' + self.code[address][1:]
        with self.assertRaisesRegex(ValueError, 'changed.*defaults'):
            self.bind()

    def test_reviewed_lookup_requires_its_original_initializer(self):
        roots = self.proof['spatial_math']['roots']
        slot, target, site = 0x7000, roots['hrtf_lookup'], roots['gain']
        self.replace(roots['table_initialize'], b'\xc7\x05' + struct.pack('<II', slot, target) + b'\xc3', 0)
        self.replace(site, b'\xff\x15' + struct.pack('<I', slot) + b'\xc2\x04\x00', 4)
        self.proof['spatial_math']['indirect'] = [dict(site=site, slot=slot, target=target)]
        self.assertIsNotNone(self.bind())
        self.replace(roots['table_initialize'], b'\xc3', 0)
        with self.assertRaisesRegex(ValueError, 'initialization'):
            self.bind()


if __name__ == '__main__':
    unittest.main()
