"""Hand-calculated mixed clip/coverage/attributes and false-admission controls."""
from contextlib import ExitStack
import copy
from fractions import Fraction
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_scene_surviving_mixed_reference as surviving
from native_scene_mixed_clip_reference import mixed_triangles
from test_native_scene_clamp_reference import texture
from test_native_scene_diffuse_reference import vertex,methods
from test_native_frame_target_reference import target


def fixture(varying=False):
    # Homogeneous corners (0,5,1,-1), (-1,-1,1,1), (1,-1,1,1).
    # Bottom clipping W-Y=0 intersects W=1/2; the retained rectangle has
    # XY=(0,0),(16,0),(16,4),(0,4), W=1 at top and 1/2 at bottom.
    # Homogeneous Z=1 therefore yields window Z=1 and 2 at top/bottom.
    vertices = [vertex(8,-8,-1,.5),vertex(0,0,1,.5),vertex(16,0,1,.5)]
    for v in vertices:v[2] = 1/v[3];v[12:16] = [.25,.25,0,1]
    if varying:vertices[0][4] = 0;vertices[0][12:14] = [.5,.5]
    capture = {'header':[0,0,6,0,3,0,0,0],'sha256':'authored-capture'}
    scene = {'header':[0]*10,'sha256':'authored-scene',
             'before':[{'shape':[16,4,87]},{'shape':[16,4,44]}],
             'textures':[texture(0xffff,0,0),None,None,None],
             'frame_resource_plan':{'target_ids':[1,2]}}
    scene['header'][5] = 3
    targets = {1:target(1,width=16,height=4),2:target(2,1,width=16,height=4,fmt=44)}
    before = {1:struct.pack('<I',0x7a010203)*64,2:struct.pack('<I',0x00ffff5b)*64,
              3:b'independent unused root'}
    m = methods()
    m.update({0x208:0x128,0x304:1,0x308:1,0x30c:1,0x318:0,0x31c:0,
              0x354:0x201,0x35c:1,0x37c:0x1d01,0x380:0,0x38c:0x1b02,
              0x390:0x1b02,0x3a0:0x900,0x394:0,0x398:0x4b7fffff,0x39c:0x405,
              0x344:0x302,0x348:0x303,0x350:0x8006,0x1b08:0x10101,
              0x1b48:0x10101,0x1b88:0x10101,0x1bc8:0x10101,
              0x200:16<<16,0x204:4<<16,0x2c0:15<<16,0x2e0:3<<16,0x20c:64*0x10001})
    m.update({word:0 for word in (0x320,0x324,0x328,0x32c,0x330,0x334,0x338)})
    return capture,scene,vertices,[0.]*3,targets,before,m


def expected_fixture(data):
    capture,scene,vertices,fog,targets,before,m = data
    with ExitStack() as stack:
        for module in ('native_scene_surviving_mixed_reference',
                       'native_scene_clamp_reference','native_scene_producer_reference'):
            stack.enter_context(patch(module+'.expected_delivery',return_value=(m,{},{})))
        return surviving.expected_surviving_mixed_draw(capture,scene,vertices,fog,targets,before,
                                                      'fma-b-first','fma')


