"""Authored malformed source records cannot opt into unfiltered reports."""
import hashlib
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_input_reports import inspect


class ReportPolicyTests(unittest.TestCase):
    def test_legacy_profile_does_not_acquire_a_new_report_policy(self):
        self.assertIsNone(inspect(None,{},{}))

    def fixture(self):
        blob=bytearray(0x200)
        blob[0:24]=struct.pack('<6I',0x1001,0x104,0x1040,0x1050,0x1060,0x1080)
        blob[0x40]=18;blob[0x50]=4;blob[0x60:0x64]=bytes([1,8,0,0])
        # This authored NOP/RET body has no copied report semantics. A correct
        # hash, report descriptor and table pointer must never suffice alone.
        blob[0x80:0xc1]=b'\x90'*64+b'\xc3'
        context=SimpleNamespace(blob=bytes(blob),image=dict(encoding='retail',base=0x1000,image_size=0x200,
            sections=[dict(address=0x1000,raw_offset=0,raw_size=0x200)]))
        def row(address,size):
            return dict(address=address,bytes=size,sha256=hashlib.sha256(blob[address-0x1000:address-0x1000+size]).hexdigest())
        selected=dict(adapter='xid-copy18-activity-v1',class_table=row(0x1000,24),callback=row(0x1080,65),activity_reset={})
        return context,dict(report_processing=selected),dict(default_polling=[1,8,0,0])

    def test_correct_hash_and_descriptors_do_not_prove_callback_semantics(self):
        with self.assertRaisesRegex(ValueError,'unfiltered-report callback'):
            inspect(*self.fixture())

    def test_unknown_and_malformed_source_records_reject(self):
        for mutation in ('adapter','missing-tail','table-hash','callback-hash','extent','boolean-address',
                         'callback-none','table-none','unmapped','duplicate-section','descriptor','polling'):
            context,proof,layout=self.fixture();selected=proof['report_processing']
            if mutation=='adapter':selected['adapter']='sdk-label-is-enough'
            elif mutation=='missing-tail':selected.pop('activity_reset')
            elif mutation=='table-hash':selected['class_table']['sha256']='0'*64
            elif mutation=='callback-hash':selected['callback']['sha256']='0'*64
            elif mutation=='extent':selected['callback']['bytes']=64
            elif mutation=='boolean-address':selected['callback']['address']=True
            elif mutation=='callback-none':selected['callback']=None
            elif mutation=='table-none':selected['class_table']=None
            elif mutation=='unmapped':context.image['sections'][0]['raw_size']=0x40
            elif mutation=='duplicate-section':context.image['sections']*=2
            elif mutation=='descriptor':
                blob=bytearray(context.blob);blob[0x40]=17;context.blob=bytes(blob)
            else:layout['default_polling']=[1,9,0,0]
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):inspect(context,proof,layout)


if __name__=='__main__':unittest.main()
