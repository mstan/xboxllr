"""Loader zero-fill, logical bounds and cleanup inference share the same bytes."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_image_reads import section_window,install_discovery,install_lifting
from native_symbol_discovery import QualifiedFunctions
from native_guest_symbols import SCHEMA,import_dia
from test_aot_inventory import xbe
import test_native_discovery as discovery_fixture
import test_native_guest_symbols as symbol_fixture


class LoadedImageReads(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        discovery_fixture.NativeDiscovery.setUpClass.__func__(cls)
        from tools.disasm import loader
        from tools.recomp import translator
        from tools.recomp import lifter
        cls.loader,cls.translator=loader,translator
        cls.lifter,cls.old_callee_pop=lifter,lifter.Lifter.__dict__['callee_pop']
        cls.original_methods=(translator.FunctionTranslator._read_func_bytes,translator.FunctionTranslator._stub_ret_bytes)
        cls.original_flag=getattr(translator.FunctionTranslator,'_xboxllr_loaded_image_reads',False)
        install_discovery(loader)
        install_lifting(translator)

    @classmethod
    def tearDownClass(cls):
        cls.translator.FunctionTranslator._read_func_bytes,cls.translator.FunctionTranslator._stub_ret_bytes=cls.original_methods
        cls.lifter.Lifter.callee_pop=cls.__dict__['old_callee_pop']
        if not cls.original_flag:
            del cls.translator.FunctionTranslator._xboxllr_loaded_image_reads

    def authored(self, raw=b'\xc2\x04', virtual=3, padding=b'\x7f\xc3'):
        blob=bytearray(xbe(raw))
        struct.pack_into('<I',blob,0x308,virtual)
        blob.extend(padding)
        section=self.section_type('.text',0x11000,virtual,0x400,len(raw),False,True,'')
        return bytes(blob),section

    def test_ret_operand_comes_from_zero_fill_not_file_padding(self):
        blob,section=self.authored()
        image=self.image_type('authored',blob,0x10000,0x2000,0x11000,0,[section])
        self.assertEqual(image.get_section_data(section),b'\xc2\x04\0')
        self.assertEqual(image.read_bytes_at_va(0x11000,100),b'\xc2\x04\0')
        engine=self.engine_type(image);engine.linear_sweep(section)
        self.assertTrue(engine.probes_as_function_body(0x11000))
        self.assertEqual(engine.block_extent_end(0x11000),0x11003)
        self.assertEqual(engine.instructions[0x11000].op_str,'4')
        translator=self.translator.FunctionTranslator(blob,{})
        self.assertEqual(translator._read_func_bytes(0x11000,0x11003),b'\xc2\x04\0')
        self.assertEqual(translator._stub_ret_bytes(0x11000),4)
        translator.lifter.func_db[0x11000]={'end':0x11003}
        self.assertEqual(translator.lifter.callee_pop(0x11000),4)

    def test_raw_file_terminator_beyond_virtual_end_is_not_code(self):
        blob,section=self.authored(raw=b'\x31\xc0',virtual=2,padding=b'\xc3\xcc')
        image=self.image_type('authored',blob,0x10000,0x2000,0x11000,0,[section])
        engine=self.engine_type(image);engine.linear_sweep(section)
        self.assertFalse(engine.probes_as_function_body(0x11000))
        self.assertIsNone(engine.block_extent_end(0x11000))
        self.assertIsNone(image.read_u32_at_va(0x11000))
        translator=self.translator.FunctionTranslator(blob,{})
        self.assertIsNone(translator._read_func_bytes(0x11000,0x11003))
        self.assertEqual(translator._stub_ret_bytes(0x11000),0)

    def test_pdb_bounds_allow_bounded_zero_tail_but_not_zero_fill_entries(self):
        blob,section=self.authored()
        doc=dict(schema=SCHEMA,xbe_sha256=hashlib.sha256(blob).hexdigest(),source_sha256='a'*64,
                 identity='matching-codeview',functions=[dict(address=0x11000,size=3,names=['Authored'],extent_kind='pdb-length')])
        self.assertEqual(QualifiedFunctions(doc,blob).ends,{0x11000:0x11003})
        doc['functions'][0].update(address=0x11002,size=1)
        with self.assertRaises(ValueError):QualifiedFunctions(doc,blob)

    def test_dia_import_keeps_real_function_operand_in_zero_tail(self):
        blob=bytearray(symbol_fixture.image())
        raw_size=len(blob)-0x400
        blob.extend(b'\x7f\xc3')
        records=symbol_fixture.metadata()
        records[1].update(offset=raw_size-1,length=2)
        symbols,_=import_dia(bytes(blob),records,'0'*64)
        self.assertEqual(symbols['functions'][0]['size'],2)
        records[1]['offset']=raw_size
        symbols,_=import_dia(bytes(blob),records,'0'*64)
        self.assertFalse(symbols['functions'])

    def test_bss_sweep_is_bounded_and_reads_stop_at_logical_gaps(self):
        blob,section=self.authored(raw=b'\xc3',virtual=0x1000000)
        image=self.image_type('authored',blob,0x10000,0x2000000,0x11000,0,[section])
        self.assertEqual(len(image.get_section_data(section)),16)
        self.assertEqual(image.read_bytes_at_va(0x12000,4),bytes(4))
        self.assertIsNone(image.read_bytes_at_va(0x1011000,4))
        self.assertIsNone(section_window(blob,(0x11000,3,0x400,10),0x11000,3))


if __name__=='__main__':
    unittest.main()
