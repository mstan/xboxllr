"""Authored controls for diffuse range, BC1 proof and consumed color equations."""
import copy
from fractions import Fraction
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_scene_diffuse_reference import (
    promote,codec_error_bound,diffuse_domain,consumed_profile,modulate,triangle_rgb,DIFFUSE_LIMIT)


def vertex(x,y,w=1,r=.75):
    return [x,y,3,w,r,.5,.25,2,0,0,0,0,0,0,0,1]+[0]*12


def methods():
    m = {0x1e60:1,0x1e70:1,0xac0:0x08040000,0x260:0x18140000,
         0x1e40:0xc00,0xaa0:0xc00,0x288:0x130e0300,0x28c:0x1c80,
         0x2a4:1,0x29c:0x800,0x9c0:0x3fc00000,0x9c4:0x80000000,0x9c8:0,
         0x300:0,0x310:0,0x314:0,0x358:0x10101,0x1b14:0x02060000,0x1b0c:0x4003ffc0}
    m.update({base+4*stage:0 for base in (0xac0,0x260,0x1e40,0xaa0) for stage in range(1,8)})
    return m


class DiffuseControls(unittest.TestCase):
    def test_msb_extension_is_exact_not_ideal_endpoint_scaling(self):
        self.assertEqual(promote(3,5),24);self.assertEqual(promote(15,6),60)
        self.assertEqual(promote(31,5),255);self.assertEqual(promote(63,6),255)
        self.assertNotEqual(Fraction(255*3,31),promote(3,5))

    def test_exhaustive_codec_bound_and_one_code_proof(self):
        self.assertEqual(codec_error_bound(),{5:Fraction(35,31),6:Fraction(23,21)})
        self.assertEqual(max(codec_error_bound().values())*DIFFUSE_LIMIT,Fraction(105,124))
        self.assertLess(Fraction(105,124),1)
        # Diffuse one cannot inherit this ideal-codec one-code proof.
        self.assertGreater(max(codec_error_bound().values()),1)

    def test_new_range_consumes_full_unrounded_diffuse(self):
        v = vertex(0,0,r=.5083203315734863);diffuse_domain([v])
        self.assertEqual(modulate([255,255,255],v[4:7]),[130,128,64])
        self.assertEqual(modulate([255,255,255],[.75,.75,.75]),[191,191,191])

    def test_changed_diffuse_and_texture_cannot_be_hidden_by_provenance(self):
        self.assertNotEqual(modulate([255,200,100],[.75,.5,.25]),
                            modulate([255,200,100],[.5,.5,.25]))
        self.assertNotEqual(modulate([255,200,100],[.75,.5,.25]),
                            modulate([200,200,100],[.75,.5,.25]))

    def test_alpha_secondary_and_outside_rgb_remain_guarded(self):
        diffuse_domain([vertex(0,0)])  # Alpha >1 saturates; target A stays masked.
        for lane,value in ((4,.75001),(4,-.01),(7,.99),(8,.01),(4,float('nan'))):
            v = vertex(0,0);v[lane] = value
            with self.assertRaises(ValueError):diffuse_domain([v])

    def test_each_consumed_combiner_fog_kill_and_write_word_rejects(self):
        m = methods();consumed_profile(m)
        for word in (0xac0,0x260,0x1e40,0xaa0,0x1e60,0x1e70,0x288,0x28c,
                     0x2a4,0x29c,0x9c0,0x9c4,0x9c8,0x300,0x310,0x314,0x358,0xac4):
            changed = dict(m);changed[word] ^= 1
            with self.assertRaises(ValueError):consumed_profile(changed)

    def test_perspective_diffuse_cannot_be_replaced_with_affine(self):
        v = [vertex(0,0,1,.75),vertex(4,0,2,.5),vertex(0,4,4,.25)]
        white = [(1,1,[[255,255,255]])]
        rgb = triangle_rgb(v,white,methods(),4,{0})[0]
        # At (.5,.5), barycentrics 3/4,1/8,1/8; divide each by W.
        d = (Fraction(3,4)*Fraction(3,4)+Fraction(1,8)/2*Fraction(1,2)+
             Fraction(1,8)/4*Fraction(1,4))/(Fraction(3,4)+Fraction(1,8)/2+Fraction(1,8)/4)
        self.assertEqual(rgb[0],int(255*d+Fraction(1,2)))
        affine = Fraction(3,4)*Fraction(3,4)+Fraction(1,8)*Fraction(1,2)+Fraction(1,8)*Fraction(1,4)
        self.assertNotEqual(rgb[0],int(255*affine+Fraction(1,2)))

    def test_equation_uses_original_colors_without_mutation(self):
        v = [vertex(0,0),vertex(4,0),vertex(0,4)]
        before = copy.deepcopy(v)
        metrics = {}
        rgb = triangle_rgb(v,[(1,1,[[255,255,255]])],methods(),4,{0},metrics)
        self.assertEqual(rgb,{0:[191,128,64]});self.assertEqual(v,before)
        self.assertEqual(metrics,{'maximum_consumed_diffuse':.75})


if __name__ == '__main__':unittest.main()
