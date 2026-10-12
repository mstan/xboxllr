#!/usr/bin/env python3
"""Whole original public PCM position API -> actual maintained APU source CBO.

The allocated ordinary voice runs at real DISPATCH_LEVEL with exclusive fixture
ownership. Every original helper/lock/conversion executes without replacement;
MMIO and shared table reads use the maintained APU. No silicon prefetch latency,
full DirectSound owner, original Stop or natural finite saved-cursor lifecycle
is inferred from the authored stopped fields.
"""
import argparse
import ctypes as c
import hashlib
import json
import os
import struct
import time
from pathlib import Path
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_READ
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
    UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESP,
    UC_X86_REG_GDTR, UC_X86_REG_CS, UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_FS)
from aot_inventory import parse_xbe

def identity(path):
    path=Path(path)
    return dict(path=str(path),bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def run(xbe,library,dependencies=None):
    dependency = os.add_dll_directory(str(dependencies.resolve())) if dependencies else None
    dll = c.CDLL(str(library.resolve()))
    dll.xlle_apu_reference_create.argtypes=[c.c_void_p,c.c_uint32]
    dll.xlle_apu_reference_create.restype=c.c_void_p
    dll.xlle_apu_reference_destroy.argtypes=[c.c_void_p]
    dll.xlle_apu_reference_error.argtypes=[c.c_void_p]
    dll.xlle_apu_reference_error.restype=c.c_char_p
    dll.xlle_apu_reference_write.argtypes=[c.c_void_p,c.c_uint32,c.c_int,c.c_uint32]
    dll.xlle_apu_reference_read.argtypes=[c.c_void_p,c.c_uint32,c.POINTER(c.c_uint32)]
    for name in ('source','rate'):
        getattr(dll,'xlle_apu_reference_'+name).argtypes=[c.c_void_p,c.c_uint32,c.POINTER(c.c_float),c.c_uint32,c.POINTER(c.c_uint32),c.POINTER(c.c_int)]
    blob=xbe.read_bytes()
    assert identity(xbe)['sha256']=='e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
    cpu=Uc(UC_ARCH_X86,UC_MODE_32)
    cpu.mem_map(0x10000,0x1000000)
    cpu.mem_map(0x30000000,0x10000)
    cpu.mem_map(0x31000000,0x10000)
    cpu.mem_map(0xfe800000,0x80000)
    for section in parse_xbe(blob)['sections']:
        if section['raw_size'] and section['address']+section['raw_size']<=0x1010000:
            cpu.mem_write(section['address'],blob[section['raw_offset']:section['raw_offset']+section['raw_size']])
    obj,fmt,pcr,out,outer,stack,end,gdt=0x30000000,0x30001000,0x30002000,0x30003000,0x30007000,0x30008000,0x30009000,0x3000f000
    def put(a,v):cpu.mem_write(a,struct.pack('<I',v&0xffffffff))
    def get(a):return struct.unpack('<I',cpu.mem_read(a,4))[0]
    fs=0x00c093000000ffff|((pcr&0xffffff)<<16)|((pcr>>24)<<56)
    cpu.mem_write(gdt,struct.pack('<QQQQ',0,0x00cf9b000000ffff,0x00cf93000000ffff,fs))
    cpu.reg_write(UC_X86_REG_GDTR,(0,gdt,31,0))
    for r,v in [(UC_X86_REG_CS,8),(UC_X86_REG_SS,16),(UC_X86_REG_DS,16),(UC_X86_REG_ES,16),(UC_X86_REG_FS,24)]:cpu.reg_write(r,v)
    cpu.mem_write(pcr+0x24,b'\x02') # Real dispatch-level precondition; no substituted kernel calls.
    put(0x17df28,0x31000000)
    put(0x17d7bc,0)
    instance=None
    ram=None
    transactions=[]
    shared_reads=[]
    def transaction(uc,access,address,size,value,data):
        assert instance and size==4
        if access==UC_MEM_READ:
            actual=c.c_uint32()
            assert dll.xlle_apu_reference_read(instance,address,c.byref(actual)),dll.xlle_apu_reference_error(instance)
            put(address,actual.value)
            transactions.append(['read',address,actual.value])
        else:
            assert dll.xlle_apu_reference_write(instance,address,size,value),dll.xlle_apu_reference_error(instance)
            transactions.append(['write',address,value])
    def shared(uc,access,address,size,value,data):
        assert instance and access==UC_MEM_READ
        physical=0x1000+address-0x31000000
        actual=bytes(ram[physical:physical+size])
        cpu.mem_write(address,actual)
        shared_reads.append([physical,size,actual.hex()])
    cpu.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,transaction,begin=0xfe800000,end=0xfe87ffff)
    cpu.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,shared,begin=0x31000000,end=0x31007fff)
    def call(address,args,context=obj):
        put(stack,end)
        for i,arg in enumerate(args):put(stack+4+i*4,arg)
        put(stack+4+len(args)*4,0xabcdef01)
        preserved=[(r,cpu.reg_read(r)) for r in (UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI)]
        cpu.reg_write(UC_X86_REG_ECX,context)
        cpu.reg_write(UC_X86_REG_ESP,stack)
        cpu.emu_start(address,end,count=100000)
        assert cpu.reg_read(UC_X86_REG_EIP)==end and cpu.reg_read(UC_X86_REG_ESP)==stack+4+len(args)*4
        assert get(stack+4+len(args)*4)==0xabcdef01
        assert all(cpu.reg_read(r)==v for r,v in preserved)
        assert cpu.mem_read(pcr+0x24,1)==b'\x02'
        return cpu.reg_read(UC_X86_REG_EAX)
    rows=[]
    started=time.monotonic()
    for bits in (8,16):
     for channels in (1,2):
      for pitch in (None,0,-501):
       for partial in (False,True):
        cpu.mem_write(obj,bytes(0x200));cpu.mem_write(fmt,bytes(0x100))
        put(obj+0x78,fmt);put(obj+0x90,fmt);put(outer+0x20,obj)
        align=channels*bits//8
        put(fmt+0xc,1);cpu.mem_write(fmt+0xe,bytes([channels,bits]));put(fmt+0x14,align)
        assert call(0x178805,[])==0
        encoded=get(obj+0x14)
        frames=128;loop_start=37 if partial else 0;loop_span=61 if partial else frames
        put(fmt+0xbc,frames*align);put(fmt+0xc8,loop_start*align);put(fmt+0xcc,loop_span*align)
        cpu.mem_write(obj+0x12,struct.pack('<H',3|(0x200 if partial else 0)))
        voice=7;base=0x1000+voice*128
        cpu.mem_write(obj+0xc,struct.pack('<H',voice));cpu.mem_write(obj+0xa,struct.pack('<H',voice));cpu.mem_write(obj+0x64,b'\x00')
        ram=(c.c_ubyte*0x10000)();instance=dll.xlle_apu_reference_create(ram,len(ram));assert instance
        def store(a,v):struct.pack_into('<I',ram,a,v&0xffffffff)
        def word(a):return struct.unpack_from('<I',ram,a)[0]
        def write(o,v):assert dll.xlle_apu_reference_write(instance,0xfe800000+o,4,v),dll.xlle_apu_reference_error(instance)
        try:
         store(0x3000,0x8000);store(0x3008,0x5000)
         for i in range(frames*align):
            address=4089+i
            ram[word(0x3000+(address>>12)*8)+(address&4095)]=(i*37+11)&255
         write(0x202c,0x1000);write(0x2030,0x3000);write(0x202f8,voice)
         store(base+0x54,1<<21)
         write(0x20304,encoded|(1<<25));write(0x203a0,4089)
         write(0x203a4,loop_start);write(0x203dc,loop_start+loop_span-1)
         if pitch is not None:write(0x2037c,(pitch&0xffff)<<16)
         for wanted in (31,37,83):
          samples=(c.c_float*(wanted*2))();count=c.c_uint32();ended=c.c_int()
          pull=dll.xlle_apu_reference_source if pitch is None else dll.xlle_apu_reference_rate
          assert pull(instance,voice,samples,wanted,c.byref(count),c.byref(ended))
          assert count.value==wanted and not ended.value
          cbo=word(base+0x58)&0xffffff
          expected_play=cbo*align
          safe=expected_play+max(32*align,align)
          expected_write=loop_start*align+safe%(loop_span*align) if partial and loop_start<=cbo<loop_start+loop_span else safe%(frames*align)
          transactions.clear();shared_reads.clear();before=bytes(ram)
          put(out,0xaabbccdd);put(out+4,0x11223344)
          assert call(0x176ab8,[outer+0x1c,out,out+4])==0
          assert (get(out),get(out+4))==(expected_play,expected_write)
          assert bytes(ram)==before
          writes=[x for x in transactions if x[0]=='write']
          assert writes==[['write',0xfe8202f8,voice],['write',0xfe8202fc,1],['write',0xfe8202fc,0]]
          assert [base+0x58,4,struct.pack('<I',word(base+0x58)).hex()] in shared_reads
          rows.append(dict(bits=bits,channels=channels,pitch=pitch,partial_loop=partial,
               output_frames_pulled=wanted,actual_cbo_frames=cbo,play=get(out),write=get(out+4),
               original_mmio=list(transactions),shared_reads=list(shared_reads)))
         # Complete stopped branch and all optional pointer combinations.
         cpu.mem_write(obj+0x12,b'\x00\x00');saved=29*align;put(obj+0x158,saved)
         for read_play,read_write in ((True,True),(True,False),(False,True),(False,False)):
          transactions.clear();put(out,0xaabbccdd);put(out+4,0x11223344)
          assert call(0x176ab8,[outer+0x1c,out if read_play else 0,out+4 if read_write else 0])==0
          assert get(out)==(saved if read_play else 0xaabbccdd) and get(out+4)==(saved if read_write else 0x11223344)
          assert not transactions
          rows.append(dict(bits=bits,channels=channels,pitch=pitch,partial_loop=partial,
                           stopped=True,read_play=read_play,read_write=read_write,saved_position=saved))
        finally:
         dll.xlle_apu_reference_destroy(instance);instance=None

    return dict(schema='xlle-original-public-audio-cursor-v1',
        outcome='PASS complete original public/helper/lock/byte-conversion chain against actual maintained source CBO',
        cases=len(rows),elapsed_seconds=time.monotonic()-started,results=rows,
        inputs=[identity(xbe),identity(library),identity(Path(__file__))],
        preconditions=['Allocated ordinary PCM voice; real DISPATCH_LEVEL FS context and exclusive fixture owner.',
          'Every MMIO load/store routes through maintained APU; actual source fetches and original voice lock/CBO reads; no substituted callees.',
          'Playback/source pitch and exact hardware prefetch latency remain scoped precision questions.',
          'Authored stopped fields test returned arithmetic only, not the original Stop/finite-completion owner.'])

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe',type=Path,required=True)
    parser.add_argument('--library',type=Path,required=True)
    parser.add_argument('--dependencies',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    result=run(args.xbe,args.library,args.dependencies)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: '+str(result['cases'])+' whole original PCM position cases, actual source CBO/MMIO lock and real stack ABI')
