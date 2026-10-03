import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import aot_compile
import prepare_aot_input
import prepare_aot_xbe
import check_repository_hygiene as hygiene
from test_aot_inventory import xbe


class PrivateInputTests(unittest.TestCase):
    # Independently authored synthetic MOV ESI,123; no captured Xbox bytes.
    code = b"\xbe\x7b\0\0\0"

    def pack(self):
        return b"XLLEBC1\0" + struct.pack("<II", 0x2000, len(self.code)) + self.code

    def test_preparation_generation_and_renamed_publication_reject(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source.blockpack"
            source.write_bytes(self.pack())
            output = root / "region.aot-private.json"
            document = prepare_aot_input.prepare(source, output, [(0x2000, 5)],
                                                  hashlib.sha256(self.pack()).hexdigest())
            self.assertEqual(document["regions"][0]["code"], self.code.hex())
            aot_compile.compile_input(output, root / "program.c", root / "program.h",
                                      root / "manifest.json")
            for file in (output, root / "program.c", root / "program.h", root / "manifest.json"):
                self.assertTrue(list(hygiene.content_findings(file.read_bytes())), file.name)
            manifest = json.loads((root / "manifest.json").read_text())
            self.assertEqual(manifest["input_sha256"], hashlib.sha256(output.read_bytes()).hexdigest())
            self.assertEqual(manifest["regions"][0]["code_sha256"], hashlib.sha256(self.code).hexdigest())
            self.assertEqual(manifest["source_format"], "xboxlle.blockpack.v1")
            self.assertEqual(manifest["source_sha256"], hashlib.sha256(self.pack()).hexdigest())
            self.assertTrue(hygiene.private_path(output.name))

    def test_bad_binary_transport_rejects(self):
        for data in (b"bad", self.pack()[:-1], self.pack() + b"x",
                     self.pack() + self.pack()[8:],
                     b"XLLEBC1\0" + struct.pack("<II", 0xffffffff, 5) + self.code):
            with self.subTest(data=data), self.assertRaises(ValueError):
                prepare_aot_input.read_blockpack(data)

    def test_identity_selection_and_public_filename_reject(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "source.blockpack"
            source.write_bytes(self.pack())
            output = Path(tmp) / "region.aot-private.json"
            for selected, sha, target in (([(0x2000, 5)], "0" * 64, output),
                                          ([(0x2001, 5)], None, output),
                                          ([(0x2000, 5)] * 2, None, output),
                                          ([(0x2000, 5)], None, Path(tmp) / "public.json")):
                with self.subTest(selected=selected, sha=sha, target=target), self.assertRaises(ValueError):
                    prepare_aot_input.prepare(source, target, selected, sha)

    def test_private_generation_requires_source_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            valid = {
                "private_guest_input": True,
                "source_format": "xboxlle.blockpack.v1",
                "source_sha256": "a" * 64,
                "regions": [{"name": "guest_00002000", "base": 0x2000,
                             "entries": [0], "code": self.code.hex()}],
            }
            for mutation, needle in (
                ({"source_format": "other"}, "source_format"),
                ({"source_sha256": "a" * 63}, "64-hex"),
                ({"source_sha256": None}, "64-hex"),
            ):
                bad = dict(valid)
                bad.update(mutation)
                input_path = root / "bad.aot-private.json"
                input_path.write_text(json.dumps(bad), encoding="utf-8")
                with self.subTest(mutation=mutation), self.assertRaisesRegex(aot_compile.AotError, needle):
                    aot_compile.compile_input(input_path, root / "bad.c", root / "bad.h",
                                              root / "bad.manifest.json")

    def test_xbe_explicit_selection_preserves_identity_and_publication_guards(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, output = root / "input.xbe", root / "path.aot-private.json"
            source.write_bytes(xbe(self.code + b"\xc3"))
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            document = prepare_aot_xbe.prepare(source, output, [(0x11000, 6)], digest)
            self.assertEqual(document["regions"][0]["code"], (self.code + b"\xc3").hex())
            aot_compile.compile_input(output, root / "path.c", root / "path.h", root / "manifest.json")
            manifest = json.loads((root / "manifest.json").read_text())
            self.assertEqual(manifest["source_sha256"], digest)
            self.assertEqual(manifest["source_format"], "xboxlle.xbe.v1")
            for name in ("path.aot-private.json", "path.c", "path.h", "manifest.json"):
                self.assertTrue(list(hygiene.content_findings((root / name).read_bytes())), name)
            self.assertNotIn("PRIVATE_CERTIFICATE", output.read_text())

    def test_xbe_rejects_unbacked_overlapping_truncated_or_unidentified_selection(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, output = root / "input.xbe", root / "path.aot-private.json"
            source.write_bytes(xbe(self.code + b"\xc3"))
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            for selected, identity, target in (
                ([(0x11000, 6)], "0" * 64, output),
                ([], digest, output),
                ([(0x11000, 6)] * 2, digest, output),
                ([(0x11000, 6), (0x11005, 1)], digest, output),
                ([(0x11000, 7)], digest, output),
                ([(0x11000, 4)], digest, output),
                ([(0x11000, 0)], digest, output),
                ([(0x11000, 6)], digest, source),
                ([(0x11000, 6)], digest, root / "public.json"),
            ):
                with self.subTest(selected=selected, identity=identity, target=target), self.assertRaises(ValueError):
                    prepare_aot_xbe.prepare(source, target, selected, identity)
                self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
