#!/usr/bin/env python3
"""Whole original 2D initialization/start owner -> real maintained APU methods.

Explicit precondition: allocated mono/stereo voice, empty original owner list,
DISPATCH_LEVEL, no spatial/output-buffer owner. No callee or return is replaced.
The synchronous method model executes every admitted write before reporting free
PIO capacity. This does not qualify interrupt/SSL/completion or the full driver.
"""
import argparse
import ctypes as c
import hashlib
import json
import os
import struct
from pathlib import Path
from aot_inventory import parse_xbe
from native_audio_configuration_reference import IMAGE
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE,UC_MEM_READ
from unicorn.x86_const import (UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EBP,
    UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EIP,UC_X86_REG_ESP,UC_X86_REG_GDTR,
    UC_X86_REG_CS,UC_X86_REG_SS,UC_X86_REG_DS,UC_X86_REG_ES,UC_X86_REG_FS)


def run(xbe,library,dependencies):
    blob=xbe.read_bytes();assert hashlib.sha256(blob).hexdigest()==IMAGE
    dependency=os.add_dll_directory(str(dependencies.resolve())) if dependencies else None
    dll=c.CDLL(str(library.resolve()))
    dll.xlle_apu_reference_create.argtypes=[c.c_void_p,c.c_uint32];dll.xlle_apu_reference_create.restype=c.c_void_p
    dll.xlle_apu_reference_destroy.argtypes=[c.c_void_p]
    dll.xlle_apu_reference_error.argtypes=[c.c_void_p];dll.xlle_apu_reference_error.restype=c.c_char_p
    dll.xlle_apu_reference_write.argtypes=[c.c_void_p,c.c_uint32,c.c_int,c.c_uint32]
    dll.xlle_apu_reference_read.argtypes=[c.c_void_p,c.c_uint32,c.POINTER(c.c_uint32)]
    dll.xlle_apu_reference_quantum.argtypes=[c.c_void_p,c.c_uint32,c.POINTER(c.c_float),c.POINTER(c.c_uint32),c.POINTER(c.c_int)]
    cpu=Uc(UC_ARCH_X86,UC_MODE_32);cpu.mem_map(0x10000,0x1000000);cpu.mem_map(0x30000000,0x10000)
    cpu.mem_map(0xfe820000,0x1000)
    for section in parse_xbe(blob)['sections']:
        if section['raw_size'] and section['address']+section['raw_size']<=0x1010000:
            cpu.mem_write(section['address'],blob[section['raw_offset']:section['raw_offset']+section['raw_size']])
    obj,settings,device,pcr,notifiers,stack,end,gdt=0x30000000,0x30001000,0x30002000,0x30003000,0x30004000,0x30008000,0x30009000,0x3000f000
    fs=0x00c093000000ffff|((pcr&0xffffff)<<16)|((pcr>>24)<<56)
    cpu.mem_write(gdt,struct.pack('<QQQQ',0,0x00cf9b000000ffff,0x00cf93000000ffff,fs))
    cpu.reg_write(UC_X86_REG_GDTR,(0,gdt,31,0))
    for r,v in [(UC_X86_REG_CS,8),(UC_X86_REG_SS,16),(UC_X86_REG_DS,16),(UC_X86_REG_ES,16),(UC_X86_REG_FS,24)]:cpu.reg_write(r,v)
    cpu.mem_write(pcr+0x24,b'\x02')
    def put(a,v):cpu.mem_write(a,struct.pack('<I',v&0xffffffff))
    def get(a):return struct.unpack('<I',cpu.mem_read(a,4))[0]
    def call(address,args):
        put(stack,end)
        for n,arg in enumerate(args):put(stack+4+n*4,arg)
        put(stack+4+len(args)*4,0xabcdef01)
        saved=[(r,cpu.reg_read(r)) for r in (UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI)]
        cpu.reg_write(UC_X86_REG_ECX,obj);cpu.reg_write(UC_X86_REG_ESP,stack)
        cpu.emu_start(address,end,count=100000)
        assert cpu.reg_read(UC_X86_REG_EIP)==end and cpu.reg_read(UC_X86_REG_ESP)==stack+4+len(args)*4
        assert get(stack+4+len(args)*4)==0xabcdef01 and all(cpu.reg_read(r)==v for r,v in saved)
        assert cpu.mem_read(pcr+0x24,1)==b'\x02'
        return cpu.reg_read(UC_X86_REG_EAX)

    instance=None;transactions=[]
    def transaction(uc,access,address,size,value,_):
        assert instance and size==4
        if access==UC_MEM_READ:
            result=c.c_uint32();assert dll.xlle_apu_reference_read(instance,address,c.byref(result)),dll.xlle_apu_reference_error(instance)
            put(address,result.value);transactions.append(('read',address,result.value))
        else:
            assert dll.xlle_apu_reference_write(instance,address,size,value),dll.xlle_apu_reference_error(instance)
            transactions.append(('write',address,value))
    cpu.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,transaction,begin=0xfe820000,end=0xfe820fff)
    rows=[]
    for channels in (1,2):
        for amplitude,filter_phase in ((0,0),(1,3),(2,1),(3,2)):
            cpu.mem_write(obj,bytes(0x100));cpu.mem_write(settings,bytes(0x100));cpu.mem_write(device,bytes(0x1000))
            # Actual original class vtable, including the original format callee.
            put(obj,0x1cbfc8);put(obj+0x78,settings);put(obj+8,device)
            put(settings+0xc,1|(channels<<16)|(16<<24));put(settings+0x14,channels*2)
            assert call(0x178705,[0])==0
            assert get(obj+0x14)==0x5000e000|((channels-1)<<27)|((channels-1)<<16)
            assert all(get(obj+offset)==0 for offset in (0x18,0x1c,0x20,0x24,0x28,0x40))
            assert call(0x179283,[0])==0 # original 2D filter defaults stay bypass
            voice=71;base=0x1000+voice*128
            cpu.mem_write(obj+0xc,struct.pack('<H',voice));cpu.mem_write(obj+0x12,b'\x01\x00')
            put(obj+0x40,(amplitude<<28)|(filter_phase<<24));put(obj+0x14,get(obj+0x14)|(1<<25))
            put(obj+0x68,notifiers);put(obj+0x6c,4);cpu.mem_write(notifiers,b'\xa5'*64)
            owner=device+0x728;put(owner,owner);put(owner+4,owner)
            ram=(c.c_ubyte*0x10000)();instance=dll.xlle_apu_reference_create(ram,len(ram));assert instance
            def word(a):return struct.unpack_from('<I',ram,a)[0]
            def store(a,v):struct.pack_into('<I',ram,a,v)
            def write(a,v):assert dll.xlle_apu_reference_write(instance,0xfe800000+a,4,v),dll.xlle_apu_reference_error(instance)
            try:
                write(0x202c,0x1000);write(0x2030,0x9000);store(0x9000,0xa000)
                for n in range(128):
                    for channel in range(channels):struct.pack_into('<h',ram,0xa000+(n*channels+channel)*2,4096*(channel+1))
                write(0x2054,0xffff);write(0x2060,0xffff);write(0x206c,0xffff);write(0x202f8,voice)
                write(0x20304,get(obj+0x14));write(0x203dc,127)
                write(0x20308,0x1001);write(0x2030c,0x2001);write(0x20310,0x3001);write(0x20314,0x4001)
                transactions.clear();call(0x179c4b,[])
                assert get(device+0x324)==0 and get(owner)==obj+0x4c and get(owner+4)==obj+0x4c
                assert get(obj+0x4c)==owner and get(obj+0x50)==owner
                assert int.from_bytes(cpu.mem_read(obj+0x12,2),'little')==3
                record=bytes(cpu.mem_read(notifiers,64));assert record==b''.join(b'\xa5'*15+b'\x80' for _ in range(4))
                commands=[(a,v) for op,a,v in transactions if op=='write']
                assert commands==[(0xfe8202f8,voice),(0xfe8202fc,1),(0xfe820304,get(obj+0x14)),
                    (0xfe820120,0x1ffff),(0xfe820124,voice|(amplitude<<28)|(filter_phase<<24)),
                    (0xfe820140,voice),(0xfe8202fc,0)]
                assert word(base+0x54)==(1<<21)|(amplitude<<28)|(filter_phase<<24)
                ca=(0,16,0,32)[amplitude];cf=(0,48,0,64)[filter_phase]
                assert word(base+0x34)==ca|(cf<<16)
                samples=(c.c_float*64)();count=c.c_uint32();ended=c.c_int()
                for _ in range(4):
                    assert dll.xlle_apu_reference_quantum(instance,voice,samples,c.byref(count),c.byref(ended)),dll.xlle_apu_reference_error(instance)
                    assert count.value==32 and not ended.value
                if amplitude==0:
                    assert all(abs(samples[n*2+channel]-(0.25 if channels==2 and channel else 0.125))<0.00003 for n in range(32) for channel in (0,1))
                elif amplitude==1:assert all(v==0 for v in samples)
                assert bytes(cpu.mem_read(notifiers,64))==record, 'source work cannot invent completed notifiers'
                rows.append(dict(channels=channels,amplitude_start=amplitude,filter_start=filter_phase,
                                 commands=commands,counters_after_start=ca|(cf<<16),frames=128))
            finally:dll.xlle_apu_reference_destroy(instance);instance=None
    if dependency:dependency.close()
    return dict(xbe_sha256=IMAGE,library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),cases=rows,
                scope='whole original nonspatial init/filter-default/start routines, actual PIO/list/envelope/source effects; allocated voice and synchronous PIO preconditions; no full driver/completion/title qualification')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for arg in ('xbe','library','output'):p.add_argument('--'+arg,type=Path,required=True)
    p.add_argument('--dependencies',type=Path);args=p.parse_args()
    result=run(args.xbe,args.library,args.dependencies);args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(f"PASS: {len(result['cases'])} whole original init/filter-default/start owners -> real PIO, list, counters and source/envelope output")
