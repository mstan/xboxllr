#!/usr/bin/env python3
"""Admission through the same image/filesystem implementation used by titles.

Opening for inspection is read-only and denies concurrent writers. This never
creates/formats/converts a disk, derives keys, or claims arbitrary saves match
the selected console. Identity/signing admission belongs to console setup.
"""
import ctypes as c
import hashlib
import struct
from pathlib import Path


class Storage:
    def __init__(self, library, image, *, writable=False):
        self.api = c.CDLL(str(Path(library).resolve(strict=True)))
        signatures = {
            'open': (c.c_void_p, [c.c_char_p, c.c_int, c.c_char_p, c.c_size_t]),
            'destroy': (c.c_uint32, [c.c_void_p]),
            'bytes': (c.c_uint64, [c.c_void_p]),
            'is_qcow2': (c.c_int, [c.c_void_p]),
            'flush': (c.c_uint32, [c.c_void_p]),
            'read_blocks': (c.c_uint32, [c.c_void_p, c.c_uint64, c.c_void_p, c.c_uint32]),
            'capacity': (c.c_uint32, [c.c_void_p, c.c_uint, c.POINTER(c.c_uint64),
                c.POINTER(c.c_uint64), c.POINTER(c.c_uint32)]),
        }
        for name, (result, args) in signatures.items():
            fn = getattr(self.api, 'xllr_storage_' + name)
            fn.restype, fn.argtypes = result, args
        error = c.create_string_buffer(1024)
        self.handle = self.api.xllr_storage_open(str(Path(image).resolve(strict=True)).encode('utf-8'),
            writable, error, len(error))
        if not self.handle:
            raise ValueError(error.value.decode('utf-8', 'replace') or 'HDD image admission failed')

    def close(self):
        if self.handle:
            status = self.api.xllr_storage_destroy(self.handle)
            self.handle = None
            if status:
                raise OSError(f'HDD image flush/close failed ({status:08x})')

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def inspect(self):
        partitions = []
        for index in range(1, 6):
            total, free, unit = c.c_uint64(), c.c_uint64(), c.c_uint32()
            status = self.api.xllr_storage_capacity(self.handle, index, c.byref(total), c.byref(free), c.byref(unit))
            if status:
                raise ValueError(f'HDD partition {index} admission failed ({status:08x})')
            partitions.append(dict(partition=index, cluster_bytes=unit.value,
                total_clusters=total.value, available_clusters=free.value))
        return dict(format='qcow2' if self.api.xllr_storage_is_qcow2(self.handle) else 'raw',
            bytes=self.api.xllr_storage_bytes(self.handle), partitions=partitions,
            disk_structure_verified=True)

    def identity(self):
        # Stable volume identity, not a hash of mutable save contents. Native
        # mounting rechecks this under its exclusive writer handle.
        data = bytearray(struct.pack('<Q', self.api.xllr_storage_bytes(self.handle)))
        for offset in (0xabe80000,0x8ca80000,0x80000,0x2ee80000,0x5dc80000):
            part = c.create_string_buffer(16)
            status = self.api.xllr_storage_read_blocks(self.handle, offset, part, 16)
            if status:
                raise ValueError(f'Cannot read HDD volume identity ({status:08x})')
            data.extend(part.raw)
        return hashlib.sha256(data).hexdigest()


class _Info(c.Structure):
    _fields_=[('length',c.c_uint64),('position',c.c_uint64),('attributes',c.c_uint32),
              ('access',c.c_uint32),('times',c.c_uint16*6),('directory',c.c_uint32),('delete_pending',c.c_uint32)]

class _Entry(c.Structure):
    _fields_=[('info',_Info),('name',c.c_char*43)]


