#!/usr/bin/env python3
"""Original NTSC voice routing/pitch helpers -> maintained APU RAM effects.

No original callees are replaced. The driver helpers and the hardware method
boundary are tested separately: this does not execute the full DirectSound
owner, its lock protocol, sample processing, or a title-selected LLE backend.
"""
import argparse
import ctypes as c
import hashlib
import json
import os
import struct
from pathlib import Path

from aot_inventory import parse_xbe
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_ESI,
    UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_EIP, UC_X86_REG_ESP)

IMAGE = 'e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'


def run(xbe, library, dependencies):
    blob=xbe.read_bytes()
    assert hashlib.sha256(blob).hexdigest()==IMAGE, 'unreviewed original image'
    dependency=os.add_dll_directory(str(dependencies.resolve())) if dependencies else None
    dll=c.CDLL(str(library.resolve()))
    dll.xlle_apu_reference_create.argtypes=[c.c_void_p,c.c_uint32]
    dll.xlle_apu_reference_create.restype=c.c_void_p
    dll.xlle_apu_reference_destroy.argtypes=[c.c_void_p]
    dll.xlle_apu_reference_error.argtypes=[c.c_void_p]
    dll.xlle_apu_reference_error.restype=c.c_char_p
    dll.xlle_apu_reference_write.argtypes=[c.c_void_p,c.c_uint32,c.c_int,c.c_uint32]
    dll.xlle_apu_reference_write.restype=c.c_int
    cpu=Uc(UC_ARCH_X86,UC_MODE_32)
    cpu.mem_map(0x10000,0x1000000)
    cpu.mem_map(0x30000000,0x10000)
    for section in parse_xbe(blob)['sections']:
        if section['raw_size'] and section['address']+section['raw_size']<=0x1010000:
            cpu.mem_write(section['address'],blob[section['raw_offset']:section['raw_offset']+section['raw_size']])
    obj,settings,out,stack,end=0x30000000,0x30001000,0x30002000,0x30008000,0x30009000
    def put(a,v):cpu.mem_write(a,struct.pack('<I',v&0xffffffff))
    def get(a):return struct.unpack('<I',cpu.mem_read(a,4))[0]
    put(obj+0x78,settings)
    def call(address,args):
        put(stack,end)
        for i,arg in enumerate(args):put(stack+4+i*4,arg)
        put(stack+4+len(args)*4,0x1234abcd)
        saved=[(UC_X86_REG_EBX,0x123),(UC_X86_REG_ESI,0x234),
               (UC_X86_REG_EDI,0x345),(UC_X86_REG_EBP,0x456)]
        for reg,value in saved:cpu.reg_write(reg,value)
        cpu.reg_write(UC_X86_REG_ECX,obj);cpu.reg_write(UC_X86_REG_ESP,stack)
        cpu.emu_start(address,end,count=10000)
        assert cpu.reg_read(UC_X86_REG_EIP)==end
        assert cpu.reg_read(UC_X86_REG_ESP)==stack+4+len(args)*4
        assert get(stack+4+len(args)*4)==0x1234abcd
        assert all(cpu.reg_read(reg)==value for reg,value in saved)

    ram=(c.c_ubyte*0x10000)()
    instance=dll.xlle_apu_reference_create(ram,len(ram));assert instance
    def write(offset,value):
        assert dll.xlle_apu_reference_write(instance,0xfe800000+offset,4,value),dll.xlle_apu_reference_error(instance)
    def word(address):return struct.unpack_from('<I',ram,address)[0]
    def store(address,value):struct.pack_into('<I',ram,address,value&0xffffffff)
    rows=[]
    try:
        write(0x202c,0x1000)
        for n,bins in enumerate([[],[0,1],[5],[31,3,7],[7,6,5,4,3,2,1,0]]):
            put(settings+0x24,len(bins))
            cpu.mem_write(settings+0x28,bytes(bins)+bytes(8-len(bins)))
            put(out,0xffffffff);put(out+4,0x583fe000)
            call(0x1789a0,[out,out+4])
            complete=bins.copy()
            for value in range(32):
                if len(complete)==8:break
                if value not in complete:complete.append(value)
            expected=sum(complete[i]<<shift for i,shift in enumerate([0,5,10,16,21,26]))
            expected_format=0x583fe000|complete[6]|(complete[7]<<5)
            assert (get(out),get(out+4))==(expected,expected_format)
            voice=7+n;base=0x1000+voice*0x80
            c.memset(c.addressof(ram)+base,0xa5,0x80)
            store(base+0x7c,0xbeef00ff)
            write(0x202f8,voice);write(0x20300,get(out));write(0x20304,get(out+4))
            assert word(base)==expected and word(base+4)==expected_format
            for pitch in [-50000,-32768,-32767,-8192,-1,0,1,4096,8191,8192,50000]:
                put(settings+0x18,pitch);put(out+8,0xaabbccdd)
                call(0x178c97,[out+8])
                effective=min(8191,max(-32767,pitch))
                assert get(out+8)==((effective&0xffff)<<16)
                write(0x2037c,get(out+8))
                assert word(base+0x7c)==((effective&0xffff)<<16)|0xff
                assert word(base+0x54)==0xa5a5a5a5 # no invented ACTIVE/PAUSE/complete change
                rows.append(dict(voice=voice,bins=bins,pitch=pitch,effective_pitch=effective))

        # Buffer offsets share words with envelope state; only the 24-bit
        # address/cursor field changes, and another voice stays untouched.
        for method,offset in [(0x3a0,0x20),(0x3a4,0x24),(0x3d8,0x58),(0x3dc,0x5c)]:
            for value in [0,1,0xffffff,0xff123456]:
                store(base+offset,0xab000000)
                write(0x20000+method,value)
                assert word(base+offset)==0xab000000|(value&0xffffff)
        assert word(0x1000+7*0x80+0x7c)==0x1fff00ff
    finally:
        dll.xlle_apu_reference_destroy(instance)

    # Every unsupported call fails persistently; later valid calls cannot
    # turn a failed run into an apparent successful configuration.
    rejected=[]
    for name,transactions in [
        ('unknown method',[(0x20320,4,0)]),
        ('wrong width',[(0x20300,2,0)]),
        ('voice handle',[(0x202f8,4,256)]),
        ('DMA beyond RAM',[(0x202c,4,0x10000),(0x20300,4,1)]),
        ('DMA address wrap',[(0x202c,4,0xfffffffc),(0x202f8,4,1),(0x20300,4,1)]),
    ]:
        instance=dll.xlle_apu_reference_create(ram,len(ram));assert instance
        before=bytes(ram)
        try:
            for offset,size,value in transactions:
                result=dll.xlle_apu_reference_write(instance,0xfe800000+offset,size,value)
            assert not result and dll.xlle_apu_reference_error(instance)
            assert not dll.xlle_apu_reference_write(instance,0xfe820300,4,0x12345678)
            assert bytes(ram)==before
            rejected.append(name)
        finally:dll.xlle_apu_reference_destroy(instance)
    if dependency:dependency.close()
    return dict(xbe_sha256=IMAGE,library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
                original_routing_pitch_cases=rows,buffer_field_cases=16,rejected=rejected,
                scope='complete original routing/pitch helpers and maintained APU voice-table configuration; no playback claim')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['xbe','library','output']:parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--dependencies',type=Path)
    args=parser.parse_args()
    result=run(args.xbe,args.library,args.dependencies)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: 55 original routing/pitch cases, 16 buffer-field cases, 5 guarded failure cases')
