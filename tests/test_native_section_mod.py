"""Authored sections prove private registry generation without retail payloads."""
import hashlib
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_section_mod import section_registry
from test_dashboard_integrity import fixture


class SectionModTests(unittest.TestCase):
    def test_registry_preserves_identity_and_exact_original_descriptor(self):
        original,_,_=fixture()
        source,receipt=section_registry(original,[('XIPS',b'new private data'*400,0xffc)],'authored.credits')
        self.assertEqual(receipt['source_xbe_sha256'],hashlib.sha256(original).hexdigest())
        record=receipt['sections'][0]
        self.assertEqual((record['index'],record['address'],record['virtual_bytes']), (1,0x13000,0x2000))
        self.assertEqual(bytes.fromhex(record['original_descriptor']),original[0x238:0x270])
        self.assertEqual(receipt['registry_sha256'],hashlib.sha256(source.encode()).hexdigest())
        self.assertIn('pilot_title_section_mods',source)
        self.assertNotIn('pilot_title_mod_open',source)

    def test_reject_code_duplicate_counter_reuse_and_owned_counter_cells(self):
        original,_,_=fixture()
        for replacements in ([('.data',b'x',0xffc)], [('XIPS',b'x',0x602)],
                             [('XIPS',b'x',0xffc),('XIPS',b'y',0xff8)],
                             [('XIPS',b'x',0xffc),('next',b'y',0xffe)]):
            with self.subTest(replacements=replacements),self.assertRaises(ValueError):
                section_registry(original,replacements,'authored.mod')

    def test_missing_identity_or_payload_and_changed_source_fail_closed(self):
        original,_,_=fixture()
        for data,sections,mod_id in ((original,[('XIPS',b'',0xffc)],'a'),
                                     (original,[],'a'),(original,[('XIPS',b'x',0xffc)],'../bad'),
                                     (original[:0x3004], [('XIPS',b'x',0xffc)],'a')):
            with self.subTest(mod_id=mod_id,sections=sections),self.assertRaises(ValueError):
                section_registry(data,sections,mod_id)


if __name__=='__main__':unittest.main()