class StorageSaves:
    """Additive native-save operations through the gameplay FATX driver.

    An exclusive image writer stays held through enumeration, staging,
    readback and publication. The only rollback targets are our new files.
    """
    def __init__(self,library,image,title_id):
        import re
        if not re.fullmatch(r'[0-9a-fA-F]{8}',title_id):raise ValueError('Invalid save title ID')
        self.root='UDATA/'+title_id.lower()
        self.storage=Storage(library,image,writable=True)
        self.api=self.storage.api;self.handle=self.storage.handle
        signatures={
          'file_open':(c.c_uint32,[c.c_void_p,c.c_uint,c.c_uint64,c.c_char_p,c.c_uint32,c.c_uint32,c.c_uint32,c.c_uint32,c.c_uint32,c.c_int64,c.POINTER(c.c_uint64),c.POINTER(c.c_uint32)]),
          'file_close':(c.c_uint32,[c.c_void_p,c.c_uint64]),
          'file_info':(c.c_uint32,[c.c_void_p,c.c_uint64,c.POINTER(_Info)]),
          'file_read':(c.c_uint32,[c.c_void_p,c.c_uint64,c.c_int64,c.c_void_p,c.c_uint32,c.POINTER(c.c_uint32)]),
          'file_write':(c.c_uint32,[c.c_void_p,c.c_uint64,c.c_int64,c.c_void_p,c.c_uint32,c.POINTER(c.c_uint32)]),
          'file_delete':(c.c_uint32,[c.c_void_p,c.c_uint64,c.c_int]),
          'file_rename':(c.c_uint32,[c.c_void_p,c.c_uint64,c.c_uint64,c.c_char_p,c.c_int]),
          'directory':(c.c_uint32,[c.c_void_p,c.c_uint64,c.c_char_p,c.c_int,c.POINTER(_Entry)])}
        for name,(result,args) in signatures.items():
            fn=getattr(self.api,'xllr_storage_'+name);fn.restype,fn.argtypes=result,args
        try:
            for path in ('UDATA',self.root):
                file=self._open(path,directory=True,disposition=3);self._close(file)
        except BaseException:self.close();raise

    @staticmethod
    def _check(status):
        if status:raise OSError(f'Native HDD save operation failed ({status:08x})')

    def _open(self,path,*,directory=False,disposition=1,missing=False):
        file,action=c.c_uint64(),c.c_uint32()
        status=self.api.xllr_storage_file_open(self.handle,1,0,str(path).replace('/','\\').encode('ascii'),
            0xc0110000,7,0x21 if directory else 0x60,disposition,0x10 if directory else 0x80,0,c.byref(file),c.byref(action))
        if missing and status==0xc0000034:return None
        self._check(status);return file.value

    def _close(self,file):self._check(self.api.xllr_storage_file_close(self.handle,file))
    def close(self):self.storage.close()

    def available_bytes(self):
        total,free,unit=c.c_uint64(),c.c_uint64(),c.c_uint32()
        self._check(self.api.xllr_storage_capacity(self.handle,1,c.byref(total),c.byref(free),c.byref(unit)))
        return free.value*unit.value

    def names(self):
        directory=self._open(self.root,directory=True);out=[];restart=True;count_entries=0
        try:
            while True:
                entry=_Entry();status=self.api.xllr_storage_directory(self.handle,directory,None,restart,c.byref(entry));restart=False
                if status==0x80000006:break
                self._check(status)
                count_entries+=1
                if count_entries>4096:raise ValueError('Too many save entries')
                name=entry.name.decode('ascii')
                if entry.info.directory:out.append(name)
                if len(out)>4096:raise ValueError('Too many save entries')
        finally:self._close(directory)
        return out

    def read(self,name,file,limit):
        from native_content_import import _save_name,_save_file
        _save_name(name);_save_file(file)
        opened=self._open(self.root+'/'+name+'/'+file,missing=True)
        if opened is None:return None
        try:
            info=_Info();self._check(self.api.xllr_storage_file_info(self.handle,opened,c.byref(info)))
            if info.directory or info.length>limit:raise ValueError('Unrecognized existing save; nothing was replaced')
            buf=c.create_string_buffer(info.length);count=c.c_uint32()
            self._check(self.api.xllr_storage_file_read(self.handle,opened,0,buf,info.length,c.byref(count)))
            if count.value!=info.length:raise OSError('Incomplete native save read')
            return buf.raw
        finally:self._close(opened)

    def publish(self,files,destination_name=None):
        import uuid,re
        destination_name=destination_name or uuid.uuid4().hex[:12].upper()
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,42}',destination_name):raise ValueError('Invalid save directory name')
        temporary=self.root+'/xllr-import-'+uuid.uuid4().hex[:20]
        directory=self._open(temporary,directory=True,disposition=2);created=[];published=False
        try:
            # The exclusive writer lease excludes game enumeration. Write
            # metadata last; a crash can leave only a complete signed design
            # visible. Same-parent publication uses the qualified FATX rename.
            for name,content in sorted(files.items(),key=lambda item:item[0]=='SaveMeta.xbx'):
                if name not in ('save.dat','SaveMeta.xbx','SaveImage.xbx','xboxllr-hd.bin'):raise ValueError('Invalid native save filename')
                file=self._open(temporary+'/'+name,disposition=2);created.append(name)
                try:
                    buf=c.create_string_buffer(content);count=c.c_uint32()
                    self._check(self.api.xllr_storage_file_write(self.handle,file,0,buf,len(content),c.byref(count)))
                    if count.value!=len(content):raise OSError('Incomplete native HDD save write')
                    check=c.create_string_buffer(len(content))
                    self._check(self.api.xllr_storage_file_read(self.handle,file,0,check,len(content),c.byref(count)))
                    if count.value!=len(content) or check.raw!=content:raise OSError('Native HDD save readback failed')
                finally:self._close(file)
            self._check(self.api.xllr_storage_flush(self.handle))
            name=destination_name
            self._check(self.api.xllr_storage_file_rename(self.handle,directory,0,(self.root.replace('/','\\')+'\\'+name).encode('ascii'),0))
            published=True;self._check(self.api.xllr_storage_flush(self.handle));return name
        finally:
            if not published:
                for name in created:
                    file=self._open(temporary+'/'+name)
                    try:self._check(self.api.xllr_storage_file_delete(self.handle,file,1))
                    finally:self._close(file)
                self._check(self.api.xllr_storage_file_delete(self.handle,directory,1))
            self._close(directory)

    def entries(self,name):
        from native_content_import import _save_name
        _save_name(name)
        directory=self._open(self.root+'/'+name,directory=True);out=[];restart=True
        try:
            while True:
                entry=_Entry();status=self.api.xllr_storage_directory(self.handle,directory,None,restart,c.byref(entry));restart=False
                if status==0x80000006:break
                self._check(status)
                if entry.info.directory or len(out)>=16:raise ValueError('Unexpected extra files in this design; nothing was deleted')
                out.append(entry.name.decode('ascii'))
        finally:self._close(directory)
        return out

    def delete(self,name,expected_sha):
        import hashlib,uuid,re
        from native_content_import import _save_name
        _save_name(name)
        if not re.fullmatch('[0-9a-f]{64}',expected_sha):raise ValueError('Invalid saved design digest')
        files=self.entries(name)
        if 'save.dat' not in files or not set(files)<={'save.dat','SaveMeta.xbx','SaveImage.xbx','xboxllr-hd.bin'} or hashlib.sha256(self.read(name,'save.dat',2*1024*1024) or b'').hexdigest()!=expected_sha:
            raise ValueError('The saved design changed or contains extra files. Refresh before deleting.')
        directory=self._open(self.root+'/'+name,directory=True)
        temporary=self.root+'/xllr-delete-'+uuid.uuid4().hex[:20]
        try:
            self._check(self.api.xllr_storage_file_rename(self.handle,directory,0,temporary.replace('/','\\').encode('ascii'),0))
            for file in files:
                opened=self._open(temporary+'/'+file)
                try:self._check(self.api.xllr_storage_file_delete(self.handle,opened,1))
                finally:self._close(opened)
            self._check(self.api.xllr_storage_file_delete(self.handle,directory,1))
            self._check(self.api.xllr_storage_flush(self.handle))
        finally:self._close(directory)
