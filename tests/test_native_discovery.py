"""Synthetic discovery regressions; no original game or SDK input required."""
import os
from pathlib import Path
import subprocess
import struct
import sys
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from native_discovery import aligned_boundary_entry, install, install_cache_key, xpp_registration_entries, _unwind_context_entries
from native_pilot import DONOR_PIN


class NativeDiscovery(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        donor = Path(os.environ.get('XLLE_CPU_DONOR', ROOT.parent/'refs/xboxrecomp-phobos665-review-20261006'))
        if not donor.is_dir():
            raise unittest.SkipTest('set XLLE_CPU_DONOR to the pinned CPU tools')
        git = 'C:/Program Files/Git/cmd/git.exe' if os.name == 'nt' else 'git'
        if subprocess.check_output([git, '-C', str(donor), 'rev-parse', 'HEAD'], text=True).strip() != DONOR_PIN:
            raise RuntimeError('CPU donor pin differs')
        sys.path.insert(0, str(donor))
        from tools.disasm import functions
        from tools.disasm.engine import DisasmEngine
        from tools.disasm.loader import BinaryImage, SectionInfo
        cls.module, cls.engine_type = functions, DisasmEngine
        cls.image_type, cls.section_type = BinaryImage, SectionInfo
        install(functions)

    def detector(self, prefix=b'\xc3', body=b'\x31\xc0\xc3', *, address=0x10020, pointer=True):
        base = 0x10000
        code = b'\x90'*(address-base-len(prefix)) + prefix + body + b'\xcc'*65536
        data = address.to_bytes(4, 'little') if pointer else b'\0'*4
        text = self.section_type('.text', base, len(code), 0, len(code), False, True, '')
        rdata = self.section_type('.rdata', 0x20000, len(data), len(code), len(data), False, False, '')
        image = self.image_type('synthetic', code+data, base, 0x10010, base, 0, [text, rdata])
        engine = self.engine_type(image)
        engine.linear_sweep(text)
        detector = self.module.FunctionDetector.__new__(self.module.FunctionDetector)
        detector.engine, detector.image = engine, image
        detector._candidates, detector._alias_entries, detector._absorbed = {}, {}, set()
        detector.functions = {}
        return detector, text

    def test_lone_callback_after_return_is_recovered_without_symbols(self):
        det, text = self.detector()
        self.assertTrue(det._pass_data_ptr_targets([text]))
        self.assertIn(0x10020, det._alias_entries)

    def test_executable_flagged_bink_data_supplies_callable_table_entries(self):
        det, text = self.detector()
        storage = det.image.sections[1]
        storage.name, storage.executable = 'BINKDATA', True
        sections = det.image.get_code_sections()
        self.assertEqual(sections, [text])
        self.assertTrue(det._pass_data_ptr_targets(sections))
        self.assertIn(0x10020, det._alias_entries)

    def test_other_bink_and_sdk_sections_keep_their_code(self):
        det, text = self.detector()
        peer = det.image.sections[1]
        peer.executable = True
        for name in ('BINK', 'BINK32', 'BINK16MX', 'BINKDATA2', 'D3D_RD'):
            peer.name = name
            self.assertEqual(det.image.get_code_sections(), [text, peer])

    def test_bink_numeric_pair_does_not_create_interior_callable_entries(self):
        det, text = self.detector(prefix=bytes.fromhex('31c0'),
                                  body=bytes.fromhex('8b4c240464890d0000000083c410c20400'))
        storage = det.image.sections[1]
        storage.name, storage.executable = 'BINKDATA', True
        # Two consecutive coefficient words happen to equal code addresses.
        det.image.raw_data += (0x10024).to_bytes(4, 'little')
        storage.raw_size += 4
        storage.virtual_size += 4
        det.functions[0x10010] = SimpleNamespace(start=0x10010, end=0x10030)
        original_image, original_bytes = det.image, det.image.raw_data
        # The first target is aligned but has no independently saved frame;
        # the second is an unaligned instruction in the same epilogue.
        self.assertFalse(det._pass_data_ptr_targets([text]))
        self.assertFalse(det._alias_entries)
        self.assertIs(det.image, original_image)
        self.assertEqual(det.image.raw_data, original_bytes)

    def test_factory_without_a_standard_prologue_is_recovered(self):
        # Authored factory shape: allocation arguments, call, cleanup, result.
        body = bytes.fromhex('6a006820000000e81000000083c40885c0c3')
        det, text = self.detector(body=body)
        self.assertTrue(det._pass_data_ptr_targets([text]))
        self.assertIn(0x10020, det._alias_entries)

    def test_factory_tail_call_requires_a_known_destination(self):
        body = bytes.fromhex('6a006820000000e81000000083c40885c074078bc8e9c6ffffff31c0c3')
        det, text = self.detector(body=body)
        self.assertFalse(det._pass_data_ptr_targets([text]))
        det.functions[0x10000] = SimpleNamespace(start=0x10000, end=0x10001)
        self.assertTrue(det._pass_data_ptr_targets([text]))

    def test_immediate_callback_uses_the_same_boundary_rule(self):
        # Direct tail call to a known entry, rather than an unknown register.
        det, text = self.detector(body=bytes.fromhex('a100000200e9d6ffffff'), pointer=False)
        det.functions[0x10000] = SimpleNamespace(start=0x10000, end=0x10001)
        det.engine.instructions[0x10000].imm_ref = 0x10020
        self.assertTrue(det._pass_imm_ref_targets([text]))
        self.assertIn(0x10020, det._candidates)

    def test_no_reference_does_not_create_an_entry(self):
        det, text = self.detector(pointer=False)
        self.assertFalse(det._pass_data_ptr_targets([text]))
        self.assertNotIn(0x10020, det._alias_entries)

    def test_decoded_multibyte_alignment_recovers_a_lone_table_entry(self):
        for prefix in (bytes.fromhex('c38da424000000008bff'),
                       bytes.fromhex('c20c008d9b00000000'),
                       bytes.fromhex('c30f1f000f1f00')):
            det, text = self.detector(prefix=prefix)
            self.assertTrue(det._pass_data_ptr_targets([text]))
            self.assertEqual(det._alias_entries, {0x10020:0x10023})

    def test_alignment_cannot_hide_side_effects_or_fallthrough(self):
        for prefix in (bytes.fromhex('c389c3'), bytes.fromhex('c38d642404'),
                       bytes.fromhex('c383c000'), bytes.fromhex('31c08d642400'),
                       bytes.fromhex('b8000000c38d642400')):
            det, _ = self.detector(prefix=prefix)
            self.assertFalse(aligned_boundary_entry(det, 0x10020))

    def test_bink_unreachable_zero_alignment_has_a_decoded_terminal(self):
        for prefix, expected in ((b'\xc3'+bytes(6),True),
                                 (b'\xc3'+bytes(7),False),
                                 (b'\x31\xc0'+bytes(7),False),
                                 (b'\xc3'+bytes(16),False)):
            det, text = self.detector(prefix=prefix)
            text.name='BINK32'
            self.assertEqual(aligned_boundary_entry(det, 0x10020),expected)
        det, _ = self.detector(prefix=b'\xc3'+bytes(6))
        self.assertFalse(aligned_boundary_entry(det, 0x10020))

    def test_bink_code_section_start_needs_a_reference_and_complete_body(self):
        det,text=self.detector(address=0x10000,prefix=b'')
        text.name='BINK32'
        self.assertTrue(det._pass_data_ptr_targets([text]))
        self.assertEqual(det._alias_entries,{0x10000:0x10003})
        det,text=self.detector(address=0x10000,prefix=b'',pointer=False)
        text.name='BINK32'
        self.assertFalse(det._pass_data_ptr_targets([text]))
        det,text=self.detector(address=0x10000,prefix=b'',body=b'\x5e\xc3')
        text.name='BINK32'
        self.assertFalse(aligned_boundary_entry(det,0x10000))
        for name in ('.text','XPP','BINKDATA','BINKDATA2'):
            det,text=self.detector(address=0x10000,prefix=b'')
            text.name=name
            self.assertFalse(aligned_boundary_entry(det,0x10000))

    def test_fallthrough_and_ret_byte_in_an_immediate_are_rejected(self):
        for prefix in (bytes.fromhex('b8000000c3'), bytes.fromhex('31c0')):
            det, _ = self.detector(prefix=prefix)
            self.assertFalse(aligned_boundary_entry(det, 0x10020))

    def test_conditional_branch_is_not_a_boundary(self):
        det, _ = self.detector(prefix=bytes.fromhex('7400'))
        self.assertFalse(aligned_boundary_entry(det, 0x10020))

    def test_unaligned_entry_and_padding_are_rejected(self):
        det, _ = self.detector(address=0x10021)
        self.assertFalse(aligned_boundary_entry(det, 0x10021))
        for body in (b'\x90\xc3', b'\xcc\xc3'):
            det, _ = self.detector(body=body)
            self.assertFalse(aligned_boundary_entry(det, 0x10020))

    def test_unsaved_register_pop_is_a_continuation(self):
        det, text = self.detector(body=bytes.fromhex('5ec3'))
        self.assertFalse(det._pass_data_ptr_targets([text]))

    def gap_epilogue(self, prefix=bytes.fromhex('31c0')):
        det, text = self.detector(address=0x10022, prefix=prefix,
                                  body=bytes.fromhex('8bc15ec3'))
        storage = det.image.sections[1]
        det.image.raw_data += (0x10010).to_bytes(4, 'little')
        storage.raw_size += 4
        storage.virtual_size += 4
        det.functions[0x10010] = SimpleNamespace(start=0x10010, end=0x10011)
        return det, text

    def test_numeric_table_pair_cannot_establish_a_missing_saved_frame(self):
        det, text = self.gap_epilogue()
        original_bytes = det.image.raw_data
        self.assertFalse(det._pass_data_ptr_targets([text]))
        self.assertNotIn(0x10022, det._alias_entries)
        self.assertEqual(det.image.raw_data, original_bytes)

    def test_original_branch_preserves_a_context_dependent_gap_entry(self):
        det, text = self.gap_epilogue(prefix=bytes.fromhex('7500'))
        self.assertTrue(det._pass_data_ptr_targets([text]))
        self.assertEqual(det._alias_entries[0x10022], 0x10026)

    def test_numeric_pair_inside_a_known_body_does_not_establish_a_caller_frame(self):
        det, text = self.gap_epilogue()
        det.functions[0x10010].end = 0x10026
        self.assertFalse(det._pass_data_ptr_targets([text]))
        self.assertNotIn(0x10022, det._alias_entries)
        self.assertEqual(det.functions[0x10010].end, 0x10026)

    def test_original_branch_preserves_an_interior_shared_epilogue(self):
        det, text = self.gap_epilogue(prefix=bytes.fromhex('567500'))
        det.functions[0x1001f] = SimpleNamespace(start=0x1001f, end=0x10026)
        self.assertTrue(det._pass_data_ptr_targets([text]))
        self.assertEqual(det._alias_entries[0x10022], 0x10026)

    def test_original_call_preserves_a_context_dependent_entry(self):
        det, text = self.gap_epilogue(prefix=bytes.fromhex('e800000000'))
        det.functions[0x10010].end = 0x10026
        self.assertTrue(det._pass_data_ptr_targets([text]))
        self.assertEqual(det._alias_entries[0x10022], 0x10026)

    def unwind_epilogue(self, *, magic=0x19930522, previous=-1, action=0x10022,
                        count=1, jump=True):
        det, text = self.gap_epilogue()
        det.functions[0x10010].end = 0x10026
        storage = det.image.sections[1]
        storage.virtual_addr = 0x30000
        info, table = 0x30008, 0x30024
        metadata = struct.pack('<7I', magic, count, table, 0, 0, 0, 0)
        metadata += struct.pack('<iI', previous, action)
        raw = bytearray(det.image.raw_data)
        raw[:10] = b'\xb8'+struct.pack('<I', info)+(b'\xe9' if jump else b'\xe8')+struct.pack('<i', 6)
        det.image.raw_data = bytes(raw)+metadata
        storage.raw_size += len(metadata)
        storage.virtual_size += len(metadata)
        for address in list(det.engine.instructions):
            if 0x10000 <= address < 0x1000a:
                del det.engine.instructions[address]
        det.engine.decode_at(0x10000)
        det.engine.decode_at(0x10005)
        return det, text

    def test_original_unwind_context_preserves_an_unframed_action(self):
        for magic in (0x19930520, 0x19930521, 0x19930522):
            det, text = self.unwind_epilogue(magic=magic)
            original = det.image.raw_data
            self.assertEqual(_unwind_context_entries(det, [text]), {0x10022})
            self.assertTrue(det._pass_data_ptr_targets([text]))
            self.assertEqual(det._alias_entries[0x10022], 0x10026)
            self.assertEqual(det.image.raw_data, original)

    def test_malformed_unwind_metadata_cannot_establish_a_frame(self):
        for options in (dict(magic=0), dict(previous=0), dict(action=0x30000),
                        dict(count=4097), dict(count=2), dict(jump=False)):
            det, text = self.unwind_epilogue(**options)
            self.assertFalse(_unwind_context_entries(det, [text]))
            self.assertFalse(det._pass_data_ptr_targets([text]))
            self.assertNotIn(0x10022, det._alias_entries)

    def test_data_pointer_pass_preserves_previously_qualified_aliases(self):
        det, text = self.gap_epilogue()
        det._alias_entries[0x10022] = 0x10026
        self.assertFalse(det._pass_data_ptr_targets([text]))
        self.assertEqual(det._alias_entries[0x10022], 0x10026)

    def test_lone_pointer_cannot_split_an_existing_body(self):
        det, text = self.detector()
        det.functions[0x10010] = SimpleNamespace(start=0x10010, end=0x10023)
        self.assertFalse(det._pass_data_ptr_targets([text]))
        self.assertNotIn(0x10020, det._alias_entries)

    def test_prefix_before_a_known_entry_cannot_become_a_truncated_body(self):
        det, text = self.detector(body=bytes.fromhex('b80100000050c3'))
        det.functions[0x10025] = SimpleNamespace(start=0x10025, end=0x10027)
        self.assertFalse(det._pass_data_ptr_targets([text]))

    def test_tail_to_an_existing_function_interior_is_rejected(self):
        det, text = self.detector(body=bytes.fromhex('e9dcffffff'))
        det.functions[0x10000] = SimpleNamespace(start=0x10000, end=0x10002)
        self.assertFalse(det._pass_data_ptr_targets([text]))

    def test_install_is_idempotent(self):
        method = self.module.FunctionDetector._is_padded_entry
        install(self.module)
        self.assertIs(self.module.FunctionDetector._is_padded_entry, method)

    def test_cache_distinguishes_unaided_and_changed_supplied_metadata(self):
        def configured(metadata):
            cache = SimpleNamespace(AnalysisCache=type('AuthoredCache', (), {
                '_opts_key': staticmethod(lambda *args, **kwargs: 'donor-options')}))
            install_cache_key(cache, metadata)
            return cache.AnalysisCache._opts_key(False, None, [])
        unaided, first, changed = configured(None), configured('a'*64), configured('b'*64)
        self.assertEqual(len({unaided, first, changed}), 3)
        self.assertEqual(first, configured('a'*64))

    def switch_detector(self, body, table, case):
        det,text=self.detector(body=body,pointer=False)
        jump=next(i for i in det.engine.get_instructions_in_range(0x10020,0x10020+len(body))
                  if i.mnemonic=='jmp')
        jump.jump_table=table
        det.engine.jump_tables[table]=table+4
        det.engine.decode_at(case)
        det._candidates={}
        return det,text

    def test_unaided_extent_stops_before_a_proven_trailing_switch_table(self):
        body=bytes.fromhex('ff248530000100b801000000c3cccccc')
        body+=(0x10027).to_bytes(4,'little')+b'\xb8\x01'
        det,text=self.switch_detector(body,0x10030,0x10027)
        self.assertEqual(det._find_function_end(0x10020,None,text.virtual_addr+text.virtual_size),0x1002d)

    def test_unaided_extent_keeps_switch_cases_after_an_interleaved_table(self):
        body=bytes.fromhex('ff24852800010090')+(0x1002c).to_bytes(4,'little')+bytes.fromhex('b801000000c3')
        det,text=self.switch_detector(body,0x10028,0x1002c)
        self.assertEqual(det._find_function_end(0x10020,None,text.virtual_addr+text.virtual_size),0x10032)

    def test_unaided_extent_keeps_direct_continuation_after_a_switch_table(self):
        body=bytes.fromhex('7512ff248530000100c3cccccccccccc')
        body+=(0x10029).to_bytes(4,'little')+bytes.fromhex('b802000000c3')
        det,text=self.switch_detector(body,0x10030,0x10029)
        det.engine.decode_at(0x10034)
        self.assertEqual(det._find_function_end(0x10020,None,text.virtual_addr+text.virtual_size),0x1003a)

    def test_unaided_extent_does_not_assume_an_unknown_call_is_noreturn(self):
        body=bytes.fromhex('ff248530000100c3e800000000cccccc')
        body+=(0x10027).to_bytes(4,'little')+b'\xb8\x01'
        det,text=self.switch_detector(body,0x10030,0x10027)
        self.assertGreaterEqual(det._find_function_end(0x10020,None,text.virtual_addr+text.virtual_size),0x10034)

    def test_unaided_extent_preserves_an_unresolved_computed_continuation(self):
        body=bytes.fromhex('ff248530000100ffe0c3cccccccccccc')
        body+=(0x10027).to_bytes(4,'little')+b'\xb8\x01'
        det,text=self.switch_detector(body,0x10030,0x10027)
        self.assertGreaterEqual(det._find_function_end(0x10020,None,text.virtual_addr+text.virtual_size),0x10034)

    def xpp(self, *, body=b'\x31\xc0\xc3', prefix=b'\xc3', mate=True, name='XPP'):
        det, text = self.detector(address=0x10051, body=body, prefix=prefix, pointer=False)
        text.name = name
        det.functions[0x10040] = SimpleNamespace(start=0x10040, end=0x10041,
                                                detection_method='call_target')
        metadata = (0x10040 if mate else 0x10061).to_bytes(4, 'little')
        metadata += (0x10051).to_bytes(4, 'little')
        det.image.raw_data = metadata + det.image.raw_data[len(metadata):]
        return det, text

    def test_xpp_registration_recovers_unaligned_callback(self):
        det, section = self.xpp()
        self.assertTrue(xpp_registration_entries(det, [section]))
        self.assertEqual(det._alias_entries, {0x10051: 0x10054})
        self.assertEqual(det.functions[0x10040].end, 0x10041)

    def test_xpp_requires_a_known_mate_and_the_registration_section(self):
        for kwargs in ({'mate': False}, {'name': '.text'}):
            det, section = self.xpp(**kwargs)
            self.assertFalse(xpp_registration_entries(det, [section]))

    def test_xpp_does_not_treat_metadata_pointers_as_code(self):
        det, section = self.xpp()
        det.image.raw_data = (0x10040).to_bytes(4, 'little') + (0x10020).to_bytes(4, 'little') + det.image.raw_data[8:]
        self.assertFalse(xpp_registration_entries(det, [section]))

    def test_xpp_rejects_fallthrough_and_unsaved_pops(self):
        for kwargs in ({'prefix': b'\x31\xc0'}, {'prefix': bytes.fromhex('b800000090')},
                       {'body': b'\x5e\xc3'}):
            det, section = self.xpp(**kwargs)
            self.assertFalse(xpp_registration_entries(det, [section]))

    def test_xpp_cannot_split_a_body_or_borrow_a_known_entry(self):
        det, section = self.xpp()
        det.functions[0x10048] = SimpleNamespace(start=0x10048, end=0x10054,
                                                detection_method='gap_prologue')
        self.assertFalse(xpp_registration_entries(det, [section]))
        det, section = self.xpp(body=bytes.fromhex('b80100000050c3'))
        det.functions[0x10056] = SimpleNamespace(start=0x10056, end=0x10058,
                                                detection_method='call_target')
        self.assertFalse(xpp_registration_entries(det, [section]))

    def test_xpp_tail_call_must_have_a_known_destination(self):
        det, section = self.xpp(body=bytes.fromhex('e9aaffffff'))  # jmp 0x10000
        self.assertFalse(xpp_registration_entries(det, [section]))
        det.functions[0x10000] = SimpleNamespace(start=0x10000, end=0x10001,
                                                detection_method='call_target')
        # Retain a metadata prefix by marking the tail target as an alias.
        det.functions[0x10000].detection_method = 'alias'
        self.assertTrue(xpp_registration_entries(det, [section]))


if __name__ == '__main__':
    unittest.main()
