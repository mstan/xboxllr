#!/usr/bin/env python3
"""Require the real reference ABI: retained observations beyond 4096 methods,
including more than one recorder window dispatched by a single MMIO kick.

Authored packets only; no game binary, host GPU, audio, network or skipped test.
"""
import argparse
import ctypes
import os
from pathlib import Path
import struct


class Fixture:
    def __init__(self,library):
        self.lib=library;self.gpu=library.xlle_nv2a_reference_create();assert self.gpu
        self.ram=ctypes.create_string_buffer(1024*1024);self.cursor=0x50000
        library.xlle_nv2a_reference_memory(self.gpu,ctypes.addressof(self.ram),len(self.ram))
        for address,value in ((0x700008,1),(0x70000c,0x80010020),(0x700200,0x97),
            (0x700010,2),(0x700014,0x80010030),(0x700300,0x3d),(0x700304,len(self.ram)-1),(0x700308,0),
            (0x700180,0x3d),(0x700184,len(self.ram)-1),(0x700188,0),
            (0x002504,1),(0x003204,0x100),(0x00322c,0x18),(0x003240,self.cursor),(0x003244,self.cursor),
            (0x003220,1),(0x003250,1),(0x003200,1),(0x400720,1),(0x400880,0x28c3ff)):
            self.write(address,value)
        self.packet(0,1);self.packet(0x194,2);self.packet(0x198,2)
        self.packet(0x200,64<<16);self.packet(0x204,32<<16);self.packet(0x208,0x128)
        self.packet(0x20c,0x01000100);self.packet(0x210,0x20000);self.packet(0x214,0x30000);self.kick()

    def close(self):self.lib.xlle_nv2a_reference_destroy(self.gpu)
    def write(self,address,value):self.lib.xlle_nv2a_reference_write(self.gpu,0xfd000000+address,4,value)
    def read(self,address):return self.lib.xlle_nv2a_reference_read(self.gpu,0xfd000000+address,4)
    def error(self):return self.lib.xlle_nv2a_reference_error(self.gpu)
    def value(self,method):return self.lib.xlle_nv2a_reference_method_value(self.gpu,method)
    def packet(self,method,*values):
        assert 0<len(values)<=2047
        blob=struct.pack('<'+'I'*(1+len(values)),(len(values)<<18)|0x40000000|method,*values)
        assert self.cursor+len(blob)<len(self.ram)
        ctypes.memmove(ctypes.addressof(self.ram)+self.cursor,blob,len(blob));self.cursor+=len(blob)
    def kick(self):self.write(0x800040,self.cursor)


def qualify(library):
    ref=Fixture(library)
    try:
        expected={0x200:64<<16,0x204:32<<16,0x208:0x128,0x20c:0x01000100,0x210:0x20000,0x214:0x30000}
        assert not ref.error() and ref.read(0x800044)==ref.cursor
        assert all(ref.value(m)==v for m,v in expected.items())
        untouched=bytes(ref.ram[0x20000:0x40000])
        for _ in range(3):
            ref.packet(0x100,*([0]*2047));ref.kick()
            assert not ref.error() and ref.read(0x800044)==ref.cursor
        # The six bindings have now aged out of the actual 4096-entry recorder.
        assert all(ref.value(m)==v for m,v in expected.items())
        assert bytes(ref.ram[0x20000:0x40000])==untouched
        assert ref.value(0x1e24)==ref.value(0x201)==ref.value(0x2000)==0
        ref.packet(0x200,128<<16);ref.kick();assert not ref.error()
        assert ref.value(0x200)==128<<16 and ref.value(0x204)==expected[0x204]
    finally:ref.close()
    other=Fixture(library)
    try:
        assert other.value(0x200)==64<<16  # independent device instance
        for _ in range(3):other.packet(0x100,*([0]*2047))
        other.packet(0x200,256<<16)
        other.kick()
        assert not other.error() and other.read(0x800044)==other.cursor
        assert other.value(0x200)==256<<16 and other.value(0x204)==32<<16
        assert other.value(0x210)==0x20000 and other.value(0x214)==0x30000
        assert bytes(other.ram[0x20000:0x40000])==untouched
        other.packet(0x200,128<<16);other.kick()
        assert not other.error() and other.value(0x200)==128<<16
    finally:other.close()
    print('PASS: retained fields after6141 later methods, single-kick history wrap, live updates, untouched bytes and independent devices')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',type=Path,required=True);parser.add_argument('--dependencies',type=Path,required=True)
    args=parser.parse_args();handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    lib=ctypes.CDLL(str(args.library.resolve()))
    for name,result,params in (('create',ctypes.c_void_p,[]),('destroy',None,[ctypes.c_void_p]),
        ('memory',None,[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint32]),
        ('read',ctypes.c_uint32,[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_int]),
        ('write',None,[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32]),
        ('error',ctypes.c_char_p,[ctypes.c_void_p]),('method_value',ctypes.c_uint32,[ctypes.c_void_p,ctypes.c_uint32])):
        fn=getattr(lib,'xlle_nv2a_reference_'+name);fn.restype=result;fn.argtypes=params
    qualify(lib)
