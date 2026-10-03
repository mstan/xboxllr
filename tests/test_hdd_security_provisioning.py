#!/usr/bin/env python3
"""Synthetic credentials only; never reads the owner's console inputs."""
import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import create_eeprom
import provision_hdd_security as provision

EXECUTABLE = Path(sys.argv.pop()).resolve() if len(sys.argv) > 1 else None


class ProvisioningTests(unittest.TestCase):
    def eeprom(self, revision="r1"):
        return create_eeprom.generate_eeprom(revision, bytes(range(256)))

    def test_independent_password_vector_all_eeprom_revisions(self):
        # Independently calculated with .NET HMACSHA1, key=08..17, message=
        # ASCII ST310211AXLLE000000000000000; ATA password zero-padded to 32.
        expected = bytes.fromhex("9d715e0ac083f1445acbc4cb480d1c3fbf95d417") + bytes(12)
        for revision in ("debug", "r1", "r2", "r3"):
            self.assertEqual(provision.derive_user_password(self.eeprom(revision)), expected)

    def test_corrupt_authentication_is_rejected(self):
        image = bytearray(self.eeprom()); image[20] ^= 1
        with self.assertRaisesRegex(ValueError, "authentication"):
            provision.derive_user_password(bytes(image))

    def test_private_file_no_overwrite_or_secret_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); eeprom = root / "eeprom.bin"; output = root / "drive.key"
            image = self.eeprom(); eeprom.write_bytes(image)
            with mock.patch.object(provision.secrets, "token_bytes", return_value=b"M" * 32):
                captured = io.StringIO()
                with contextlib.redirect_stdout(captured):
                    provision.provision(eeprom, output)
            expected = provision.derive_user_password(image) + b"M" * 32
            self.assertEqual(output.read_bytes(), expected)
            self.assertEqual(captured.getvalue(), "")
            with self.assertRaises(FileExistsError):
                provision.provision(eeprom, output)
            self.assertEqual(output.read_bytes(), expected)
            self.assertEqual(eeprom.read_bytes(), image)
            with self.assertRaisesRegex(ValueError, ".key"):
                provision.provision(eeprom, root / "public.txt")

    def test_invalid_input_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); eeprom = root / "bad.bin"; output = root / "drive.key"
            for size in (0, 255, 256, 257):
                eeprom.write_bytes(bytes(size))
                with self.assertRaises(ValueError):
                    provision.provision(eeprom, output)
                self.assertFalse(output.exists())

    @unittest.skipIf(EXECUTABLE is None, "supply xlle executable for CLI validation")
    def test_cli_rejects_ambiguous_or_missing_policy(self):
        cases = [
            (["--hdd-security", "anything"], "expects locked or unlocked"),
            (["--hdd-security", "unlocked"], "require --hdd"),
            (["--hdd-security-file", "missing.key"], "require --hdd"),
            (["--hdd", "missing.img"], "locked mode requires"),
            (["--hdd", "missing.img", "--hdd-security", "unlocked",
              "--hdd-security-file", "missing.key"], "cannot also provision"),
        ]
        for args, message in cases:
            result = subprocess.run([str(EXECUTABLE), *args], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn(message, result.stderr)

    @unittest.skipIf(EXECUTABLE is None, "supply xlle executable for CLI validation")
    def test_cli_requires_exact_credential_file_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            private = Path(tmp) / "drive.key"
            for size in (0, 32, 63, 65, 65536):
                private.write_bytes(bytes(size))
                result = subprocess.run([str(EXECUTABLE), "--hdd", "missing.img",
                                         "--hdd-security-file", str(private)],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 1)
                self.assertIn("exactly 64 bytes", result.stderr)


if __name__ == "__main__":
    unittest.main()
