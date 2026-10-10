"""Authored equations, register fields and admission controls for FF state."""
import copy
from fractions import Fraction
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_frame_combiner_reference import argument,portion,expected_fixed_function
from test_native_frame_state_delivery import fixture


def channels(word,registers):
    # Independent NV sum-of-products semantics for authored positive inputs.
    values=[]
    for shift in (24,16,8,0):
        byte=(word>>shift)&255;source=registers.get(byte&15,(0,0))
        value=source[bool(byte&16)]
        if byte&32:value=1-value
        values.append(value)
    a,b,c,d=values
    return min(1,max(-1,a*b+c*d))


class FixedCombiners(unittest.TestCase):
    def test_documented_register_sources_and_modifiers(self):
        self.assertEqual(argument(2,3,False),11)
        self.assertEqual(argument(1,0,False),4)
        self.assertEqual(argument(1,1,True),28)
        self.assertEqual(argument(4|16|32,0,False),53)
        for value in (3,5,6,0x40,-1):
            with self.subTest(value=value),self.assertRaises(ValueError):argument(value,0,False)

    def test_modulate_and_add_match_independent_channel_equations(self):
        registers={4:(Fraction(1,2),Fraction(3,8)),8:(Fraction(1,4),Fraction(3,4)),
                   5:(Fraction(1,8),Fraction(7,8))}
        rgb=portion(4,2,0,0,False,True);alpha=portion(4,2,0,0,True,True)
        self.assertEqual(rgb,0x08040000);self.assertEqual(alpha,0x18140000)
        self.assertEqual(channels(rgb,registers),Fraction(1,8))
        self.assertEqual(channels(alpha,registers),Fraction(9,32))
        registers[12]=(Fraction(1,8),Fraction(9,32))
        add=portion(7,1,4,1,False,False)
        self.assertEqual(add,0x0c202005)
        self.assertEqual(channels(add,registers),Fraction(1,4))

    def test_select_and_complement_use_unity_from_inverted_zero(self):
        registers={4:(Fraction(1,4),Fraction(3,4)),8:(Fraction(1,2),Fraction(1,8))}
        for op in (2,3):
            self.assertEqual(channels(portion(op,0,0,0,False,True),registers),Fraction(1,4))
        self.assertEqual(channels(portion(4,2|16,0|32,0,False,True),registers),Fraction(3,8))

    def test_missing_texture_falls_back_for_whole_consumed_portion(self):
        self.assertEqual(portion(4,2,0,0,False,False),0x04200000)
        self.assertEqual(portion(4,2,0,0,True,False),0x14200000)
        self.assertEqual(portion(2,2,0,1,True,False),0x1c200000)
        # An unused texture argument cannot force a fallback.
        self.assertEqual(portion(2,4,2,0,False,False),0x05200000)

    def test_bound_second_texture_replaces_current_alpha(self):
        self.assertEqual(portion(2,2,0,1,True,True),0x19200000)
        registers={9:(Fraction(1,4),Fraction(3,4)),12:(Fraction(1,2),Fraction(1,8))}
        self.assertEqual(channels(portion(2,2,0,1,True,True),registers),Fraction(3,4))

    def test_unused_slots_are_explicitly_cleared_and_stage_disable_stops_chain(self):
        capture,scene,_=fixture();methods=expected_fixed_function(capture,scene)
        self.assertEqual(methods[0x1e60],1);self.assertEqual(methods[0x1e70],1)
        self.assertEqual(methods[0x1e78],0x210000)
        self.assertEqual(len(methods),35)
        self.assertTrue(all(methods[base+4*i]==0 for base in (0xac0,0x260,0x1e40,0xaa0) for i in range(1,8)))
        capture['texture'][2*32+12]=7
        self.assertEqual(expected_fixed_function(capture,scene),methods)

    def test_two_stages_keep_prior_alpha_when_second_texture_is_missing(self):
        capture,scene,_=fixture();state=capture['texture'];state[32+12]=7
        state[32+14:32+20]=[1,4,2,1,2,0]
        methods=expected_fixed_function(capture,scene)
        self.assertEqual(methods[0x1e60],2)
        self.assertEqual(methods[0xac4],0x0c202005)
        self.assertEqual(methods[0x264],0x1c200000)

    def test_unmodelled_operation_destination_and_custom_shader_reject(self):
        for name in ('operation','result','custom','inactive-binding'):
            capture,scene,_=fixture()
            if name=='operation':capture['texture'][12]=24
            elif name=='result':capture['texture'][20]=5
            elif name=='custom':scene['header'][8]=1
            else:scene['textures'][3]=copy.deepcopy(scene['textures'][0])
            with self.subTest(name=name),self.assertRaises(ValueError):expected_fixed_function(capture,scene)


if __name__=='__main__':unittest.main()
