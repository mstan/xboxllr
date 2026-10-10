"""Independent byte and scope controls for retained producer expectations."""
import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_scene_producer_reference import native_depth,compare_storage,producer_domain


def target(role):
    return dict(role=role,width=2,height=1,format=44 if role else 87,
                resource=[0,0,0,(0x2e if role else 0x12)<<8,1])


class ProducerControls(unittest.TestCase):
    def setUp(self):
        self.targets={1:target(0),2:target(1)}
        self.expected={1:struct.pack('<16I',*([0x12345678]*16)),
                       2:struct.pack('<16I',*([0xabcdef37]*16))}

    def test_physical_depth_representation(self):
        source=struct.pack('<4I',0,0xffffff00,0x12345678,0x000000ff)
        self.assertEqual(struct.unpack('<4I',native_depth(source)),
                         (0,0x00ffffff,0x78123456,0xff000000))

    def changed(self,target_id,index,xor):
        data=bytearray(self.expected[target_id]);v=struct.unpack_from('<I',data,index*4)[0]
        struct.pack_into('<I',data,index*4,v^xor)
        return {**self.expected,target_id:bytes(data)}

    def test_baseline_and_single_code_interval(self):
        self.assertTrue(compare_storage(self.targets,self.expected,self.expected,{1:{0}})['qualified'])
        self.assertTrue(compare_storage(self.targets,self.expected,self.changed(1,0,1),{1:{0}})['qualified'])

    def test_wrong_written_rgb_rejects(self):
        self.assertFalse(compare_storage(self.targets,self.expected,self.changed(1,0,16),{1:{0}})['qualified'])

    def test_masked_alpha_rejects_even_at_written_pixel(self):
        self.assertFalse(compare_storage(self.targets,self.expected,self.changed(1,0,1<<24),{1:{0}})['qualified'])

    def test_untouched_color_and_padding_reject(self):
        for index in (1,15):
            self.assertFalse(compare_storage(self.targets,self.expected,self.changed(1,index,1),{1:{0}})['qualified'])

    def test_depth_and_stencil_are_exact(self):
        for xor in (1,1<<8,1<<31):
            self.assertFalse(compare_storage(self.targets,self.expected,self.changed(2,0,xor),{1:{0},2:{0}})['qualified'])

    def test_missing_truncated_and_unknown_root_reject(self):
        for actual in ({1:self.expected[1]}, {**self.expected,2:self.expected[2][:-4]}, {**self.expected,3:self.expected[1]}):
            with self.assertRaises(ValueError):compare_storage(self.targets,self.expected,actual,{})

    def test_padding_cannot_gain_a_color_interval(self):
        with self.assertRaises(ValueError):compare_storage(self.targets,self.expected,self.expected,{1:{2}})

    def test_viewport_and_depth_clipping_are_explicitly_guarded(self):
        vertices=[[0,0,0,1]+[0]*24,[2,0,0,1]+[0]*24,[0,1,0,1]+[0]*24]
        producer_domain(vertices,2,1)
        for lane,value in ((0,2.0625),(1,-.0625),(2,16777216),(3,0),(0,float('nan'))):
            changed=[list(v) for v in vertices];changed[0][lane]=value
            with self.assertRaises(ValueError):producer_domain(changed,2,1)

    def test_fully_culled_face_preserves_even_outside_viewport(self):
        vertices=[[3,0,0,1]+[0]*24,[4,0,0,1]+[0]*24,[3,1,0,1]+[0]*24]
        producer_domain(vertices,2,1,0x404)
        with self.assertRaises(ValueError):producer_domain(vertices,2,1,0x405)


if __name__=='__main__':unittest.main()
