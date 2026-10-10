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
from native_scene_clamp_reference import texture_error_bound,precision_domain,clamp_vertices


class MixedClipControls(unittest.TestCase):
    def unused_palette_inputs(self):
        vertices=[vertex(16,2,-1),vertex(-16,0,1),vertex(-16,4,1)]
        for v in vertices:v[4:7]=[1.,1.,1.]
        scene={'before':[{'shape':[16,1,87]},{'shape':[16,1,44]}],
               'textures':[texture((31 << 11)|(13 << 5),15 << 5,3)],
               'frame_resource_plan':{'target_ids':[1,2]}}
        targets={1:target(1,width=16,height=1),2:target(2,1,width=16,height=1,fmt=44)}
        before={1:bytes(range(64)),2:bytes(reversed(range(64))),3:b'independent other root'}
        m=methods();m.update({0x38c:0x1b02,0x390:0x1b02,0x308:1,0x39c:0x404})
        return vertices,scene,targets,before,m

    def test_unused_palette_error_cannot_reject_a_proven_empty_draw(self):
        v,scene,targets,before,m=self.unused_palette_inputs()
        # Exact rational green: (255*13/63+2*255*15/63)/3 =3655/63.
        # The maintained palette's selected green is57, giving64/63 codes.
        errors=texture_error_bound(scene['textures'][0])
        self.assertEqual(errors,[Fraction(0),Fraction(64,63),Fraction(0)])
        with self.assertRaisesRegex(ValueError,'one-code bound'):
            precision_domain(errors,clamp_vertices(v))
        with patch('native_scene_mixed_clip_reference.expected_delivery',return_value=(m,{},{})):
            storage,writes,row=expected_empty_mixed_draw({'header':[0,0,6]},scene,v,[0]*3,
                                                       targets,before,'fma-b-first','fma')
        self.assertEqual(storage,before);self.assertIsNot(storage,before);self.assertFalse(writes)
        self.assertEqual(row['expanded_triangles'],0);self.assertEqual(row['tested_fragments'],0)
        self.assertEqual(row['input_prequantization_codec_error_codes'],['0','64/63','0'])
        self.assertTrue(row['codec_precision_unconsumed_because_no_raster_leaves'])

    def test_unused_palette_exception_cannot_admit_surviving_geometry(self):
        v,scene,targets,before,m=self.unused_palette_inputs()
        v=[vertex(16,2,-1),vertex(0,0,1),vertex(16,0,1)]
        for p in v:p[4:7]=[1.,1.,1.]
        with patch('native_scene_mixed_clip_reference.expected_delivery',
                   side_effect=AssertionError('state reached before emptiness proof')):
            with self.assertRaisesRegex(ValueError,'Surviving mixed-W'):
                expected_empty_mixed_draw({'header':[0,0,6]},scene,v,[0]*3,
                                         targets,before,'fma-b-first','fma')
        with self.assertRaisesRegex(ValueError,'one-code bound'):
            precision_domain(texture_error_bound(scene['textures'][0]),clamp_vertices(v))

    def test_empty_draw_still_requires_complete_encoded_texture(self):
        v,scene,targets,before,m=self.unused_palette_inputs()
        scene['textures'][0]['cpu']=b'incomplete'
        with patch('native_scene_mixed_clip_reference.expected_delivery',return_value=(m,{},{})):
            with self.assertRaisesRegex(ValueError,'complete supplied BC1'):
                expected_empty_mixed_draw({'header':[0,0,6]},scene,v,[0]*3,
                                         targets,before,'fma-b-first','fma')

    def test_empty_draw_still_requires_complete_owned_target_storage(self):
        v,scene,targets,before,m=self.unused_palette_inputs();before[2]=before[2][:-4]
        with patch('native_scene_mixed_clip_reference.expected_delivery',return_value=(m,{},{})):
            with self.assertRaisesRegex(ValueError,'target representation'):
                expected_empty_mixed_draw({'header':[0,0,6]},scene,v,[0]*3,
                                         targets,before,'fma-b-first','fma')

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
