import hashlib,struct,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_texture_replacements import prepare


class ReplacementTests(unittest.TestCase):
    def test_index_dedup_ambiguity_and_cleanup(self):
        entry=dict(format=0xe,width=4,height=4,source=bytes(range(16)),hd_width=16,hd_height=16,rgba=bytes((1,2,3,255))*256)
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);index=prepare(root,[entry,entry]);data=index.read_bytes()
            self.assertEqual(data[:12],b'XLLEHD1\0'+struct.pack('<I',1))
            self.assertEqual(len(data),120)
            self.assertEqual(data[56:88],hashlib.sha256(entry['source']).digest())
            first=hashlib.sha256(entry['rgba']).hexdigest()+'.rgba'
            self.assertEqual((root/first).read_bytes(),entry['rgba'])
            conflict=dict(entry,rgba=bytes((4,5,6,255))*256)
            self.assertIsNone(prepare(root,[entry,conflict]))
            self.assertEqual((root/'textures.bin').read_bytes(),b'XLLEHD1\0'+bytes(4))
            prepare(root,[conflict]);self.assertFalse((root/first).exists())
            for bad in (dict(entry,hd_width=0),dict(entry,rgba=b'x'),dict(entry,format=0xf)):
                with self.assertRaises(ValueError):prepare(root,[bad])

if __name__=='__main__':unittest.main()
