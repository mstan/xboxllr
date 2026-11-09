"""Synthetic discovery regressions; no original game or SDK input required."""
import os
from pathlib import Path
import subprocess
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from native_discovery import aligned_boundary_entry, install, install_cache_key, xpp_registration_entries, _unwind_context_entries, _original_entry_contexts, _switch_index_data_window, _repair_weak_callback_clamps, _indexed_numeric_byte_entries
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

    def sentinel_switch(self, *, scheduled=False, real_code=False):
        code = bytearray(b'\xcc' * 0x80)
        prefix = bytes.fromhex('0fb68138000100')
        if scheduled:
            prefix += bytes.fromhex('890d00000200')
        prefix += bytes.fromhex('ff248530000100')
        code[:len(prefix)] = prefix
        code[0x20] = code[0x24] = 0xc3
        code[0x30:0x38] = struct.pack('<II', 0x10020, 0x10024)
        code[0x38:0x40] = bytes.fromhex('0001020002909090')
        if real_code:
            code[0x3a:0x3d] = bytes.fromhex('02c3c3')  # ADD AL,BL; RET.
        code[0x40:0x46] = bytes.fromhex('b801000000c3')
        text = self.section_type('.text', 0x10000, len(code), 0, len(code), False, True, '')
        data = self.section_type('.data', 0x20000, 4, len(code), 4, True, False, '')
        image = self.image_type('authored switch', bytes(code) + bytes(4),
                                0x10000, 0x10004, 0x10000, 0, [text, data])
        engine = self.engine_type(image)
        engine.linear_sweep(text)
        det = SimpleNamespace(image=image, engine=engine, functions={
            0x10000: SimpleNamespace(start=0x10000, end=0x10020, detection_method='entry_point'),
            0x10040: SimpleNamespace(start=0x10040, end=0x10046, detection_method='call_target')})
        return det

    def test_sparse_switch_sentinel_is_data_including_a_scheduled_global_store(self):
        for scheduled in (False, True):
            with self.subTest(scheduled=scheduled):
                det = self.sentinel_switch(scheduled=scheduled)
                proof = _switch_index_data_window(det, 0x1003a)
                self.assertIsNotNone(proof)
                self.assertEqual((proof['entries'], proof['index_data_end'], proof['next_entry']),
                                 (2, 0x1003d, 0x10040))
                self.assertIsNone(_switch_index_data_window(det, 0x10040))

    def test_returning_code_after_a_switch_is_not_a_sentinel_data_run(self):
        det = self.sentinel_switch(real_code=True)
        self.assertIsNone(_switch_index_data_window(det, 0x1003a))

    def test_sparse_index_guard_requires_matching_registers_and_a_strong_next_entry(self):
        det = self.sentinel_switch()
        det.image.raw_data = det.image.raw_data[:9] + b'\x8d' + det.image.raw_data[10:]
        self.assertIsNone(_switch_index_data_window(det, 0x1003a))
        det = self.sentinel_switch()
        det.functions[0x10040].detection_method = 'after_switch_table'
        self.assertIsNone(_switch_index_data_window(det, 0x1003a))

    def numeric_byte_array(self, *, extra_word=False, address_taken=False,
                           real_call=False, wrong_mask_register=False,
                           signed_guard=False, scheduled=False):
        # Authored numeric byte flags, with unrelated locations and values.
        target = 0x10100
        prefix = bytes.fromhex('558bec83f808')
        if scheduled:
            prefix += bytes.fromhex('8945fc')  # MOV preserves EAX and flags.
        prefix += bytes.fromhex(('7d' if signed_guard else '73') + '13')
        prefix += bytes.fromhex('f680000101003c750a0fb68000010100')
        prefix += bytes.fromhex('83e13c' if wrong_mask_register else '83e03c')
        prefix += bytes.fromhex('5dc3')
        code = bytearray(b'\xcc' * 0x180)
        code[:len(prefix)] = prefix
        code[0x100:0x108] = bytes([1, 4, 8, 12, 0, 32, 64, 128])
        if address_taken:
            code[0x40:0x45] = b'\xb8' + struct.pack('<I', target)
        if real_call:
            code[0x40:0x45] = b'\xe8' + struct.pack('<i', target - 0x10045)
        tail = struct.pack('<I', target) if extra_word else bytes(4)
        text = self.section_type('.text', 0x10000, len(code), 0, len(code), False, True, '')
        rdata = self.section_type('.rdata', 0x20000, len(tail), len(code), len(tail), False, False, '')
        image = self.image_type('authored numeric readers', bytes(code) + tail,
                                0x10000, 0x10000, 0x10000, 0, [text, rdata])
        engine = self.engine_type(image)
        engine.linear_sweep(text)
        strong = SimpleNamespace(start=0x10000, end=0x10000 + len(prefix),
                                 detection_method='entry_point', has_prologue=True,
                                 called_by=[], calls_to=[])
        weak = SimpleNamespace(start=target, end=target + 32,
                               detection_method='after_switch_table', has_prologue=False,
                               called_by=[], calls_to=[])
        return SimpleNamespace(image=image, engine=engine,
                               functions={strong.start: strong, target: weak})

    def test_bounded_numeric_byte_readers_exclude_only_weak_data_guesses(self):
        for scheduled in (False, True):
            with self.subTest(scheduled=scheduled):
                det = self.numeric_byte_array(scheduled=scheduled)
                proof = _indexed_numeric_byte_entries(det)
                self.assertEqual(set(proof), {0x10100})
                self.assertEqual(proof[0x10100]['entries'], 8)
                self.assertNotEqual(proof[0x10100]['consumer'],
                                    proof[0x10100]['numeric_mask_consumer'])

    def test_numeric_data_filter_preserves_entry_and_unresolved_pointer_authority(self):
        for options in ({'extra_word': True}, {'address_taken': True}, {'real_call': True}):
            with self.subTest(options=options):
                self.assertEqual(_indexed_numeric_byte_entries(self.numeric_byte_array(**options)), {})
        det = self.numeric_byte_array()
        with patch('native_discovery._unwind_context_entries', return_value={0x10100}):
            self.assertEqual(_indexed_numeric_byte_entries(det), {})
        for field, value in [('has_prologue', True), ('called_by', [0x10040]),
                             ('calls_to', [0x10040]), ('detection_method', 'seed')]:
            det = self.numeric_byte_array()
            setattr(det.functions[0x10100], field, value)
            self.assertEqual(_indexed_numeric_byte_entries(det), {})

    def test_numeric_data_filter_requires_unsigned_bound_and_matching_numeric_result(self):
        for options in ({'wrong_mask_register': True}, {'signed_guard': True}):
            with self.subTest(options=options):
                self.assertEqual(_indexed_numeric_byte_entries(self.numeric_byte_array(**options)), {})

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

    def test_switch_index_scan_limit_does_not_become_a_function_boundary(self):
        table = 0x10010
        count = 200
        table_end = table + count*4
        false_entry = table_end + 256
        code = b'\xc3' + b'\xcc'*15 + struct.pack('<I', 0x10000)*count
        # All these bytes are valid table indices, including the apparent
        # XOR/RET body after the donor's 256-byte scan limit.
        code += b'\0'*256 + bytes.fromhex('31c0c3') + b'\0'*32
        det, text = self.detector(address=0x10000, prefix=b'', body=code, pointer=False)
        det.functions[0x10000] = SimpleNamespace(start=0x10000, end=0x10001)
        det.engine.jump_tables[table] = table_end
        det._pass_gap_prologues([text])
        self.assertNotIn(false_entry, det._candidates)
        # The weak table heuristic must not override independent input roots.
        det._add_candidate(false_entry, 1.0, 'seed')
        self.assertEqual(det._candidates[false_entry], (1.0, 'seed'))

    def test_short_switch_index_table_keeps_the_following_real_body(self):
        table = 0x10010
        for length in (5, 256):
            with self.subTest(index_bytes=length):
                code = b'\xc3' + b'\xcc'*15 + struct.pack('<4I', *([0x10000]*4))
                code += bytes(i % 4 for i in range(length)) + bytes.fromhex('b82a000000c3')
                entry = table + 16 + length
                det, text = self.detector(address=0x10000, prefix=b'', body=code, pointer=False)
                det.functions[0x10000] = SimpleNamespace(start=0x10000, end=0x10001)
                det.engine.jump_tables[table] = table + 16
                det._pass_gap_prologues([text])
                self.assertEqual(det._candidates[entry][1], 'after_switch_table')

    def test_weak_gap_entries_require_word_aligned_straight_line_cleanup(self):
        for method in ('after_switch_table', 'imm_ref_target'):
            with self.subTest(method=method):
                det, _ = self.detector(body=bytes.fromhex('51c21500'), pointer=False)
                det._add_candidate(0x10020, 1.0, method)
                self.assertNotIn(0x10020, det._candidates)
                # A known caller can establish a different guest contract.
                det._add_candidate(0x10020, 1.0, 'call_target')
                self.assertIn(0x10020, det._candidates)
                det, _ = self.detector(body=bytes.fromhex('b82a000000c20400'), pointer=False)
                det._add_candidate(0x10020, 1.0, method)
                self.assertIn(0x10020, det._candidates)

    def test_text_literals_do_not_establish_weak_code_entries(self):
        for method in ('cc_boundary', 'after_switch_table', 'imm_ref_target'):
            for literal in (b'Plain fixture string\0', b'XUnit payload data\0'):
                with self.subTest(method=method, literal=literal):
                    det, _ = self.detector(body=literal, pointer=False)
                    det._add_candidate(0x10020, 1.0, method)
                    self.assertNotIn(0x10020, det._candidates)
                    det._add_candidate(0x10020, 1.0, 'seed')
                    self.assertIn(0x10020, det._candidates)

    def test_text_like_instructions_keep_original_call_contracts(self):
        # Eight printable PUSH/POP bytes followed by ADD AL,AL and RET are
        # actual authored code. Its original CALL establishes the entry.
        body = b'PXQYRXW_' + bytes.fromhex('00c0c3')
        for method in ('cc_boundary', 'after_switch_table', 'imm_ref_target'):
            with self.subTest(method=method):
                det, _ = self.detector(prefix=bytes.fromhex('e800000000'), body=body, pointer=False)
                det._add_candidate(0x10020, 1.0, method)
                self.assertIn(0x10020, det._candidates)
                det, _ = self.detector(body=b'PXQYRXW_'+bytes.fromhex('01c0c3'), pointer=False)
                det._add_candidate(0x10020, 1.0, method)
                self.assertIn(0x10020, det._candidates)

    def parameter_tables(self, *, real_call=False, bound=16, code_entry=False):
        # Authored byte-parameter clamp, with different registers/locations
        # from any binary input. An E8 byte inside its limits is scalar data.
        entry, default, minimum, maximum, target = 0x10020, 0x10120, 0x10140, 0x10160, 0x10200
        code = bytearray(); labels = {}; fixups = []
        def label(name): labels[name] = entry + len(code)
        def emit(value): code.extend(bytes.fromhex(value))
        def branch(opcode, name):
            emit(opcode); fixups.append((len(code), name)); code.append(0)
        emit('558bec8a08')
        emit('80f9' + bytes([bound]).hex()); branch('74', 'init')
        emit('80f920'); branch('74', 'init'); branch('eb', 'done')
        label('init'); emit('8945f00fb6d14a8bf3'); branch('78', 'done')
        emit('8bfbf7df42')
        label('loop'); emit('8b45f08a0884c9'); branch('75', 'min')
        emit('8a8c37'); code.extend(struct.pack('<I', default))
        branch('eb', 'store')
        label('min'); emit('8a8437'); code.extend(struct.pack('<I', minimum))
        emit('3ac8'); branch('73', 'max')
        label('limit'); emit('8806'); branch('eb', 'next')
        label('max'); emit('8a8437'); code.extend(struct.pack('<I', maximum))
        emit('3ac8'); branch('77', 'limit')
        label('store'); emit('880e')
        label('next'); emit('ff45f0464a'); branch('75', 'loop')
        label('done'); emit('c9c3')
        for at, name in fixups:
            distance = labels[name] - (entry + at + 1)
            code[at] = distance & 255
        code.extend(b'\xcc'*(default-entry-len(code)))
        code.extend(b'\x2a'*32 + b'\0'*32 + b'\x90'*32)
        source = maximum + 6
        code[source-entry:source-entry+5] = b'\xe8' + struct.pack('<i', target-source-5)
        if real_call:
            code.extend(b'\xcc'*(0x10300-entry-len(code)))
            destination = source if code_entry else target
            code.extend(b'\xe8' + struct.pack('<i', destination-0x10305) + b'\xc3')
        det, text = self.detector(body=bytes(code), pointer=False)
        return det, text, source, target

    def test_parameter_bytes_do_not_admit_a_spurious_call_target(self):
        det, text, source, target = self.parameter_tables()
        self.assertTrue(det.engine.instructions[source].is_call)
        det._pass_call_targets([text])
        self.assertNotIn(target, det._candidates)
        det._add_candidate(target, 1.0, 'seed')
        self.assertIn(target, det._candidates)

    def test_parameter_data_filter_preserves_a_real_call_to_the_same_target(self):
        det, text, _, target = self.parameter_tables(real_call=True)
        det._pass_call_targets([text])
        self.assertIn(target, det._candidates)

    def test_parameter_data_filter_keeps_dual_code_and_data_entries(self):
        det, text, source, target = self.parameter_tables(real_call=True, code_entry=True)
        det._pass_call_targets([text])
        self.assertIn(source, det._candidates)
        self.assertIn(target, det._candidates)

    def test_parameter_data_filter_requires_validated_bounds_and_readonly_storage(self):
        for invalid in ('short_bound', 'writable'):
            with self.subTest(invalid=invalid):
                det, text, _, target = self.parameter_tables(bound=2 if invalid=='short_bound' else 16)
                if invalid=='writable': text.writable = True
                det._pass_call_targets([text])
                self.assertIn(target, det._candidates)

    def test_parameter_data_filter_rejects_entries_bypassing_initialization(self):
        for entry in ('image', 'seed'):
            with self.subTest(entry=entry):
                det, text, _, target = self.parameter_tables()
                loop = next(i.jump_target for i in det.engine.instructions.values()
                            if i.mnemonic=='jne' and i.jump_target is not None and i.jump_target<i.address)
                if entry=='image':det.image.entry_point=loop
                else:det._add_candidate(loop,1.0,'seed')
                det._pass_call_targets([text])
                self.assertIn(target,det._candidates)

    def test_sparse_switch_pointer_tail_is_not_a_following_function(self):
        table, end, entry = 0x10010, 0x10020, 0x10028
        prefix = b'\xc3' + b'\xcc'*15 + struct.pack('<4I', *([0x10000]*4)) + b'\0'*8
        code = prefix + struct.pack('<3I', *([0x10000]*3)) + b'\xcc'*16
        det, _ = self.detector(address=0x10000, prefix=b'', body=code, pointer=False)
        det.engine.jump_tables[table] = end
        det._add_candidate(entry, 1.0, 'after_switch_table')
        self.assertNotIn(entry, det._candidates)
        det._add_candidate(entry, 1.0, 'call_target')
        self.assertIn(entry, det._candidates)
        code = prefix + bytes.fromhex('b82a000000c3')
        det, _ = self.detector(address=0x10000, prefix=b'', body=code, pointer=False)
        det.engine.jump_tables[table] = end
        det._add_candidate(entry, 1.0, 'after_switch_table')
        self.assertIn(entry, det._candidates)

    def test_unaided_unresolved_virtual_continuation_keeps_its_conservative_extent(self):
        code = bytes.fromhex('83f801770fff2485180001008b01ff6008909090c2040090')
        code += struct.pack('<2I', 0x1000c, 0x10014) + b'\xb8\x01'
        det, _ = self.detector(address=0x10000, prefix=b'', body=code, pointer=False)
        det.engine.instructions[0x10005].jump_table = 0x10018
        det.engine.jump_tables[0x10018] = 0x10020
        original = det.image.raw_data
        self.assertGreaterEqual(det._find_function_end(0x10000, 0x10022, 0x10022), 0x10020)
        self.assertEqual(det.image.raw_data, original)

    def trailing_virtual_switch(self, tail=bytes.fromhex('8b115effa244010000')):
        # Two bounded cases converge on a virtual tail: mov edx,[ecx],
        # restore esi, jmp [edx+slot]. The switch table follows alignment.
        prefix = bytes.fromhex('5683f801770eff2485')
        cases = bytes.fromhex('b801000000eb0231c0')
        size = len(prefix) + 4 + len(cases) + len(tail)
        table = 0x10000 + (size + 15) // 16 * 16
        code = prefix + struct.pack('<I', table) + cases + tail
        code += b'\x90' * (table - 0x10000 - len(code))
        code += struct.pack('<2I', 0x1000d, 0x10014) + b'\xb8\x01'
        det, _ = self.detector(address=0x10000, prefix=b'', body=code, pointer=False)
        det.engine.instructions[0x10006].jump_table = table
        det.engine.jump_tables[table] = table + 8
        return det, table, 0x10000 + size

    def test_unaided_virtual_tail_excludes_its_trailing_switch_table(self):
        det, table, end = self.trailing_virtual_switch()
        original = det.image.raw_data
        self.assertEqual(det._find_function_end(0x10000, table+10, table+10), end)
        self.assertEqual(det.image.raw_data, original)

    def test_unproved_tail_shapes_keep_the_conservative_switch_extent(self):
        for tail in (bytes.fromhex('8b115effa285010000'),  # Unaligned slot.
                     bytes.fromhex('8b115eff648a04'),      # Indexed transfer.
                     bytes.fromhex('8b115a52ffa244010000'), # Overwritten vtable.
                     bytes.fromhex('8b51045effa244010000'), # Object member, not vtable.
                     bytes.fromhex('8b1149ffa244010000')): # Changed this pointer.
            with self.subTest(tail=tail.hex()):
                det, table, _ = self.trailing_virtual_switch(tail)
                self.assertGreaterEqual(det._find_function_end(0x10000, table+10, table+10), table+8)

    def test_virtual_tail_cannot_hide_a_case_after_the_table(self):
        det, table, _ = self.trailing_virtual_switch()
        det.engine.jump_table_entries = lambda address: [0x1000d, table+8]
        self.assertGreaterEqual(det._find_function_end(0x10000, table+10, table+10), table+8)

    def test_virtual_tail_cannot_bypass_its_vtable_load(self):
        det, table, _ = self.trailing_virtual_switch()
        det.engine.instructions[0x10004].jump_target = 0x10018
        self.assertGreaterEqual(det._find_function_end(0x10000, table+10, table+10), table+8)

    def test_unaided_direct_tail_cannot_hide_a_shared_return_before_a_table(self):
        code = bytes.fromhex('83f801770fff248518000100eb06909090909090c2040090')
        code += struct.pack('<2I', 0x1000c, 0x10014) + b'\xb8\x01'
        det, _ = self.detector(address=0x10000, prefix=b'', body=code, pointer=False)
        det.engine.instructions[0x10005].jump_table = 0x10018
        det.engine.jump_tables[0x10018] = 0x10020
        self.assertEqual(det._find_function_end(0x10000, 0x10022, 0x10022), 0x10017)
        # The interior view does not contain its owner's table dispatch.
        self.assertEqual(det._find_function_end(0x1000c, 0x10022, 0x10022), 0x10017)

    def test_unaided_dispatch_still_reaches_cases_after_an_interleaved_table(self):
        code = bytes.fromhex('ff24850800010090') + struct.pack('<2I', 0x10010, 0x10010)
        code += bytes.fromhex('b801000000c20400')
        det, _ = self.detector(address=0x10000, prefix=b'', body=code, pointer=False)
        det.engine.instructions[0x10000].jump_table = 0x10008
        det.engine.jump_tables[0x10008] = 0x10010
        self.assertEqual(det._find_function_end(0x10000, 0x10018, 0x10018), 0x10018)

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

    def test_data_table_word_keeps_a_weak_context_without_claiming_a_frame(self):
        det, text = self.gap_epilogue()
        original_bytes = det.image.raw_data
        self.assertTrue(det._pass_data_ptr_targets([text]))
        self.assertEqual(det._alias_entries[0x10022], 0x10026)
        self.assertNotIn(0x10022, det._candidates)
        self.assertNotIn(0x10022, _original_entry_contexts(det, [text]))
        self.assertTrue(det.engine.entry_pops_unsaved(0x10022))
        self.assertEqual(det.functions[0x10010].end, 0x10011)
        self.assertEqual(det.image.raw_data, original_bytes)

    def test_weak_context_guesses_retain_code_without_establishing_a_frame(self):
        for method in ('after_switch_table', 'imm_ref_target'):
            with self.subTest(method=method):
                det, _ = self.gap_epilogue()
                det._add_candidate(0x10022, 1.0, method)
                self.assertIn(0x10022, det._candidates)
                self.assertNotIn(0x10022, _original_entry_contexts(det, [det.image.sections[0]]))
                self.assertTrue(det.engine.entry_pops_unsaved(0x10022))
                det._add_candidate(0x10022, 1.0, 'seed')
                self.assertIn(0x10022, det._candidates)

    def test_weak_context_retention_keeps_packed_cleanup_policy(self):
        for method in ('after_switch_table', 'imm_ref_target'):
            with self.subTest(method=method):
                det, text = self.detector(body=bytes.fromhex('5ec20200'), pointer=False)
                self.assertTrue(det.engine.entry_pops_unsaved(0x10020))
                det._add_candidate(0x10020, .86, method)
                self.assertNotIn(0x10020, det._candidates)

    def test_runtime_table_targets_keep_caller_prepared_register_state(self):
        base, table, first, second = 0x10000, 0x22000, 0x10080, 0x10090
        code = bytearray(b'\xcc' * 0xa0)
        stores = b''.join(b'\xc7\x05' + struct.pack('<II', table + 4*i, target)
                          for i, target in enumerate((first, second)))
        code[:len(stores)] = stores
        code[len(stores):len(stores)+7] = b'\xff\x24\x85' + struct.pack('<I', table)
        code[0x80:0x82] = bytes.fromhex('5ec3')
        code[0x90:0x92] = bytes.fromhex('5fc3')
        det, text = self.detector(address=base, prefix=b'', body=bytes(code), pointer=False)
        for target in (first, second):
            self.assertTrue(det.engine.entry_pops_unsaved(target))
            self.assertNotIn(target, _original_entry_contexts(det, [text]))
            det._add_candidate(target, .86, 'imm_ref_target')
            self.assertEqual(det._candidates[target], (.86, 'imm_ref_target'))
        # Keeping a proposed context does not register a new ordinary frame.
        self.assertFalse(det.functions)

    def test_weak_context_entries_keep_original_control_and_unwind_contracts(self):
        for method in ('after_switch_table', 'imm_ref_target'):
            for factory in (lambda: self.gap_epilogue(prefix=bytes.fromhex('7500')),
                            self.unwind_epilogue):
                with self.subTest(method=method, factory=factory):
                    det, _ = factory()
                    det._add_candidate(0x10022, 1.0, method)
                    self.assertIn(0x10022, det._candidates)

    def test_original_branch_preserves_a_context_dependent_gap_entry(self):
        det, text = self.gap_epilogue(prefix=bytes.fromhex('7500'))
        self.assertTrue(det._pass_data_ptr_targets([text]))
        self.assertEqual(det._alias_entries[0x10022], 0x10026)

    def test_weak_context_alias_does_not_split_a_body_or_gain_a_frame(self):
        det, text = self.gap_epilogue()
        det.functions[0x10010].end = 0x10026
        self.assertTrue(det._pass_data_ptr_targets([text]))
        self.assertEqual(det._alias_entries[0x10022], 0x10026)
        self.assertNotIn(0x10022, det._candidates)
        self.assertNotIn(0x10022, _original_entry_contexts(det, [text]))
        self.assertTrue(det.engine.entry_pops_unsaved(0x10022))
        det.labels = self.module.LabelManager()
        det._build_alias_entries()
        alias = det.functions[0x10022]
        self.assertEqual(alias.detection_method, 'tail_jump_alias')
        self.assertFalse(alias.has_prologue)
        self.assertFalse(alias.called_by)
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
            self.assertTrue(det._pass_data_ptr_targets([text]))
            self.assertEqual(det._alias_entries[0x10022], 0x10026)
            self.assertNotIn(0x10022, det._candidates)
            self.assertNotIn(0x10022, _original_entry_contexts(det, [text]))
            self.assertTrue(det.engine.entry_pops_unsaved(0x10022))

    def test_data_pointer_pass_preserves_previously_qualified_aliases(self):
        det, text = self.gap_epilogue()
        det._alias_entries[0x10022] = 0x10026
        self.assertFalse(det._pass_data_ptr_targets([text]))
        self.assertEqual(det._alias_entries[0x10022], 0x10026)

    def catch_resume_epilogue(self, *, tries=1, low=0, high=0, catch_high=1,
                              catches=1, handler=0x10010, resume=0x10022,
                              returns=True):
        det, text = self.unwind_epilogue(count=2)
        storage = det.image.sections[1]
        unwind, try_map, handlers = 0x30024, 0x30034, 0x30048
        metadata = struct.pack('<7I', 0x19930520, 2, unwind, tries, try_map, 0, 0)
        metadata += struct.pack('<iIiI', -1, 0, -1, 0)
        metadata += struct.pack('<iiiII', low, high, catch_high, catches, handlers)
        metadata += struct.pack('<4I', 0, 0, 0, handler)
        raw = bytearray(det.image.raw_data[:storage.raw_addr + 8])
        raw[0x10:0x16] = b'\xb8' + struct.pack('<I', resume) + (b'\xc3' if returns else b'\xcc')
        det.image.raw_data = bytes(raw) + metadata
        storage.raw_size = storage.virtual_size = 8 + len(metadata)
        for address in list(det.engine.instructions):
            if 0x10010 <= address < 0x10016:
                del det.engine.instructions[address]
        det.engine.decode_at(0x10010)
        det.engine.decode_at(0x10015)
        return det, text

    def test_original_catch_return_preserves_its_frame_dependent_resume(self):
        det, text = self.catch_resume_epilogue()
        original = det.image.raw_data
        self.assertTrue(det.engine.entry_pops_unsaved(0x10022))
        self.assertEqual(_unwind_context_entries(det, [text]), {0x10022})
        self.assertFalse(det._candidates)  # Recognition does not seed functions.
        det._add_candidate(0x10022, 0.86, 'imm_ref_target')
        self.assertIn(0x10022, det._candidates)
        self.assertEqual(det.image.raw_data, original)

    def test_catch_resume_requires_bounded_original_metadata_and_code(self):
        for options in (dict(tries=0), dict(tries=4097), dict(low=-1),
                        dict(high=2), dict(catch_high=2), dict(catches=0),
                        dict(catches=4097), dict(handler=0x30000),
                        dict(resume=0x30000), dict(returns=False)):
            with self.subTest(options=options):
                det, text = self.catch_resume_epilogue(**options)
                self.assertFalse(_unwind_context_entries(det, [text]))
                det._add_candidate(0x10022, 0.86, 'imm_ref_target')
                self.assertIn(0x10022, det._candidates)
                self.assertNotIn(0x10022, _original_entry_contexts(det, [text]))

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
        # The unknown call is reached by the switch and has a real postlude.
        # Returning from it must execute MOV EAX,2 and the following RET.
        body=bytes.fromhex('ff248534000100e800000000b802000000c3cccc')
        body+=(0x10027).to_bytes(4,'little')+b'\xb8\x01'
        det,text=self.switch_detector(body,0x10034,0x10027)
        self.assertEqual(det._find_function_end(0x10020,None,text.virtual_addr+text.virtual_size),0x10032)
        self.assertEqual(det.engine.instructions[0x1002c].op_str,'eax, 2')

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

    def weak_callback_clamp(self, *, method='imm_ref_target', frame_free=False,
                            crossing=True, tail=False, called=False,
                            parent_method='seed', allocating_frame=True, stack_reset=False):
        code = bytearray(b'\xcc'*0x41)
        # A saved-register frame has an early return and a later conditional
        # exit. The weak block between them uses that same original frame.
        code[:14] = bytes.fromhex('83ec045683f80074275e83c404c3')
        code[14:29] = bytes.fromhex('b801000000e8280000005e83c404c3')
        code[0x30:0x3f] = bytes.fromhex('b82a000000e8060000005e83c404c3')
        code[0x40] = 0xc3
        if not allocating_frame:
            code[:4] = b'\x90'*4
        if frame_free:
            code[14:20] = bytes.fromhex('b801000000c3')
        if stack_reset:
            code[14:22] = bytes.fromhex('8b65e85e83c404c3')
        if not crossing:
            code[8] = 0
        if tail:
            code[9:14] = bytes.fromhex('e900000000')
        det, _ = self.detector(prefix=b'', body=bytes(code), pointer=False)
        function = self.module.Function
        det.functions = {
            0x10020: function(0x10020, 0x1002e, 'parent', '.text', 1., parent_method),
            0x1002e: function(0x1002e, 0x10036 if stack_reset else 0x10034 if frame_free else 0x1003d,
                              'weak', '.text', .8, method),
            0x10060: function(0x10060, 0x10061, 'callee', '.text', 1., 'seed'),
        }
        det._candidates = {a: (f.confidence, f.detection_method)
                           for a, f in det.functions.items()}
        det._alias_entries[0x1002e] = (.8, method)
        det._build_call_graph()
        if called:
            det.functions[0x1002e].called_by.append(0x10010)
        return det

    def test_final_weak_callback_cannot_cut_off_original_frame_exit(self):
        det = self.weak_callback_clamp()
        self.assertTrue(_repair_weak_callback_clamps(det))
        self.assertEqual(det.functions[0x10020].end, 0x1005f)
        self.assertNotIn(0x1002e, det.functions)
        self.assertNotIn(0x1002e, det._candidates)
        self.assertNotIn(0x1002e, det._alias_entries)
        # The removed view's outgoing CALL now belongs to its retained parent.
        self.assertEqual(det.functions[0x10020].calls_to, [0x10060])
        self.assertEqual(det.functions[0x10060].called_by, [0x10020])

    def flag_context_clamp(self, *, producer=True, early_restore=False,
                           early_return=False, parent_method='seed'):
        prefix = bytes.fromhex('83ec08535685db5e')
        if not producer:
            prefix = prefix[:5] + bytes.fromhex('9090') + prefix[7:]
        if early_restore:
            prefix = prefix[:4] + b'\x5b' + prefix[5:]
        if early_return:
            # One branch returns after its own complete epilogue; the other
            # reaches the final flag consumer with both initial saves live.
            prefix = bytes.fromhex('83ec08535685c9740b85db5e0f95c05b83c408c385db5e')
        suffix = bytes.fromhex('0f95c05b83c408c3')
        det, _ = self.detector(prefix=b'', body=prefix + suffix, pointer=False)
        function = self.module.Function
        entry = 0x10020 + len(prefix)
        det.functions = {
            0x10020: function(0x10020, entry, 'parent', '.text', 1., parent_method),
            entry: function(entry, entry + len(suffix), 'context', '.text', .88, 'tail_jump_alias'),
        }
        det._candidates = {a: (f.confidence, f.detection_method)
                           for a, f in det.functions.items()}
        det._alias_entries[entry] = (.88, 'tail_jump_alias')
        return det

    def test_flag_context_retention_keeps_original_owner_and_alias(self):
        det = self.flag_context_clamp()
        alias = det.functions[0x10028]
        self.assertTrue(_repair_weak_callback_clamps(det, flag_contexts=True))
        self.assertEqual(det.functions[0x10020].end, 0x10030)
        self.assertIs(det.functions[0x10028], alias)
        self.assertEqual(det._candidates[0x10028], (.88, 'tail_jump_alias'))
        self.assertIn(0x10028, det._alias_entries)
        self.assertFalse(_repair_weak_callback_clamps(det, flag_contexts=True))

    def test_flag_context_recovery_requires_produced_flags_and_live_saved_frame(self):
        for options in ({'producer': False}, {'early_restore': True}):
            with self.subTest(options=options):
                det = self.flag_context_clamp(**options)
                self.assertFalse(_repair_weak_callback_clamps(det, flag_contexts=True))
                self.assertEqual(det.functions[0x10020].end, 0x10028)
        det = self.flag_context_clamp()
        first = det.engine.instructions.pop(0x10028)
        first.address += 1
        det.engine.instructions[first.address] = first
        self.assertFalse(_repair_weak_callback_clamps(det, flag_contexts=True))

    def test_flag_context_recovery_does_not_absorb_independent_or_registered_entries(self):
        for field, value in [('has_prologue', True), ('called_by', [0x10010]),
                             ('detection_method', 'seed')]:
            det = self.flag_context_clamp()
            setattr(det.functions[0x10028], field, value)
            self.assertFalse(_repair_weak_callback_clamps(det, flag_contexts=True))
        det = self.flag_context_clamp()
        with patch('native_discovery._unwind_context_entries', return_value={0x10028}):
            self.assertFalse(_repair_weak_callback_clamps(det, flag_contexts=True))

    def test_fpo_weak_parent_keeps_flags_on_path_after_a_separate_early_return(self):
        det = self.flag_context_clamp(early_return=True, parent_method='imm_ref_target')
        entry = det.functions[0x10020].end
        alias = det.functions[entry]
        self.assertFalse(det.functions[0x10020].has_prologue)
        self.assertFalse(det.functions[0x10020].called_by)
        self.assertTrue(_repair_weak_callback_clamps(det, flag_contexts=True))
        self.assertEqual(det.functions[0x10020].end, alias.end)
        self.assertIs(det.functions[entry], alias)

    def unbound_frame_context_clamp(self, *, known_target=False, frame_free=False,
                                    table_dispatch=False):
        # The original conditional and JMP meet at a local label inside the
        # first inherited-frame view, not a standalone function start.
        prefix = bytes.fromhex('83ec08565785c9740bb801000000')
        if table_dispatch:
            prefix = bytes.fromhex('83ec085657ff248d8000010090eb0085c9740bb801000000')
        middle = bytes.fromhex('eb046a009090')
        suffix = bytes.fromhex('b8070000005f5e83c408c3')
        if frame_free:
            suffix = bytes.fromhex('b807000000909083c408c3')
        payload = bytearray(prefix + middle + suffix)
        if table_dispatch:
            payload.extend(b'\xcc' * (0x60 - len(payload)))
            payload.extend(struct.pack('<II', 0x1002c, 0x1002f))
        det, _ = self.detector(prefix=b'', body=bytes(payload), pointer=False)
        if table_dispatch:
            det.engine.jump_tables[0x10080] = 0x10088
        start = 0x10020
        weak, target = start + len(prefix), start + len(prefix + middle)
        secondary = target + 5
        function = self.module.Function
        det.functions = {
            start: function(start, weak, 'parent', '.text', .86, 'imm_ref_target'),
            weak: function(weak, secondary, 'context', '.text', .86, 'imm_ref_target'),
            secondary: function(secondary, start + len(prefix + middle + suffix),
                                'second context', '.text', .88, 'tail_jump_alias'),
        }
        if known_target:
            det.functions[target] = function(target, secondary, 'independent',
                                              '.text', 1., 'seed')
        det._candidates = {a: (f.confidence, f.detection_method)
                           for a, f in det.functions.items()}
        det._alias_entries[secondary] = (.88, 'tail_jump_alias')
        return det, weak, target, secondary

    def test_original_unbound_branches_rejoin_owner_without_dropping_contexts(self):
        det, weak, target, secondary = self.unbound_frame_context_clamp()
        entries = {a: det.functions[a] for a in (weak, secondary)}
        self.assertNotIn(target, det.functions)
        self.assertTrue(_repair_weak_callback_clamps(det, branch_contexts=True))
        self.assertEqual(det.functions[0x10020].end, det.functions[secondary].end)
        for address, entry in entries.items():
            self.assertIs(det.functions[address], entry)
            self.assertIn(address, det._candidates)
        self.assertIn(secondary, det._alias_entries)

    def test_owner_overlap_requires_unbound_original_target_and_inherited_frame(self):
        for options in ({'known_target': True}, {'frame_free': True}):
            with self.subTest(options=options):
                det, weak, _, _ = self.unbound_frame_context_clamp(**options)
                self.assertFalse(_repair_weak_callback_clamps(det, branch_contexts=True))
                self.assertEqual(det.functions[0x10020].end, weak)
        det, weak, _, _ = self.unbound_frame_context_clamp()
        det.functions[weak].called_by.append(0x10010)
        self.assertFalse(_repair_weak_callback_clamps(det, branch_contexts=True))

    def test_owner_context_reachability_follows_original_jump_table_cases(self):
        det, weak, _, secondary = self.unbound_frame_context_clamp(table_dispatch=True)
        self.assertTrue(_repair_weak_callback_clamps(det, branch_contexts=True))
        self.assertEqual(det.functions[0x10020].end, det.functions[secondary].end)
        self.assertIn(weak, det.functions)

    def test_final_callback_repair_preserves_independent_entry_contracts(self):
        for options in ({'method': 'seed_vtable_thunk'}, {'method': 'call_target'},
                        {'method': 'prologue'}, {'called': True}, {'frame_free': True}):
            with self.subTest(options=options):
                det = self.weak_callback_clamp(**options)
                self.assertFalse(_repair_weak_callback_clamps(det))
                self.assertIn(0x1002e, det.functions)
                self.assertEqual(det.functions[0x10020].end, 0x1002e)

    def test_original_allocating_fpo_frame_establishes_weak_callback_owner(self):
        det = self.weak_callback_clamp(parent_method='imm_ref_target')
        self.assertTrue(_repair_weak_callback_clamps(det))
        self.assertEqual(det.functions[0x10020].end, 0x1005f)

    def test_unreferenced_guess_without_original_frame_cannot_absorb_callback(self):
        det = self.weak_callback_clamp(parent_method='imm_ref_target', allocating_frame=False)
        self.assertFalse(_repair_weak_callback_clamps(det))
        self.assertIn(0x1002e, det.functions)

    def test_original_external_control_consumers_preserve_callback(self):
        from tools.disasm.engine import Instruction
        for call in (False, True):
            with self.subTest(call=call):
                det = self.weak_callback_clamp()
                det.engine.instructions[0x10010] = Instruction(
                    0x10010, 5 if call else 2, 'call' if call else 'jne', '0x1002e', '',
                    is_call=call, is_cond_jump=not call,
                    call_target=0x1002e if call else None,
                    jump_target=None if call else 0x1002e)
                self.assertFalse(_repair_weak_callback_clamps(det))
                self.assertIn(0x1002e, det.functions)
                self.assertEqual(det.functions[0x10020].end, 0x1002e)

    def test_registered_unwind_context_preserves_callback(self):
        det = self.weak_callback_clamp()
        with patch('native_discovery._unwind_context_entries', return_value={0x1002e}):
            self.assertFalse(_repair_weak_callback_clamps(det))
        self.assertIn(0x1002e, det.functions)

    def test_internal_call_or_address_taken_context_can_overlap_its_owner(self):
        from tools.disasm.engine import Instruction
        for call in (False, True):
            with self.subTest(call=call):
                det = self.weak_callback_clamp()
                det.engine.instructions[0x10055] = Instruction(
                    0x10055, 5, 'call' if call else 'mov', '0x1002e', '',
                    is_call=call, call_target=0x1002e if call else None,
                    imm_ref=None if call else 0x1002e)
                original = det.functions[0x1002e]
                self.assertTrue(_repair_weak_callback_clamps(det))
                self.assertEqual(det.functions[0x10020].end, 0x1005f)
                self.assertIs(det.functions[0x1002e], original)
                self.assertIn(0x1002e, det.functions)
                self.assertNotIn(0x1002e, det._absorbed)

    def test_callback_repair_restores_an_address_taken_record_removed_by_walker(self):
        from tools.disasm.engine import Instruction
        det = self.weak_callback_clamp()
        det.engine.instructions[0x10010] = Instruction(
            0x10010, 5, 'mov', 'eax, 0x1002e', '', imm_ref=0x1002e)
        original = det.functions[0x1002e]
        candidate = det._candidates[0x1002e]
        measure = det._find_function_end

        def destructive_walk(*args):
            end = measure(*args)
            det.functions.pop(0x1002e, None)
            det._candidates.pop(0x1002e, None)
            det._absorbed.add(0x1002e)
            return end

        with patch.object(det, '_find_function_end', side_effect=destructive_walk):
            self.assertTrue(_repair_weak_callback_clamps(det))
        self.assertEqual(det.functions[0x10020].end, 0x1005f)
        self.assertIs(det.functions[0x1002e], original)
        self.assertEqual(det._candidates[0x1002e], candidate)
        self.assertNotIn(0x1002e, det._absorbed)
        self.assertFalse(original.has_prologue)
        self.assertEqual(original.detection_method, 'imm_ref_target')

    def test_callback_repair_preserves_secondary_address_taken_context_records(self):
        from tools.disasm.engine import Instruction
        for overwrite in (False, True):
            with self.subTest(overwrite=overwrite):
                det = self.weak_callback_clamp()
                original = self.module.Function(
                    0x1005a, 0x1005f, 'secondary context', '.text', .86, 'imm_ref_target')
                det.functions[0x1005a] = original
                candidate = (.86, 'imm_ref_target')
                det._candidates[0x1005a] = candidate
                det._alias_entries[0x1005a] = 0x1005f
                det.engine.instructions[0x10010] = Instruction(
                    0x10010, 5, 'mov', 'eax, 0x1005a', '', imm_ref=0x1005a)
                measure = det._find_function_end

                def destructive_walk(*args):
                    end = measure(args[0], 0x10060, args[2])
                    det.functions.pop(0x1005a, None)
                    if overwrite:
                        det.functions[0x1005a] = self.module.Function(
                            0x1005a, 0x10051, 'overwritten', '.text', .1, 'gap_prologue')
                    det._candidates.pop(0x1005a, None)
                    det._absorbed.add(0x1005a)
                    return end

                with patch.object(det, '_find_function_end', side_effect=destructive_walk):
                    self.assertTrue(_repair_weak_callback_clamps(det))
                self.assertEqual(det.functions[0x10020].end, 0x1005f)
                self.assertNotIn(0x1002e, det.functions)
                self.assertIs(det.functions[0x1005a], original)
                self.assertEqual(det._candidates[0x1005a], candidate)
                self.assertEqual(det._alias_entries[0x1005a], 0x1005f)
                self.assertNotIn(0x1005a, det._absorbed)
                self.assertFalse(original.has_prologue)
                self.assertFalse(original.called_by)

    def test_callback_repair_does_not_restore_a_numeric_data_word_only(self):
        det = self.weak_callback_clamp()
        det.functions[0x1005a] = self.module.Function(
            0x1005a, 0x1005f, 'numeric guess', '.text', .86, 'imm_ref_target')
        det._candidates[0x1005a] = (.86, 'imm_ref_target')
        det._alias_entries[0x1005a] = 0x1005f
        # A data word alone supplies no decoded original address consumer.
        det.image.raw_data += struct.pack('<I', 0x1005a)
        measure = det._find_function_end

        def destructive_walk(*args):
            end = measure(args[0], 0x10060, args[2])
            det.functions.pop(0x1005a, None)
            det._candidates.pop(0x1005a, None)
            det._absorbed.add(0x1005a)
            return end

        with patch.object(det, '_find_function_end', side_effect=destructive_walk):
            self.assertTrue(_repair_weak_callback_clamps(det))
        self.assertEqual(det.functions[0x10020].end, 0x1005f)
        self.assertNotIn(0x1005a, det.functions)
        self.assertNotIn(0x1005a, det._candidates)
        self.assertNotIn(0x1005a, det._alias_entries)

    def test_restored_parent_frame_cannot_absorb_callback(self):
        from tools.disasm.engine import Instruction
        det = self.weak_callback_clamp()
        det.engine.instructions[0x10024] = Instruction(0x10024, 1, 'pop', 'esi', '')
        self.assertFalse(_repair_weak_callback_clamps(det))
        self.assertIn(0x1002e, det.functions)

    def test_stack_reset_context_is_not_absorbed_as_callback_guess(self):
        det = self.weak_callback_clamp(stack_reset=True)
        self.assertFalse(_repair_weak_callback_clamps(det))
        self.assertIn(0x1002e, det.functions)

    def test_missing_original_branch_target_cannot_produce_partial_repair(self):
        det = self.weak_callback_clamp()
        det.engine.instructions.pop(0x10050)
        self.assertFalse(_repair_weak_callback_clamps(det))
        self.assertEqual(det.functions[0x10020].end, 0x1002e)
        self.assertIn(0x1002e, det.functions)

    def test_final_callback_repair_requires_original_crossing_without_direct_tail(self):
        for options in ({'crossing': False}, {'tail': True}):
            with self.subTest(options=options):
                det = self.weak_callback_clamp(**options)
                self.assertFalse(_repair_weak_callback_clamps(det))
                self.assertIn(0x1002e, det.functions)


if __name__ == '__main__':
    unittest.main()
