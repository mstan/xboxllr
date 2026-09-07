#!/usr/bin/env python3
import hashlib
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import create_eeprom


class EepromGenerationTests(unittest.TestCase):
    def deterministic(self, seed=0):
        return bytes((seed + i * 37) & 0xFF for i in range(512))

    def test_sha1_standard_vectors_and_padding_boundaries(self):
        for data in (
            b"",
            b"abc",
            b"a" * 55,
            b"a" * 56,
            b"a" * 57,
            b"a" * 63,
            b"a" * 64,
            b"a" * 65,
            bytes(range(256)) * 5,
        ):
            self.assertEqual(create_eeprom._sha1_digest(data), hashlib.sha1(data).digest())

    def test_rc4_standard_vector(self):
        self.assertEqual(create_eeprom._rc4_crypt(b"Key", b"Plaintext").hex(), "bbf316e8d940af0ad3")

    def test_generate_valid_256_for_all_revisions(self):
        images = []
        for revision in ("debug", "r1", "r2", "r3"):
            image = create_eeprom.generate_eeprom(revision, self.deterministic(3))
            self.assertEqual(len(image), 256)
            result = create_eeprom.validate_eeprom(image, revision)
            self.assertTrue(result.ok, result.reason)
            self.assertEqual(result.revision, revision)
            images.append(image)
        self.assertEqual(len(set(images)), 4)

    def test_default_revision_is_current_tested_r2_profile(self):
        image = create_eeprom.generate_eeprom(random_bytes=self.deterministic(5))
        self.assertTrue(create_eeprom.validate_eeprom(image, "r2").ok)
        self.assertFalse(create_eeprom.validate_eeprom(image, "r1").ok)

    def test_wrong_revision_rejected(self):
        image = create_eeprom.generate_eeprom("r1", self.deterministic(9))
        result = create_eeprom.validate_eeprom(image, "r2")
        self.assertFalse(result.ok)
        self.assertIn("authentication", result.reason)

    def test_security_matches_original_xemu_c_oracle(self):
        # Original xemu 482088490c2d5983ed5e1eafbd09906157c1eca0 C generator,
        # supplied monotonically increasing synthetic RNG bytes (0, 1, ...).
        # Hash only its authenticated/encrypted 48-byte security section; our
        # locally administered MAC intentionally differs outside that section.
        # No console dump or private identity is used as a test fixture.
        expected = {
            "debug": "116b3eec7bcaad4c305de38e7b4f45e89b39ba4c509d4d24d110c61f643ab7dd",
            "r1": "a56a149cf91c926ed93a8fa7119e6f734b42f06d9566297aa25c10c749b8de8c",
            "r2": "70af7f47e4fd468d1aa45d5ccf97076be4c484081d048ce44378cc846fd7189b",
            "r3": "812eaded4aaa05c7ec48191b8c5f8a176082e17e9c99a78aab30bc66a4231f7c",
        }
        for revision, digest in expected.items():
            image = create_eeprom.generate_eeprom(revision, bytes(range(256)))
            self.assertEqual(hashlib.sha256(image[:48]).hexdigest(), digest)

    def test_random_identity_changes(self):
        image_a = create_eeprom.generate_eeprom("r2", self.deterministic(1))
        image_b = create_eeprom.generate_eeprom("r2", self.deterministic(2))
        self.assertNotEqual(image_a, image_b)
        self.assertTrue(create_eeprom.validate_eeprom(image_a, "r2").ok)
        self.assertTrue(create_eeprom.validate_eeprom(image_b, "r2").ok)

    def test_serial_is_decimal_and_mac_has_six_random_bytes_with_local_unicast_bits(self):
        image_a = create_eeprom.generate_eeprom("r2", self.deterministic(11))
        image_b = create_eeprom.generate_eeprom("r2", self.deterministic(12))
        serial = image_a[52:64]
        mac_a = image_a[64:70]
        mac_b = image_b[64:70]
        self.assertTrue(all(ord("0") <= b <= ord("9") for b in serial))
        self.assertEqual(mac_a[0] & 0x03, 0x02)
        self.assertEqual(mac_b[0] & 0x03, 0x02)
        self.assertNotEqual(mac_a, mac_b)
        self.assertNotEqual(mac_a[:3], b"\x02\x00\x00")

    def test_short_or_nonbytes_random_source_rejected_before_malformed_output(self):
        with self.assertRaises(create_eeprom.EepromError):
            create_eeprom.generate_eeprom("r2", lambda n: b"\0" * (n - 1))
        with self.assertRaises(create_eeprom.EepromError):
            create_eeprom.generate_eeprom("r2", lambda n: "not bytes")

    def test_tamper_detection_security_checksum_and_user_sections(self):
        image = bytearray(create_eeprom.generate_eeprom("r2", self.deterministic(13)))
        for offset, expected_reason in ((20, "authentication"), (52, "checksum"), (100, "checksum")):
            tampered = bytearray(image)
            tampered[offset] ^= 0x40
            result = create_eeprom.validate_eeprom(bytes(tampered), "r2")
            self.assertFalse(result.ok)
            self.assertIn(expected_reason, result.reason)

    def test_truncated_and_oversized_data_rejected(self):
        for data in (b"\0" * 255, b"\0" * 257):
            result = create_eeprom.validate_eeprom(data, "r2")
            self.assertFalse(result.ok)
            self.assertIn("256", result.reason)

    def test_read_existing_file_is_bounded_and_regular_only(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            too_large = root / "eeprom.bin"
            too_large.write_bytes(b"\0" * 257)
            with self.assertRaises(create_eeprom.EepromError):
                create_eeprom.read_eeprom_file(too_large)
            with self.assertRaises(create_eeprom.EepromError):
                create_eeprom.read_eeprom_file(root)

            valid = root / "valid.bin"
            image = create_eeprom.generate_eeprom("r2", self.deterministic(15))
            valid.write_bytes(image)
            self.assertEqual(create_eeprom.read_eeprom_file(valid), image)

    def test_existing_valid_file_is_reused_untouched(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "eeprom.bin"
            original = create_eeprom.generate_eeprom("r2", self.deterministic(17))
            path.write_bytes(original)
            create_eeprom.write_new_eeprom(path, "r2")
            self.assertEqual(path.read_bytes(), original)

    def test_existing_wrong_revision_or_corrupt_file_not_overwritten(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "eeprom.bin"
            original = create_eeprom.generate_eeprom("r1", self.deterministic(19))
            path.write_bytes(original)
            with self.assertRaises(create_eeprom.EepromError):
                create_eeprom.write_new_eeprom(path, "r2")
            self.assertEqual(path.read_bytes(), original)

            corrupt = b"not an eeprom"
            path.write_bytes(corrupt)
            with self.assertRaises(create_eeprom.EepromError):
                create_eeprom.write_new_eeprom(path, "r2")
            self.assertEqual(path.read_bytes(), corrupt)

    def test_create_makes_parent_and_validate_only_cli(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "private" / "eeprom.bin"
            self.assertEqual(create_eeprom.main([str(path), "--revision", "r3"]), 0)
            self.assertEqual(len(path.read_bytes()), 256)
            self.assertEqual(create_eeprom.main([str(path), "--revision", "r3", "--validate-only"]), 0)
            self.assertEqual(create_eeprom.main([str(path), "--revision", "r1", "--validate-only"]), 1)

    def test_default_cli_creates_r2_profile(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "eeprom.bin"
            self.assertEqual(create_eeprom.main([str(path)]), 0)
            self.assertTrue(create_eeprom.validate_eeprom(path.read_bytes(), "r2").ok)

    def test_race_file_created_during_exclusive_open_is_not_removed(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "eeprom.bin"
            real_open = os.open

            def race_open(*args, **kwargs):
                path.write_bytes(b"rival")
                return real_open(*args, **kwargs)

            with mock.patch.object(create_eeprom.os, "open", side_effect=race_open):
                with self.assertRaises(FileExistsError):
                    create_eeprom.write_new_eeprom(path, "r2")
            self.assertEqual(path.read_bytes(), b"rival")

    def test_partial_cleanup_does_not_unlink_replacement_file(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "eeprom.bin"
            created_stat = mock.Mock(st_dev=1, st_ino=2, st_size=0)

            class FailingFile:
                def __enter__(self):
                    return self

                def __exit__(self, exc_type, exc, tb):
                    return False

                def write(self, data):
                    path.write_bytes(b"replacement")
                    raise OSError("write failed after replacement")

                def flush(self):
                    pass

                def fileno(self):
                    return 123

            with mock.patch.object(create_eeprom.os, "open", return_value=123), \
                 mock.patch.object(create_eeprom.os, "fstat", return_value=created_stat), \
                 mock.patch.object(create_eeprom.os, "fdopen", return_value=FailingFile()), \
                 mock.patch.object(create_eeprom.os, "close"):
                with self.assertRaises(OSError):
                    create_eeprom.write_new_eeprom(path, "r2")
            self.assertEqual(path.read_bytes(), b"replacement")

    def test_partial_cleanup_removes_own_failed_file(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "eeprom.bin"
            with mock.patch.object(create_eeprom, "generate_eeprom", side_effect=RuntimeError("boom")):
                with self.assertRaises(RuntimeError):
                    create_eeprom.write_new_eeprom(path, "r2")
            self.assertFalse(path.exists())


if __name__ == "__main__":
    unittest.main()
