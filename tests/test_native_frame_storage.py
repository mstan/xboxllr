"""Authored producer mapping and live-context state without a GPU."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_frame_storage import draw_plan,validate
from native_frame_target_reference import placement
from native_frame_state_delivery import expected_delivery,qualify
from test_native_frame_state_delivery import fixture as state_fixture
from test_native_frame_target_reference import target


def fixture():
    capture,scene,actual=state_fixture();targets={1:target(1),2:target(2,1,width=4,height=2,pitch=128),3:target(3)}
    scene['color_header']=targets[1]['resource'];scene['depth_header']=targets[2]['resource']
    capture['render'][157]=0x3f800000
    draw=dict(target=1,depth=2)
    return capture,scene,actual,targets,draw


def observe(capture,scene,actual,plan):
    scene['frame_resource_plan']=plan;actual['frame_resource_plan']=copy.deepcopy(plan)
    m,f,p=expected_delivery(capture,scene);actual['methods']={hex(k):v for k,v in {**m,**f,**p}.items()}
    return actual


class FrameStorage(unittest.TestCase):
    def test_cpu_textures_are_separate_from_target_storage(self):
        _,scene,_,targets,draw=fixture();p=draw_plan(targets,draw,scene)
        self.assertEqual(p['texture_addresses'],[0x3000000,None,None,None]);self.assertEqual(p['texture_producers'],[0]*4)
        self.assertEqual(p['target_addresses'],[0x600000,0x800000])

    def test_producer_is_shared_without_using_sampled_bytes(self):
        _,scene,_,targets,draw=fixture();t=scene['textures'][0]
        t['resource']=targets[3]['resource'][:];t['cpu']=bytes(128);t['mips']=[dict(producer_target=3,data=b'not an expectation')]
        p=draw_plan(targets,draw,scene);self.assertEqual(p['texture_addresses'][0],0xa00000)
        self.assertEqual(p['texture_producers'][0],3)
        t['mips'][0]['data']=b'changed GPU observation';self.assertEqual(draw_plan(targets,draw,scene),p)

    def test_multiple_stages_share_one_producer(self):
        _,scene,_,targets,draw=fixture();t=scene['textures'][0]
        t['resource']=targets[3]['resource'][:];t['cpu']=bytes(128);t['mips']=[dict(producer_target=3,data=b'')]
        scene['textures'][1]=copy.deepcopy(t)
        p=draw_plan(targets,draw,scene);self.assertEqual(p['texture_addresses'][:2],[0xa00000]*2)

    def test_unknown_mixed_mips_or_wrong_producer_layout_guard(self):
        for case in ('unknown','mixed','size','normalized'):
            _,scene,_,targets,draw=fixture();t=scene['textures'][0]
            t['resource']=targets[3]['resource'][:];t['cpu']=bytes(128);t['mips']=[dict(producer_target=3,data=b'')]
            if case=='unknown':t['mips'][0]['producer_target']=4
            elif case=='mixed':t['mips'].append(dict(producer_target=0,data=b''))
            elif case=='size':t['resource'][4]^=1
            else:t['resource'][3]=0x01110c21;t['resource'][4]=0
            with self.assertRaises(ValueError):draw_plan(targets,draw,scene)

    def test_invalid_addresses_roles_and_inactive_storage_guard(self):
        for case in ('header','overlap','unaligned','inactive','producer'):
            _,scene,_,targets,draw=fixture();p=draw_plan(targets,draw,scene)
            if case=='header':p['target_headers'][0]=1
            elif case=='overlap':p['target_addresses'][1]=p['target_addresses'][0]+64
            elif case=='unaligned':p['texture_addresses'][0]+=1
            elif case=='inactive':p['texture_addresses'][1]=0x3000040
            else:p['texture_producers'][0]=3
            with self.assertRaises(ValueError):validate(p,scene)

    def test_actual_state_uses_declared_targets_and_role_pitches(self):
        capture,scene,actual,targets,draw=fixture();p=draw_plan(targets,draw,scene);observe(capture,scene,actual,p)
        self.assertTrue(qualify(capture,scene,actual)['state_word_components_qualified'])
        _,fields,_=expected_delivery(capture,scene);self.assertEqual(fields[0x20c],64|(128<<16))
        self.assertEqual(actual['methods']['0x380'],8)

    def test_normalized_texture_ignores_stale_linear_fields(self):
        capture,scene,actual,targets,draw=fixture();p=draw_plan(targets,draw,scene);observe(capture,scene,actual,p)
        actual['methods']['0x1b10']=0x05000000;actual['methods']['0x1b1c']=0x14000f0
        self.assertTrue(qualify(capture,scene,actual)['state_word_components_qualified'])
        actual['methods']['0x1b04']^=0x100
        self.assertFalse(qualify(capture,scene,actual)['state_word_components_qualified'])

    def test_line_width_is_input_and_unit_fixture_derived(self):
        capture,scene,actual,targets,draw=fixture();p=draw_plan(targets,draw,scene);observe(capture,scene,actual,p)
        capture['render'][157]=0x3f980000  # 1.1875 ->9.5 eighth-pixels rounds10.
        methods,_,_=expected_delivery(capture,scene);self.assertEqual(methods[0x380],10)
        self.assertFalse(qualify(capture,scene,actual)['state_word_components_qualified'])
        capture['render'][157]=0x42800000;self.assertEqual(expected_delivery(capture,scene)[0][0x380],511)
        capture['render'][157]=0x7fc00000
        with self.assertRaises(ValueError):expected_delivery(capture,scene)

    def test_changed_plan_with_refreshed_identity_fails_actual_delivery(self):
        capture,scene,actual,targets,draw=fixture();p=draw_plan(targets,draw,scene);observe(capture,scene,actual,p)
        scene['frame_resource_plan']['texture_addresses'][0]+=0x10000;actual['frame_resource_plan']=copy.deepcopy(scene['frame_resource_plan'])
        capture['sha256']=actual['vertex_capture_sha256']='refreshed';scene['sha256']=actual['scene_capture_sha256']='refreshed'
        self.assertFalse(qualify(capture,scene,actual)['state_word_components_qualified'])


if __name__=='__main__':unittest.main()
