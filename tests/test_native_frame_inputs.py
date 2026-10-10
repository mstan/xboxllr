"""Independent authored containers exercise missing data and temporal linkage.

No renderer, original title code, audio or hardware runs in this suite. The C
writer contract separately emits this format from authored RAM/storage.
"""
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_frame_inputs import audit, MAGIC, fvf_extent


def words(values):
    return struct.pack('<'+'I'*len(values),*values)


def authored():
    records=[];assets={}
    def record(kind,values):records.append((kind,bytearray(words(values))))
    def asset(data):
        if data not in assets:
            number=len(assets)+1;assets[data]=number
            records.append((1,bytearray(words([number,len(data)])+hashlib.sha256(data).digest()+data)))
        return assets[data]
    resource=asset(bytes(20));back=asset(bytes(range(16)));depth=asset(bytes(range(16,32)))
    record(2,[1,0,0x100,0xfffffffd,0,0,2,2,87,back]+[0]*5)
    record(2,[2,0,0x140,0xfffffffe,0,1,2,2,44,depth]+[0]*5)
    record(7,[1,2]);record(5,[1,0,0,0xf3,0,0x3f800000,0,1,2])
    record(3,[1,2,asset(bytes(16))]);record(3,[2,2,depth])
    initial=asset(bytes(range(32,48)))
    record(2,[3,2,0x1100,7,0,0,2,2,87,initial]+[0]*5)
    record(6,[2,3,2,0x1100,0x140,resource,resource])
    render=asset(bytes(664));texture_states=asset(bytes(512));packed=asset(bytes(64))
    code=asset(bytes(16));constants=asset(bytes(3072));fvf=asset(bytes(0x114))
    elements=asset(words([0x42,0,0]+[item for _ in range(15) for item in (2,0,0)]))
    def texture(producer,key,header):
        alias=producer==1
        return asset(words([header,9 if alias else 7,1 if alias else 0,0,0,0,int(alias),1,0x12,2,2,1,
                            resource,0 if alias else initial,0,key,0,0,0,2,2,8,2,16,0,
                            back if alias else initial,producer]))
    rows=[dict(event='begin',schema=1,swap=17,width=2,height=2,target=0x100,depth=0x140)]
    def call(index,name,args,shader=0x2001,input_sequence=0):
        rows.append(dict(event='call',sequence=index,name=name,args=args,shader=shader,
                         target=0x1100 if index>=2 else 0x100,depth=0x140,
                         capture_sequence=0,return_eax=0,input_sequence=input_sequence))
    call(1,'D3DDevice_Clear',[0,0,0xf3,0,0x3f800000,0])
    call(2,'D3DDevice_SetRenderTarget',[0x1100,0x140])
    for kind in range(4):
        up=kind&1;fixed=kind>=2;count=4 if fixed else 3;extent=24 if fixed else 16
        stride=extent if up else 32 if fixed else 24;first=0 if up or fixed else 1
        pointer=0x5000 if up else 0;shader=0x104 if fixed else 0x2001
        consumed=(count-1)*stride+extent
        stream=[stride,consumed,asset(bytes(i%256 for i in range(consumed))),0 if up else 0x3000,
                pointer if up else 0x4000+first*stride,extent]
        tex=texture(3 if fixed else 1,7 if fixed else 0xfffffffd,0x1000 if fixed else 0x1200)
        draw=[kind+1,kind+3,kind,8 if fixed else 5,first,count,shader,pointer,stride if up else 0,3,2,
              0,0,2,2,0,0x3f800000,0,0,0x4b7fffff,
              0 if fixed else 1,0 if fixed else code,0 if fixed else constants,0 if fixed else elements,
              fvf if fixed else 0,render,texture_states,0,0,packed]+stream+[0]*90+[tex,0,0,0]
        record(4,draw)
        args=[draw[3],count,pointer,stride] if up else [draw[3],first,count]
        call(kind+3,'D3DDevice_DrawVerticesUP' if up else 'D3DDevice_DrawVertices',args,shader,kind+1)
    record(3,[3,7,asset(bytes(range(48,64)))]);record(3,[2,7,depth])
    record(8,[6,4,len(assets),3])
    rows.append(dict(event='end',swap=18,calls=6,draws=4,captured=0,clears=1,target_sets=1,inputs=4))
    return [MAGIC,2,17,2,2,0x100,0x140,0],records,rows


