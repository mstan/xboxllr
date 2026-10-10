"""Fault controls for declared primary clamp order and source-derived codec bounds."""
from fractions import Fraction
from pathlib import Path
import copy
import struct
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_scene_clamp_reference import clamp_vertices,texture_error_bound,precision_domain,triangle_colors,expected_clamp_draw
from native_scene_viewport_reference import clipped_triangles
from test_native_scene_diffuse_reference import methods,vertex
from test_native_frame_target_reference import target


def texture(e0,e1,selector):
    data = struct.pack('<HHI',e0,e1,selector*0x55555555)
    return dict(resource=[0,0,0,12 << 8,0],cpu=data,mips=[dict(shape=[4,4],data=data)])


def empty_fixture():
    # All Y are strictly above the viewport. X clipping creates two vertices,
    # then Y+W removes the complete polygon; no face or sample remains.
    vertices = [vertex(-4,-4),vertex(20,-4),vertex(0,-2)]
    for v in vertices:v[2] = 3.;v[4:7] = [1.,1.,1.]
    capture = {'header':[0,0,6,0,3,0,0,0],'sha256':'authored-capture'}
    scene = {'header':[0]*10,'sha256':'authored-scene',
        'before':[{'shape':[16,4,87]},{'shape':[16,4,44]}],
        'textures':[texture((31<<11)|(13<<5),15<<5,3),None,None,None],
        'frame_resource_plan':{'target_ids':[1,2]}}
    scene['header'][5] = 3
    targets = {1:target(1,width=16,height=4),2:target(2,1,width=16,height=4,fmt=44),
               3:target(3,width=16,height=1,fmt=28)}
    before = {1:struct.pack('<I',0x7a010203)*64,2:struct.pack('<I',0x00ffff5b)*64,
              3:bytes(range(64))}
    m = methods()
    m.update({0x208:0x128,0x304:1,0x308:1,0x30c:1,0x318:0,0x31c:0,
              0x354:0x201,0x35c:1,0x37c:0x1d01,0x380:29,0x38c:0x1b02,
              0x390:0x1b02,0x3a0:0x900,0x394:0,0x398:0x4b7fffff,0x39c:0x405,
              0x344:0x302,0x348:0x303,0x350:0x8006,0x1b08:0x10101,
              0x1b48:0x10101,0x1b88:0x10101,0x1bc8:0x10101,
              0x200:16<<16,0x204:4<<16,0x2c0:15<<16,0x2e0:3<<16,0x20c:64*0x10001})
    m.update({word:0 for word in (0x320,0x324,0x328,0x32c,0x330,0x334,0x338)})
    return capture,scene,vertices,[0.]*3,targets,before,m


