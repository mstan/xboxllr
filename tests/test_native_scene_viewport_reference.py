"""Authored geometric/numeric controls for homogeneous viewport clipping."""
from fractions import Fraction
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_scene_viewport_reference import clipped_triangles,homogeneous,clip_polygon,project,snap,lerp


def vertex(x,y,z,w,u=0):
    return [x,y,z,w,.25,.25,.25,1,0,0,0,0,u,0,0,1]+[0]*12


class ViewportControls(unittest.TestCase):
    def test_mixed_w_intersection_preserves_window_depth_plane(self):
        original=[vertex(2,1,0,1),vertex(6,1,100,3,1),vertex(2,3,0,1)]
        polygon=clip_polygon([homogeneous(v,4,4) for v in original],0,-1)
        clipped=[project(v,4,4) for v in polygon]
        edge=[v for v in clipped if v[0]==4]
        self.assertEqual(len(edge),2)
        for v in edge:
            self.assertEqual(v[2],50)
            self.assertEqual(v[3],1.5)
            self.assertEqual(v[12],.25)
            self.assertNotEqual(v[2],25)  # Divided Z lerp by homogeneous t is wrong.

    def test_right_clipped_triangle_becomes_two_fan_triangles(self):
        triangles,stats=clipped_triangles([vertex(2,1,0,1),vertex(6,1,100,3),vertex(2,3,0,1)],4,4,'fma')
        self.assertEqual(len(triangles),2)
        self.assertEqual(stats['clipped_source_triangles'],1)
        self.assertTrue(all(0<=v[0]<=4 and 0<=v[1]<=4 for t in triangles for v in t))

    def test_whole_inside_is_bit_preserved(self):
        v=[vertex(0,0,3,2),vertex(4,0,7,4),vertex(0,4,9,8)]
        triangles,stats=clipped_triangles(v,4,4,'fma')
        self.assertEqual(triangles,[v]);self.assertEqual(stats['clipped_source_triangles'],0)

    def test_fully_outside_is_empty(self):
        triangles,_=clipped_triangles([vertex(5,1,3,2),vertex(6,1,7,4),vertex(5,3,9,8)],4,4,'fma')
        self.assertEqual(triangles,[])

    def test_top_left_and_bottom_clipping_stay_in_extent(self):
        for original in ([vertex(-2,2,3,2),vertex(2,-2,7,4),vertex(2,2,9,8)],
                         [vertex(2,2,3,2),vertex(2,6,7,4),vertex(6,2,9,8)]):
            triangles,_=clipped_triangles(original,4,4,'fma')
            self.assertTrue(triangles)
            self.assertTrue(all(0<=v[0]<=4 and 0<=v[1]<=4 for t in triangles for v in t))

    def test_half_subpixel_ties_are_away_from_zero(self):
        self.assertEqual(snap(Fraction(1,32)),Fraction(1,16))
        self.assertEqual(snap(Fraction(-1,32)),Fraction(-1,16))
        self.assertEqual(snap(Fraction(3,32)),Fraction(1,8))

    def test_rounded_difference_then_fused_lerp_has_explicit_result(self):
        self.assertEqual(lerp(1+Fraction(1,2**23),-1,Fraction(1,2)),Fraction(1,2**23))

    def test_near_w_and_depth_planes_remain_guarded(self):
        for bad in (vertex(2,1,3,0),vertex(2,1,3,.00001),vertex(2,1,-1,1),vertex(2,1,16777216,1)):
            with self.assertRaises(ValueError):clipped_triangles([bad,vertex(3,1,3,1),vertex(2,3,4,1)],4,4,'fma')

    def test_arithmetic_must_be_declared(self):
        with self.assertRaises(ValueError):clipped_triangles([vertex(0,0,0,1)]*3,4,4,'selected-by-pixels')


if __name__=='__main__':unittest.main()
