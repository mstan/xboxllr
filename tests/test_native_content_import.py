import os,struct,hashlib
import tempfile
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_content_import import FolderSaves,service,gallery,preview

class ContentTests(unittest.TestCase):
    def test_selected_delete_rejects_changed_or_unrelated_save(self):
        with tempfile.TemporaryDirectory() as temporary:
            store=FolderSaves(temporary,'5345000a')
            name=store.publish({'save.dat':b'design','xboxllr-hd.bin':b'detail'},'85BE4CFBC301')
            digest=hashlib.sha256(b'design').hexdigest()
            for unsafe in ('../progress','',str(Path(temporary))):
                with self.assertRaises(ValueError):store.delete(unsafe,digest)
            with self.assertRaises(ValueError):store.delete(name,'0'*64)
            extra=store.root/name/'JSRFDATA.SAV';extra.write_bytes(b'progress')
            with self.assertRaises(ValueError):store.delete(name,digest)
            self.assertEqual(extra.read_bytes(),b'progress');extra.unlink()
            other=store.root/'native-progress';other.mkdir();(other/'JSRFDATA.SAV').write_bytes(b'owner save')
            store.delete(name,digest)
            self.assertEqual(store.names(),['native-progress'])
            self.assertEqual((other/'JSRFDATA.SAV').read_bytes(),b'owner save')
            self.assertFalse(list(Path(temporary).glob('graffiti-delete-*')))

    def test_gallery_and_full_preview_bridge(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            design=dict(slot=1,size=0,density=4,width=128,height=128,sha='12'*32,rgba=bytes((7,11,13,255))*128*128,deletable=True)
            class Codec:
                @staticmethod
                def saved_design(*args):return design
                @staticmethod
                def resize(pixels,w,h,tw,th):return pixels[:4]*tw*th
            gallery(folder,Codec,None,bytes(16),{1})
            data=(folder/'gallery.bin').read_bytes()
            self.assertEqual(data[:8],b'XLLEGA1\0');self.assertEqual(struct.unpack_from('<9I',data,8),(1,1,0,4,128,128,64,64,1))
            self.assertEqual(data[44:76],bytes.fromhex(design['sha']))
            self.assertEqual(len(data),76+64*64*4)
            preview(folder,design);data=(folder/'preview.bin').read_bytes()
            self.assertEqual(data[:8],b'XLLEGP1\0');self.assertEqual(struct.unpack_from('<3I',data,8),(1,128,128))
            self.assertEqual(data[52:],design['rgba'])
            calls=[]
            def handler(fields,pixels):
                calls.append(fields);return dict(status='Done',capacity='28 free',slots=28,locked=True,gallery=True)
            for slot in ('28','-1','1'):
                (folder/'request').write_text('xboxllr-image-import 1\naction delete\ngame 67616d652e786265\nslot '+slot+'\nsha '+'12'*32+'\nend\n')
                service(folder,handler)
            self.assertEqual(len(calls),1);self.assertEqual(calls[0]['_bridge'],folder)

    def test_additive_slots_and_capacity(self):
        with tempfile.TemporaryDirectory() as temporary:
            store=FolderSaves(temporary,'5345000a')
            existing={'save.dat':b'original progress','SaveMeta.xbx':'Name=GRAFFITI001\r\n'.encode('utf-16'),'SaveImage.xbx':b'original art'}
            first=store.publish(existing)
            self.assertEqual(len(store.names()),1)
            second=store.publish(dict(existing,**{'SaveMeta.xbx':'Name=GRAFFITI002\r\n'.encode('utf-16')}))
            self.assertNotEqual(first,second)
            self.assertEqual(len(store.names()),2)
            self.assertGreater(store.available_bytes(),0)
            for name,content in existing.items():self.assertEqual(store.read(first,name,8192),content)
            for slot in range(2,28):store.publish({'SaveMeta.xbx':f'Name=GRAFFITI{slot+1:03}\r\n'.encode('utf-16')})
            self.assertEqual(len(store.names()),28)
            self.assertFalse(list(Path(temporary).glob('graffiti-import-*')))

    def test_bounded_handoff_and_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            fields={'action':'install','game':'game.xbe'.encode().hex(),'source':'art.png'.encode().hex(),'size':'0','fit':'0','width':'1','height':'1'}
            request='xboxllr-image-import 1\n'+''.join(k+' '+v+'\n' for k,v in fields.items())+'end\n'
            (folder/'request').write_text(request);(folder/'pixels.rgba').write_bytes(bytes((1,2,3,255)))
            calls=[]
            def install(fields,pixels):
                calls.append((fields,pixels))
                return dict(status='Installed',capacity='27 of 28 slots free',slots=27,locked=True)
            self.assertTrue(service(folder,install));self.assertEqual(len(calls),1)
            self.assertEqual(calls[0][0]['game'],'game.xbe');self.assertEqual(calls[0][1],bytes((1,2,3,255)))
            self.assertFalse((folder/'pixels.rgba').exists());self.assertIn('slots 27',(folder/'response').read_text())
            (folder/'request').write_text(request.replace('width 1','width 99999'))
            service(folder,install);self.assertEqual(len(calls),1)
            self.assertIn('slots -1',(folder/'response').read_text())

@unittest.skipUnless(os.environ.get('XLLE_TEST_STORAGE_DLL'),'Supply the built storage DLL for actual FATX qualification')
class ActualHddTests(unittest.TestCase):
    def test_add_reopen_capacity_and_preserve(self):
        from native_hdd_install import empty_image
        from native_storage import StorageSaves
        with tempfile.TemporaryDirectory() as temporary:
            image=Path(temporary)/'authored-fixture.raw'
            empty_image(image,8*1024**3)
            starts=(0xabe80000,0x8ca80000,0x80000,0x2ee80000,0x5dc80000)
            sizes=(0x1312d6000,0x1f400000,0x2ee00000,0x2ee00000,0x2ee00000)
            with image.open('r+b') as out:
                for start,size in zip(starts,sizes):
                    cluster=16384;entries=size//cluster+1;entry_bytes=2 if entries<0xfff0 else 4
                    fat_bytes=(entries*entry_bytes+4095)&~4095
                    out.seek(start);out.write(b'FATX'+struct.pack('<III',0x12345678,32,1)+bytes(4080))
                    fat=bytearray(fat_bytes)
                    struct.pack_into('<'+('H' if entry_bytes==2 else 'I')*2,fat,0,*([0xffff if entry_bytes==2 else 0xffffffff]*2))
                    out.write(fat);out.write(b'\xff'*cluster)
            library=os.environ['XLLE_TEST_STORAGE_DLL'];store=StorageSaves(library,image,'5345000a')
            try:
                before=store.available_bytes()
                first=store.publish({'save.dat':b'original save','SaveMeta.xbx':'Name=GRAFFITI001\r\n'.encode('utf-16')})
                second=store.publish({'save.dat':bytes(range(256))*799,'SaveMeta.xbx':'Name=GRAFFITI002\r\n'.encode('utf-16'),
                    'xboxllr-hd.bin':b'authored high-resolution companion'*317})
                self.assertLess(store.available_bytes(),before)
            finally:store.close()
            store=StorageSaves(library,image,'5345000a')
            try:
                self.assertEqual(len(store.names()),2)
                self.assertEqual(store.read(first,'save.dat',1024),b'original save')
                self.assertEqual(store.read(second,'save.dat',300000),bytes(range(256))*799)
                self.assertIsNone(store.read(second,'SaveImage.xbx',4096))
                self.assertEqual(store.read(second,'xboxllr-hd.bin',20000),b'authored high-resolution companion'*317)
                before_delete=store.available_bytes()
                with self.assertRaises(ValueError):store.delete(second,'0'*64)
                store.delete(second,hashlib.sha256(bytes(range(256))*799).hexdigest())
                self.assertGreater(store.available_bytes(),before_delete)
                self.assertEqual(store.names(),[first])
                self.assertEqual(store.read(first,'save.dat',1024),b'original save')
            finally:store.close()

if __name__=='__main__':unittest.main()