def expected_empty_fixture(data):
    capture,scene,vertices,fog,targets,before,m = data
    with patch('native_scene_clamp_reference.expected_delivery',return_value=(m,{},{})):
        return expected_clamp_draw(capture,scene,vertices,fog,targets,before,'fma-b-first','fma')


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

    def test_exact_green_64_over_63_error_is_unconsumed_after_complete_empty_clip(self):
        data = empty_fixture();unchanged = copy.deepcopy(data)
        # MSB endpoint expansion gives52 and60; nearest third is57.
        # Ideal565 scaling gives255*(13+2*15)/(3*63)=3655/63.
        self.assertEqual(Fraction(3655,63)-57,Fraction(64,63))
        self.assertEqual(texture_error_bound(data[1]['textures'][0]),[0,Fraction(64,63),0])
        result,writes,row = expected_empty_fixture(data)
        self.assertEqual(result,data[5]);self.assertIsNot(result,data[5]);self.assertFalse(writes)
        self.assertEqual(data,unchanged)
        self.assertEqual(row['expanded_triangles'],0);self.assertEqual(row['source_triangles'],1)
        self.assertEqual(row['input_prequantization_codec_error_codes'],['0','64/63','0'])
        self.assertTrue(row['codec_precision_unconsumed_because_no_raster_leaves'])
        for key in ('tested_fragments','expected_written_pixels','maximum_consumed_diffuse'):
            self.assertEqual(row[key],0)

    def test_changed_to_visible_geometry_cannot_skip_the_same_failed_precision_bound(self):
        data = empty_fixture()
        for v,xy in zip(data[2],((0,0),(0,4),(16,0))):v[:2] = xy
        with self.assertRaisesRegex(ValueError,'cannot prove one-code bound'):
            expected_empty_fixture(data)

    def test_empty_precision_path_still_inspects_complete_encoded_texture(self):
        for fault in ('wrong-cpu','short-mip','transparent-selector','missing-format'):
            with self.subTest(fault=fault):
                data = empty_fixture();t = data[1]['textures'][0]
                if fault == 'wrong-cpu':t['cpu'] = b'changed'
                elif fault == 'short-mip':t['mips'][0]['data'] = t['cpu'] = t['cpu'][:-1]
                elif fault == 'transparent-selector':data[1]['textures'][0] = texture(0,0xffff,3)
                else:t['resource'][3] = 0
                with self.assertRaises(ValueError):expected_empty_fixture(data)

    def test_empty_precision_path_requires_every_complete_independently_owned_root(self):
        for target_id in (1,2,3):
            for fault in ('missing','short'):
                with self.subTest(target=target_id,fault=fault):
                    data = empty_fixture()
                    if fault == 'missing':data[5].pop(target_id)
                    else:data[5][target_id] = data[5][target_id][:-1]
                    with self.assertRaisesRegex(ValueError,'independently owned producer'):
                        expected_empty_fixture(data)
        data = empty_fixture();data[5][4] = b'undeclared root'
        with self.assertRaisesRegex(ValueError,'independently owned producer'):
            expected_empty_fixture(data)

    def test_empty_precision_path_retains_original_counts_and_finite_fog(self):
        for fault in ('capture-count','scene-count','fog-count','nonfinite-fog'):
            with self.subTest(fault=fault):
                data = empty_fixture()
                if fault == 'capture-count':data[0]['header'][4] = 4
                elif fault == 'scene-count':data[1]['header'][5] = 4
                elif fault == 'fog-count':data[3].pop()
                else:data[3][0] = float('nan')
                with self.assertRaisesRegex(ValueError,'vertex/fog count'):
                    expected_empty_fixture(data)

    def test_empty_precision_path_does_not_bypass_raster_or_consumed_profile_guards(self):
        for method in (0x208,0x308,0x354,0x358,0x39c,0x38c,0x1b08,0x1b14,0x200,0x2c0,0x9c0,0xac0):
            with self.subTest(method=hex(method)):
                data = empty_fixture();data[6][method] ^= 1
                if method == 0x39c:data[6][method] = 0x406
                if method == 0x1b14:data[6][method] = 0x03060000
                with self.assertRaises((ValueError,AssertionError)):expected_empty_fixture(data)
        for width in (True,-1,512):
            data = empty_fixture();data[6][0x380] = width
            with self.assertRaisesRegex(ValueError,'line width'):expected_empty_fixture(data)

    def test_empty_precision_path_keeps_strip_texture_and_active_target_contracts(self):
        for fault in ('primitive','texture-stage','scene-flag','target-role','target-pair','target-shape'):
            with self.subTest(fault=fault):
                data = empty_fixture()
                if fault == 'primitive':data[0]['header'][2] = 5
                elif fault == 'texture-stage':data[1]['textures'][1] = data[1]['textures'][0]
                elif fault == 'scene-flag':data[1]['header'][8] = 1
                elif fault == 'target-role':data[4][1]['role'] = 1
                elif fault == 'target-pair':data[1]['frame_resource_plan']['target_ids'] = [1,1]
                else:data[1]['before'][1]['shape'][0] = 15
                with self.assertRaises((ValueError,AssertionError)):expected_empty_fixture(data)


if __name__ == '__main__':unittest.main()
