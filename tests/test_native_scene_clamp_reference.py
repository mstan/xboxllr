"""Fault controls for declared primary clamp order and source-derived codec bounds."""
from fractions import Fraction
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_scene_clamp_reference import clamp_vertices,texture_error_bound,precision_domain,triangle_colors
from native_scene_viewport_reference import clipped_triangles
from test_native_scene_diffuse_reference import methods,vertex


def texture(e0,e1,selector):
    data = struct.pack('<HHI',e0,e1,selector*0x55555555)
    return dict(resource=[0,0,0,12 << 8,0],cpu=data,mips=[dict(shape=[4,4],data=data)])


class ClampControls(unittest.TestCase):
    def test_primary_clamp_is_before_attribute_clipping(self):
        original = [vertex(2,1,1,0),vertex(6,1,3,2),vertex(2,3,1,0)]
        expected,_ = clipped_triangles(clamp_vertices(original),4,4,'fma')
        wrong,_ = clipped_triangles(original,4,4,'fma')
        correct_edge = [v[4] for t in expected for v in t if v[0] == 4]
        wrong_edge = [min(1,max(0,v[4])) for t in wrong for v in t if v[0] == 4]
        self.assertTrue(correct_edge);self.assertEqual(set(correct_edge),{.25})
        self.assertEqual(set(wrong_edge),{.5});self.assertNotEqual(correct_edge,wrong_edge)

    def test_clamp_does_not_mutate_original_shader_output_inputs(self):
        original = [vertex(0,0,r=2)];original[0][5] = -.5
        clamped = clamp_vertices(original)
        self.assertEqual(clamped[0][4:8],[1,0,.25,1])
        self.assertEqual(original[0][4:8],[2,-.5,.25,2])

    def test_opaque_selected_endpoint_error_is_exact(self):
        self.assertEqual(texture_error_bound(texture(3 << 11,0,0)),[Fraction(21,31),0,0])
        self.assertEqual(texture_error_bound(texture(0xffff,0,2)),[0,0,0])

    def test_changed_selector_changes_consumed_codec_precision_contract(self):
        self.assertNotEqual(texture_error_bound(texture(3 << 11,0,0)),
                            texture_error_bound(texture(3 << 11,0,1)))

    def test_precision_guard_uses_input_primary_maxima_and_rejects_unproved_full_diffuse(self):
        errors = texture_error_bound(texture(3 << 11,7 << 11,2))
        self.assertEqual(errors[0],Fraction(35,31))
        self.assertEqual(precision_domain(errors,[vertex(0,0,r=.75)])[0],Fraction(105,124))
        with self.assertRaises(ValueError):precision_domain(errors,clamp_vertices([vertex(0,0,r=2)]))

    def test_transparent_or_incomplete_input_rejects(self):
        with self.assertRaises(ValueError):texture_error_bound(texture(0,0xffff,3))
        t = texture(0xffff,0,2);t['cpu'] = b'changed'
        with self.assertRaises(ValueError):texture_error_bound(t)

    def test_nonfinite_primary_and_nonsaturated_alpha_reject(self):
        for lane,value in ((4,float('nan')),(5,float('inf')),(7,.5),(8,.1)):
            v = vertex(0,0);v[lane] = value
            with self.assertRaises(ValueError):clamp_vertices([v])

    def test_full_saturated_diffuse_still_uses_one_code_expected_rgb(self):
        v = [vertex(0,0,r=2),vertex(4,0,r=2),vertex(0,4,r=2)]
        metrics = {'maximum_consumed_diffuse':0}
        result = triangle_colors(clamp_vertices(v),[(1,1,[[255,255,255]])],methods(),4,{0},metrics)
        self.assertEqual(result,{0:[255,128,64]});self.assertEqual(metrics['maximum_consumed_diffuse'],1)

    def test_interpolate_then_clamp_is_a_detectable_wrong_equation(self):
        v = [vertex(0,0,r=2),vertex(4,0,r=0),vertex(0,4,r=0)]
        result = triangle_colors(clamp_vertices(v),[(1,1,[[255,255,255]])],methods(),4,{0},
                                 {'maximum_consumed_diffuse':0})
        self.assertEqual(result[0][0],191)  # Per-vertex clamps give (3/4)*1.
        wrong = min(255,int(255*(2*Fraction(3,4))+Fraction(1,2)))
        self.assertEqual(wrong,255);self.assertGreater(abs(result[0][0]-wrong),1)


if __name__ == '__main__':unittest.main()
