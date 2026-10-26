"""Authored disc fixtures exercise public XDVDFS structures and import behavior."""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_disc as disc
from test_xbe_identity import fixture


def table(rows):
    chunks=[];offset=0
    for i,(name,attributes,sector,size) in enumerate(rows):
        raw=name.encode('cp1252');length=(14+len(raw)+3)&~3
        record=bytearray(length)
        struct.pack_into('<HHIIBB',record,0,0,(offset+length)//4 if i+1<len(rows) else 0,sector,size,attributes,len(raw))
        record[14:14+len(raw)]=raw;chunks.append(record);offset+=length
    return b''.join(chunks)


def sparse(file):
    if os.name=='nt':
        import ctypes,msvcrt
        kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        kernel.DeviceIoControl.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_uint,ctypes.POINTER(ctypes.c_uint),ctypes.c_void_p]
        done=ctypes.c_uint()
        if not kernel.DeviceIoControl(msvcrt.get_osfhandle(file.fileno()),0x900c4,None,0,None,0,ctypes.byref(done),None):
            raise OSError(ctypes.get_last_error(),'Cannot mark authored image sparse')


def make_disc(path, *, base=0, xbe=None, extra=(), mutate=None):
    xbe=fixture('Halo') if xbe is None else xbe
    payload=b'authored texture payload\0\xff'
    child=table([('texture.bin',0,60,len(payload))])
    root=table([('default.xbe',0,50,len(xbe)),('assets',0x10,42,len(child)),('empty',0x10,0,0),*extra])
    header=bytearray(2048);header[:20]=header[-20:]=disc.MAGIC
    struct.pack_into('<II',header,20,40,len(root))
    with path.open('w+b') as file:
        sparse(file);file.truncate(base+80*2048)
        for sector,content in [(32,header),(40,root),(42,child),(50,xbe),(60,payload)]:
            file.seek(base+sector*2048);file.write(content)
        if mutate:mutate(file,base)
    return xbe,payload


class SilentProgress:
    def __init__(self,cancel=False):self.entered=False;self.cancel=cancel;self.updates=[]
    def __enter__(self):self.entered=True;return self
    def __exit__(self,*_):pass
    def check(self):pass
    def update(self,*values):
        self.updates.append(values)
        if self.cancel:raise disc.ImportCancelled()


