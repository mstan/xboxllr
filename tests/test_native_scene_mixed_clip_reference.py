"""Authored W/XY clip-order and homogeneous-attribute numeric fault controls."""
from fractions import Fraction
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_scene_mixed_clip_reference import (reconstruct,distance,clip_plane,
    project_unsnapped,mixed_triangles,rounded,expected_empty_mixed_draw)
from native_scene_rejection_reference import W_EPSILON
from test_native_scene_diffuse_reference import vertex,methods
from test_native_scene_clamp_reference import texture
from test_native_frame_target_reference import target


class MixedClipControls(unittest.TestCase):
    def test_w_plane_uses_positive_threshold_before_xy(self):
        v = [Fraction(0),Fraction(0),Fraction(3),Fraction(0)]+[Fraction(0)]*24
        self.assertEqual(distance(v,0),-W_EPSILON)
        self.assertEqual(distance(v,1),0)
        self.assertEqual(clip_plane([v,v,v],0)[0],[])

    def test_mixed_triangle_is_not_assumed_empty(self):
        v = [vertex(4,2,-1),vertex(0,0,1),vertex(4,0,1)]
        leaves,stats = mixed_triangles(v,4,4)
        self.assertTrue(leaves);self.assertGreater(stats['homogeneous_plane_intersections'],0)
        self.assertTrue(all(p[3] >= float(W_EPSILON) for t in leaves for p in t))

    def test_z_is_homogeneous_not_divided_window_interpolation(self):
        a = vertex(0,0,-1);a[2] = 4
        b = vertex(2,0,1);b[2] = 10
        polygon,_ = clip_plane([reconstruct(a,4,4),reconstruct(b,4,4)],0)
        crossing = [v for v in polygon if v[3] != 1]
        self.assertTrue(crossing)
        # Homogeneous Z endpoints are -4 and10; the crossing is near3,
        # whereas interpolating divided window Z would be near7.
        self.assertTrue(all(abs(float(v[2])-3) < .001 for v in crossing))
        self.assertTrue(all(float(v[2]/v[3]) > 1000000 for v in crossing))

    def test_projection_does_not_snap_before_recursive_classification(self):
        h = [Fraction(1,2),Fraction(0),Fraction(3),Fraction(1)]+[Fraction(0)]*24
        p = project_unsnapped(h,5,4)
        self.assertEqual(p[0],3.75)
        h[0] = Fraction(3,8)
        p = project_unsnapped(h,5,4)
        self.assertEqual(p[0],3.4375)
        h[0] = Fraction(1,3);p = project_unsnapped(h,5,4)
        self.assertNotEqual(p[0]*16,round(p[0]*16))

    def test_original_near_zero_w_and_nonfinite_inputs_remain_guarded(self):
        for value in (0,.00001,float('nan')):
            v = [vertex(4,2,-1),vertex(0,0,1),vertex(4,0,value)]
            with self.assertRaises(ValueError):mixed_triangles(v,4,4)

    def test_attribute_intersection_is_fma_after_rounded_difference(self):
        # Independent normal binary32 inputs expose double rounding by one
        # ulp; no observed renderer vertex selects either answer.
        a=Fraction(5663897,33554432);b=Fraction(-418201,262144);t=Fraction(3504007,8388608)
        correct=rounded(a+rounded(b-a)*t)
        wrong=rounded(a+rounded(rounded(b-a)*t))
        self.assertEqual(correct,Fraction(-9530941,16777216))
        self.assertEqual(wrong,Fraction(-4765471,8388608));self.assertNotEqual(correct,wrong)

    def test_mixed_rejection_has_explicit_negative_plane_proof(self):
        v = [vertex(4,2,-1),vertex(-4,0,1),vertex(-4,4,1)]
        leaves,stats = mixed_triangles(v,4,4)
        self.assertEqual(leaves,[])
        stages=stats['clip_stage_proofs']
        self.assertGreater(stages[0]['after_vertices'],0)  # W clipping alone does not reject it.
        self.assertEqual(stages[1]['plane'],'X+W');self.assertEqual(stages[1]['after_vertices'],0)
        self.assertLess(stages[1]['maximum_distance'],0);self.assertEqual(stages[1]['intersections'],0)

    def test_surviving_mixed_geometry_stays_outside_empty_draw_contract(self):
        v = [vertex(4,2,-1),vertex(0,0,1),vertex(4,0,1)]
        with self.assertRaisesRegex(ValueError,'Surviving mixed-W'):
            expected_empty_mixed_draw({}, {'before':[{'shape':[4,4,87]}]},v,[0]*3,{}, {},'fma-b-first','fma')

    def test_whole_empty_mixed_draw_preserves_independent_predecessors(self):
        v=[vertex(16,2,-1),vertex(-16,0,1),vertex(-16,4,1)]
        scene={'before':[{'shape':[16,1,87]},{'shape':[16,1,44]}],
               'textures':[texture(0xffff,0,2)],'frame_resource_plan':{'target_ids':[1,2]}}
        targets={1:target(1,width=16,height=1),2:target(2,1,width=16,height=1,fmt=44)}
        before={1:bytes(range(64)),2:bytes(reversed(range(64))),3:b'independent other root'}
        m=methods();m.update({0x38c:0x1b02,0x390:0x1b02,0x308:1,0x39c:0x404})
        with patch('native_scene_mixed_clip_reference.expected_delivery',return_value=(m,{},{})):
            storage,writes,stats=expected_empty_mixed_draw({'header':[0,0,6]},scene,v,[0]*3,
                                                         targets,before,'fma-b-first','fma')
        self.assertEqual(storage,before);self.assertIsNot(storage,before)
        self.assertFalse(writes);self.assertTrue(stats['mixed_clip_all_primitives_empty'])


if __name__ == '__main__':unittest.main()
