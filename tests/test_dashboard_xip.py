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
            # The later ordinary file's offset moves, while its content,
            # type/timestamp, name table and all other directory fields stay.
            self.assertEqual(updated.entry('other.bin')[1], updated.start + len(replacement))
            self.assertEqual(struct.unpack_from('<II', result, 40), (0, 456))
            self.assertEqual(result[48:source.start], original[48:source.start])
            self.assertEqual(source.read("default.xap"), b"old")

    def test_retail_sequential_reader_and_packed_mesh_references(self):
        # The initial retail loader ignores ordinary offsets. A parser must
        # see the NEW script at the current cursor, followed by the unchanged
        # texture/IB/VB, without consuming metadata-only mesh-reference words.
        names = b'default.xap\0texture.xbx\0'
        start = 16 + 5 * 16 + 2 * 4 + len(names)
        entries = ((0, 3, 0, 11), (0x02000123, 9, 4, 22),
                   (3, 7, 2, 33), (10, 4, 5, 44), (14, 6, 6, 55))
        original = (struct.pack('<IIHHI', 0x30504958, start, 5, 2, 20)
                    + b''.join(struct.pack('<IIII', *e) for e in entries)
                    + struct.pack('<HHHH', 0, 0, 2, 12) + names + b'oldXPR0texIB!!VBdata')
        output = Xip(original).replace('default.xap', b'new script body\n')
        cursor = start
        observed = []
        for i in range(5):
            offset, size, kind, _ = struct.unpack_from('<IIII', output, 16 + 16 * i)
            if kind == 4:
                self.assertEqual(output[32:48], original[32:48])
                continue
            self.assertEqual(offset, cursor - start)
            observed.append(output[cursor:cursor + size])
            cursor += size
        self.assertEqual(observed, [b'new script body\n', b'XPR0tex', b'IB!!', b'VBdata'])
        # Repeated replacement also preserves the reader contract and keeps
        # only padding outside material payloads.
        again = Xip(output).replace('default.xap', b'short')
        self.assertEqual(Xip(again).read('default.xap'), b'short')
        bad = bytearray(original)
        struct.pack_into('<I', bad, 48, 4)  # an ordinary payload gap
        with self.assertRaises(ValueError):
            Xip(bytes(bad)).replace('default.xap', b'new')

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
