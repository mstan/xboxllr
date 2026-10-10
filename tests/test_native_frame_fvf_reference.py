"""Authored pretransformed coordinates, FVF fields and admission failures."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_frame_fvf_reference import declaration, expectation, qualify


def fixture(diffuse=False,rhw=1):
    fvf=0x144 if diffuse else 0x104
    source=struct.pack('<4f',-.5,1.5,0,rhw)
    if diffuse:source+=struct.pack('<I',0x80402010)
    source+=struct.pack('<2f',.25,.75)
    elements=[[2,0,0] for _ in range(16)];elements[0]=[0x42,0,0]
    if diffuse:elements[3]=[0x40,0,16]
    elements[9]=[0x22,0,20 if diffuse else 16]
    descriptor=[0,0x402 if diffuse else 2,0,0,2]
    for fmt,stream,offset in elements:descriptor.extend([stream,offset,fmt,0])
    metadata=[[0,0] for _ in range(16)];metadata[0]=[len(source),len(source)]
    streams=[b'' for _ in range(16)];streams[0]=source
    bias=struct.unpack('<I',struct.pack('<f',17/32))[0]
    value=dict(draw=dict(input=1,shader=fvf,primitive=6,kind=3,count=1,
        viewport=[0,0,32,32,0,0x3f800000],screen=[bias,bias]),
        render=[0]*166,elements=elements,metadata=metadata,streams=streams,sha256='authored')
    color=[struct.unpack('<f',struct.pack('<f',n/255))[0] for n in (64,32,16,128)] if diffuse else [1]*4
    vertices=[[0,2,0,1 if rhw else 2**64]+color+[0]*4+[.25,.75,0,1]+[0,0,0,1]*3]
    counts=[0]*26;counts[1]=1
    observed=dict(input=1,input_sha256='authored',original_setter_returned=True,
        original_draw_returned=True,fifo_get=32,fifo_put=32,submission='original DrawVerticesUP',
        vertices=vertices,fog=[0],draw_counters=counts,descriptor=descriptor)
    return value,observed


class FvfComponent(unittest.TestCase):
    def test_independent_format_fields_and_consumed_offsets(self):
        for diffuse in (False,True):
            value,_=fixture(diffuse)
            self.assertEqual(declaration(value['draw']['shader']),value['elements'])
        for fvf in (0,0x102,0x184,0x204):
            with self.subTest(fvf=fvf):
                with self.assertRaises(ValueError):declaration(fvf)

    def test_screen_bias_defaults_texcoords_and_optional_argb(self):
        for diffuse in (False,True):
            value,observed=fixture(diffuse)
            self.assertEqual(expectation(value),(observed['vertices'],[0]))
            row=qualify(value,observed)
            self.assertTrue(row['software_fvf_qualified'])
            self.assertEqual(row['compared_values'],29)
            self.assertFalse(row['complete_scene_pixel_qualified'])

    def test_positive_zero_rhw_has_published_clamped_reciprocal(self):
        value,observed=fixture(True,0)
        self.assertEqual(expectation(value)[0][0][3],2**64)
        self.assertTrue(qualify(value,observed)['software_fvf_qualified'])

    def test_negative_zero_nonfinite_or_unqualified_rhw_reject(self):
        for bits in (0x80000000,0x7f800000,0x7fc00000,0x40000000):
            with self.subTest(bits=bits):
                value,_=fixture();data=value['streams'][0]
                value['streams'][0]=data[:12]+struct.pack('<I',bits)+data[16:]
                with self.assertRaises((AssertionError,ValueError)):expectation(value)

    def test_changed_live_input_or_bias_with_refreshed_identity_fails(self):
        for change in ('position','texcoord','bias'):
            value,observed=fixture()
            if change=='bias':value['draw']['screen']=[0,0]
            else:
                offset=0 if change=='position' else 16;data=bytearray(value['streams'][0])
                struct.pack_into('<f',data,offset,7);value['streams'][0]=bytes(data)
            value['sha256']=observed['input_sha256']='changed'
            self.assertFalse(qualify(value,observed)['software_fvf_qualified'])

    def test_disabled_descriptor_offsets_are_unconsumed(self):
        value,observed=fixture();observed['descriptor'][18]=16
        self.assertTrue(qualify(value,observed)['software_fvf_qualified'])
        observed['descriptor'][42]=24  # Active texture0 offset.
        with self.assertRaises(ValueError):qualify(value,observed)

    def test_missing_original_calls_transport_route_or_provenance_reject(self):
        for key,new in (('original_setter_returned',False),('original_draw_returned',False),
                ('fifo_get',36),('submission','original DrawVertices'),('input',True),
                ('input_sha256','changed')):
            with self.subTest(key=key):
                value,observed=fixture();observed[key]=new
                with self.assertRaises(ValueError):qualify(value,observed)

    def test_guards_and_missing_triangle_execution_cannot_qualify(self):
        for counts in ([0]*26,[0,1,0,1]+[0]*22,[0,True]+[0]*24):
            value,observed=fixture();observed['draw_counters']=counts
            with self.assertRaises(ValueError):qualify(value,observed)
        value,observed=fixture();value['draw']['primitive']=8;observed['draw_counters']=[0]*26
        self.assertTrue(qualify(value,observed)['software_fvf_qualified'])

    def test_corrupt_partial_or_nonfinite_observations_reject(self):
        for change in ('vertex','lane','fog','nonfinite','format'):
            with self.subTest(change=change):
                value,observed=fixture()
                if change=='vertex':observed['vertices'].pop()
                elif change=='lane':observed['vertices'][0].pop()
                elif change=='fog':observed['fog'].pop()
                elif change=='nonfinite':observed['vertices'][0][0]=float('nan')
                else:observed['descriptor'][7]=2
                with self.assertRaises(ValueError):qualify(value,observed)

    def test_unqualified_depth_fog_viewport_and_declarations_reject(self):
        for change in ('depth','fog','viewport','format'):
            with self.subTest(change=change):
                value,_=fixture()
                if change=='depth':
                    data=bytearray(value['streams'][0]);struct.pack_into('<f',data,8,.5)
                    value['streams'][0]=bytes(data)
                elif change=='fog':value['render'][92]=1
                elif change=='viewport':value['draw']['viewport'][4]=1
                else:value['elements'][0][0]=0x32
                with self.assertRaises(ValueError):expectation(value)


if __name__=='__main__':unittest.main()