class FrameInputs(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.path=Path(self.temporary.name)/'frame.jsonl';self.binary=self.path.with_name('frame-inputs.bin')
        self.header,self.records,self.rows=authored()

    def run_capture(self):
        self.path.write_text(''.join(json.dumps(row)+'\n' for row in self.rows))
        self.binary.write_bytes(words(self.header)+b''.join(words([kind,len(data)])+data for kind,data in self.records))
        return audit(self.path)

    def record_index(self,kind,occurrence=0):
        return [i for i,(k,_) in enumerate(self.records) if k==kind][occurrence]

    def set_word(self,kind,index,value,occurrence=0):
        struct.pack_into('<I',self.records[self.record_index(kind,occurrence)][1],index*4,value)

    def omit(self,kind,occurrence=0):
        del self.records[self.record_index(kind,occurrence)]

    def reject(self):
        with self.assertRaises(ValueError):self.run_capture()

    def test_four_kinds_and_target_dependencies_are_coverage_only(self):
        result=self.run_capture()
        self.assertTrue(result['complete_draw_input_coverage'])
        self.assertFalse(result['complete_pixel_qualification'])
        self.assertEqual(list(result['draw_kinds'].values()),[1]*4)
        self.assertEqual(result['vertices'],14)
        self.assertEqual(result['texture_producer_references'],4)
        self.assertEqual(len(result['targets']),3)

    def test_minimum_bound_span_does_not_require_last_stride_padding(self):
        result=self.run_capture()
        self.assertEqual(result['draws_detail'][0]['streams'][0][:2],[24,64])
        self.assertEqual(result['draws_detail'][2]['streams'][0][:2],[32,120])

    def test_zero_bound_stride_broadcast_is_complete(self):
        sixteen=next(struct.unpack_from('<I',data)[0] for k,data in self.records
                     if k==1 and data[40:]==bytes(range(16)))
        self.set_word(4,30,0);self.set_word(4,31,16);self.set_word(4,32,sixteen)
        self.assertTrue(self.run_capture()['complete_draw_input_coverage'])

    def test_missing_draw_even_when_footer_is_adjusted(self):
        self.omit(4,3);self.set_word(8,1,3);self.rows[-1]['inputs']=3;self.reject()

    def test_deleted_draw_reused_number_or_wrong_call(self):
        for position,value in ((0,1),(1,5),(6,0x104),(9,1),(10,3),(30,25),(31,72),(35,17),(126,9999)):
            with self.subTest(position=position):
                self.header,self.records,self.rows=authored();self.set_word(4,position,value,1);self.reject()

    def test_bad_checksum_is_rejected(self):
        self.records[self.record_index(1)][1][-1]^=1;self.reject()

    def test_missing_asset_is_rejected(self):
        self.omit(1);self.reject()

    def test_missing_clear_bind_root_and_footer(self):
        for kind in (5,6,7,8):
            with self.subTest(kind=kind):
                self.header,self.records,self.rows=authored();self.omit(kind);self.reject()

    def test_missing_initial_target(self):
        self.omit(2);self.reject()

    def test_missing_prebind_and_final_observations(self):
        for index in range(4):
            with self.subTest(index=index):
                self.header,self.records,self.rows=authored();self.omit(3,index);self.reject()

    def test_duplicate_prebind_observation(self):
        index=self.record_index(3);self.records.insert(index,copy.deepcopy(self.records[index]));self.reject()

    def test_observed_image_does_not_replace_initial_root(self):
        self.set_word(2,9,9999);self.reject()

    def test_observation_of_unbound_target(self):
        self.set_word(3,0,1,2);self.reject()

    def test_target_subresource_cannot_be_rooted_twice(self):
        self.set_word(2,3,0xfffffffd,2);self.reject()

    def test_target_binding_guest_identity(self):
        self.set_word(6,3,0x1200);self.reject()

    def test_container_belongs_to_same_frame(self):
        self.header[2]+=1;self.reject()

    def test_ledger_input_sequence_is_a_word(self):
        self.rows[3]['input_sequence']=True;self.reject()

    def test_returned_submission_links_primitive_range_and_shader(self):
        for field in ('shader','target','depth'):
            with self.subTest(field=field):
                self.header,self.records,self.rows=authored();self.rows[3][field]+=2;self.reject()
        self.header,self.records,self.rows=authored();self.rows[3]['args'][1]+=1;self.reject()

    def test_missing_shader_or_state_asset(self):
        for index in (21,22,23,25,26,29):
            with self.subTest(index=index):
                self.header,self.records,self.rows=authored();self.set_word(4,index,0);self.reject()

    def test_missing_fvf_descriptor_and_user_stream(self):
        self.set_word(4,24,0,2);self.reject()
        self.header,self.records,self.rows=authored();self.set_word(4,32,0,1);self.reject()

    def test_texture_producer_requires_matching_subresource(self):
        for index,value in ((26,2),(26,0),(15,99),(16,1)):
            with self.subTest(index=index):
                self.header,self.records,self.rows=authored()
                asset=struct.unpack_from('<I',self.records[self.record_index(4)][1],126*4)[0]
                entry=next(data for k,data in self.records if k==1 and struct.unpack_from('<I',data)[0]==asset)
                struct.pack_into('<I',entry,40+index*4,value)
                entry[8:40]=hashlib.sha256(entry[40:]).digest();self.reject()

    def test_partial_record_and_data_after_footer(self):
        self.run_capture();original=self.binary.read_bytes()
        for data in (original[:-1],original[:-16],original+b'bad',original+words([7,8,1,2])):
            with self.subTest(length=len(data)):
                self.binary.write_bytes(data)
                with self.assertRaises(ValueError):audit(self.path)

    def test_oversized_record_is_rejected_before_payload(self):
        self.run_capture();self.binary.write_bytes(words(self.header)+words([1,0xffffffff]))
        with self.assertRaises(ValueError):audit(self.path)

    def test_fvf_sizes_and_unsupported_layouts(self):
        self.assertEqual(fvf_extent(0x104),24)
        self.assertEqual(fvf_extent(0x1c4),32)
        for value in (0x102,0x504,0x10004,0x114):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):fvf_extent(value)


if __name__=='__main__':unittest.main()
