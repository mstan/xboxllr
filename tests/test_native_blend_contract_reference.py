import sys
import unittest
from fractions import Fraction as F
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_blend_contract_reference import blend, fixtures, pack, source


class BlendContractReferenceTest(unittest.TestCase):
    def test_fraction_controls_distinguish_source_order(self):
        rows = fixtures()['fixtures']
        self.assertEqual(rows[0]['rgba'], [0, 0, 0, 207])
        self.assertEqual(rows[1]['rgba'], [21, 21, 21, 243])
        self.assertEqual(rows[0]['source_packed_counterfactual'], [1, 1, 1, 207])
        self.assertEqual(rows[1]['source_packed_counterfactual'], [21, 21, 21, 244])
        for row in rows:
            self.assertEqual(row['conditional_interval'], [[c, c] for c in row['rgba']])

    def test_final_half_up_integer_boundaries_and_clipping(self):
        for code in range(255):
            boundary = F(2*code+1, 510)
            self.assertEqual(pack(boundary-F(1, 1 << 40)), code)
            self.assertEqual(pack(boundary), code+1)
            self.assertEqual(pack(boundary+F(1, 1 << 40)), code+1)
        self.assertEqual([pack(F(-1)), pack(0), pack(1), pack(F(2))], [0, 0, 255, 255])

    def test_same_factor_pair_includes_alpha(self):
        self.assertEqual(blend([F(1, 4)]*4, [0, 0, 0, 255]), [F(1, 16)]*3+[F(13, 16)])
        self.assertEqual(blend([F(1)]*4, [16]*4), [F(1)]*4)
        self.assertEqual(blend([F(0)]*4, [16]*4), [F(16, 255)]*4)

    def test_quarter_filter_is_not_average_or_nearest(self):
        color = source([(1, 1, 1, 64), (2, 2, 2, 64), (1, 1, 1, 65), (2, 2, 2, 65)])
        self.assertEqual(color[0], F(448, 65025))
        self.assertEqual(color[3], F(16448, 65025))
        with self.assertRaises(ValueError):
            source([(0, 0, 0, 0)]*4, (F(1),)*4)


if __name__ == '__main__':
    unittest.main()
