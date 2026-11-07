"""Authored body/alias shapes; no original game code or symbols."""
import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from native_symbol_discovery import QualifiedFunctions, bind, install
from native_guest_symbols import SCHEMA
from test_aot_inventory import xbe
import test_native_discovery as discovery_fixture
import test_native_guest_symbols as symbol_fixture
from native_guest_symbols import import_dia


class QualifiedDiscovery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        discovery_fixture.NativeDiscovery.setUpClass.__func__(cls)

    def fixture(self, code, *, length=None):
        blob = bytes(xbe(code + b'\xcc'*32))
        document = dict(schema=SCHEMA, xbe_sha256=hashlib.sha256(blob).hexdigest(),
                        source_sha256='a'*64, identity='matching-codeview',
                        functions=[dict(address=0x11000, size=length or len(code),
                                        names=['Authored'], extent_kind='pdb-length')])
        return blob, document

    def detector(self, blob, document):
        qualified = QualifiedFunctions(document, blob)
        module = SimpleNamespace(FunctionDetector=type('QualifiedDetector', (self.module.FunctionDetector,), {}))
        install(module, qualified)
        text = self.section_type('.text', 0x11000, len(blob)-0x400,
                                 0x400, len(blob)-0x400, False, True, '')
        image = self.image_type('authored', blob, 0x10000, 0x2000, 0x11000, 0, [text])
        engine = self.engine_type(image)
        engine.linear_sweep(text)
        labels = SimpleNamespace(get=lambda _: None, auto_name_function=lambda *_: None)
        detector = module.FunctionDetector(engine, image, None, labels)
        bind(detector, qualified)
        return detector, text

    def test_interior_entry_does_not_cut_parent_or_lose_second_return(self):
        code = bytes.fromhex('85c07406b802000000c3b801000000c3')
        blob, document = self.fixture(code)
        det, text = self.detector(blob, document)
        det._add_candidate(0x1100a, .9, 'call_target')
        det._build_functions([text])
        self.assertEqual(det.functions[0x11000].end, 0x11010)
        self.assertEqual(det.functions[0x1100a].end, 0x11010)

    def test_declared_trailing_data_is_not_forced_into_instruction_body(self):
        blob, document = self.fixture(bytes.fromhex('b801000000c3') + b'\0'*12)
        det, text = self.detector(blob, document)
        det._build_functions([text])
        self.assertEqual(det.functions[0x11000].end, 0x11006)

    def test_alias_stays_callable_without_borrowing_next_body(self):
        blob, document = self.fixture(bytes.fromhex('85c07406b802000000c3b801000000c3'))
        det, text = self.detector(blob, document)
        det._build_functions([text])
        det._alias_entries[0x1100a] = 0x11020
        det._build_alias_entries()
        self.assertEqual(det.functions[0x1100a].end, 0x11010)
        self.assertEqual(det.functions[0x11000].end, 0x11010)

    def test_map_entries_wrong_image_and_unbacked_bounds_are_rejected(self):
        blob, original = self.fixture(b'\x31\xc0\xc3')
        for key, value in [('identity', 'entry-and-section-layout'),
                           ('xbe_sha256', 'f'*64), ('source_sha256', 'bad')]:
            document = copy.deepcopy(original); document[key] = value
            with self.assertRaises(ValueError): QualifiedFunctions(document, blob)
        for key,value in [('extent_kind','entry-only'), ('size',0x1000)]:
            document = copy.deepcopy(original); document['functions'][0][key] = value
            with self.assertRaises(ValueError): QualifiedFunctions(document, blob)

    def test_decoded_instruction_cannot_cross_declared_end(self):
        blob, document = self.fixture(bytes.fromhex('b801000000c3'), length=4)
        det, text = self.detector(blob, document)
        with self.assertRaisesRegex(ValueError, 'crosses PDB boundary'):
            det._build_functions([text])

    def test_trailing_switch_index_map_is_data_even_if_it_decodes_past_end(self):
        code = bytes.fromhex('ff248510100100b801000000c3cccccc')
        code += (0x11007).to_bytes(4, 'little') + b'\xb8\x01'
        blob, document = self.fixture(code)
        det, text = self.detector(blob, document)
        det.engine.instructions[0x11000].jump_table = 0x11010
        det.engine.jump_tables[0x11010] = 0x11014
        det._build_functions([text])
        self.assertEqual(det.functions[0x11000].end, 0x1100d)

    def test_alias_in_trailing_switch_data_is_removed_without_cutting_code(self):
        code = bytes.fromhex('ff248510100100b801000000c3cccccc')
        code += (0x11007).to_bytes(4, 'little') + b'\xb8\x01'
        blob, document = self.fixture(code)
        det, text = self.detector(blob, document)
        det.engine.instructions[0x11000].jump_table = 0x11010
        det.engine.jump_tables[0x11010] = 0x11014
        det._build_functions([text])
        det._alias_entries[0x11014] = 0x11016
        det._pass_alias_tail_jumps([text])
        self.assertNotIn(0x11014,det._alias_entries)
        det._build_alias_entries()
        self.assertNotIn(0x11014,det.functions)
        self.assertEqual(det.functions[0x11000].end,0x1100d)

    def test_overlapping_declared_ranges_find_enclosing_owner(self):
        blob, document = self.fixture(b'\x90'*20+b'\xc3')
        document['functions'].append(dict(address=0x11004, size=2,
                                          names=['Nested'],extent_kind='pdb-length'))
        qualified = QualifiedFunctions(document, blob)
        self.assertEqual(qualified.containing_end(0x11004), 0x11006)
        self.assertEqual(qualified.containing_end(0x11008), 0x11015)
        self.assertIsNone(qualified.containing_end(0x11015))

    def test_virtual_tail_before_shared_return_does_not_skip_to_trailing_table(self):
        # Two switch cases share a RET beyond a virtual tail dispatch. A table
        # close to either indirect jump must not hide the intervening code.
        code = bytes.fromhex('83f801770fff2485181001008b01ff6008909090c2040090')
        code += (0x1100c).to_bytes(4, 'little') + (0x11014).to_bytes(4, 'little')
        code += b'\xb8\x01'  # Index bytes can decode beyond the PDB extent.
        blob, document = self.fixture(code)
        det, text = self.detector(blob, document)
        det.engine.instructions[0x11005].jump_table = 0x11018
        det.engine.jump_tables[0x11018] = 0x11020
        det._build_functions([text])
        self.assertEqual(det.functions[0x11000].end, 0x11017)
        self.assertEqual(det.engine.instructions[0x11014].mnemonic, 'ret')

    def test_interleaved_table_alignment_preserves_cases_after_the_table(self):
        code = bytes.fromhex('ff24850810010090')
        code += (0x11010).to_bytes(4, 'little') * 2
        code += bytes.fromhex('b801000000c20400')
        blob, document = self.fixture(code)
        det, text = self.detector(blob, document)
        det.engine.instructions[0x11000].jump_table = 0x11008
        det.engine.jump_tables[0x11008] = 0x11010
        det._build_functions([text])
        self.assertEqual(det.functions[0x11000].end, 0x11018)

    def test_cli_applies_metadata_and_separates_its_cache_from_unaided(self):
        framework = Path(__file__).resolve().parents[1]
        donor = Path(os.environ.get('XLLE_CPU_DONOR', framework.parent/'refs/xboxrecomp-phobos665-review-20261006'))
        blob = symbol_fixture.image()
        symbols, _ = import_dia(blob, symbol_fixture.metadata(), 'a'*64)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); image = root/'authored.xbe'; image.write_bytes(blob)
            metadata = root/'symbols.aot-private.json';metadata.write_text(json.dumps(symbols))
            analysis = root/'analysis.json'
            parsed = subprocess.run([sys.executable,'-m','tools.xbe_parser',str(image),'--json',str(analysis)],
                                    cwd=donor,capture_output=True,text=True)
            self.assertEqual(parsed.returncode,0,parsed.stdout+parsed.stderr)
            out = root/'output'
            command = [sys.executable,str(framework/'tools/native_discovery.py'),str(donor),str(image),
                       '--analysis-json',str(analysis),'-o',str(out)]
            result = subprocess.run(command+['--discovery-symbols',str(metadata)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            rows = json.loads((out/'functions.json').read_text())
            entry = next(f for f in rows if int(f['start'],16)==0x11000)
            self.assertEqual(int(entry['end'],16),0x11002)
            from tools.disasm.config import CACHE_FILENAME
            before = json.loads((out/CACHE_FILENAME).read_text())['opts_key']
            result = subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            after = json.loads((out/CACHE_FILENAME).read_text())['opts_key']
            self.assertNotEqual(before,after)


if __name__ == '__main__': unittest.main()
