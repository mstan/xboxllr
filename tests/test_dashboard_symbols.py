"""Authored XBE metadata and constructor programs; no retail code."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from dashboard_symbols import Image, recover


def image(constructor=None):
    data = bytearray(0x600)
    data[:4] = b"XBEH"
    struct.pack_into("<II", data, 0x104, 0x10000, 0x200)
    struct.pack_into("<II", data, 0x11C, 2, 0x10140)
    struct.pack_into("<IIIII", data, 0x140, 0x16, 0x11000, 0x100, 0x400, 0x100)
    # Xbox data sections can also carry the executable flag. They must not be
    # mistaken for immutable code when reconstructing descriptor storage.
    struct.pack_into("<IIIII", data, 0x178, 7, 0x12000, 0x100, 0x500, 0x100)
    data[0x400:0x500] = b"\x90" * 0x100
    data[0x410] = 0xC3
    name = "SetValue\0".encode("utf-16le")
    data[0x580:0x580 + len(name)] = name
    if constructor is None:
        struct.pack_into("<IIIIII", data, 0x500, 0x11000, 0, 0, 0, 1, 0x12080)
    else:
        data[0x420:0x420 + len(constructor)] = constructor
    return Image(bytes(data))


def constructor(prefix=b""):
    return (b"\xb8" + struct.pack("<I", 0x11000) + prefix
            + b"\xa3" + struct.pack("<I", 0x12000)
            + b"\x66\xc7\x05" + struct.pack("<IH", 0x12010, 1)
            + b"\xc7\x05" + struct.pack("<II", 0x12014, 0x12080) + b"\xc3")


class SymbolTests(unittest.TestCase):
    def test_static_descriptor_and_register_constructor(self):
        for authored in (image(), image(constructor())):
            symbols = recover(authored)
            self.assertEqual(len(symbols), 1)
            self.assertEqual(symbols[0]["address"], "0x00011000")
            self.assertEqual(symbols[0]["methods"][0]["name"], "SetValue")
            self.assertEqual(symbols[0]["methods"][0]["signature"], 1)

    def test_calls_and_branches_do_not_supply_assumed_constants(self):
        for barrier in (b"\xe8\x00\x00\x00\x00", b"\xeb\x00"):
            self.assertEqual(recover(image(constructor(barrier))), [])

    def test_conflicting_stores_do_not_become_names(self):
        program = constructor()[:-1] + b"\xc7\x05" + struct.pack("<II", 0x12000, 0x11010) + b"\xc3"
        self.assertEqual(recover(image(program)), [])

    def test_reject_bad_section_extent(self):
        data = bytearray(image().data)
        struct.pack_into("<I", data, 0x150, 0xFFFFFFFF)
        with self.assertRaises(ValueError):
            Image(bytes(data))


if __name__ == "__main__":
    unittest.main()
