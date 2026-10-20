import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_function_extent import apply_extents, retire_missing_guard
from native_pilot import retire_recovered_entry_units


class ExtentRecovery(unittest.TestCase):
    def setUp(self):
        self.blob=bytes.fromhex('56eb0390909031c05ec3')
        self.image={'sections':[dict(address=0x10000,raw_offset=0,raw_size=len(self.blob),flags=4)]}
        self.functions=[dict(start='0x00010000',end='0x00010003',size=3,num_instructions=2)]
        self.record=dict(start=0x10000,original_end=0x10003,end=0x1000a,sha256=hashlib.sha256(self.blob).hexdigest())

    def test_replay_is_idempotent_without_mutating_input(self):
        first=apply_extents(self.blob,self.image,self.functions,[self.record])
        self.assertEqual(first[0]['end'],'0x0001000A')
        self.assertEqual(first[0]['size'],len(self.blob))
        self.assertEqual(self.functions[0]['size'],3)
        self.assertEqual(apply_extents(self.blob,self.image,first,[self.record]),first)

    def test_reject_changed_image_bytes(self):
        with self.assertRaisesRegex(ValueError,'bytes changed'):
            apply_extents(b'\x90'+self.blob[1:],self.image,self.functions,[self.record])

    def test_reject_overlapping_discovery(self):
        other=dict(start='0x00010006',end='0x0001000A',size=4)
        with self.assertRaisesRegex(ValueError,'overlaps'):
            apply_extents(self.blob,self.image,self.functions+[other],[self.record])

    def test_reject_non_executable_extent(self):
        self.image['sections'][0]['flags']=0
        with self.assertRaisesRegex(ValueError,'executable'):
            apply_extents(self.blob,self.image,self.functions,[self.record])

    def test_source_bound_shared_tail_keeps_its_independent_entry(self):
        other=dict(start='0x00010006',end='0x0001000A',size=4)
        self.record['contained_entries']=[dict(start=0x10006,end=0x1000a,
            sha256=hashlib.sha256(self.blob[6:]).hexdigest())]
        result=apply_extents(self.blob,self.image,self.functions+[other],[self.record])
        self.assertEqual(len(result),2)
        self.assertEqual(result[1],other)
        self.assertEqual(apply_extents(self.blob,self.image,result,[self.record]),result)

    def test_shared_tail_requires_exact_source_identity_and_boundary(self):
        other=dict(start='0x00010006',end='0x0001000A',size=4)
        self.record['contained_entries']=[dict(start=0x10006,end=0x1000a,sha256='0'*64)]
        with self.assertRaisesRegex(ValueError,'bytes changed'):
            apply_extents(self.blob,self.image,self.functions+[other],[self.record])
        self.record['contained_entries'][0]['end']=0x10009
        with self.assertRaisesRegex(ValueError,'overlaps'):
            apply_extents(self.blob,self.image,self.functions+[other],[self.record])

    def test_reject_changed_old_boundary(self):
        self.functions[0]['end']='0x00010004'
        with self.assertRaisesRegex(ValueError,'extent changed'):
            apply_extents(self.blob,self.image,self.functions,[self.record])

    def test_reject_missing_return(self):
        blob=self.blob[:-1]+b'\x90';self.record['sha256']=hashlib.sha256(blob).hexdigest()
        with self.assertRaisesRegex(ValueError,'through a return'):
            apply_extents(blob,self.image,self.functions,[self.record])

    def test_recover_observed_entry_and_replay(self):
        self.image['sections'][0]['name']='.text'
        record=dict(start=0x10006,original_end=0x10006,end=0x1000a,new_entry=True,
                    sha256=hashlib.sha256(self.blob[6:]).hexdigest())
        result=apply_extents(self.blob,self.image,self.functions,[record])
        self.assertEqual(result[1]['name'],'sub_00010006')
        self.assertEqual(result[1]['num_instructions'],3)
        self.assertEqual(len(self.functions),1)
        self.assertEqual(apply_extents(self.blob,self.image,result,[record]),result)

    def test_new_entry_cannot_split_existing_body(self):
        self.image['sections'][0]['name']='.text'
        existing=dict(start='0x00010004',end='0x0001000A',size=6)
        record=dict(start=0x10006,original_end=0x10006,end=0x1000a,new_entry=True,
                    sha256=hashlib.sha256(self.blob[6:]).hexdigest())
        with self.assertRaisesRegex(ValueError,'overlaps'):
            apply_extents(self.blob,self.image,[existing],[record])

    def test_explicit_source_bound_tail_wrapper_replays_without_next_body(self):
        blob=bytes.fromhex('8b442404ff209090c3')
        image={'sections':[dict(name='.text',address=0x10000,raw_offset=0,raw_size=len(blob),flags=4)]}
        next_body=dict(start='0x00010008',end='0x00010009',size=1)
        record=dict(start=0x10000,original_end=0x10000,end=0x10006,new_entry=True,
                    terminal='tail-jump',sha256=hashlib.sha256(blob[:6]).hexdigest())
        result=apply_extents(blob,image,[next_body],[record])
        self.assertEqual(result[0]['size'],6)
        self.assertEqual(result[1],next_body)
        self.assertEqual(apply_extents(blob,image,result,[record]),result)
        del record['terminal']
        with self.assertRaisesRegex(ValueError,'through a return'):
            apply_extents(blob,image,[next_body],[record])

    def test_tail_wrapper_rejects_fallthrough_conditional_and_partial_instruction(self):
        for blob in [bytes.fromhex('31c090'),bytes.fromhex('31c07400'),bytes.fromhex('31c0ff24')]:
            image={'sections':[dict(name='.text',address=0x10000,raw_offset=0,raw_size=len(blob),flags=4)]}
            record=dict(start=0x10000,original_end=0x10000,end=0x10000+len(blob),new_entry=True,
                        terminal='tail-jump',sha256=hashlib.sha256(blob).hexdigest())
            with self.assertRaisesRegex(ValueError,'unconditional tail jump'):
                apply_extents(blob,image,[],[record])

    def test_full_regeneration_retires_only_replaced_entry(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary);old=directory/'recomp_entry_00010006.c'
            old.write_text('original retained body',encoding='utf-8')
            record=dict(start=0x10006,new_entry=True)
            with self.assertRaisesRegex(ValueError,'did not replace'):
                retire_recovered_entry_units(directory,[record])
            self.assertTrue(old.exists())
            (directory/'recomp_0000.c').write_text('void sub_00010006(void)\n{\n}\n',encoding='utf-8')
            retire_recovered_entry_units(directory,[record])
            self.assertFalse(old.exists())
            self.assertTrue((directory/'recomp_0000.c').exists())
            retire_recovered_entry_units(directory,[record])


