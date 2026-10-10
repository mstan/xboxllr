"""Authored dataflow and full-coverage rejection cases; no device or outputs."""
import copy
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_frame_clear_dependency import initial_depth,opaque_overwrite,prior_uses


def fixture():
    r=[0]*166;r[67]=0x01010101;r[144]=1;r[70]=0x207
    capture=dict(header=[0,0,6,0,4],viewport=[0,0,4,2,0,0x3f800000],render=r)
    pixel=[0]*60;pixel[54]=0x21
    scene=dict(before=[dict(shape=[4,2,28])],header=[0]*8+[1],pixel=pixel,textures=[{}, {},None,None])
    vertices=[]
    for position in ([0,2,0,1],[0,0,0,1],[4,2,0,1],[4,0,0,1]):
        v=list(position)+[0]*24;v[15]=v[19]=1;vertices.append(v)
    return capture,scene,vertices


class ClearDependency(unittest.TestCase):
    def test_full_overwrite(self):
        c,s,v=fixture();self.assertEqual(opaque_overwrite(c,s,v),8)
        c['render'][144]=0;c['render'][70]=0xdead;self.assertEqual(opaque_overwrite(c,s,v),8)

    def test_partial_coverage_or_wrong_viewport_rejects(self):
        for case in ('viewport','edge','depth','w','primitive','count','vertices'):
            c,s,v=fixture()
            if case=='viewport':c['viewport'][0]=1
            elif case=='edge':v[2][0]=3
            elif case=='depth':v[2][2]=1
            elif case=='w':v[1][3]=0
            elif case=='primitive':c['header'][2]=5
            elif case=='count':c['header'][4]=3
            else:v.pop()
            with self.assertRaises(ValueError):opaque_overwrite(c,s,v)

    def test_destination_reads_tests_or_masks_reject(self):
        for state in (59,60,64,67,79,80,81,143,147,149,150):
            c,s,v=fixture();c['render'][state]^=1
            with self.assertRaises(ValueError):opaque_overwrite(c,s,v)

    def test_stencil_rejection_rejects(self):
        c,s,v=fixture();c['render'][70]=0x202
        with self.assertRaises(ValueError):opaque_overwrite(c,s,v)

    def test_shader_discard_or_invalid_q_rejects(self):
        for case in ('clip','q','nonfinite','missing'):
            c,s,v=fixture()
            if case=='clip':s['pixel'][54]=4
            elif case=='q':v[2][15]=0
            elif case=='nonfinite':v[2][16]=float('nan')
            else:s['textures'][1]=None
            with self.assertRaises(ValueError):opaque_overwrite(c,s,v)

    def test_idempotent_depth_input(self):
        target=dict(role=1,format=45,width=4,height=2);data=b'\xff\xff\xff\0'*8
        self.assertEqual(initial_depth(target,data,[0,0,0xf3,0,0x3f800000,0]),8)

    def test_depth_stencil_input_or_clear_change_rejects(self):
        for case in ('depth','stencil','depth_arg','stencil_arg','flags','layout'):
            target=dict(role=1,format=45,width=4,height=2);data=b'\xff\xff\xff\0'*8
            args=[0,0,0xf3,0,0x3f800000,0]
            if case=='depth':data=bytes([0])+data[1:]
            elif case=='stencil':data=data[:3]+bytes([128])+data[4:]
            elif case=='depth_arg':args[4]=0
            elif case=='stencil_arg':args[5]=128
            elif case=='flags':args[2]=0x1
            else:target['format']=55
            with self.assertRaises(ValueError):initial_depth(target,data,args)

    def test_no_prior_color_draw_or_sampling_including_writer(self):
        for case in ('baseline','prior_draw','prior_sample','writer_sample'):
            words=[0]*27;words[11]=1;words[26]=1 if 'sample' in case else 3
            writer=dict(input=2,target=1,textures=[1 if case=='writer_sample' else 0])
            early=dict(input=1,target=1 if case=='prior_draw' else 3,textures=[1 if case=='prior_sample' else 0])
            reader=SimpleNamespace(draws=[early,writer],asset=lambda _:dict(bytes=108),words=lambda *_:words)
            if case=='baseline':prior_uses(reader,writer,1)
            else:
                with self.assertRaises(ValueError):prior_uses(reader,writer,1)


if __name__=='__main__':unittest.main()
