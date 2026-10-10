"""Fault controls for positive-W depth-domain empty whole-draw admission."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_scene_empty_depth_reference import empty_positive_depth_proof,expected_empty_depth_draw
from test_native_scene_diffuse_reference import vertex,methods
from test_native_scene_clamp_reference import empty_fixture
from test_native_frame_target_reference import target


class EmptyDepthControls(unittest.TestCase):
    def outside(self):
        v=[vertex(-4,0,1),vertex(-4,4,1),vertex(-2,0,2)];v[0][2]=-3
        return v

    def test_negative_vertex_depth_alone_does_not_prove_rejection(self):
        v=[vertex(0,0,1),vertex(4,0,1),vertex(0,4,1)];v[0][2]=-3
        with self.assertRaisesRegex(ValueError,'Surviving out-of-range-depth'):
            empty_positive_depth_proof(v,4,4)

    def test_above_max_depth_alone_does_not_prove_rejection(self):
        v=[vertex(0,0,1),vertex(4,0,1),vertex(0,4,1)];v[0][2]=16777216
        with self.assertRaisesRegex(ValueError,'Surviving out-of-range-depth'):
            empty_positive_depth_proof(v,4,4)

    def test_complete_xy_empty_result_precedes_depth_arithmetic(self):
        p=empty_positive_depth_proof(self.outside(),4,4)
        self.assertTrue(p['complete_clip_results_derived']);self.assertEqual(p['expanded_triangles'],0)
        x=[s for s in p['clip_stage_proofs'] if s['plane']=='X+W'][0]
        self.assertLess(x['maximum_distance'],0);self.assertEqual(x['after_vertices'],0)

    def test_changed_xy_cannot_retain_wrong_whole_draw_emptiness(self):
        original=self.outside();empty_positive_depth_proof(original,4,4)
        original[2][0]=2;original[2][1]=2
        with self.assertRaisesRegex(ValueError,'Surviving out-of-range-depth'):
            empty_positive_depth_proof(original,4,4)

    def test_near_zero_negative_w_and_nonfinite_remain_guarded(self):
        for lane,value in ((3,0),(3,.00001),(3,-1),(2,float('nan')),(0,float('inf'))):
            v=self.outside();v[0][lane]=value
            with self.assertRaises(ValueError):empty_positive_depth_proof(v,4,4)

    def test_whole_empty_depth_domain_preserves_all_expected_owned_roots(self):
        capture,scene,_,_,targets,before,m=empty_fixture();v=self.outside()
        with patch('native_scene_empty_depth_reference.expected_delivery',return_value=(m,{},{})):
            storage,writes,row=expected_empty_depth_draw(capture,scene,v,[0]*3,targets,before,'fma-b-first','fma')
        self.assertEqual(storage,before);self.assertIsNot(storage,before);self.assertFalse(writes)
        self.assertTrue(row['positive_w_depth_domain_all_primitives_empty'])


if __name__ == '__main__':unittest.main()
