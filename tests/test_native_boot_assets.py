"""Startup admission safety; original firmware stays outside the test tree."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "native_boot_assets", Path(__file__).resolve().parents[1] / "tools/native_boot_assets.py")
assets = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(assets)


class BootAssetsTests(unittest.TestCase):
    def test_short_signing_dump_and_unknown_kernel_rejected(self):
        for data in (b"", bytes(458752), bytes(assets.KERNEL_BYTES), b"MZ" + bytes(32)):
            with self.subTest(size=len(data)), self.assertRaises(assets.AssetError):
                assets.extract(data)

    def test_read_bound_is_enforced_before_admission(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "oversized.input"
            with path.open("wb") as stream:
                stream.truncate(assets.MAX_INPUT_BYTES + 1)
            with self.assertRaisesRegex(assets.AssetError, "size limit"):
                assets.read_kernel(path)

    def test_invalid_input_creates_no_destination(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root / "input.kernel"
            source.write_bytes(b"invalid")
            with self.assertRaises(assets.AssetError):
                assets.write_pack(source, root / "output")
            self.assertFalse((root / "output").exists())
            self.assertEqual(source.read_bytes(), b"invalid")

    def test_sequence_relocations_keep_scalar_words_and_omit_dead_dispatch(self):
        sequence = bytearray(0x6F8)
        sequence[:4] = struct.pack("<HH", 30, 16)
        sequence[4:0x210] = b"\xA5" * 0x20C
        struct.pack_into("<I", sequence, 0x210, 0x800A8598)
        struct.pack_into("<I", sequence, 0x214, 256)
        struct.pack_into("<I", sequence, 0x21C, 0x800BC3A0)
        struct.pack_into("<I", sequence, 0x4A2, 0x80083608)
        result = assets._relocate_sequence(bytes(sequence))
        self.assertEqual(result[:4], sequence[:4])
        self.assertEqual(result[4:0x210], bytes(0x20C))
        self.assertEqual(struct.unpack_from("<I", result, 0x210)[0], 0x80093B78)
        self.assertEqual(struct.unpack_from("<I", result, 0x214)[0], 256)
        self.assertEqual(struct.unpack_from("<I", result, 0x21C)[0], 0x800A7980)
        self.assertEqual(struct.unpack_from("<I", result, 0x4A2)[0], 0x8006EBF8)

    def test_pointer_table_rejects_addresses_outside_sequence(self):
        for value in (0, 0x80083317, 0x80083A10, 0xFFFFFFFF):
            with self.subTest(value=value), self.assertRaises(assets.AssetError):
                assets._relocate_pointers(struct.pack("<25I", *([value] * 24 + [2])))

    def test_pointer_table_relocates_all_24_pointers_and_preserves_event_max(self):
        source = struct.pack("<25I", *([0x80083608] * 24 + [2]))
        words = struct.unpack("<25I", assets._relocate_pointers(source))
        self.assertEqual(words, tuple([0x8006EBF8] * 24 + [2]))

    def test_inventory_has_every_audio_and_geometry_range_and_no_embedded_assets(self):
        self.assertEqual(len(assets.RANGES), 11)
        for name, logical, owned, size, fingerprint in assets.RANGES:
            with self.subTest(name=name):
                self.assertLessEqual(len(name), 16)
                self.assertEqual(len(fingerprint), 64)
                self.assertGreater(size, 0)
                self.assertGreaterEqual(owned, assets.BASE)
                self.assertLessEqual(owned - assets.BASE + size, assets.KERNEL_BYTES)
        self.assertEqual(sum(r[3] for r in assets.RANGES), 71779)

    @unittest.skipUnless(os.environ.get("XBOXLLR_BOOT_TEST_KERNEL"), "requires private owned kernel")
    def test_owned_extraction_is_exact_and_does_not_overwrite(self):
        source = Path(os.environ["XBOXLLR_BOOT_TEST_KERNEL"])
        original = assets.sha256(assets.read_kernel(source))
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "assets"
            manifest = assets.write_pack(source, target)
            pack = (target / "xboot.pak").read_bytes()
            self.assertEqual(len(pack), 72224)
            self.assertEqual(assets.sha256(pack),
                             "6c6df777c291a447a2de2693e3edfc4a69e0c4151548c8e058b512669a09233f")
            self.assertEqual(manifest["pack_sha256"], assets.sha256(pack))
            self.assertFalse(manifest["firmware_execution"])
            with self.assertRaises(FileExistsError):
                assets.write_pack(source, target)
            self.assertEqual(assets.sha256(assets.read_kernel(source)), original)

    @unittest.skipUnless(os.environ.get("XBOXLLR_BOOT_TEST_EXE") and
                         os.environ.get("XBOXLLR_BOOT_TEST_PACK"), "requires native host and private pack")
    def test_native_completion_skip_and_damaged_pack_admission(self):
        exe = os.environ["XBOXLLR_BOOT_TEST_EXE"]
        pack = Path(os.environ["XBOXLLR_BOOT_TEST_PACK"])
        base = [exe, "--headless", "--mute", "--core-only", "--pack", str(pack)]
        complete = subprocess.run(base, text=True, capture_output=True, timeout=15)
        self.assertEqual(complete.returncode, 0, complete.stderr)
        report = json.loads(complete.stdout)
        self.assertEqual(report["status"], "completed")
        self.assertEqual(report["frames"], 480)
        self.assertEqual(report["audio_frames"], 384000)
        self.assertGreater(report["audio_peak"], 0)
        self.assertEqual(report["presentation"], "core-only")
        skip = subprocess.run(base + ["--skip-at-ms", "1000"], text=True,
                              capture_output=True, timeout=15)
        self.assertEqual(skip.returncode, 0, skip.stderr)
        self.assertEqual(json.loads(skip.stdout)["status"], "skipped")
        self.assertLess(json.loads(skip.stdout)["time"], 1)
        unmuted = subprocess.run([exe, "--headless", "--pack", str(pack)],
                                 text=True, capture_output=True, timeout=15)
        self.assertEqual(unmuted.returncode, 2)
        self.assertIn("require --mute", unmuted.stderr)
        with tempfile.TemporaryDirectory() as folder:
            bad = Path(folder) / "damaged.pak"
            data = bytearray(pack.read_bytes())
            data[-1] ^= 1
            bad.write_bytes(data)
            reject = subprocess.run([exe, "--headless", "--mute", "--pack", str(bad)],
                                    text=True, capture_output=True, timeout=15)
            self.assertEqual(reject.returncode, 2)
            self.assertIn("damaged startup pack", reject.stderr)
            self.assertNotIn("completed", reject.stdout)


if __name__ == "__main__":
    unittest.main()
