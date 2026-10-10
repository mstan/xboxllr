"""Reject incomplete ledgers and mismatched frame submissions without a GPU."""
import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_frame_audit import audit


class FrameAudit(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.path = self.directory / 'frame.jsonl'
        def call(index, name, args, **fields):
            return dict(event='call', sequence=index, name=name, args=args, shader=fields.pop('shader', 1),
                        target=fields.pop('target', 100), depth=200,
                        capture_sequence=fields.pop('capture_sequence', 0), return_eax=0, **fields)
        self.rows = [dict(event='begin', schema=1, swap=17, width=640, height=480, target=100, depth=200),
                     call(1, 'D3DDevice_Clear', [0, 0, 0xf3, 0, 0x3f800000, 0]),
                     call(2, 'D3DDevice_DrawVertices', [5, 7, 3], capture_sequence=1),
                     call(3, 'D3DDevice_SetRenderTarget', [300, 200], target=300),
                     call(4, 'D3DDevice_DrawVerticesUP', [8, 4, 4096, 32], shader=2, target=300),
                     dict(event='end', swap=18, calls=4, draws=2, captured=1, clears=1, target_sets=1)]
        (self.directory / 'draw-1.bin').write_bytes(struct.pack('<8I', 0x44534c58, 1, 5, 7, 3, 11, 1, 0))
        (self.directory / 'scene-1.bin').write_bytes(struct.pack('<10I', 0x534c4c58, 1, 1, 5, 7, 3, 100, 200, 0, 0))

    def run_rows(self, rows):
        self.path.write_text(''.join(json.dumps(row) + '\n' for row in rows))
        return audit(self.path)

    def test_accounting_keeps_uncaptured_and_pixel_gaps(self):
        result = self.run_rows(self.rows)
        self.assertTrue(result['frame_boundary_complete'])
        self.assertFalse(result['complete_pixel_qualification'])
        self.assertEqual(result['draw_kinds'], {'programmable_bound': 1, 'fvf_up': 1})
        self.assertEqual(result['missing_draw_companions'][0]['call'], 4)
        self.assertEqual(result['clear_operations'][0]['flags'], 0xf3)
        self.assertEqual(result['target_switch_calls'], [3])

    def test_missing_final_swap(self):
        with self.assertRaises(ValueError): self.run_rows(self.rows[:-1])

    def test_missing_initial_swap(self):
        with self.assertRaises(ValueError): self.run_rows(self.rows[1:])

    def test_nonconsecutive_swaps(self):
        rows = copy.deepcopy(self.rows); rows[-1]['swap'] += 1
        with self.assertRaises(ValueError): self.run_rows(rows)

    def test_deleted_call(self):
        rows = copy.deepcopy(self.rows); del rows[2]
        with self.assertRaises(ValueError): self.run_rows(rows)

    def test_deleted_call_with_renumbering(self):
        rows = copy.deepcopy(self.rows); del rows[2]
        for index, row in enumerate(rows[1:-1], 1): row['sequence'] = index
        with self.assertRaises(ValueError): self.run_rows(rows)

    def test_terminal_counts(self):
        for field in ('calls', 'draws', 'captured', 'clears', 'target_sets'):
            with self.subTest(field=field):
                rows = copy.deepcopy(self.rows); rows[-1][field] += 1
                with self.assertRaises(ValueError): self.run_rows(rows)

    def test_boolean_is_not_a_word(self):
        rows = copy.deepcopy(self.rows); rows[2]['args'][0] = True
        with self.assertRaises(ValueError): self.run_rows(rows)

    def test_negative_word(self):
        rows = copy.deepcopy(self.rows); rows[2]['depth'] = -1
        with self.assertRaises(ValueError): self.run_rows(rows)

    def test_draw_abi(self):
        rows = copy.deepcopy(self.rows); rows[2]['args'].pop()
        with self.assertRaises(ValueError): self.run_rows(rows)

    def test_user_pointer_stride(self):
        rows = copy.deepcopy(self.rows); rows[4]['args'][3] = 0
        with self.assertRaises(ValueError): self.run_rows(rows)

    def test_wrong_draw_companion(self):
        rows = copy.deepcopy(self.rows); rows[2]['args'][1] += 1
        with self.assertRaises(ValueError): self.run_rows(rows)

    def test_wrong_target_companion(self):
        rows = copy.deepcopy(self.rows); rows[2]['target'] += 1
        with self.assertRaises(ValueError): self.run_rows(rows)

    def test_fvf_capture_is_not_invented(self):
        rows = copy.deepcopy(self.rows); rows[4]['capture_sequence'] = 2
        with self.assertRaises(ValueError): self.run_rows(rows)

    def test_capture_on_a_clear(self):
        rows = copy.deepcopy(self.rows); rows[1]['capture_sequence'] = 1
        with self.assertRaises(ValueError): self.run_rows(rows)

    def test_missing_companion(self):
        (self.directory / 'scene-1.bin').unlink()
        with self.assertRaises(OSError): self.run_rows(self.rows)

    def test_truncated_companion(self):
        (self.directory / 'scene-1.bin').write_bytes(b'no')
        with self.assertRaises(ValueError): self.run_rows(self.rows)

    def test_partial_line(self):
        self.run_rows(self.rows)
        self.path.write_bytes(self.path.read_bytes().rstrip(b'\n'))
        with self.assertRaises(ValueError): audit(self.path)


if __name__ == '__main__':
    unittest.main()
