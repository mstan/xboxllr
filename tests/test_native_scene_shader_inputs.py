"""Authored fetch/default/span checks for the independent scene input oracle."""
from fractions import Fraction
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_scene_shader_reference import fetch, power32, scalar_op, evaluate


def capture(fmt,data,stride,offset=0,stream=0,slot=0):
    elements=[[2,0,0] for _ in range(16)];elements[slot]=[fmt,stream,offset]
    metadata=[[0,0] for _ in range(16)];metadata[stream]=[stride,len(data)]
    streams=[b'' for _ in range(16)];streams[stream]=data
    return dict(elements=elements,metadata=metadata,streams=streams)


class ShaderInputs(unittest.TestCase):
    def test_float_dimensions_have_documented_defaults(self):
        values=[Fraction(1,4),Fraction(-1,2),Fraction(3,4),Fraction(2)]
        for count,expected in ((1,[values[0],0,0,1]),(2,values[:2]+[0,1]),
                               (3,values[:3]+[1]),(4,values)):
            with self.subTest(count=count):
                data=struct.pack('<'+'f'*count,*values[:count])
                self.assertEqual(fetch(capture((count<<4)|2,data,len(data)),0)[0],expected)

    def test_multiple_streams_and_nonzero_declaration_offset(self):
        data=bytes(8)+struct.pack('<4f',.25,-.5,.75,2)
        result=fetch(capture(0x42,data,32,8,15,7),0)
        self.assertEqual(result[7],[Fraction(1,4),Fraction(-1,2),Fraction(3,4),2])
        self.assertEqual(result[0],[0,0,0,1])

    def test_minimum_final_span_and_zero_stride(self):
        data=struct.pack('<2f',.25,-.5)+bytes(8)+struct.pack('<2f',.75,2)
        self.assertEqual(fetch(capture(0x22,data,16),1)[0],[Fraction(3,4),2,0,1])
        self.assertEqual(fetch(capture(0x22,data[:8],0),100)[0],[Fraction(1,4),Fraction(-1,2),0,1])

    def test_overlapping_vertices_are_legal_when_bytes_exist(self):
        data=struct.pack('<3f',.25,.5,.75)
        self.assertEqual(fetch(capture(0x22,data,4),1)[0],[Fraction(1,2),Fraction(3,4),0,1])

    def test_missing_live_bytes_and_unknown_format_are_rejected(self):
        for fmt,data,stride,vertex in ((0x42,bytes(15),16,0),(0x22,bytes(23),16,1),(0x99,bytes(16),16,0)):
            with self.subTest(fmt=fmt,vertex=vertex):
                with self.assertRaises(AssertionError):fetch(capture(fmt,data,stride),vertex)

    def test_bad_stream_extent_and_index_are_rejected(self):
        value=capture(0x42,bytes(16),16);value['metadata'][0][1]=17
        with self.assertRaises(AssertionError):fetch(value,0)
        value=capture(0x42,bytes(16),16);value['elements'][0][1]=16
        with self.assertRaises(AssertionError):fetch(value,0)

    def test_nonfinite_float_inputs_need_a_separate_contract(self):
        for bits in (0x7f800000,0xff800000,0x7fc00001):
            with self.subTest(bits=bits):
                with self.assertRaises(AssertionError):fetch(capture(0x12,struct.pack('<I',bits),4),0)

    def test_legacy_packed_inputs_keep_exact_channel_math(self):
        color=struct.pack('<I',0x80102040)
        self.assertEqual(fetch(capture(0x40,color,4),0)[0],
                         [Fraction(struct.unpack('<f',struct.pack('<f',n/255))[0]) for n in (16,32,64,128)])
        short=struct.pack('<2h',-32768,32767)
        self.assertEqual(fetch(capture(0x25,short,4),0)[0],[-32768,32767,0,1])

    def test_fractional_power_rounding_has_an_independent_midpoint_proof(self):
        for degree in (2,4,8,16):
            for value in (Fraction(1,4),Fraction(3,4),Fraction(5,4),Fraction(49,4),Fraction(31,32)):
                with self.subTest(degree=degree,value=value):
                    result=power32(value,Fraction(1,degree))
                    bits=struct.unpack('<I',struct.pack('<f',float(result)))[0]
                    number=lambda v:Fraction(struct.unpack('<f',struct.pack('<I',v))[0])
                    lo=(number(bits-1)+result)/2;hi=(number(bits+1)+result)/2
                    self.assertTrue(lo**degree<=value<=hi**degree)
                    if value in (lo**degree,hi**degree):self.assertEqual(bits%2,0)

    def test_lit_vector_and_zero_branches(self):
        self.assertEqual(scalar_op(7,[Fraction(-1),Fraction(1,2),0,Fraction(3,2)]),[1,0,0,1])
        self.assertEqual(scalar_op(7,[Fraction(1,2),Fraction(1,2),0,Fraction(2)]),[1,Fraction(1,2),Fraction(1,4),1])
        self.assertEqual(scalar_op(7,[Fraction(1),Fraction(0),0,Fraction(0)]),[1,1,1,1])
        self.assertEqual(scalar_op(7,[Fraction(1),Fraction(0),0,Fraction(2)]),[1,1,0,1])
        with self.assertRaises(AssertionError):scalar_op(7,[Fraction(1),Fraction(0),0,Fraction(-1)])

    def arl_capture(self,value,paired=False):
        result=capture(0x12,struct.pack('<f',value),4,slot=1)
        # Authored published fields: ARL v1.x; MOV oD0,c[96+a0]. A paired
        # ILU reads the old address before MAC commits its new scalar address.
        arl=[0,(13<<21)|(1<<9)|0x1b,2<<26,0]
        if paired:
            arl[1]|=(1<<25)|(96<<13)
            arl[2]|=0x1b<<2
            arl[3]=(3<<28)|(15<<12)|(1<<11)|(3<<3)|(1<<2)|(1<<1)
        mov=[0,(1<<21)|(96<<13)|0x1b,3<<26,(15<<12)|(1<<11)|((9 if paired else 3)<<3)|3]
        result['code']=arl+mov
        result['constants']=struct.pack('<'+'f'*768,*[value for i in range(192) for value in (i,i+1,i+2,i+3)])
        return result

    def test_arl_floor_signed_relative_indices_and_scalar_store(self):
        for value,index in ((1.5,97),(-1.5,94),(0,96),(-96,0)):
            with self.subTest(value=value):
                outputs,masks=evaluate(self.arl_capture(value),0)
                self.assertEqual(outputs[3],[index,index+1,index+2,index+3])
                self.assertEqual(masks[3],15)

    def test_paired_arl_reads_old_address_before_committing_new(self):
        outputs,_=evaluate(self.arl_capture(1.5,True),0)
        self.assertEqual(outputs[3],[96,97,98,99])
        self.assertEqual(outputs[9],[97,98,99,100])

    def test_near_integer_arl_and_outside_context_remain_guarded(self):
        almost_one=struct.unpack('<f',struct.pack('<I',0x3f7fffff))[0]
        for value in (almost_one,-97,100):
            with self.subTest(value=value):
                with self.assertRaises(AssertionError):evaluate(self.arl_capture(value),0)


if __name__=='__main__':unittest.main()
