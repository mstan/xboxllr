"""Bounded, content-addressed presentation textures from a trusted title codec.

The binary index contains no paths or executable code. Pixel filenames are
their SHA256, and the native renderer checks that digest again before upload.
Guest resource dimensions, bytes, CPU reads and saved games remain original.
"""
import hashlib
import os
from pathlib import Path
import re
import struct
import uuid

MAX_BYTES=1024*1024*1024


def prepare(folder,entries):
    folder=Path(folder);folder.mkdir(parents=True,exist_ok=True)
    if folder.is_symlink() or getattr(folder,'is_junction',lambda:False)():raise ValueError('Linked texture cache')
    records={};ambiguous=set();retained=set();total=0
    for entry in entries:
        fmt,w,h,source=(entry[k] for k in ('format','width','height','source'))
        hw,hh,rgba=(entry[k] for k in ('hd_width','hd_height','rgba'))
        if (fmt not in (0xe,0x11,0x12) or not 4<=w<=1024 or not 4<=h<=1024 or
            (fmt==0xe and (w&(w-1) or h&(h-1))) or (fmt==0x11 and w%32) or
            len(source)!=w*h*({0xe:1,0x11:2,0x12:4}[fmt]) or not 1<=hw<=4096 or not 1<=hh<=4096 or hw*h!=hh*w or
            hw not in (w*2,w*4) or len(rgba)!=hw*hh*4):raise ValueError('Invalid presentation texture')
        digest=hashlib.sha256(source).digest();high=hashlib.sha256(rgba).digest()
        key=(fmt,w,h,digest)
        if key in ambiguous:continue
        record=struct.pack('<7I',fmt,w,h,len(source),hw,hh,len(rgba))+source[:16]+digest+high
        if key in records:
            if records[key]!=record:
                # Different high-res art can quantize to identical native art.
                # Never choose an arbitrary design for that ambiguous texture.
                del records[key];ambiguous.add(key)
            continue
        path=folder/(high.hex()+'.rgba')
        native=folder/(digest.hex()+'.native')
        if native.is_symlink():raise ValueError('Linked texture source cache')
        if not native.exists() or native.stat().st_size!=len(source) or native.read_bytes()!=source:
            temporary=folder/(uuid.uuid4().hex+'.tmp')
            try:
                with temporary.open('xb') as out:
                    out.write(source);out.flush();os.fsync(out.fileno())
                if temporary.read_bytes()!=source:raise OSError('Texture source readback failed')
                temporary.replace(native)
            finally:temporary.unlink(missing_ok=True)
        retained.add(native.name)
        if path.name not in retained:total+=len(rgba)
        if total>MAX_BYTES or len(records)>=2048:raise ValueError('Presentation texture budget exceeded')
        if path.is_symlink():raise ValueError('Linked texture cache file')
        if not path.exists() or path.stat().st_size!=len(rgba) or hashlib.sha256(path.read_bytes()).digest()!=high:
            temporary=folder/(uuid.uuid4().hex+'.tmp')
            try:
                with temporary.open('xb') as out:
                    out.write(rgba);out.flush();os.fsync(out.fileno())
                if hashlib.sha256(temporary.read_bytes()).digest()!=high:raise OSError('Texture cache readback failed')
                temporary.replace(path)
            finally:temporary.unlink(missing_ok=True)
        retained.add(path.name);records[key]=record
    index=folder/'textures.bin';temporary=folder/'textures.tmp'
    temporary.write_bytes(b'XLLEHD1\0'+struct.pack('<I',len(records))+b''.join(records.values()))
    temporary.replace(index)
    for path in folder.iterdir():
        if re.fullmatch('[0-9a-f]{64}\\.(rgba|native)',path.name) and path.name not in retained and path.is_file() and not path.is_symlink():path.unlink()
    return index if records else None
