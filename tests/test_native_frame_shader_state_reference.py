"""Authored joint gate catches shader/state failures and inactive FVF fog."""
import copy
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_frame_shader_state_reference import Prepared,controls
from native_frame_state_delivery import expected_delivery
from test_native_frame_shader_reference import fixture as shader_fixture
from test_native_frame_state_delivery import fixture as state_fixture
from test_native_frame_fvf_reference import fixture as fvf_fixture


def program():
    capture,shader=shader_fixture();state,scene,actual=state_fixture()
    capture.update(render=state['render'],texture=state['texture'],viewport=state['viewport'])
    capture['header'][4]=3;capture['streams'][0]+=capture['streams'][0][:16]
    capture['metadata'][0][1]=48
    shader['vertices'].append(copy.deepcopy(shader['vertices'][0]));shader['fog'].append(0)
    scene['header'][3:6]=capture['header'][2:5]
    actual.update(shader)
    return capture,scene,actual


def fixed(rhw=1):
    value,bare=fvf_fixture(True,rhw);capture,scene,actual=state_fixture()
    capture.update(header=[0,2,6,0,1,0,0x144,0],elements=value['elements'],
        metadata=value['metadata'],streams=value['streams'],submission_kind=3,
        viewport=value['draw']['viewport'])
    capture['render'][92]=0;scene['screen']=value['draw']['screen']
    scene['header'][3:6]=capture['header'][2:5]
    scene['before'][0]['shape']=[32,32,87];scene['before'][1]['shape']=[32,32,44]
    actual['vertices']=bare['vertices'];actual['fog']=[float('inf') if rhw==0 else 1]
    for vertex in actual['vertices']:
        for stage in range(1,4):vertex[12+stage*4:16+stage*4]=[.25,.75,0,1]
    methods,targets,pixel=expected_delivery(capture,scene)
    actual['methods']={hex(k):v for k,v in {**methods,**targets,**pixel}.items()}
    actual['submission']='original DrawVerticesUP'
    return capture,scene,actual


class JointShaderState(unittest.TestCase):
    def test_same_actual_draw_qualifies_shader_and_state_components(self):
        cap,scene,actual=program();row=Prepared(cap,scene).qualify(actual,'authored-build')
        self.assertTrue(row['shader_state_components_qualified'])
        self.assertEqual(row['vertices'],3);self.assertEqual(row['vertex_compared_values'],87)
        self.assertEqual(row['fvf_unconsumed_fog_values'],0)
        self.assertFalse(row['complete_scene_pixel_qualified'])

    def test_original_fvf_coordinates_are_requested_for_all_four_stages(self):
        cap,scene,actual=fixed();prepared=Prepared(cap,scene)
        self.assertEqual(prepared.vertices,actual['vertices'])
        row=prepared.qualify(actual)
        self.assertTrue(row['shader_state_components_qualified'])
        self.assertEqual(row['vertex_compared_values'],28)
        self.assertEqual(row['fvf_unconsumed_fog_values'],1)

    def test_disabled_fog_coordinate_can_be_nonfinite_without_becoming_expected_math(self):
        cap,scene,actual=fixed(0);prepared=Prepared(cap,scene)
        self.assertEqual(prepared.vertices[0][3],2**64);self.assertIsNone(prepared.fog)
        self.assertTrue(prepared.qualify(actual)['shader_state_components_qualified'])
        actual['fog']=[float('nan')]
        self.assertTrue(prepared.qualify(actual)['shader_state_components_qualified'])

    def test_fvf_fog_enable_and_unmodelled_texcoord_index_reject(self):
        for name in ('fog','coordinate'):
            cap,scene,actual=fixed()
            if name=='fog':cap['render'][92]=1
            else:cap['texture'][32+28]=1
            with self.subTest(name=name),self.assertRaises((AssertionError,ValueError)):Prepared(cap,scene)

    def test_corrupt_consumed_fvf_coordinate_is_detected(self):
        cap,scene,actual=fixed();actual['vertices'][0][16]=.5
        row=Prepared(cap,scene).qualify(actual)
        self.assertFalse(row['shader_state_components_qualified'])
        self.assertEqual(row['vertex_failure_count'],1)

    def test_refreshed_live_stream_cannot_be_hidden_by_passing_state(self):
        cap,scene,actual=program();cap['streams'][0]=struct.pack('<f',.75)+cap['streams'][0][4:]
        cap['sha256']=actual['vertex_capture_sha256']='refreshed-live-input'
        row=Prepared(cap,scene).qualify(actual)
        self.assertTrue(row['state_word_components_qualified'])
        self.assertFalse(row['shader_state_components_qualified'])

    def test_changed_render_cannot_be_hidden_by_passing_shader(self):
        cap,scene,actual=program();cap['render'][57]^=1
        cap['sha256']=actual['vertex_capture_sha256']='refreshed-render-input'
        row=Prepared(cap,scene).qualify(actual)
        self.assertEqual(row['vertex_failure_count'],0)
        self.assertFalse(row['shader_state_components_qualified'])

    def test_native_outputs_do_not_supply_expected_vertices(self):
        cap,scene,actual=program();cap['outputs']=['changed observed outputs']
        self.assertTrue(Prepared(cap,scene).qualify(actual)['shader_state_components_qualified'])

    def test_missing_or_nonfinite_consumed_outputs_reject(self):
        for name in ('vertex','lane','fog','nonfinite'):
            cap,scene,actual=program()
            if name=='vertex':actual['vertices'].pop()
            elif name=='lane':actual['vertices'][0].pop()
            elif name=='fog':actual['fog'].pop()
            else:actual['vertices'][0][0]=float('inf')
            with self.subTest(name=name),self.assertRaises(ValueError):Prepared(cap,scene).qualify(actual)

    def test_completion_flags_must_be_boolean_and_draws_must_return(self):
        for key in ('complete_draw_executed','original_draw_returned','noise_disabled_fixture','input_only_capture'):
            cap,scene,actual=program();actual[key]=1
            with self.subTest(key=key),self.assertRaises(ValueError):Prepared(cap,scene).qualify(actual)

    def test_controls_cover_both_programmable_and_fvf_routes(self):
        for source,count in ((program,11),(fixed,12)):
            cap,scene,actual=source();rows=controls(cap,scene,actual,'authored-build')
            self.assertEqual(len(rows),count)
            self.assertTrue(all(r['control_passed'] for r in rows))


if __name__=='__main__':unittest.main()
