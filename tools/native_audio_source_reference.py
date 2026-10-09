#!/usr/bin/env python3
"""Original buffer-region arithmetic and conversion -> actual maintained PCM DMA.

The region routine executes up to its first PIO wait, without replaced calls.
No enclosing API completion or FIFO availability is fabricated. Original whole
byte/sample helpers then read back units from the consumed device cursor.
"""
import argparse
import ctypes as c
import hashlib
import json
import os
import struct
from pathlib import Path
from aot_inventory import parse_xbe
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,
    UC_X86_REG_EBP,UC_X86_REG_EIP,UC_X86_REG_ESP,UC_X86_REG_GDTR,
    UC_X86_REG_CS,UC_X86_REG_SS,UC_X86_REG_DS,UC_X86_REG_ES,UC_X86_REG_FS)
from native_audio_configuration_reference import IMAGE

def run(xbe,library,dependencies):
    blob=xbe.read_bytes();assert hashlib.sha256(blob).hexdigest()==IMAGE
    dependency=os.add_dll_directory(str(dependencies.resolve())) if dependencies else None
    dll=c.CDLL(str(library.resolve()))
    dll.xlle_apu_reference_create.argtypes=[c.c_void_p,c.c_uint32];dll.xlle_apu_reference_create.restype=c.c_void_p
    dll.xlle_apu_reference_destroy.argtypes=[c.c_void_p]
    dll.xlle_apu_reference_write.argtypes=[c.c_void_p,c.c_uint32,c.c_int,c.c_uint32];dll.xlle_apu_reference_write.restype=c.c_int
    dll.xlle_apu_reference_source.argtypes=[c.c_void_p,c.c_uint32,c.POINTER(c.c_float),c.c_uint32,c.POINTER(c.c_uint32),c.POINTER(c.c_int)]
    dll.xlle_apu_reference_source.restype=c.c_int
    cpu=Uc(UC_ARCH_X86,UC_MODE_32);cpu.mem_map(0x10000,0x1000000);cpu.mem_map(0x30000000,0x10000)
    for s in parse_xbe(blob)['sections']:
        if s['raw_size'] and s['address']+s['raw_size']<=0x1010000:
            cpu.mem_write(s['address'],blob[s['raw_offset']:s['raw_offset']+s['raw_size']])
    obj,settings,stack,end,pcr,gdt=0x30000000,0x30001000,0x30008000,0x30009000,0x30003000,0x3000f000
    descriptor=0x00c093000000ffff|((pcr&0xffffff)<<16)|((pcr>>24)<<56)
    cpu.mem_write(gdt,struct.pack('<QQQQ',0,0x00cf9b000000ffff,0x00cf93000000ffff,descriptor))
    cpu.reg_write(UC_X86_REG_GDTR,(0,gdt,31,0))
    for r,v in [(UC_X86_REG_CS,8),(UC_X86_REG_SS,16),(UC_X86_REG_DS,16),(UC_X86_REG_ES,16),(UC_X86_REG_FS,24)]:cpu.reg_write(r,v)
    # Explicit caller precondition: DISPATCH_LEVEL, so the original scoped
    # IRQL helper correctly needs no raising/lowering kernel service.
    cpu.mem_write(pcr+0x24,b'\x02')
    def put(a,v):cpu.mem_write(a,struct.pack('<I',v&0xffffffff))
    def get(a):return struct.unpack('<I',cpu.mem_read(a,4))[0]
    def start(address,argument,until=end):
        put(stack,end);put(stack+4,argument)
        cpu.reg_write(UC_X86_REG_ECX,obj);cpu.reg_write(UC_X86_REG_ESP,stack)
        cpu.emu_start(address,until,count=10000);assert cpu.reg_read(UC_X86_REG_EIP)==until
        if until==end:assert cpu.reg_read(UC_X86_REG_ESP)==stack+8
        return cpu.reg_read(UC_X86_REG_EAX)
    rows=[]
    for bits in (8,16):
        for channels in (1,2):
            align=bits//8*channels
            for length in (1,31,32,33,65):
                for looping in (False,True):
                    first=length//3;span=max(1,length//2)
                    cpu.mem_write(obj,bytes(0x100));cpu.mem_write(settings,bytes(0x100))
                    put(obj+0x78,settings);put(obj+0x90,settings)
                    put(settings+0xc,1);put(settings+0x14,align)
                    put(settings+0xc4,length*align);put(settings+0xc8,first*align);put(settings+0xcc,span*align)
                    # Execute all original region arithmetic and its real helper
                    # calls. Stop at the first MMIO poll, not at a fake return.
                    start(0x179efd,int(looping),0x179f9b)
                    frame=cpu.reg_read(UC_X86_REG_EBP)
                    loop=get(frame-4);last=cpu.reg_read(UC_X86_REG_EBX)
                    assert (loop,last)==((first,first+span-1) if looping else (0,length-1))
                    assert bool(get(obj+0x14)&0x02000000)==looping
                    assert cpu.mem_read(pcr+0x24,1)==b'\x02'
                    ram=(c.c_ubyte*0x10000)();ref=dll.xlle_apu_reference_create(ram,len(ram));assert ref
                    def store(a,v):struct.pack_into('<I',ram,a,v&0xffffffff)
                    def word(a):return struct.unpack_from('<I',ram,a)[0]
                    def write(o,v):assert dll.xlle_apu_reference_write(ref,0xfe800000+o,4,v)
                    try:
                        table,voice,sge,base=0x1000,7,0x3000,4089
                        store(sge,0x8000);store(sge+8,0x5000)
                        values=[]
                        for n in range(length):
                            pair=[]
                            for channel in range(channels):
                                value=(n*7+channel*13)%128-64 if bits==8 else (n*613+channel*357)%60000-30000
                                pair.append(value/(128 if bits==8 else 32768))
                                encoded=struct.pack('<B',value+128) if bits==8 else struct.pack('<h',value)
                                for byte,v in enumerate(encoded):
                                    a=base+(n*channels+channel)*(bits//8)+byte
                                    ram[word(sge+(a>>12)*8)+(a&4095)]=v
                            values.append(pair if channels==2 else pair*2)
                        write(0x202c,table);write(0x2030,sge);write(0x202f8,voice)
                        store(table+voice*128+0x54,1<<21)
                        format=(0x50000000 if bits==16 else 0)|((channels-1)<<27)|((channels-1)<<16)|get(obj+0x14)
                        write(0x20304,format);write(0x203a0,base);write(0x203a4,loop);write(0x203dc,last)
                        requested=length*2+13;out=(c.c_float*(requested*2))();count=c.c_uint32();ended=c.c_int()
                        assert dll.xlle_apu_reference_source(ref,voice,out,requested,c.byref(count),c.byref(ended))
                        expected_count=requested if looping else length
                        assert count.value==expected_count and bool(ended.value)==(not looping)
                        cursor=0
                        for n in range(expected_count):
                            assert list(out[n*2:n*2+2])==values[cursor]
                            cursor=(loop if looping else last) if cursor==last else cursor+1
                        actual=word(table+voice*128+0x58)&0xffffff;assert actual==cursor
                        actual_bytes=start(0x1786e4,actual)
                        assert actual_bytes==cursor*align
                        assert start(0x179e2c,actual_bytes)==cursor
                        rows.append(dict(bits=bits,channels=channels,length=length,loop=looping,
                                         original_lbo=loop,original_ebo=last,frames=count.value,cursor_bytes=actual_bytes))
                    finally:dll.xlle_apu_reference_destroy(ref)
    if dependency:dependency.close()
    return dict(xbe_sha256=IMAGE,library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),cases=rows,
        scope='original region prefix before MMIO and whole unit helpers; actual maintained PCM DMA/source end handoff, not final notifier or public API completion')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','library','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--dependencies',type=Path);a=p.parse_args()
    result=run(a.xbe,a.library,a.dependencies);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: 40 original region -> maintained PCM source cases, exact samples and original cursor unit round trips')
