import hashlib
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_function_extent import apply_extents


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

    def test_reject_changed_old_boundary(self):
        self.functions[0]['end']='0x00010004'
        with self.assertRaisesRegex(ValueError,'extent changed'):
            apply_extents(self.blob,self.image,self.functions,[self.record])

    def test_reject_missing_return(self):
        blob=self.blob[:-1]+b'\x90';self.record['sha256']=hashlib.sha256(blob).hexdigest()
        with self.assertRaisesRegex(ValueError,'through a return'):
            apply_extents(blob,self.image,self.functions,[self.record])


if __name__=='__main__':unittest.main()
