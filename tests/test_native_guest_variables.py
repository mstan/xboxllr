"""Guest-context location tests, including scope and missing-context guards."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_guest_variables import Variables,register_bytes,frame_program_base


def fixture():
    types={'1':dict(id=1,tag=16,basic=6,length=4),'2':dict(id=2,tag=14,length=4,type=1),
           '3':dict(id=3,tag=16,basic=8,length=4)}
    scopes={'10':dict(id=10,tag=5,parent=0,address=0x11000,end=0x11040,debug_start=0x11004,debug_end=0x11038),
            '11':dict(id=11,tag=6,parent=10,address=0x11010,end=0x11020)}
    variables={'20':dict(id=20,parent=10,name='argument',type=1,data_kind=3,location=3,register_id=30006,location_offset=4),
               '21':dict(id=21,parent=11,name='local',type=1,data_kind=1,location=5,register_id=17),
               '22':dict(id=22,parent=10,name='stack',type=1,data_kind=1,location=3,register_id=22,location_offset=-4),
               '23':dict(id=23,parent=0,name='tls',type=1,data_kind=6,location=2,address=0x15004),
               '24':dict(id=24,parent=0,name='member',type=1,data_kind=7,location=4,location_offset=8),
               '25':dict(id=25,parent=10,name='pointer',type=2,data_kind=3,location=5,register_id=18),
               '26':dict(id=26,parent=10,name='constant',type=1,data_kind=9,location=10,constant=dict(kind='integer',value='-7')),
               '27':dict(id=27,parent=10,name='float',type=3,data_kind=1,location=5,register_id=162)}
    document=dict(schema='xboxllr.guest-types.v1',xbe_sha256='a'*64,types=types,scopes=scopes,variables=variables,
                  tls_template=dict(start=0x15000,size=16))
    context=dict(xbe_sha256='a'*64,pc=0x11014,registers=dict(eax=42,ecx=0x13000,ebp=0x18000,
                 xmm0=bytes.fromhex('0000803e')+bytes(12)),frame_bases={'10':{'30006':0x17000}},
                 tls=dict(base=0x19000),this_address=0x1a000)
    return document,context


class GuestVariables(unittest.TestCase):
    def test_register_stack_virtual_frame_tls_member_and_constant_values(self):
        document,context=fixture();resolver=Variables(document,'a'*64)
        memory={0x17004:123,0x17ffc:234,0x19004:345,0x1a008:456}
        def read(address,size):self.assertEqual(size,4);return memory[address].to_bytes(4,'little')
        for identifier,expected in [(20,123),(21,42),(22,234),(23,345),(24,456)]:
            self.assertEqual(resolver.value(identifier,context,read)['decoded']['value'],str(expected))
        self.assertEqual(resolver.value(25,context,read)['decoded']['address'],0x13000)
        self.assertEqual(resolver.value(26,context,read)['value'],'-7')
        self.assertEqual(resolver.value(27,context,read)['decoded']['value'],'0.25')

    def test_scope_boundaries_and_function_prologue_epilogue(self):
        document,context=fixture();resolver=Variables(document,'a'*64)
        context['pc']=0x11000;self.assertEqual(resolver.locate(20,context)['status'],'unavailable')
        for pc,status in [(0x11000,'out-of-scope'),(0x11004,'out-of-scope'),(0x11010,'available'),
                          (0x1101f,'available'),(0x11020,'out-of-scope'),(0x11040,'out-of-scope')]:
            context['pc']=pc;self.assertEqual(resolver.locate(21,context)['status'],status)
        context['pc']=0x11038;self.assertEqual(resolver.locate(20,context)['status'],'unavailable')

    def test_missing_frame_base_is_never_guessed_from_esp_or_ebp(self):
        document,context=fixture();context.pop('frame_bases');context['registers']['esp']=0x17000
        self.assertEqual(Variables(document,'a'*64).locate(20,context)['status'],'unavailable')

    def test_real_dia_program_forms_use_ebp_or_certified_return_search(self):
        document,context=fixture();context.pop('frame_bases')
        frame=dict(address=0x11000,end=0x11040,locals_length=12,saved_registers_length=8,params_length=4,
                   program='$T2 $esp .cbLocals + .cbSavedRegs + = $T0 .raSearchStart = $eip $T0 ^ =')
        document['frames']=[frame];context['registers']['esp']=0x16000
        resolver=Variables(document,'a'*64)
        self.assertEqual(resolver.locate(20,context)['status'],'unavailable')
        context['frame_search_start']={'10':0x17000}
        self.assertEqual(resolver.locate(20,context)['address'],0x17004)
        frame['program']='$T0 $ebp = $eip $T0 4 + ^ = $ebp $T0 ^ ='
        before=copy.deepcopy(context)
        self.assertEqual(resolver.locate(20,context)['address'],0x18004)
        self.assertEqual(context,before)

    def test_frame_program_memory_reads_and_malformed_programs_are_bounded(self):
        _,context=fixture()
        frame=dict(program='$T0 $ebp 4 - ^ =')
        self.assertEqual(frame_program_base(frame,'$T0',context,10,
                         lambda address,size:(0x17000).to_bytes(4,'little')),0x17000)
        for program in ('$T0 $missing =','$T0 4294967296 =','$T0 - =','$T0 1 2 xor =',' '.join(['0']*257)):
            frame['program']=program
            self.assertIsNone(frame_program_base(frame,'$T0',context,10))

    def test_wrong_build_optimized_away_and_missing_register_are_distinct(self):
        document,context=fixture();resolver=Variables(document,'a'*64)
        context['xbe_sha256']='b'*64
        with self.assertRaises(ValueError):resolver.locate(21,context)
        context['xbe_sha256']='a'*64;document['variables']['21']['optimized_away']=True
        self.assertEqual(resolver.locate(21,context)['status'],'optimized-away')
        document['variables']['21']['optimized_away']=False;context['registers'].pop('eax')
        self.assertEqual(resolver.locate(21,context)['status'],'unavailable')

    def test_pc_liveness_ranges_and_control_flow_dependent_locations(self):
        document,context=fixture();variable=document['variables']['21']
        variable.update(live_start=0x11016,live_end=0x11018,live_range_count=1)
        resolver=Variables(document,'a'*64)
        self.assertEqual(resolver.locate(21,context)['status'],'out-of-scope')
        context['pc']=0x11016;self.assertEqual(resolver.locate(21,context)['status'],'available')
        context['pc']=0x11018;variable['live_range_count']=2
        self.assertEqual(resolver.locate(21,context)['status'],'unavailable')
        del variable['live_start'];del variable['live_end'];variable['control_flow_dependent']=True
        self.assertEqual(resolver.locate(21,context)['status'],'unavailable')

    def test_register_slices_preserve_x86_and_sse_bits(self):
        registers=dict(eax=0x12345678,edx=0xabcdef01,xmm0=bytes(range(16)))
        self.assertEqual(register_bytes(5,registers),b'\x56')
        self.assertEqual(register_bytes(9,registers),bytes.fromhex('7856'))
        self.assertEqual(register_bytes(212,registers),bytes.fromhex('7856341201efcdab'))
        self.assertEqual(register_bytes(163,registers),bytes(range(4,8)))
        self.assertEqual(register_bytes(202,registers),bytes(range(8,16)))

    def test_unmapped_memory_and_invalid_scope_do_not_produce_zero(self):
        document,context=fixture();resolver=Variables(document,'a'*64)
        def unreadable(*_):raise ValueError('unmapped')
        self.assertEqual(resolver.value(20,context,unreadable)['status'],'unavailable')
        document['scopes']['11']['parent']=11
        self.assertEqual(resolver.locate(21,context)['status'],'unavailable')


if __name__=='__main__':unittest.main()
