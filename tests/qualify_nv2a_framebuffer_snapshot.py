#!/usr/bin/env python3
"""Passive original-route framebuffer ABI controls; never final-output acceptance."""
import argparse
import ctypes as C
import os
from pathlib import Path
import struct

READER=C.CFUNCTYPE(C.c_int,C.c_uint32,C.c_void_p,C.c_size_t)

def bind(lib):
    for name,result,args in (
        ('create',C.c_void_p,[]),('destroy',None,[C.c_void_p]),
        ('write',None,[C.c_void_p,C.c_uint32,C.c_int,C.c_uint32]),
        ('read',C.c_uint32,[C.c_void_p,C.c_uint32,C.c_int]),
        ('error',C.c_char_p,[C.c_void_p]),('irq',C.c_int,[C.c_void_p]),
        ('scanout',C.c_int,[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,C.c_void_p]),
        ('first_context_snapshot',C.c_int,[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p]),
        ('framebuffer_snapshot',C.c_int,[C.c_void_p,C.c_void_p,C.c_uint32,C.c_void_p,READER])):
        fn=getattr(lib,'xlle_nv2a_reference_'+name);fn.restype=result;fn.argtypes=args

def qualify(lib):
    for depth in (2,3):
        gpu=lib.xlle_nv2a_reference_create();assert gpu
        reads=[];ram=bytes((i*19+7)&255 for i in range(8192));deny=False
        @READER
        def reader(a,p,n):
            reads.append((a,n))
            if deny or a+n>len(ram):return 0
            C.memmove(p,ram[a:a+n],n);return 1
        def write(a,v,size=4):lib.xlle_nv2a_reference_write(gpu,a,size,v)
        def crtc(i,v):write(0xfd6013d4,i,1);write(0xfd6013d5,v,1)
        def context():
            out=C.create_string_buffer(2*1024*1024);n=C.c_uint32()
            assert lib.xlle_nv2a_reference_first_context_snapshot(gpu,out,len(out),C.byref(n))
            return out.raw[:n.value],lib.xlle_nv2a_reference_irq(gpu)
        def snapshot():
            out=C.create_string_buffer(16384);n=C.c_uint32()
            assert lib.xlle_nv2a_reference_framebuffer_snapshot(gpu,out,len(out),C.byref(n),reader)
            data=out.raw[:n.value];h=struct.unpack_from('<32I',data)
            assert h[:3]==(0x31444246,1,1860) and h[30:]==(1,0)
            assert len(data)==1860+h[4]
            return h,data[1604:1860].split(b'\0',1)[0],data
        try:
            crtc(0x13,4);crtc(0x28,depth)
            write(0xfd680800,2);write(0xfd680820,3);write(0xfd600800,4096)
            write(0xfd680600,0x100030);write(0xfd680630,2)
            # Blanking and an incomplete DAC are metadata, not substitutions
            # for stored pixel bytes. Neither final-output guard is weakened.
            write(0xfd0c03c4,1,1);write(0xfd0c03c5,0x21,1)
            write(0xfd6813c8,0,1);write(0xfd6813c9,77,1)
            assert not lib.xlle_nv2a_reference_error(gpu)
            before=context();n=C.c_uint32();short=C.create_string_buffer(b'unchanged')
            assert not lib.xlle_nv2a_reference_framebuffer_snapshot(gpu,short,len(short),C.byref(n),reader)
            assert short.value==b'unchanged' and not reads and n.value==1860+4*3*(2 if depth==2 else 4)
            h,error,data=snapshot();bpp=2 if depth==2 else 4
            assert h[3]==1 and not error and h[5:10]==(4096,32,4,3,bpp*8)
            assert h[12:17]==(1,1,0,0,1)
            assert data[300]==77 and data[1068:1580]==bytes(512) # unknown encoder
            assert data[1860:]==b''.join(ram[4096+y*32:4096+y*32+4*bpp] for y in range(3))
            assert context()==before and not lib.xlle_nv2a_reference_error(gpu)
            deny=True;h,error,data=snapshot()
            assert h[3:5]==(0,0) and b'RAM copy failed' in error
            assert context()==before and not lib.xlle_nv2a_reference_error(gpu)
            deny=False;crtc(0x13,0);old_reads=len(reads);h,error,_=snapshot()
            assert h[3:5]==(0,0) and b'pitch/extent' in error and len(reads)==old_reads
            crtc(0x13,4);write(0xfd600800,0x03fffffc);h,error,_=snapshot()
            assert h[3]==0 and b'pitch/extent' in error and len(reads)==old_reads
            write(0xfd600800,4096)
            output=C.create_string_buffer(b'untouched');w=C.c_uint32();height=C.c_uint32()
            assert not lib.xlle_nv2a_reference_scanout(gpu,output,len(output),C.byref(w),C.byref(height))
            failure=lib.xlle_nv2a_reference_error(gpu)
            assert b'display processing' in failure and output.value==b'untouched'
            h,error,_=snapshot()
            assert h[3]==0 and error==failure and lib.xlle_nv2a_reference_error(gpu)==failure
        finally:lib.xlle_nv2a_reference_destroy(gpu)
    print('PASS: 16/32-bit storage, padding, bounds, failed reads, capacity, DAC/blanking/unknown-encoder metadata, unchanged GPU state and retained output guard')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--library',type=Path,required=True)
    p.add_argument('--dependencies',type=Path);a=p.parse_args()
    handles=[os.add_dll_directory(str(a.dependencies.resolve()))] if a.dependencies and os.name=='nt' else []
    lib=C.CDLL(str(a.library.resolve()));bind(lib);qualify(lib)