class MissingTargetRetirement(unittest.TestCase):
    first='void sub_00010000(void) { RECOMP_UNIMPL("undiscovered direct target", 0x00010000u); }\n'
    other='void sub_00020000(void) { RECOMP_UNIMPL("undiscovered direct target", 0x00020000u); }\n'

    def test_direct_recovery_keeps_only_other_guards_and_updates_inventory(self):
        source='#include "recomp_funcs.h"\n'+self.first+self.other
        unchanged='void sub_00030000(void)\n{ sub_00010000(); }\n'
        edits,count=retire_missing_guard([('guards.c',source),('caller.c',unchanged)],'sub_00010000',0x10000,2)
        self.assertEqual(count,1)
        self.assertEqual(edits,{'guards.c':'#include "recomp_funcs.h"\n'+self.other})
        self.assertEqual(source.count('RECOMP_UNIMPL'),2) # proposed edits do not mutate input

    def test_indirect_entry_without_old_guard_preserves_inventory(self):
        self.assertEqual(retire_missing_guard([('guards.c',self.other)],'sub_00010000',0x10000,1),({},1))

    def test_reject_duplicate_owners_or_duplicate_definitions(self):
        for sources in [[('a.c',self.first),('b.c',self.first)],[('a.c',self.first+self.first)]]:
            with self.assertRaisesRegex(ValueError,'duplicate|unreviewed'):
                retire_missing_guard(sources,'sub_00010000',0x10000,2)

    def test_reject_wrong_address_or_non_guard_body(self):
        for body in [self.first.replace('0x00010000u','0x00010001u'),
                     self.first.replace('undiscovered direct target','translation failed'),
                     self.first.rstrip()+' extra_tokens\n',
                     'void sub_00010000(void) { return; }\n']:
            with self.assertRaisesRegex(ValueError,'unreviewed'):
                retire_missing_guard([('a.c',body)],'sub_00010000',0x10000,1)

    def test_reject_invalid_inventory_before_publishing_edits(self):
        for count in [0,-1,True,'1']:
            with self.assertRaisesRegex(ValueError,'inventory disagrees'):
                retire_missing_guard([('a.c',self.first)],'sub_00010000',0x10000,count)


if __name__=='__main__':unittest.main()
