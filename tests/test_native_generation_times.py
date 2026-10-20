import os
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_pilot import generated_file_times,retain_identical_file_times

class GenerationTimes(unittest.TestCase):
    def test_only_final_identical_compiler_inputs_retain_their_old_times(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);old=1_600_000_000_000_000_000
            for name in ('same.c','same.h','changed.h','deleted.c','metadata.json'):
                p=root/name;p.write_text('old bytes');os.utime(p,ns=(old,old))
            saved=generated_file_times(root)
            (root/'deleted.c').unlink();(root/'changed.h').write_text('changed bytes')
            for name in ('same.c','same.h','metadata.json'):(root/name).write_text('old bytes')
            (root/'new.c').write_text('new bytes')
            retained_names=('same.c','same.h')
            fresh={n:(root/n).stat().st_mtime_ns for n in ('changed.h','new.c','metadata.json')}
            self.assertEqual(retain_identical_file_times(root,saved),2)
            for name in retained_names:
                self.assertEqual((root/name).stat().st_mtime_ns,old)
                self.assertEqual((root/name).read_text(),'old bytes')
            for name,stamp in fresh.items():self.assertEqual((root/name).stat().st_mtime_ns,stamp)
            self.assertEqual((root/'changed.h').read_text(),'changed bytes')
            self.assertFalse((root/'deleted.c').exists())
    def test_first_generation_and_missing_output_need_no_special_case(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'not-yet-created'
            self.assertEqual(generated_file_times(root),{})
            self.assertEqual(retain_identical_file_times(root,{}),0)

if __name__=='__main__':unittest.main()
