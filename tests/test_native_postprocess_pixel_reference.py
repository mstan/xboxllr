"""Hand-authored pixel equations and guards; no title data or renderer imports."""
from pathlib import Path
import sys
import unittest
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from native_postprocess_pixel_reference import combiner, compare, mapping, pixels, rgba, rectangle, sample, stencil_bytes, depth_only_view


def shader(count=1):
    m = {0x1e60:count|0x11100, 0x288:0x0000000c, 0x28c:0x00001c00,
         0x1e20:0, 0x1e24:0}
    for base in (0xa60, 0xa80, 0xac0, 0x260, 0x1e40, 0xaa0):
        for stage in range(8):m[base+4*stage] = 0
    m[0xac0] = 0x08200000  # texture RGB times one -> spare0
    m[0x1e40] = 0xc0
    return m


def evaluate(methods, texture=(.25, .5, .75, .375), primary=(0,0,0,0), secondary=(0,0,0,0)):
    return combiner(methods, [np.array([texture]), None, None, None],
                    np.array([primary]), np.array([secondary]))[0][0]


def corners(width=2, height=2):
    return np.array([[x,y,0,1,1,1,1,1,0,0,0,0,x,y,0,1,*([0,0,0,1]*3)]
                     for x,y in ((0,0),(width,0),(width,height),(0,height))], dtype=float)