class SurvivingMixedControls(unittest.TestCase):
    def test_full_clip_keeps_hand_calculated_rectangle_and_degenerate_fan_order(self):
        data = fixture();leaves,stats = mixed_triangles(data[2],16,4)
        corners = [(16.,0.,1.,1.),(16.,4.,2.,.5),(0.,4.,2.,.5),(0.,0.,1.,1.)]
        # Sutherland-Hodgman retains duplicate boundary corners; its fan
        # appends two degenerate leaves after the two rectangle halves.
        self.assertEqual([[tuple(v[:4]) for v in t] for t in leaves],
            [[corners[0],corners[1],corners[2]],
             [corners[0],corners[2],corners[3]],
             [corners[0],corners[3],corners[3]],
             [corners[0],corners[3],corners[0]]])
        self.assertEqual(stats['homogeneous_clip_passes'],1)
        self.assertEqual(stats['homogeneous_plane_intersections'],8)

    def test_rectangle_coverage_depth_and_masked_channels_are_hand_calculated(self):
        data = fixture();storage,writes,stats = expected_fixture(data)
        self.assertEqual(writes,set(range(64)))
        self.assertEqual(stats['tested_fragments'],64)  # Each shared edge has one owner.
        self.assertEqual(stats['triangles'],2)
        self.assertEqual(storage[1],struct.pack('<I',0x7a808040)*64)
        # Z=1+y/4 at centers y=.5,1.5,2.5,3.5: every incoming depth floors1.
        self.assertEqual(storage[2],struct.pack('<I',0x0000015b)*64)
        self.assertEqual(storage[3],data[5][3])

    def test_clipped_primary_uv_and_perspective_color_are_hand_calculated(self):
        data = fixture(varying=True);leaves,_ = mixed_triangles(data[2],16,4)
        for leaf in leaves:
            for v in leaf:
                self.assertEqual(v[4],.375 if v[1] == 4 else .5)
                self.assertEqual(v[12:14],[.3125,.3125] if v[1] == 4 else [.25,.25])
        storage,writes,_ = expected_fixture(data)
        # Perspective R=(.5+.25*s)/(1+s), s=y/4. Row-center values are
        #17/36,19/44,21/52,23/60; nearest UNORM gives120,110,103,98.
        expected = b''.join(struct.pack('<I',(0x7a<<24)|(r<<16)|(128<<8)|64)*16
                            for r in (120,110,103,98))
        self.assertEqual(storage[1],expected);self.assertEqual(len(writes),64)
        wrong_affine_first = int(255*Fraction(31,64)+Fraction(1,2))
        self.assertEqual(wrong_affine_first,124);self.assertGreater(wrong_affine_first-120,1)

    def test_omitting_a_surviving_fan_half_is_detectable(self):
        data = fixture();leaves,stats = mixed_triangles(data[2],16,4)
        with patch.object(surviving,'mixed_triangles',return_value=(leaves[1:],stats)):
            storage,writes,row = expected_fixture(data)
        self.assertEqual(len(writes),32);self.assertEqual(row['tested_fragments'],32)
        self.assertNotEqual(storage[1],struct.pack('<I',0x7a808040)*64)

    def test_equal_depth_dependency_preserves_fan_order(self):
        data = fixture();leaves,stats = mixed_triangles(data[2],16,4)
        first = copy.deepcopy(leaves[0]);second = copy.deepcopy(first)
        for v in second:v[4] = .25
        with patch.object(surviving,'mixed_triangles',return_value=([first,second],stats)):
            result,writes,row = expected_fixture(data)
        with patch.object(surviving,'mixed_triangles',return_value=([second,first],stats)):
            wrong,other,_ = expected_fixture(data)
        self.assertEqual(writes,other);self.assertEqual(len(writes),32)
        self.assertEqual(row['rejected_depth_fragments'],32)
        i=min(writes)
        self.assertEqual((struct.unpack_from('<I',result[1],i*4)[0]>>16)&255,128)
        self.assertEqual((struct.unpack_from('<I',wrong[1],i*4)[0]>>16)&255,64)

    def test_leaf_near_w_depth_primary_and_texture_domains_stay_guarded(self):
        leaf = mixed_triangles(fixture()[2],16,4)[0][0]
        for lane,value in ((0,-.01),(1,4.01),(2,-1),(2,16777216),(2,1.5),
                           (3,0),(3,.00001),(4,-.01),(5,1.01),(7,.99),(8,.01),(15,.5),
                           (2,float('nan'))):
            with self.subTest(lane=lane,value=value):
                changed = copy.deepcopy(leaf);changed[0][lane] = value
                with self.assertRaisesRegex(ValueError,'unchanged positive-W'):
                    surviving.admitted_leaf(changed,16,4,0x404)

    def test_active_constant_depth_stays_guarded_but_degenerate_leaf_writes_nothing(self):
        leaves,_ = mixed_triangles(fixture()[2],16,4)
        active = copy.deepcopy(leaves[0])
        for v in active:v[2] = 1
        with self.assertRaisesRegex(ValueError,'constant-depth'):
            surviving.admitted_leaf(active,16,4,0x405)
        _,proof = surviving.admitted_leaf(leaves[2],16,4,0x405)
        self.assertFalse(proof['survives_cull']);self.assertEqual(proof['snapped_area_16'],0)

    def test_clamp_order_changing_original_primary_is_not_silently_admitted(self):
        data = list(fixture());data[2] = copy.deepcopy(data[2]);data[2][0][4] = 2
        with self.assertRaisesRegex(ValueError,'original RGB'):
            expected_fixture(data)

    def test_consumed_state_alpha_codec_and_original_near_w_faults_reject(self):
        for name in ('fog','alpha','texture','near-w','combiner'):
            data = list(fixture())
            if name == 'fog':data[3] = [float('nan')]*3
            elif name == 'alpha':data[2][0][7] = .5
            elif name == 'texture':data[1]['textures'][0]['cpu'] = b'changed'
            elif name == 'near-w':data[2][0][3] = -.00001
            elif name == 'combiner':data[6][0xac0] ^= 1
            with self.subTest(name=name),self.assertRaises(ValueError):expected_fixture(data)

    def test_original_1943_scene_hash_cannot_be_swapped(self):
        with self.assertRaisesRegex(ValueError,'Wrong original draw1943'):
            surviving.expected_bounded_surviving_draw(
                {'input_sequence':1943,'sha256':surviving.CAPTURE_1943_SHA256},
                {'sha256':'other scene'},[],[],{}, {},
                'fma-b-first','fma')

    def test_same_capture_hash_on_other_input_keeps_the_old_guards(self):
        with patch.object(surviving,'expected_empty_depth_draw',return_value='old guarded model') as old:
            result = surviving.expected_bounded_surviving_draw(
                {'input_sequence':1944,'sha256':surviving.CAPTURE_1943_SHA256},
                {'sha256':surviving.SCENE_1943_SHA256},[],[],{}, {},'fma-b-first','fma')
        self.assertEqual(result,'old guarded model');old.assert_called_once()

    def test_large_grid_and_unknown_cull_remain_guarded(self):
        leaf = [vertex(0,0),vertex(256,0),vertex(0,256)]
        for v,z in zip(leaf,(1,2,3)):v[2] = z
        with self.assertRaisesRegex(ValueError,'small-grid'):
            surviving.admitted_leaf(leaf,256,256,0x405)
        with self.assertRaisesRegex(ValueError,'viewport/cull'):
            surviving.admitted_leaf(leaf,256,256,0)

    def test_undeclared_clip_or_depth_rounding_graph_rejects(self):
        for depth,clip in (('separate','fma'),('fma-b-first','separate')):
            with self.assertRaisesRegex(ValueError,'pinned clipping/depth graph'):
                surviving.expected_surviving_mixed_draw({}, {},[],[],{}, {},depth,clip)

    def test_changed_retained_source_is_not_qualified_by_a_library_pin(self):
        with patch.object(surviving,'mixed_software_evidence',return_value={'code_sites':[]}):
            with self.assertRaisesRegex(ValueError,'pinned retained source'):
                surviving.surviving_software_evidence(None,SimpleNamespace(read_bytes=lambda:b'changed source'))


if __name__ == '__main__':unittest.main()
