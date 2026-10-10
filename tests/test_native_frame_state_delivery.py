"""Authored API expectations and semantic/provenance failures, without a GPU."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_frame_state_delivery import default_final,expected_delivery,qualify,controls
from native_vertex_fetch_reference import XBE_SHA


def fixture(custom=False):
    render=[0]*166
    for index,value in {57:0x201,59:1,62:0x302,63:0x303,64:1,66:0x1d01,
        67:0x10101,74:0x8006,82:1,92:1,93:1,103:1,138:0xff123456,
        139:0x1b02,140:0x1b02,143:1,146:0x900,147:0x901}.items():render[index]=value
    texture=[]
    for stage in range(4):
        states=[0]*32;states[:9]=[1,1,1,2,2,2,0xbf99999a,0,1]
        states[12:22]=[4 if stage==0 else 1,1,2,0,4,1,2,0,1,0]
        texture.extend(states)
    capture=dict(header=[0,2,6,0,4,0,1,0],render=render,texture=texture,
        viewport=[0,0,2,2,0,0x3f800000],outputs=None,sha256='authored',
        input_sequence=1,submission_kind=0)
    scene=dict(input_only=True,after=None,header=[0,2,1,6,0,4,0,0,int(custom),0x4b7fffff],
        before=[dict(shape=[2,2,87],data=bytes(16)),dict(shape=[4,2,44],data=bytes(32))],
        textures=[dict(resource=[1,0,0,0x01110c21,0],cpu=bytes(8),mips=[dict(data=bytes(8))])]+[None]*3,
        pixel=[0]*60 if custom else None,sha256='scene-authored')
    if custom:
        render[0]=0x18200000;render[10]=0xff804020;render[26]=0xc00
        render[34]=0x08200000;render[45]=0xc00;render[53]=1;render[54]=1
    methods,targets,pixel=expected_delivery(capture,scene)
    counts=[0]*26;counts[1]=1
    observation=dict(vertex_capture_sha256='authored',scene_capture_sha256='scene-authored',
        input_only_capture=True,input_sequence=1,xbe_sha256=XBE_SHA,library_sha256='authored-build',
        noise_disabled_fixture=True,complete_draw_executed=True,original_draw_returned=True,
        pixel_targets_observed=False,fifo_get=32,fifo_put=32,vertices=[[0]*28]*4,
        submission='original DrawVertices',draw_counters=counts,
        methods={hex(k):v for k,v in {**methods,**targets,**pixel}.items()})
    return capture,scene,observation


class StateDelivery(unittest.TestCase):
    def test_sampler_packing_cull_fog_and_target_roles(self):
        capture,scene,_=fixture();methods,targets,_=expected_delivery(capture,scene)
        self.assertEqual(methods[0x1b14],0x02063ece)
        self.assertEqual(methods[0x1b08],0x010101)
        self.assertEqual(methods[0x1b0c],0x4003ffc0)
        self.assertEqual(methods[0x1b4c],0x3ffc0)
        self.assertEqual(methods[0x39c],0x405)
        self.assertEqual(methods[0x2a8],0x563412)
        self.assertEqual(targets[0x20c],0x00100008)

    def test_disabled_modes_do_not_claim_inactive_registers(self):
        capture,scene,_=fixture();capture['render'][92]=capture['render'][147]=0
        methods,_,_=expected_delivery(capture,scene)
        self.assertEqual(methods[0x308],0);self.assertEqual(methods[0x2a4],0)
        self.assertNotIn(0x39c,methods);self.assertNotIn(0x29c,methods)
        self.assertNotIn(0x9c0,methods)

    def test_final_combiner_is_derived_from_fog_and_specular(self):
        for fog,specular,rgb in ((0,0,12),(0,1,14),(1,0,0x130c0300),(1,1,0x130e0300)):
            with self.subTest(fog=fog,specular=specular):
                render=[0]*166;render[92]=fog;render[103]=specular
                self.assertEqual(default_final(render),(rgb,0x1c80))

    def test_custom_mapped_constants_use_active_api_cache(self):
        capture,scene,_=fixture(True);scene['pixel'][10]=0x12345678
        methods,_,pixel=expected_delivery(capture,scene)
        self.assertEqual(pixel[0xa60],0xff804020)
        self.assertEqual(pixel[0x288],0x130e0300)
        self.assertEqual(methods[0x288],pixel[0x288])

    def test_explicit_final_definition_prevents_default_substitution(self):
        capture,scene,_=fixture(True);scene['pixel'][8]=capture['render'][8]=0xc
        capture['render'][9]=0x1c00
        _,_,pixel=expected_delivery(capture,scene)
        self.assertEqual((pixel[0x288],pixel[0x28c]),(0xc,0x1c00))

    def test_linear_textures_derive_header_pitch_and_live_storage_offsets(self):
        capture,scene,_=fixture();texture=scene['textures'][0]
        texture['resource'][3:]=[0x00011229,1|(1<<12)]
        texture['cpu']=bytes(128);scene['textures'][1]=copy.deepcopy(texture)
        capture['texture'][32+12]=capture['texture'][32+16]=2
        methods,_,_=expected_delivery(capture,scene)
        self.assertEqual(methods[0x1b10],64<<16);self.assertEqual(methods[0x1b1c],0x20002)
        self.assertEqual(methods[0x1b40],0x3010000)
        self.assertEqual(methods[0x1b4c],0x4003ffc0)
        texture['cpu']=bytes(71)
        with self.assertRaisesRegex(ValueError,'Incomplete pitch-linear'):expected_delivery(capture,scene)

    def test_unknown_views_formats_fog_and_coordinates_stay_guarded(self):
        for name in ('format','fog','coordinate','viewport'):
            capture,scene,_=fixture()
            if name=='format':scene['textures'][0]['resource'][3]^=0x100
            elif name=='fog':capture['render'][97]=0x3f800000
            elif name=='coordinate':capture['texture'][28]=1
            else:capture['viewport'][4]=1
            with self.subTest(name=name),self.assertRaises(ValueError):expected_delivery(capture,scene)

    def test_component_pass_never_qualifies_full_state_or_pixels(self):
        capture,scene,observation=fixture();row=qualify(capture,scene,observation,'authored-build')
        self.assertTrue(row['state_word_components_qualified'])
        self.assertFalse(row['captured_input_to_full_state_qualified'])
        self.assertFalse(row['complete_scene_pixel_qualified'])

    def test_refreshed_live_api_and_observation_corruption_are_detected(self):
        capture,scene,observation=fixture(True)
        rows=controls(capture,scene,observation,'authored-build')
        self.assertEqual(len(rows),15);self.assertTrue(all(row['control_passed'] for row in rows))

    def test_missing_draw_wrong_provenance_and_bool_fifo_reject(self):
        for key,value in (('complete_draw_executed',False),('original_draw_returned',False),
                ('vertex_capture_sha256','another'),('input_sequence',True),('fifo_get',True)):
            capture,scene,observation=fixture();observation[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):qualify(capture,scene,observation)

    def test_quads_require_returned_submission_and_exact_vertex_count(self):
        capture,scene,observation=fixture();capture['header'][2]=scene['header'][3]=8
        observation['draw_counters']=[0]*26
        with self.assertRaisesRegex(ValueError,'Guarded or missing draw'):qualify(capture,scene,observation)
        observation['draw_counters'][1]=1
        self.assertTrue(qualify(capture,scene,observation)['state_word_components_qualified'])
        observation['vertices'].pop()
        with self.assertRaisesRegex(ValueError,'Incomplete original'):qualify(capture,scene,observation)

    def test_current_stencil_skip_census_is_complete_and_checked(self):
        for count in (26,28):
            capture,scene,observation=fixture();observation['draw_counters']=[0]*count
            observation['draw_counters'][1]=1
            self.assertTrue(qualify(capture,scene,observation)['state_word_components_qualified'])
            observation['draw_counters'][-1]=1
            with self.assertRaisesRegex(ValueError,'Guarded or missing draw'):qualify(capture,scene,observation)
        for count in (25,27,29):
            capture,scene,observation=fixture();observation['draw_counters']=[0]*count
            observation['draw_counters'][1]=1
            with self.assertRaisesRegex(ValueError,'Guarded or missing draw'):qualify(capture,scene,observation)


if __name__=='__main__':unittest.main()
