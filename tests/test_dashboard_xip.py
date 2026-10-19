"""Authored archives check overlays without any dashboard files."""
import io
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from dashboard_xip import Xip


def fixture():
    names = b"default.xap\0other.bin\0"
    start = 16 + 2 * 16 + 2 * 4 + len(names)
    return (struct.pack("<IIHHI", 0x30504958, start, 2, 2, 7)
            + struct.pack("<IIII", 0, 3, 0, 123)
            + struct.pack("<IIII", 3, 4, 0, 456)
            + struct.pack("<HHHH", 0, 0, 1, 12)
            + names + b"oldkeep")


class ArchiveTests(unittest.TestCase):
    def test_appended_member_allows_eager_reader_refill(self):
        # An original member normally has later archive data behind it. A
        # replacement at EOF must also allow the retail reader's next refill.
        for length in (1, 65535, 65536, 65537):
            payload = b'x' * length
            output = Xip(fixture()).replace('default.xap', payload)
            archive = Xip(output)
            _, begin, size = archive.entry('default.xap')
            reader = io.BytesIO(output)
            reader.seek(begin)
            self.assertEqual(reader.read(size), payload)
            refill = ((begin + size + 65535) // 65536) * 65536
            reader.seek(refill)
            self.assertEqual(len(reader.read(65536)), 65536)
            self.assertEqual(archive.end, len(output))

    def test_replacement_preserves_other_payloads_and_metadata(self):
        original = fixture()
        source = Xip(original)
        for replacement in (b"", b"a", b"new member" * 91):
            result = source.replace("DEFAULT.XAP", replacement)
            updated = Xip(result)
            self.assertEqual(updated.read("default.xap"), replacement)
            self.assertEqual(updated.read("other.bin"), b"keep")
            self.assertEqual(struct.unpack_from("<II", result, 24), (0, 123))
            allowed = set(range(12, 16)) | set(range(16, 24))
            self.assertTrue(all(a == b for i, (a, b) in enumerate(zip(original, result)) if i not in allowed))
            self.assertEqual(source.read("default.xap"), b"old")

    def test_reject_malformed_directories_and_special_entries(self):
        for offset, value in ((0, 0), (4, 1), (12, 999), (16, 999), (20, 999), (24, 2), (48, 99), (54, 0)):
            data = bytearray(fixture())
            struct.pack_into("<I" if offset < 48 else "<H", data, offset, value)
            with self.assertRaises(ValueError):
                Xip(bytes(data)).read("default.xap")
        with self.assertRaises(ValueError):
            Xip(fixture()[:10])


if __name__ == "__main__":
    unittest.main()
