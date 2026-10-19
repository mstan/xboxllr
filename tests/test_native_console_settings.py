import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from create_eeprom import generate_eeprom, _decrypt_security, _xbox_sha1_compute, _rc4_crypt
from native_console_settings import derive_region, prepare

def authored(region, revision='r2'):
    image = bytearray(generate_eeprom(revision, random_bytes=lambda n: bytes((i*17+7)&255 for i in range(n))))
    plain = bytearray(_decrypt_security(image, revision))
    struct.pack_into('<I', plain, 24, region)
    digest = _xbox_sha1_compute(revision, bytes(plain))
    image[:20] = digest
    image[20:48] = _rc4_crypt(_xbox_sha1_compute(revision, digest), bytes(plain))
    return bytes(image)

class ConsoleSettings(unittest.TestCase):
    def test_authenticated_derivation_normalization_and_identity(self):
        for raw in (0, 1, 2, 4, 0x80000000, 0x80000002, 0x40000000):
            with self.subTest(case='authored-domain'), tempfile.TemporaryDirectory() as directory:
                image = authored(raw); before = hashlib.sha256(image).digest()
                expected = raw if raw else 0x80000000
                self.assertEqual(derive_region(image), expected)
                output = Path(directory) / 'console-settings.aot-private.bin'
                receipt = prepare(image, output); artifact = output.read_bytes()
                self.assertEqual(artifact, b'XLLESET1'+before+struct.pack('<I', expected))
                self.assertEqual(len(artifact), 44)
                self.assertEqual(hashlib.sha256(image).digest(), before)
                self.assertTrue(receipt['security_authenticated'])
                self.assertFalse(receipt['payload_values_logged'])
                self.assertNotIn('region', receipt)
                self.assertEqual(json.loads(output.with_suffix('.bin.json').read_text()), receipt)

    def test_existing_auth_revisions_use_same_region_contract(self):
        for revision in ('debug','r1','r2','r3'):
            self.assertEqual(derive_region(authored(2, revision)), 2)

    def test_auth_checksum_size_and_domain_fail_before_output(self):
        good = authored(1)
        bad_auth = bytearray(good); bad_auth[0] ^= 1
        bad_factory = bytearray(good); bad_factory[0x34] ^= 1
        bad_user = bytearray(good); bad_user[0x68] ^= 1
        for image in (bytes(bad_auth),bytes(bad_factory),bytes(bad_user),good[:-1],good+b'\0',authored(3),authored(0x80000003)):
            with self.subTest(case='invalid-authored-input'), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / 'settings.bin'
                with self.assertRaises(ValueError): prepare(image, output)
                self.assertFalse(output.exists())
                self.assertFalse(output.with_suffix('.bin.json').exists())

    def test_existing_output_is_never_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'settings.bin'; output.write_bytes(b'owned-fixture')
            with self.assertRaises(FileExistsError): prepare(authored(1), output)
            self.assertEqual(output.read_bytes(), b'owned-fixture')

if __name__ == '__main__': unittest.main()
