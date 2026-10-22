"""Authored signing-input fixtures; no private kernel, keys or game bodies."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_save_keys as s
from create_eeprom import generate_eeprom


def authored_xbe():
    image = bytearray(0x401)
    image[:4] = b'XBEH'
    struct.pack_into('<III', image, 0x104, 0x10000, 0x400, 0x2000)
    struct.pack_into('<III', image, 0x118, 0x10200, 1, 0x10180)
    struct.pack_into('<I', image, 0x128, 0x11000 ^ 0xa8fc57ab)
    struct.pack_into('<IIIIII', image, 0x180, 6, 0x11000, 0x100, 0x400, 1, 0x101c0)
    image[0x1c0:0x1c6] = b'.text\0'
    struct.pack_into('<I', image, 0x200, 0x1d0)
    struct.pack_into('<I', image, 0x208, 0xf00d0001)
    for i in range(272):
        image[0x2c0 + i] = i & 255
    image[0x400] = 0xc3
    return bytes(image)


class NativeSigningTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        # Purely authored fingerprint-bound kernel-shaped data. It has no
        # executable implementation and must never reach the original oracle.
        self.kernel = bytes(s.CERTIFICATE_KEY_OFFSET) + bytes(range(1, 17))
        fingerprint = patch.object(s, 'KERNEL_SHA', hashlib.sha256(self.kernel).hexdigest())
        fingerprint.start(); self.addCleanup(fingerprint.stop)
        self.xbe = authored_xbe()
        self.eeprom = generate_eeprom(random_bytes=bytes(range(58)))
        self.output = self.root / 'keys.aot-private.bin'

    def test_normal_preparation_needs_no_interpreter_and_preserves_wire_identity(self):
        with patch.dict(sys.modules, {'unicorn': None, 'unicorn.x86_const': None}):
            receipt = s.prepare(self.xbe, self.kernel, self.eeprom, self.output)
        data = self.output.read_bytes()
        self.assertEqual(len(data), 360)
        self.assertEqual(data[:8], b'XLLEKEY1')
        self.assertEqual(data[8:40], hashlib.sha256(self.xbe).digest())
        self.assertEqual(data[40:72], hashlib.sha256(self.eeprom).digest())
        self.assertFalse(receipt['original_execution'])
        self.assertEqual(receipt['per_title_key_count'], 17)
        self.assertEqual(receipt['original_title_key_matches'], 0)
        self.assertFalse(receipt['key_values_logged'])
        self.assertEqual(json.loads(self.output.with_suffix('.bin.json').read_text()), receipt)

    def test_optional_verification_has_an_explicit_dependency_and_no_partial_output(self):
        with patch.dict(sys.modules, {'unicorn': None, 'unicorn.x86_const': None}):
            with self.assertRaisesRegex(ValueError, 'optional Unicorn'):
                s.prepare(self.xbe, self.kernel, self.eeprom, self.output, verify_original=True)
        self.assertFalse(self.output.exists())

    def test_kernel_fingerprint_is_required_before_any_write(self):
        with self.assertRaisesRegex(ValueError, 'Unreviewed original kernel'):
            s.prepare(self.xbe, self.kernel + b'changed', self.eeprom, self.output)
        self.assertFalse(self.output.exists())

    def test_zero_or_missing_root_is_not_a_signing_fallback(self):
        for kernel in [bytes(s.CERTIFICATE_KEY_OFFSET), bytes(s.CERTIFICATE_KEY_OFFSET + 16)]:
            with self.subTest(size=len(kernel)), patch.object(s, 'KERNEL_SHA', hashlib.sha256(kernel).hexdigest()):
                with self.assertRaisesRegex(ValueError, 'root is unavailable'):
                    s.prepare(self.xbe, kernel, self.eeprom, self.output)
        self.assertFalse(self.output.exists())

    def test_incomplete_and_out_of_header_signature_tables_are_rejected(self):
        for size in [0x1c0, 0x210]:
            image = bytearray(self.xbe)
            struct.pack_into('<I', image, 0x200, size)
            with self.subTest(size=size), self.assertRaisesRegex(ValueError, 'Signing certificate'):
                s.prepare(bytes(image), self.kernel, self.eeprom, self.output)
        self.assertFalse(self.output.exists())

    def test_corrupt_eeprom_is_not_replaced_or_accepted(self):
        corrupted = bytearray(self.eeprom); corrupted[60] ^= 1
        with self.assertRaisesRegex(ValueError, 'Signing EEPROM rejected'):
            s.prepare(self.xbe, self.kernel, bytes(corrupted), self.output)
        self.assertFalse(self.output.exists())

    def test_existing_signing_profile_is_never_overwritten(self):
        self.output.write_bytes(b'Preserve existing identity')
        with self.assertRaisesRegex(ValueError, 'preserve the existing profile'):
            s.prepare(self.xbe, self.kernel, self.eeprom, self.output)
        self.assertEqual(self.output.read_bytes(), b'Preserve existing identity')

    def test_existing_receipt_is_not_overwritten_by_a_new_keyfile(self):
        receipt = self.output.with_suffix('.bin.json')
        receipt.write_text('Preserve existing receipt')
        with self.assertRaisesRegex(ValueError, 'preserve the existing profile'):
            s.prepare(self.xbe, self.kernel, self.eeprom, self.output)
        self.assertFalse(self.output.exists())
        self.assertEqual(receipt.read_text(), 'Preserve existing receipt')


if __name__ == '__main__':
    unittest.main()
