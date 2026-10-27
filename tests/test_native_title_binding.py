"""Authored machine-code fixtures, without game binaries or generated assets."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_binding import Binding
import native_title_binding as title


class TitleFunctions(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.context = c = Binding.__new__(Binding)
        c.output = root
        c.generated = root / 'gen'
        c.generated.mkdir()
        c.manifest_path = root / 'generation.aot-private.json'
        c.manifest = {}
        c.image = dict(sha256='a'*64, sections=[dict(address=0x1000, raw_offset=0, raw_size=32)])
        c.blob = bytes.fromhex('b811000000c3') + bytes(10) + bytes.fromhex('b822000000c20400') + bytes(8)
        c.functions = [dict(start=hex(a), end=hex(a+n), size=n, name=f'sub_{a:08X}') for a, n in [(0x1000,6),(0x1010,8)]]
        entries = [dict(id=i+1,address=int(f['start'],16),bytes=f['size'],sha256=c.digest(f),stack_bytes=i*4) for i,f in enumerate(c.functions)]
        self.proof = dict(adapter='static-title-functions-v1',functions=entries)
        c.require_profile = lambda *args, **kwargs: self.proof
        (c.generated/'recomp_0.c').write_text('\n'.join(f'void {f["name"]}(void)\n{{ /* authored fixture */ }}' for f in c.functions))

    def bind(self):
        with patch.object(title, 'Binding', return_value=self.context):
            return title.bind(None,None,None)

    def test_wrappers_original_registry_and_idempotent_inventory(self):
        self.bind()
        generated = self.context.generated
        original = (generated/'recomp_0.c').read_text()
        self.assertIn('void pilot_title_original_1(void)',original)
        self.assertIn('pilot_title_function(2,pilot_title_original_2,4)',(generated/'pilot_title_hook_2_binding.c').read_text())
        self.assertIn('case 2:*cleanup=4;return pilot_title_original_2;', (generated/'pilot_title_binding.c').read_text())
        inventory = dict(self.context.manifest['files'])
        self.bind()
        self.assertEqual(self.context.manifest['files'],inventory)
        for name,digest in inventory.items():
            self.assertEqual(hashlib.sha256((generated/name).read_bytes()).hexdigest(),digest)

    def test_reject_unknown_body_address_duplicate_and_stack_contract(self):
        for key,bad in [('sha256','b'*64),('address',0x1001),('id',0),('id',True),('stack_bytes',6),('bytes',0)]:
            row = self.proof['functions'][0]
            old = row[key]
            row[key] = bad
            with self.subTest(key=key,bad=bad), self.assertRaises(ValueError):
                self.bind()
            row[key] = old
        self.proof['functions'][1]['id'] = 1
        with self.assertRaisesRegex(ValueError,'duplicate'):
            self.bind()
        self.assertFalse((self.context.generated/'pilot_title_binding.c').exists())

    def test_source_bound_caller_filter_and_rejection(self):
        c=self.context
        blob=bytearray(c.blob);blob[8:13]=b'\xe8'+(-13).to_bytes(4,'little',signed=True);c.blob=bytes(blob)
        row=self.proof['functions'][0];row['callers']=[0x100d]
        self.bind()
        wrapper=(c.generated/'pilot_title_hook_1_binding.c').read_text()
        self.assertIn('if(MEM32(g_esp)!=0x0000100du){pilot_title_original_1();return;}',wrapper)
        for invalid in ([0x100c],[0x1010],[],[True],[0x100d,0x100d]):
            row['callers']=invalid
            with self.subTest(callers=invalid),self.assertRaises(ValueError):self.bind()

if __name__ == '__main__':
    unittest.main()