class PostprocessEquations(unittest.TestCase):
    def test_all_eight_documented_input_mappings_include_negative_values(self):
        values = np.array([-.75, .25, 1.])
        expected = [[0,.25,1], [1,.75,0], [-1,-.5,1], [1,.5,-1],
                    [-.5,-.25,.5], [.5,.25,-.5], [-.75,.25,1], [.75,-.25,-1]]
        for operation, result in enumerate(expected):
            np.testing.assert_equal(mapping(values, operation), result)

    def test_initial_spare_alpha_comes_from_texture_zero(self):
        np.testing.assert_equal(evaluate(shader()), [.25,.5,.75,.375])

    def test_alpha_blue_selection_and_replication(self):
        m = shader();m[0x260] = 0x08300000;m[0xaa0] = 0xc0
        self.assertEqual(evaluate(m)[3], .75)
        m[0xac0] = 0x18200000
        np.testing.assert_equal(evaluate(m)[:3], [.375]*3)

    def test_stage_reads_prior_rgb_and_alpha_before_any_write(self):
        m = shader(2);m[0x260] = 0x18300000;m[0xaa0] = 0xc0
        m[0xac4] = 0x1c200000;m[0x1e44] = 0xc0
        m[0x264] = 0x0c300000;m[0xaa4] = 0xc0
        np.testing.assert_equal(evaluate(m), [.375,.375,.375,.75])

    def test_rgb_dot_product_blue_to_alpha_changes_later_stage(self):
        m = shader(2);m[0xa60] = 0x80804020
        m[0xac0] = 0x08010000;m[0x1e40] = 0x820c0
        m[0xac4] = 0x1c200000;m[0x1e44] = 0xc0
        dot = (.25*128+.5*64+.75*32)/255
        np.testing.assert_allclose(evaluate(m), [dot]*4, rtol=0, atol=1e-16)

    def test_rgb_dot_replicates_both_alpha_operands_before_three_component_dot(self):
        m = shader();m[0xac0] = 0x18140000;m[0x1e40] = 0x20c0
        np.testing.assert_equal(evaluate(m,primary=(0,0,0,.25)), [.28125]*3+[.375])
        m[0x1e40] = 0x2c00
        with self.assertRaises(ValueError):evaluate(m)

    def test_output_bias_and_scale_then_signed_clamp(self):
        for operation, expected in [(0,.25), (1,-.25), (2,.5), (3,-.5), (4,1), (6,.125)]:
            m = shader();m[0x1e40] |= operation << 15
            # Final unsigned mapping clamps a negative general output to zero.
            self.assertEqual(evaluate(m)[0], max(0,expected))

    def test_msb_mux_uses_prior_spare_alpha_not_same_stage_alpha_write(self):
        m = shader();m[0xac0] = 0x04200520;m[0x1e40] = 0x4c00
        m[0x260] = 0x30300000;m[0xaa0] = 0xc0
        self.assertEqual(evaluate(m, primary=(.25,)*4, secondary=(.75,)*4)[0], .25)
        self.assertEqual(evaluate(m, texture=(0,0,0,.75), primary=(.25,)*4, secondary=(.75,)*4)[0], .75)

    def test_ideal_mux_threshold_ties_and_nearby_values_and_lsb_guard(self):
        m = shader();m[0xac0] = 0x04200520;m[0x1e40] = 0x4c00
        for value in (.5, .5-1e-6, .5+1e-6):
            with self.subTest(value=value):
                self.assertEqual(evaluate(m, texture=(0,0,0,value), primary=(.25,)*4,
                                          secondary=(.75,)*4)[0], .25 if value < .5 else .75)
        m[0x1e60] &= ~0x100
        with self.assertRaises(ValueError):evaluate(m)

    def test_final_interpolation_ef_product_and_unsigned_invert(self):
        m = shader();m[0x288] = 0x140f2c05;m[0x28c] = 0x04051c00
        # A=.25, B=primary*secondary=.25*.5, C=1-texture, D=secondary.
        np.testing.assert_equal(evaluate(m, primary=(.25,)*4, secondary=(.5,)*4),
                                [1, .25*.125+.75*.5+.5,
                                 .25*.125+.75*.25+.5, .375])

    def test_separate_stage_constants_and_shared_constant_control(self):
        m = shader(2);m[0xac0] = 0x01200000;m[0xa60] = 0x40404040
        m[0xac4] = 0x01200000;m[0x1e44] = 0xc0;m[0xa64] = 0x80808080
        self.assertEqual(evaluate(m)[0], 128/255)
        m[0x1e60] &= ~0x1000
        self.assertEqual(evaluate(m)[0], 64/255)

    def test_undefined_registers_and_overlapping_outputs_reject(self):
        for icw, ocw in [(0x0d200000,0xc0), (0x09200000,0xc0), (0x08200000,0xcc0)]:
            m = shader();m[0xac0] = icw;m[0x1e40] = ocw
            with self.subTest(icw=icw,ocw=ocw), self.assertRaises(ValueError):evaluate(m)
        m = shader();m[0x1e40] |= 1<<19;m[0x260] = 0x18300000;m[0xaa0] = 0xc0
        with self.assertRaises(ValueError):evaluate(m)

    def test_final_specular_sum_clamping(self):
        m = shader();m[0x288] = 0xe
        np.testing.assert_equal(evaluate(m, secondary=(.5,)*4), [.75,1,1,.375])
        m[0x28c] |= 128
        np.testing.assert_equal(evaluate(m, secondary=(.5,)*4), [.75,1,1,.375])

    def test_pitch_padding_and_depth_as_argb_channel_layout(self):
        # Physical Z24S8: D=0x123456, S=0x78 -> A=0x12,R=0x34,G=0x56,B=0x78.
        data = bytes.fromhex('78563412 ffffffff')
        np.testing.assert_equal(rgba(data,1,1,8), np.array([[[0x34,0x56,0x78,0x12]]])/255)
        np.testing.assert_equal(depth_only_view(data,1,1,8), [[[0x123456/0xffffff,0,0,1]]])

    def test_rectangle_coverage_order_and_invalid_rhw_reject(self):
        quad = corners();strip = quad[[3,0,2,1]]
        np.testing.assert_equal(rectangle(quad,8,2,2), rectangle(strip,6,2,2))
        for lane in (0,2,3,4):
            v = quad.copy();v[0,lane] += .25
            with self.subTest(lane=lane),self.assertRaises(ValueError):rectangle(v,8,2,2)

    def test_texel_center_linear_and_point_sampling_clamp_edges(self):
        image = np.array([[[0,0,0,0],[1,1,1,1]],[[.5,.5,.5,.5],[.25,.25,.25,.25]]])
        v = corners()
        np.testing.assert_equal(sample(image,v,0,2,2,True), image)
        np.testing.assert_equal(sample(image,v,0,2,2,False), image)
        v[:,12:14] += .5
        self.assertEqual(sample(image,v,0,2,2,True)[0,0,0], .4375)
        self.assertEqual(sample(image,v,0,2,2,False)[0,0,0], .25)
        v[:,12:14] -= 100
        np.testing.assert_equal(sample(image,v,0,2,2,True), np.zeros((2,2,4)))

    def test_comparison_declares_one_code_interval_and_rejects_missing_pixels(self):
        expected = np.full((2,2,4), 128, dtype=np.uint8)
        actual = expected.copy();actual[0,0,0] += 1
        self.assertTrue(compare(expected,actual)['component_pixels_qualified'])
        actual[0,0,0] += 1
        self.assertFalse(compare(expected,actual)['component_pixels_qualified'])
        with self.assertRaises(ValueError):compare(expected,actual[:1])

    def test_full_rectangle_blend_and_raster_admission(self):
        m = shader();m.update({0x200:2<<16,0x204:2<<16,0x208:0x128,
            0x2c0:1<<16,0x2e0:1<<16,0x314:0,0x320:0,0x38c:0x1b02,0x390:0x1b02,
            0x300:0,0x304:0,0x308:0,0x30c:0,0x310:0,0x318:0,0x324:0,
            0x358:0x1010101,0x35c:0,0x37c:0x1d01,0x17bc:0,
            0x1e70:1,0x1b04:0x11229,0x1b08:0x10303,0x1b0c:0x4003ffc0,
            0x1b14:0x02060000,0x1b1c:(2<<16)|2,
            0x344:0x302,0x348:0x303,0x350:0x8006})
        image = np.broadcast_to(np.array([1,0,.5,128/255]),(2,2,4))
        before = np.broadcast_to(np.array([0,1,.25,1]),(2,2,4))
        got,_ = pixels(m,corners(),8,2,2,[image,None,None,None],before)
        np.testing.assert_equal(got, np.broadcast_to([255,0,128,128],(2,2,4)))
        m[0x17bc]=1;m[0x17c0]=0x1503
        np.testing.assert_equal(pixels(m,corners(),8,2,2,[image,None,None,None],before)[0], got)
        m[0x17bc]=0
        m[0x304]=1
        got,_ = pixels(m,corners(),8,2,2,[image,None,None,None],before)
        np.testing.assert_equal(got, np.broadcast_to([128,127,96,191],(2,2,4)))
        for field in (0x308,0x30c,0x310,0x300,0x35c,0x17bc):
            changed = dict(m);changed[field]=1
            with self.subTest(field=field),self.assertRaises(ValueError):
                pixels(changed,corners(),8,2,2,[image,None,None,None],before)
        for field,value in ((0x1b04,0x11239),(0x1b08,0x10101),
                            (0x1b0c,0x4003ffc4),(0x1b14,0x01060000),
                            (0x1e70,2),(0x350,0x800a)):
            changed = dict(m);changed[field]=value
            with self.subTest(field=field),self.assertRaises(ValueError):
                pixels(changed,corners(),8,2,2,[image,None,None,None],before)

    def test_stencil_replace_preserves_depth_low_mask_padding_and_uncovered_rows(self):
        data = bytes.fromhex('a5785634 b6785634 cc785634 dd785634')*2
        m = {0x32c:1,0x30c:0,0x35c:0,0x364:0x207,0x378:0x1e01,
             0x360:0xfffffff0,0x368:0xffffff80}
        want = bytes.fromhex('85785634 86785634 cc785634 dd785634')+data[16:]
        self.assertEqual(stencil_bytes(m,data,3,2,16,2,1),want)
        m[0x378]=0x1e00
        self.assertEqual(stencil_bytes(m,data,3,2,16,2,1),data)
        m[0x378]=0x1e01;m[0x32c]=0
        self.assertEqual(stencil_bytes(m,data,3,2,16,2,1),data)
        for field,value in ((0x32c,2),(0x30c,1),(0x35c,1),(0x364,0x202),(0x378,0x1e02)):
            changed={**m,0x32c:1,field:value}
            with self.subTest(field=field),self.assertRaises(ValueError):stencil_bytes(changed,data,3,2,16,2,1)


if __name__ == '__main__':unittest.main()
