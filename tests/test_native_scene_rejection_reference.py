"""Fault controls for input-only whole-primitive behind-camera rejection."""
import copy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_scene_rejection_reference import behind_camera_proof,expected_rejection_draw
from native_scene_viewport_reference import clipped_triangles
from test_native_scene_diffuse_reference import vertex,methods
from test_native_scene_clamp_reference import texture
from test_native_frame_target_reference import target


class RejectionControls(unittest.TestCase):
    def test_every_whole_strip_primitive_has_strictly_negative_convex_w_hull(self):
        v = [vertex(0,0,-1),vertex(4,0,-2),vertex(0,4,-3),vertex(4,4,-4)]
        proof = behind_camera_proof(v)
        self.assertEqual([p['indices'] for p in proof['primitives']],[[0,1,2],[2,1,3]])
        self.assertEqual(proof['rejected_whole_primitives'],2)
        self.assertTrue(all(p['maximum_w'] < 0 and p['maximum_software_plane_distance'] < 0 and
            p['classification_transitions'] == p['expected_clipped_vertices'] == 0 for p in proof['primitives']))

    def test_mixed_w_and_positive_vertex_cannot_be_called_wholly_rejected(self):
        for w in (1,.00001,0,-0.):
            v = [vertex(0,0,-1),vertex(4,0,-2),vertex(0,4,w)]
            with self.assertRaises(ValueError):behind_camera_proof(v)

    def test_later_positive_vertex_prevents_whole_strip_early_out(self):
        v = [vertex(0,0,-1)]*3+[vertex(4,4,1)]
        with self.assertRaises(ValueError):behind_camera_proof(v)

    def test_rejection_precedes_irrelevant_window_depth_and_xy(self):
        v = [vertex(-2000,1900,-1),vertex(7000,-6000,-2),vertex(0,0,-3)]
        for i,p in enumerate(v):p[2] = 18000000+i
        self.assertTrue(behind_camera_proof(v)['every_whole_primitive_rejected'])
        with self.assertRaises(ValueError):clipped_triangles(v,640,480,'fma')

    def test_positive_w_surviving_depth_and_mixed_w_guards_remain(self):
        v = [vertex(0,0),vertex(4,0),vertex(0,4)]
        v[0][2] = 18000000
        with self.assertRaises(ValueError):clipped_triangles(v,4,4,'fma')
        v[0][2] = 3;v[1][3] = -1
        with self.assertRaises(ValueError):clipped_triangles(v,4,4,'fma')

    def test_nonfinite_or_incomplete_geometry_is_not_rejection_proof(self):
        for lane,value in ((0,float('nan')),(2,float('inf')),(3,float('-inf'))):
            v = [vertex(0,0,-1),vertex(4,0,-2),vertex(0,4,-3)];v[0][lane] = value
            with self.assertRaises(ValueError):behind_camera_proof(v)
        with self.assertRaises(ValueError):behind_camera_proof([vertex(0,0,-1)]*2)

    def test_no_clamp_projection_or_observed_pixel_can_supply_rejected_expectations(self):
        vertices = [vertex(0,0,-1),vertex(4,0,-2),vertex(0,4,-3)]
        for v in vertices:v[2] = 18000000
        original = copy.deepcopy(vertices)
        capture = {'header':[0,0,6]}
        scene = {'textures':[texture(0xffff,0,2)],'frame_resource_plan':{'target_ids':[1,2]},
                 'before':[{'shape':[16,1,87]},{'shape':[16,1,44]}]}
        targets = {1:target(1,width=16,height=1),2:target(2,1,width=16,height=1,fmt=44)}
        before = {1:bytes(range(64)),2:bytes(reversed(range(64))),3:b'other owned root'}
        m = methods();m.update({0x38c:0x1b02,0x390:0x1b02,0x308:1,0x39c:0x404})
        with patch('native_scene_rejection_reference.expected_delivery',return_value=(m,{},{})), \
             patch('native_scene_rejection_reference.expected_clamp_draw',side_effect=AssertionError('raster reached')):
            storage,writes,stats = expected_rejection_draw(capture,scene,vertices,[-.1,-.2,-.3],targets,before,
                                                         'fma-b-first','fma')
        self.assertEqual(storage,before);self.assertIsNot(storage,before);self.assertFalse(writes)
        self.assertEqual(stats['expected_written_pixels'],0);self.assertEqual(vertices,original)


if __name__ == '__main__':unittest.main()
