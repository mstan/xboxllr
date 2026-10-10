"""No recorder-only or newly guarded draw may pass complete submission."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_scene_draw_reference import counter_fields,complete_submission


class Submission(unittest.TestCase):
    def test_all_dynamic_counter_fields_are_discovered(self):
        names=[b'attempts',b'executions',b'color-writes']+[f'reason-{i}'.encode() for i in range(25)]
        self.assertEqual(counter_fields(lambda i:names[i] if i<len(names) else b'unknown'),
                         [n.decode() for n in names])

    def test_missing_duplicate_or_unbounded_names_reject(self):
        for fn in (lambda i:None,lambda i:b'unknown',lambda i:b'attempts',lambda i:f'unbounded-{i}'.encode()):
            with self.assertRaises(AssertionError):counter_fields(fn)

    def test_returned_vertices_without_raster_execution_reject(self):
        c=dict(attempts=0,executions=0,**{'color-writes':0})
        self.assertFalse(complete_submission(100,100,4,4,c))

    def test_zero_writes_after_an_executed_draw_are_valid(self):
        c=dict(attempts=1,executions=1,**{'color-writes':0})
        self.assertTrue(complete_submission(100,100,4,4,c))
        self.assertFalse(complete_submission(96,100,4,4,c))
        self.assertFalse(complete_submission(100,100,3,4,c))

    def test_a_new_skip_reason_rejects_submission(self):
        c=dict(attempts=1,executions=1,**{'color-writes':1,'stencil-func':1})
        self.assertFalse(complete_submission(100,100,4,4,c))


if __name__=='__main__':unittest.main()
