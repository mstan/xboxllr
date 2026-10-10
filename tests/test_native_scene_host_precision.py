"""Authored interval controls for signed post-transform window quantization."""
from fractions import Fraction
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_scene_host_precision import Interval,window_snap


class WindowIntervals(unittest.TestCase):
    def test_signed_half_grid_truncates_toward_zero(self):
        for value,want in ((Fraction(1,32),0),(Fraction(-1,32),0),
                           (Fraction(3,32),Fraction(1,16)),
                           (Fraction(-3,32),Fraction(-1,16))):
            result=window_snap(value)
            self.assertEqual((result.lo,result.hi),(want,want))

    def test_an_interval_inside_one_bin_collapses(self):
        result=window_snap(Interval(Fraction(33,64),Fraction(35,64)))
        self.assertEqual((result.lo,result.hi),(Fraction(1,2),Fraction(1,2)))

    def test_boundary_uncertainty_retains_both_results(self):
        for sign in (-1,1):
            result=window_snap(sign*Interval(Fraction(7,128),Fraction(9,128)))
            self.assertEqual((result.lo,result.hi),
                             (Fraction(-1,16),0) if sign<0 else (0,Fraction(1,16)))

    def test_crossing_zero_remains_zero_inside_first_bins(self):
        result=window_snap(Interval(Fraction(-1,32),Fraction(1,32)))
        self.assertEqual((result.lo,result.hi),(0,0))

    def test_unadmitted_large_window_is_guarded(self):
        with self.assertRaises(AssertionError):window_snap(2**20)


if __name__=='__main__':unittest.main()
