#!/usr/bin/env python3
"""Authored XBE fixtures: no game bytes or certificate keys."""
import json
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import aot_inventory as inventory


def xbe(code, entry=0x11000):
    data = bytearray(0x400 + len(code))
    data[:4] = b"XBEH"
    def put(offset, *values):
        struct.pack_into("<" + "I" * len(values), data, offset, *values)
    put(0x104, 0x10000, 0x400, 0x2000, 0x178)
    put(0x118, 0x10200, 1, 0x10300)
    put(0x128, entry ^ 0xA8FC57AB)
    put(0x200, 0xa4)
    put(0x208, 0x12345678)
    put(0x29c, 2, 1)  # Authored DVD-X2-only, region 1 launch constraints.
    put(0x300, 6, 0x11000, 0x1000, 0x400, len(code), 0x10380)
    data[0x380:0x386] = b".text\0"
    data[0x260:0x280] = b"PRIVATE_CERTIFICATE_SENTINEL".ljust(32, b"!")
    data[0x400:] = code
    return data


class InventoryTests(unittest.TestCase):
    def test_image_identity_has_no_certificate_or_code_payload(self):
        data = xbe(bytes.fromhex("90c3"))
        image = inventory.parse_xbe(data)
        self.assertEqual(image["entry"], 0x11000)
        self.assertEqual(image["title_id"], 0x12345678)
        self.assertEqual(image["title_name"], '')
        self.assertEqual(image["allowed_media"], 2)
        self.assertEqual(image["region"], 1)
        self.assertEqual(image["sections"][0]["raw_sha256"], inventory.sha(b"\x90\xc3"))
        text = json.dumps(image)
        self.assertNotIn("PRIVATE_CERTIFICATE", text)
        self.assertNotIn("90c3", text)

    def test_discovery_follows_call_and_continuation_but_not_data(self):
        # call callee; ret; unreachable UD2; callee: inc eax; ret
        data = xbe(bytes.fromhex("e803000000c30f0b40c3"))
        result = inventory.discover(data, inventory.parse_xbe(data))
        self.assertEqual([i["address"] for i in result["instructions"]],
                         [0x11000, 0x11005, 0x11008, 0x11009])
        self.assertEqual(result["summary"]["recovered_bytes"], 8)
        self.assertFalse(result["summary"]["whole_program_complete"])
        self.assertEqual(result["summary"]["unclassified_bytes"], 0x1000 - 8)
        self.assertTrue(any(e["kind"] == "dynamic-return" for e in result["edges"]))

    def test_indirect_jmp_is_a_gap_and_never_fabricates_fallthrough(self):
        data = xbe(bytes.fromhex("ffe090c3"))
        result = inventory.discover(data, inventory.parse_xbe(data))
        self.assertEqual(len(result["instructions"]), 1)
        self.assertEqual(result["gaps"], [dict(address=0x11000, reason="unresolved-indirect-branch")])

    def test_indirect_call_keeps_continuation_but_reports_missing_target(self):
        data = xbe(bytes.fromhex("ffd090c3"))
        result = inventory.discover(data, inventory.parse_xbe(data))
        self.assertEqual(len(result["instructions"]), 3)
        self.assertEqual(result["gaps"][0]["reason"], "unresolved-indirect-call")

    def test_discovery_continues_past_unsupported_lowering_without_claiming_support(self):
        data = xbe(bytes.fromhex("f6d840c3"))  # NEG AL remains unsupported.
        result = inventory.discover(data, inventory.parse_xbe(data))
        self.assertEqual([i["lowering_supported"] for i in result["instructions"]], [False, True, True])

    def test_conflicting_entry_is_reported_and_not_double_counted(self):
        data = xbe(bytes.fromhex("b801000000c3"))
        result = inventory.discover(data, inventory.parse_xbe(data), [0x11001])
        self.assertEqual(result["gaps"][0]["reason"], "overlapping-instruction-entry")
        self.assertEqual(result["summary"]["recovered_bytes"], 6)

    def test_resource_limit_keeps_pending_work_visible(self):
        data = xbe(bytes.fromhex("9090c3"))
        result = inventory.discover(data, inventory.parse_xbe(data), limit=1)
        self.assertTrue(result["summary"]["limit_reached"])
        self.assertEqual(result["pending_addresses"], [0x11001])

    def test_bad_extents_entries_and_names_rejected(self):
        for offset, value in ((0x108, 2), (0x120, 0x100), (0x310, 0xffff),
                              (0x314, 0x99999999), (0x118, 0xffffffff)):
            with self.subTest(offset=offset):
                data = xbe(b"\xc3")
                struct.pack_into("<I", data, offset, value)
                with self.assertRaises(ValueError): inventory.parse_xbe(data)
        with self.assertRaises(ValueError): inventory.parse_xbe(xbe(b"\xc3", entry=0x50000))
        with self.assertRaises(ValueError): inventory.parse_xbe(b"XBEH")

    def test_zero_fill_and_non_executable_seeds_are_not_invented_code(self):
        data = xbe(b"\xc3")
        image = inventory.parse_xbe(data)
        with self.assertRaises(ValueError): inventory.discover(data, image, [0x11002])
        with self.assertRaises(ValueError): inventory.discover(data, image, limit=0)


if __name__ == "__main__":
    unittest.main()
