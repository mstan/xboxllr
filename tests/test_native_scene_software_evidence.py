"""Authored generation/PE/site fault controls; never execute a renderer."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_scene_software_evidence as evidence


def image():
    # One authored AMD64 PE32+ section with three relevant instruction payloads.
    data = bytearray(0x300);data[:2] = b'MZ'
    struct.pack_into('<I',data,0x3c,0x80);data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH',data,0x84,0x8664,1)
    struct.pack_into('<H',data,0x94,0xf0)
    struct.pack_into('<H',data,0x98,0x20b)
    struct.pack_into('<Q',data,0xb0,0x180000000)
    struct.pack_into('<IIII',data,0x190,0x100,0x1000,0x100,0x200)
    data[0x200:0x211] = bytes.fromhex('c5ea595108c4e269994308c4e279994908')
    sites = {'B-rounded':(0x180001000,'c5ea595108'),
             'A-FMA':(0x180001005,'c4e269994308'),
             'C-FMA':(0x18000100b,'c4e279994908')}
    return bytes(data),sites


class SoftwareEvidenceControls(unittest.TestCase):
    def test_retained_and_corrected_sources_match_only_their_exact_dll_generation(self):
        for library,source,name in (
            (evidence.LIBRARY_SHA256,evidence.SOURCE_SHA256,'retained-postclip-colors'),
            (evidence.CORRECTED_LIBRARY_SHA256,evidence.CORRECTED_SOURCE_SHA256,'original-preclip-colors')):
            self.assertEqual(evidence.generation_for(library,source)['name'],name)
            with self.assertRaisesRegex(ValueError,'Pinned source'):
                evidence.generation_for(library,'changed-source')
        for library,source in (
            (evidence.LIBRARY_SHA256,evidence.CORRECTED_SOURCE_SHA256),
            (evidence.CORRECTED_LIBRARY_SHA256,evidence.SOURCE_SHA256)):
            with self.assertRaisesRegex(ValueError,'Pinned source'):
                evidence.generation_for(library,source)

    def test_unknown_dll_cannot_borrow_a_pinned_source(self):
        for source in (evidence.SOURCE_SHA256,evidence.CORRECTED_SOURCE_SHA256):
            with self.assertRaisesRegex(ValueError,'Unknown'):
                evidence.generation_for('another-dll',source)

    def test_both_generations_keep_the_complete_old_geometry_depth_semantics(self):
        self.assertEqual(len(evidence.OLD_CODE_SITES),40)
        self.assertEqual(set(evidence.OLD_CODE_SITES),set(evidence.CORRECTED_CODE_SITES))
        self.assertEqual(evidence.GENERATIONS[evidence.LIBRARY_SHA256]['sites'],evidence.OLD_CODE_SITES)
        self.assertNotIn('original_primary_consumed_gate',evidence.OLD_CODE_SITES)
        self.assertIn('original_primary_consumed_gate',evidence.GENERATIONS[evidence.CORRECTED_LIBRARY_SHA256]['sites'])

    def test_each_authored_depth_instruction_is_checked_at_its_own_virtual_address(self):
        data,sites = image();rows = evidence.verify_code_sites(data,sites)
        self.assertEqual([r['semantic'] for r in rows],['B-rounded','A-FMA','C-FMA'])
        self.assertEqual([r['address'] for r in rows],['0x180001000','0x180001005','0x18000100b'])

    def test_each_rounded_or_fused_depth_instruction_fault_is_rejected(self):
        data,sites = image()
        for offset,name in ((0,'B-rounded'),(5,'A-FMA'),(11,'C-FMA')):
            changed = bytearray(data);changed[0x200+offset] ^= 1
            with self.subTest(name=name),self.assertRaisesRegex(ValueError,name):
                evidence.verify_code_sites(bytes(changed),sites)

    def test_same_instruction_at_another_address_cannot_satisfy_the_proof(self):
        data,sites = image();sites = dict(sites)
        sites['A-FMA'] = (0x18000100b,sites['A-FMA'][1])
        with self.assertRaisesRegex(ValueError,'A-FMA'):
            evidence.verify_code_sites(data,sites)

    def test_site_cannot_read_outside_supplied_pe_storage(self):
        data,_ = image()
        with self.assertRaisesRegex(ValueError,'supplied PE storage'):
            evidence.verify_code_sites(data,{'missing':(0x180002000,'c3')})

    def test_missing_and_incomplete_manifests_fail_closed(self):
        data,_ = image()
        for sites in ({},{'empty':(0x180001000,'')},{'bool-address':(True,'c3')}):
            with self.subTest(sites=sites),self.assertRaisesRegex(ValueError,'Missing|Incomplete'):
                evidence.verify_code_sites(data,sites)

    def test_wrong_architecture_cannot_supply_code_evidence(self):
        data,sites = image();changed = bytearray(data)
        struct.pack_into('<H',changed,0x84,0x14c)
        with self.assertRaisesRegex(ValueError,'AMD64'):
            evidence.verify_code_sites(bytes(changed),sites)


if __name__=='__main__':unittest.main()
