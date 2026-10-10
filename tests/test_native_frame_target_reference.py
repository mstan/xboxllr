"""Authored target encodings, masks, retained storage and rejection checks."""
import copy
import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_frame_target_reference import (layout,initial_storage,target_words,
    clear_storage,Prepared,controls,placement,XBE_SHA)


def target(n,role=0,width=2,height=2,pitch=64,fmt=None):
    return dict(id=n,role=role,width=width,height=height,format=fmt if fmt is not None else (45 if role else 87),
        resource=[0x1000002,0,0,0x10029|((0x2e if role else 0x12)<<8),
                  (pitch//64-1)<<24|(height-1)<<12|(width-1)])


def fixture():
    targets={1:target(1),2:target(2,1,width=4,height=3,pitch=128),3:target(3)}
    before={n:initial_storage(t,struct.pack('<I',0x12345678)*(t['width']*t['height'])) for n,t in targets.items()}
    headers,storage=placement(targets);prepared=Prepared(targets,headers,storage,[1,2],None,before)
    actual=dict(original_returned=True,fifo_get=1234,fifo_put=1234,guard=None,methods={hex(m):w for m,w in prepared.words.items()},
        bound_headers=[headers[1],headers[2]],viewport=prepared.viewport[:],xbe_sha256=XBE_SHA,
        full_target_viewport_fixture=True,draws_omitted=True,submission=prepared.submission,library_sha256='build',inputs_sha256='inputs')
    return targets,headers,storage,before,prepared,actual


class TargetComponents(unittest.TestCase):
    def test_declared_input_color_and_depth_conversions(self):
        color=target(1,fmt=28);b=initial_storage(color,bytes([1,2,3,4])*4)
        self.assertEqual(b[:8],bytes([3,2,1,4])*2);self.assertEqual(b[8:64],b'\xa5'*56)
        depth=target(2,1);self.assertEqual(initial_storage(depth,struct.pack('<I',0x12345678)*4)[:4],bytes.fromhex('12785634'))

    def test_mixed_dimensions_and_independent_pitches(self):
        targets,_,storage,_,_,_=fixture();w=target_words(targets,[1,2],storage)
        self.assertEqual(w[0x20c],64|(128<<16));self.assertEqual(w[0x200],2<<16);self.assertEqual(w[0x208],0x128)
        self.assertEqual(w[0x214],storage[2])

    def test_reject_wrong_formats_dimensions_and_pitches(self):
        for name in ('format','width','pitch','surface'):
            t=target(1)
            if name=='format':t['format']=44
            elif name=='width':t['width']=3
            elif name=='pitch':t['width']=65;t['resource'][4]=(1<<12)|64
            else:t['resource'][3]=0x10629
            with self.assertRaises(ValueError):layout(t)

    def test_reject_depth_smaller_than_color(self):
        targets,_,storage,_,_,_=fixture();targets[2]=target(2,1,width=1)
        with self.assertRaises(ValueError):target_words(targets,[1,2],storage)

    def test_complete_clear_and_unbound_padding_preservation(self):
        targets,_,_,before,_,_=fixture();after,words=clear_storage(targets,[1,2],[0,0,0xf3,0xabcdef12,0x3f800000,9],before)
        self.assertEqual(after[1][:8],struct.pack('<I',0xabcdef12)*2)
        self.assertEqual(after[2][:8],struct.pack('<I',0xffffff09)*2)
        self.assertEqual(after[2][8:128],before[2][8:128]);self.assertEqual(after[2][256:],before[2][256:])
        self.assertEqual(after[3],before[3]);self.assertEqual(words[0x1d98],1<<16)

    def test_selective_color_channels(self):
        targets,_,_,before,_,_=fixture()
        for bit,lane in ((4,2),(5,1),(6,0),(7,3)):
            after,_=clear_storage(targets,[1,2],[0,0,1<<bit,0xabcdef12,0,0],before)
            want=bytearray(before[1]);want[lane:8:4]=bytes([struct.pack('<I',0xabcdef12)[lane]])*2
            want[64+lane:72:4]=bytes([struct.pack('<I',0xabcdef12)[lane]])*2
            self.assertEqual(after[1],bytes(want));self.assertEqual(after[2],before[2])

    def test_depth_and_stencil_preserve_other_component(self):
        targets,_,_,before,_,_=fixture()
        after,_=clear_storage(targets,[1,2],[0,0,1,0,0,999],before)
        self.assertEqual(after[2][:4],struct.pack('<I',0x12))
        after,_=clear_storage(targets,[1,2],[0,0,2,0,0x7fc00000,0x9a],before)
        self.assertEqual(after[2][:4],struct.pack('<I',0x3456789a))
        self.assertEqual(after[1],before[1])

    def test_noop_and_consumed_argument_guards(self):
        targets,_,_,before,_,_=fixture()
        after,words=clear_storage(targets,[1,2],[0,0,0,0,0x7fc00000,999],before)
        self.assertEqual(after,before);self.assertEqual(words,{})
        for args in ([1,0,0xf3,0,0,0],[0,4,0xf3,0,0,0],[0,0,4,0,0,0],
                     [0,0,1,0,0x3f000000,0],[0,0,2,0,0,256]):
            with self.assertRaises(ValueError):clear_storage(targets,[1,2],args,before)

    def test_joint_words_binding_viewport_and_all_bytes(self):
        _,_,_,before,prepared,actual=fixture();self.assertTrue(prepared.qualify(actual,before,'build','inputs')['component_qualified'])
        mutated=copy.deepcopy(before);mutated[3]=bytes([mutated[3][0]^1])+mutated[3][1:]
        self.assertFalse(prepared.qualify(actual,mutated)['component_qualified'])

    def test_semantic_changed_root_with_refreshed_identity_fails(self):
        targets,headers,storage,before,_,actual=fixture();targets[1]=target(1,width=1)
        prepared=Prepared(targets,headers,storage,[1,2],None,before)
        actual['inputs_sha256']='refreshed'
        self.assertFalse(prepared.qualify(actual,before,'build','refreshed')['component_qualified'])

    def test_complete_original_and_fixture_flags_are_exact(self):
        _,_,_,before,prepared,actual=fixture()
        for name in ('original_returned','full_target_viewport_fixture','draws_omitted'):
            obs=copy.deepcopy(actual);obs[name]=1
            with self.assertRaises(ValueError):prepared.qualify(obs,before)
        obs=copy.deepcopy(actual);obs['fifo_get']=obs['fifo_put']=True
        with self.assertRaises(ValueError):prepared.qualify(obs,before)

    def test_failure_controls_and_placement_scope(self):
        _,_,_,before,prepared,actual=fixture();rows=controls(prepared,actual,before,'build','inputs')
        self.assertEqual(len(rows),16);self.assertTrue(all(r['control_passed'] for r in rows))
        with self.assertRaises(ValueError):placement(dict.fromkeys(range(15)))


if __name__=='__main__':unittest.main()
