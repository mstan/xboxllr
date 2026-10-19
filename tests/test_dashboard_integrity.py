"""Authored XBE/XIPS fixtures; no retail executable or archive contents."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from dashboard_integrity import blocks, digest, replace_records, relocate_table, table_records, Xbe


def record(name, payload):
    hashes = blocks(payload)
    return name.encode("utf-16le").ljust(128, b"\0") + struct.pack("<I", len(hashes) // 20) + hashes


def fixture():
    old = {"default.xip": b"old first", "mainmenu5.xip": b"old second"}
    table = b"".join(record(name, payload) for name, payload in old.items())
    table += record("untouched.xip", b"separate") + bytes(132)
    data = bytearray(0x4000)
    data[:4] = b"XBEH"
    struct.pack_into("<III", data, 0x104, 0x10000, 0x1000, 0x2010)
    struct.pack_into("<II", data, 0x11C, 3, 0x10200)
    for i, (name, flags, va, payload) in enumerate((
            (".data", 7, 0x11000, bytes(0x900)),
            ("XIPS", 0xA, 0x11900, table),
            ("next", 1, 0x12000, b"preserve me!!!"))):
        raw, header, name_at = (i + 1) * 0x1000, 0x200 + i * 56, 0x400 + i * 16
        data[name_at:name_at + len(name) + 1] = name.encode() + b"\0"
        struct.pack_into("<9I", data, header, flags, va, len(payload), raw, len(payload),
                         0x10000 + name_at, 0, 0x10600 + i * 2, 0x10602 + i * 2)
        data[header + 36:header + 56] = digest(payload)
        data[raw:raw + len(payload)] = payload
    return bytes(data), table, old


class IntegrityTests(unittest.TestCase):
    def test_length_prefix_and_short_last_block(self):
        payload = b"a" * 65536 + b"tail"
        expected = (hashlib.sha1(b"\0\0\x01\0" + payload[:65536]).digest()
                    + hashlib.sha1(b"\x04\0\0\0tail").digest())
        self.assertEqual(blocks(payload), expected)
        self.assertNotEqual(digest(b"tail"), hashlib.sha1(b"tail").digest())

    def test_replacement_preserves_other_records_and_sentinel(self):
        _, table, originals = fixture()
        changed = dict(originals, **{"default.xip": b"b" * 131073})
        result = replace_records(table, {name: (old, changed[name]) for name, old in originals.items()})
        records = dict(table_records(result))
        self.assertEqual(records["default.xip"][132:], blocks(changed["default.xip"]))
        self.assertEqual(records["mainmenu5.xip"], dict(table_records(table))["mainmenu5.xip"])
        self.assertEqual(records["untouched.xip"], dict(table_records(table))["untouched.xip"])
        self.assertEqual(records[""], bytes(132))
        with self.assertRaises(ValueError):
            replace_records(table, {"default.xip": (b"wrong", b"new")})

    def test_relocation_isolates_pages_and_preserves_every_unrelated_byte(self):
        data, table, old = fixture()
        table = replace_records(table, {"default.xip": (old["default.xip"], b"n" * 200000)})
        updated = relocate_table(data, table, 0xFFC)
        before, after = Xbe(data), Xbe(updated)
        section = after.named("XIPS")
        self.assertEqual((section.va, section.size, section.raw, section.length),
                         (0x13000, 0x1000, 0x4000, len(table)))
        self.assertEqual((section.head, section.tail), (0x10FFC, 0x10FFE))
        self.assertEqual(after.size, 0x4000)
        self.assertEqual(updated[section.raw:], table)
        header = before.named("XIPS").header
        allowed = set(range(0x10C, 0x110)) | set(range(header + 4, header + 20))
        allowed |= set(range(header + 28, header + 56))
        self.assertTrue(all(a == b for i, (a, b) in enumerate(zip(data, updated)) if i not in allowed))
        for name in (".data", "next"):
            self.assertEqual(before.named(name), after.named(name))
        self.assertEqual(updated[0x600:0x608], bytes(8))

    def test_reject_occupied_or_shared_reference_cells(self):
        data, table, _ = fixture()
        for reservation in (0x600, 0x601, 0xFFE, 0x200):
            with self.assertRaises(ValueError):
                relocate_table(data, table, reservation)
        data = bytearray(data)
        data[0xFFF] = 1
        with self.assertRaises(ValueError):
            relocate_table(bytes(data), table, 0xFFC)

    def test_reject_malformed_integrity_and_section_layout(self):
        data, table, _ = fixture()
        for bad in (table[:-1], table + b"\0", table[:-132], table + table[:152]):
            with self.assertRaises(ValueError):
                table_records(bad)
        duplicate = table[:152] * 2 + bytes(132)
        with self.assertRaises(ValueError):
            table_records(duplicate)
        for at, value in ((0x20C, 0xFFFFFFF0), (0x204, 0x11900), (0x228, 0x11FFF)):
            bad = bytearray(data)
            struct.pack_into("<I", bad, at, value)
            with self.assertRaises(ValueError):
                Xbe(bytes(bad))
        bad = bytearray(data)
        bad[0x2000] ^= 1
        with self.assertRaises(ValueError):
            Xbe(bytes(bad))


if __name__ == "__main__":
    unittest.main()