class DiscTests(unittest.TestCase):
    def cache_entries(self):
        return [p for p in (self.root/'data/discs').iterdir() if p.name!='session.lock']
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.path=self.root/'game.iso'
        self.xbe,self.payload=make_disc(self.path)
        self.data=dict(title=dict(name='Halo',game_id='4D530004',xbe_sha256=hashlib.sha256(self.xbe).hexdigest()))

    def test_partition_locations_extensions_and_64_bit_source_size(self):
        for base in disc.PARTITIONS:
            for extension in ('.iso','.XISO'):
                path=self.root/f'offset-{base:x}{extension}';make_disc(path,base=base)
                with disc.Disc(path) as image:
                    self.assertEqual(image.base,base)
                    self.assertEqual(image.xbe,self.xbe)
                    self.assertEqual({e.path for e in image.entries()}, {'default.xbe','assets','empty','assets/texture.bin'})
                self.assertEqual(disc.image_digest(path),self.data['title']['xbe_sha256'])

    def test_import_preserves_all_bytes_and_reuses_without_payload_read(self):
        before=self.path.stat();progress=SilentProgress()
        result=disc.prepare_game(self.root,self.data,self.path,progress)
        self.assertEqual(result.read_bytes(),self.xbe)
        self.assertEqual((result.parent/'assets/texture.bin').read_bytes(),self.payload)
        self.assertTrue((result.parent/'empty').is_dir())
        self.assertTrue(progress.entered)
        self.assertEqual(sum(1 for _ in result.parent.rglob('texture.bin')),1)
        self.assertEqual(self.path.stat().st_mtime_ns,before.st_mtime_ns)
        reuse=SilentProgress()
        with patch.object(disc.Disc,'entries',side_effect=AssertionError('cache hit scanned payload tree')):
            self.assertEqual(disc.prepare_game(self.root,self.data,self.path,reuse),result)
        self.assertFalse(reuse.entered)
        self.assertEqual(len(self.cache_entries()),1)

    def test_unknown_game_and_revision_rejected_before_any_extraction(self):
        for image in (fixture('JSRF',0x5345000a),fixture('Halo')+b'other revision'):
            make_disc(self.path,xbe=image)
            with self.assertRaisesRegex(ValueError,'Wrong game|not a supported original revision'):
                disc.prepare_game(self.root,self.data,self.path,SilentProgress())
            self.assertFalse((self.root/'data').exists())

    def test_malformed_tables_and_extents_never_publish(self):
        mutations=[
            lambda f,b:(f.seek(b+32*2048+2047),f.write(b'?')),
            lambda f,b:(f.seek(b+40*2048+2),f.write(struct.pack('<H',1))),
            lambda f,b:(f.seek(b+40*2048+4),f.write(struct.pack('<I',0xffffffff))),
            lambda f,b:(f.seek(b+40*2048+13),f.write(b'\xff')),
        ]
        for mutate in mutations:
            make_disc(self.path,mutate=mutate)
            with self.assertRaises(ValueError):disc.prepare_game(self.root,self.data,self.path,SilentProgress())
            self.assertFalse((self.root/'data/discs').exists())

    def test_traversal_devices_streams_and_case_collisions(self):
        for name in ('../escape','a/b','a\\b','file:stream','NUL','COM1.txt','trailing.','trailing ','DEFAULT.XBE'):
            make_disc(self.path,extra=[(name,0,70,0)])
            with self.assertRaises(ValueError):disc.prepare_game(self.root,self.data,self.path,SilentProgress())
            self.assertFalse((self.root/'data').exists())

    def test_nested_cycles_and_shared_file_payloads_rejected(self):
        def cycle(file,base):
            file.seek(base+42*2048);file.write(table([('cycle',0x10,40,80)]))
        make_disc(self.path,mutate=cycle)
        with self.assertRaises(ValueError):disc.prepare_game(self.root,self.data,self.path,SilentProgress())
        make_disc(self.path,extra=[('alias.bin',0,60,len(self.payload))])
        with self.assertRaisesRegex(ValueError,'overlap'):
            disc.prepare_game(self.root,self.data,self.path,SilentProgress())
        self.assertEqual(self.cache_entries(),[])

    def test_cancel_removes_our_partial_files_and_preserves_native_saves(self):
        save=self.root/'data/state/Partition1/owner.sav';save.parent.mkdir(parents=True);save.write_bytes(b'progress')
        with self.assertRaises(disc.ImportCancelled):disc.prepare_game(self.root,self.data,self.path,SilentProgress(True))
        self.assertEqual(save.read_bytes(),b'progress')
        self.assertEqual(self.cache_entries(),[])
        self.assertTrue(disc.prepare_game(self.root,self.data,self.path,SilentProgress()).is_file())

    def test_short_space_and_copy_failure_do_not_leave_staging_data(self):
        with patch.object(disc.shutil,'disk_usage',return_value=type('Usage',(),{'free':0})()):
            with self.assertRaisesRegex(ValueError,'free space'):disc.prepare_game(self.root,self.data,self.path,SilentProgress())
        original=disc.Disc.read
        def fail(image,offset,size):
            if offset==60*2048:raise OSError('authored read error')
            return original(image,offset,size)
        with patch.object(disc.Disc,'read',fail):
            with self.assertRaisesRegex(OSError,'authored read error'):disc.prepare_game(self.root,self.data,self.path,SilentProgress())
        self.assertEqual(self.cache_entries(),[])

    def test_source_mutation_and_incomplete_cached_assets_are_rejected(self):
        with disc.Disc(self.path) as image:
            with self.path.open('ab') as file:file.write(b'changed')
            with self.assertRaisesRegex(ValueError,'changed'):image.unchanged()
        make_disc(self.path)
        result=disc.prepare_game(self.root,self.data,self.path,SilentProgress())
        (result.parent/'assets/texture.bin').write_bytes(b'truncated')
        with self.assertRaisesRegex(ValueError,'incomplete'):disc.prepare_game(self.root,self.data,self.path,SilentProgress())
        self.assertEqual((result.parent/'assets/texture.bin').read_bytes(),b'truncated')

    def test_marked_crash_remnant_is_cleaned_but_unrecognized_files_stay(self):
        store=self.root/'data/discs';partial=store/'.import-authored-crash';partial.mkdir(parents=True)
        (partial/'.import-owner.json').write_text(json.dumps(dict(schema=disc.SCHEMA)))
        (partial/'payload').write_bytes(b'unfinished')
        unrelated=store/'owner-notes.txt';unrelated.write_text('keep')
        disc.prepare_game(self.root,self.data,self.path,SilentProgress())
        self.assertFalse(partial.exists());self.assertEqual(unrelated.read_text(),'keep')


if __name__=='__main__':unittest.main()
