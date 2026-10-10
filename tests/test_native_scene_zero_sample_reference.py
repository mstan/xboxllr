"""Authored faults distinguish proven emptiness from sampled color precision."""
import copy
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_scene_rejection_reference as rejection
import native_scene_empty_depth_reference as empty_depth
from test_native_scene_clamp_reference import empty_fixture


class ZeroSampleControls(unittest.TestCase):
    def fixture(self,domain):
        data=empty_fixture()
        for i,v in enumerate(data[2]):
            if domain is rejection:v[:4]=[i+.125,-i-.375,16777216.+i,-1.-i]
            else:v[2]=-1.
        return data

    def execute(self,domain,data):
        capture,scene,vertices,fog,targets,before,methods=data
        function=(domain.expected_rejection_draw if domain is rejection else
                  domain.expected_empty_depth_draw)
        with patch.object(domain,'expected_delivery',return_value=(methods,{},{})), \
             patch('native_scene_clamp_reference.expected_delivery',return_value=(methods,{},{})):
            return function(capture,scene,vertices,fog,targets,before,'fma-b-first','fma')

    def test_exact_unused_64_over_63_palette_preserves_storage_without_raster_work(self):
        for domain in (rejection,empty_depth):
            with self.subTest(domain=domain.__name__):
                data=self.fixture(domain);original=copy.deepcopy(data)
                with patch('native_scene_clamp_reference.expected_draw',side_effect=AssertionError('raster')), \
                     patch('native_scene_clamp_reference.triangle_colors',side_effect=AssertionError('sample')), \
                     patch('native_scene_clamp_reference.precision_domain',side_effect=AssertionError('unused precision')):
                    storage,writes,stats=self.execute(domain,data)
                self.assertEqual(storage,data[5]);self.assertIsNot(storage,data[5])
                self.assertFalse(writes);self.assertEqual(data,original)
                self.assertEqual(stats['input_codec_error_codes'],['0','64/63','0'])
                self.assertTrue(stats['codec_precision_unconsumed_because_no_raster_leaves'])

    def test_complete_original_input_and_owned_storage_contract_stays_enforced(self):
        faults=[('missing inactive root',lambda d:d[5].pop(3)),
                ('short active root',lambda d:d[5].__setitem__(1,d[5][1][:-1])),
                ('extra root',lambda d:d[5].__setitem__(4,b'extra')),
                ('wrong count',lambda d:d[0]['header'].__setitem__(4,4)),
                ('wrong role',lambda d:d[4][1].__setitem__('role',1)),
                ('wrong shape',lambda d:d[1]['before'][1]['shape'].__setitem__(0,15)),
                ('wrong primitive',lambda d:d[0]['header'].__setitem__(2,5)),
                ('short encoded mip',lambda d:d[1]['textures'][0]['mips'][0].__setitem__('data',b'x')),
                ('wrong alpha',lambda d:d[2][0].__setitem__(7,.5)),
                ('wrong fog count',lambda d:d[3].pop()),
                ('nonfinite geometry',lambda d:d[2][0].__setitem__(0,float('nan'))),
                ('nonfinite attribute',lambda d:d[2][0].__setitem__(4,float('nan'))),
                ('changed depth state',lambda d:d[6].__setitem__(0x354,0))]
        for domain in (rejection,empty_depth):
            for name,mutate in faults:
                with self.subTest(domain=domain.__name__,fault=name):
                    data=self.fixture(domain);mutate(data)
                    with self.assertRaises((ValueError,AssertionError)):self.execute(domain,data)

    def test_changed_visible_geometry_cannot_use_empty_palette_exemption(self):
        for domain in (rejection,empty_depth):
            with self.subTest(domain=domain.__name__):
                data=self.fixture(domain)
                for v,xy in zip(data[2],((0,0),(0,4),(16,0))):v[:4]=[*xy,3.,1.]
                with self.assertRaisesRegex(ValueError,'cannot prove one-code bound'):
                    self.execute(domain,data)


if __name__=='__main__':unittest.main()
