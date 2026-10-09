#!/usr/bin/env python3
"""Original NTSC routing/pitch/volume helpers -> maintained APU source/send stage.

No original callees are replaced. The driver helpers and the hardware method
boundary are tested separately: this does not execute the full DirectSound
owner, its lock protocol, upstream resampling/envelopes, or a title LLE backend.
"""
import argparse
import ctypes as c
import hashlib
import json
import math
import os
import struct
from pathlib import Path

from aot_inventory import parse_xbe
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_ESI,
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
    dll.xlle_apu_reference_source.argtypes=[c.c_void_p,c.c_uint32,c.POINTER(c.c_float),c.c_uint32,c.POINTER(c.c_uint32),c.POINTER(c.c_int)]
    dll.xlle_apu_reference_source.restype=c.c_int
    dll.xlle_apu_reference_mix.argtypes=[c.c_void_p,c.c_uint32,c.POINTER(c.c_float),c.c_uint32,c.POINTER(c.c_float)]
    dll.xlle_apu_reference_mix.restype=c.c_int
    dll.xlle_apu_reference_rate.argtypes=dll.xlle_apu_reference_source.argtypes
    dll.xlle_apu_reference_rate.restype=c.c_int
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
        return cpu.reg_read(UC_X86_REG_EAX)

    ram=(c.c_ubyte*0x10000)()
    instance=dll.xlle_apu_reference_create(ram,len(ram));assert instance
    def write(offset,value):
        assert dll.xlle_apu_reference_write(instance,0xfe800000+offset,4,value),dll.xlle_apu_reference_error(instance)
    def word(address):return struct.unpack_from('<I',ram,address)[0]
    def store(address,value):struct.pack_into('<I',ram,address,value&0xffffffff)
    rows=[];mix_rows=[];rate_rows=[]
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

        # Complete original 2D volume packing helper, then real source DMA and
        # maintained final send-stage output. The raw source is already at
        # 48 kHz, unity envelope and no filtering: those upstream stages are
        # separate contracts, not silently synthesized by the mix function.
        for channels in (1,2):
            for bins in ([],[0,1],[5],[31,3,7],[7,6,5,4,3,2,1,0],[7,7,31,2]):
                for overall in (-10000,-6400,-6399,-6000,-1800,-1,0):
                    cpu.mem_write(obj,bytes(0x100));cpu.mem_write(settings,bytes(0x100))
                    put(obj+0x78,settings);cpu.mem_write(obj+0x64,b'\x01')
                    put(settings+0x1c,overall);put(settings+0x24,len(bins))
                    cpu.mem_write(settings+0x28,bytes(bins))
                    levels={b:(-1200,-31,0,-600)[b%4] for b in bins}
                    for b,level in levels.items():put(settings+0x30+b*4,level)
                    put(out,0);put(out+4,0x50000000|((channels-1)<<27)|((channels-1)<<16))
                    call(0x1789a0,[out,out+4])
                    cpu.mem_write(out+0x100,b'\xa5'*36)
                    call(0x178a7b,[out+0x100])
                    packed=[get(out+0x100+i*12) for i in range(3)]
                    attenuation=[min(4095,-(overall+levels[b])*64//100) for b in bins]+[4095]*(8-len(bins))
                    for part in range(3):
                        expected=(attenuation[part*2]<<4)|(attenuation[part*2+1]<<20)
                        expected|=((attenuation[6]>>(part*4))&15)|(((attenuation[7]>>(part*4))&15)<<16)
                        assert packed[part]==expected
                    assert all(get(out+0x100+i*4)==0xa5a5a5a5 for i in (1,2,4,5,7,8))
                    voice=71;base=0x1000+voice*128
                    c.memset(c.addressof(ram)+base,0,128);store(base+0x54,1<<21)
                    write(0x2030,0x9000);store(0x9000,0xa000)
                    values=[]
                    for n in range(32):
                        pair=[(n-16)/32,(32-n)/64] if channels==2 else [(n-16)/32]*2
                        values.append(pair)
                        for ch in range(channels):struct.pack_into('<h',ram,0xa000+(n*channels+ch)*2,round(pair[ch]*32768))
                    write(0x202f8,voice);write(0x20300,get(out));write(0x20304,get(out+4))
                    write(0x203dc,31)
                    for part,value in enumerate(packed):write(0x20360+part*4,value)
                    headrooms=[(b%4 if overall==-1800 else 0) for b in range(32)]
                    for b,h in enumerate(headrooms):write(0x20200+b*4,0xa5000000|h)
                    source=(c.c_float*64)();count=c.c_uint32();ended=c.c_int()
                    assert dll.xlle_apu_reference_source(instance,voice,source,32,c.byref(count),c.byref(ended))
                    assert count.value==32 and ended.value==1
                    assert list(source)==[v for pair in values for v in pair]
                    before=bytes(ram);actual=(c.c_float*1024)()
                    assert dll.xlle_apu_reference_mix(instance,voice,source,32,actual)
                    assert bytes(ram)==before, 'send stage must not mutate cursor/envelope/notification state'
                    expected=[0.0]*1024
                    for slot,b in enumerate(bins):
                        gain=0.0 if attenuation[slot]==4095 else 10**(-attenuation[slot]/1280)/2**headrooms[b]
                        for n in range(32):expected[b*32+n]+=values[n][slot%channels]*gain
                    assert all(math.isclose(v,e,rel_tol=3e-7,abs_tol=2e-8) for v,e in zip(actual,expected))
                    mix_rows.append(dict(channels=channels,bins=bins,overall_db100=overall,
                                         volume_words=packed,submix_headroom=headrooms))

        # Whole original pitch helper drives actual rate conversion. The source
        # tone is authored independently; neither donor resampler nor HLE output
        # supplies the expected frequency or amplitude.
        for channels in (1,2):
            for pitch in (-8192,-4096,-501,0,4096,8191):
                cpu.mem_write(settings,bytes(0x100));put(settings+0x18,pitch)
                put(settings+0xc,1);put(settings+0x14,channels*2)
                call(0x178c97,[out+8])
                c.memset(c.addressof(ram)+base,0,128);store(base+0x54,1<<21)
                write(0x202f8,71);write(0x20304,0x52000000|((channels-1)<<27)|((channels-1)<<16))
                write(0x203a0,0);write(0x203a4,0);write(0x203d8,0);write(0x203dc,127)
                write(0x2037c,get(out+8))
                for n in range(128):
                    for channel in range(channels):
                        value=round(0.2*32768*math.sin(math.tau*n/64+channel*math.pi/2))
                        struct.pack_into('<h',ram,0xa000+(n*channels+channel)*2,value)
                samples=(c.c_float*8192)();count=c.c_uint32();ended=c.c_int()
                assert dll.xlle_apu_reference_rate(instance,71,samples,4096,c.byref(count),c.byref(ended))
                assert count.value==4096 and not ended.value
                hz=48000*2**(pitch/4096)/64
                sines=[math.sin(math.tau*hz*n/48000) for n in range(3584)]
                cosines=[math.cos(math.tau*hz*n/48000) for n in range(3584)]
                ss=sum(v*v for v in sines);cc=sum(v*v for v in cosines);sc=sum(s*v for s,v in zip(sines,cosines))
                fits=[]
                for channel in (0,1):
                    data=list(samples[1024+channel::2]);ys=sum(y*s for y,s in zip(data,sines));yc=sum(y*v for y,v in zip(data,cosines))
                    a=(ys*cc-yc*sc)/(ss*cc-sc*sc);b=(yc*ss-ys*sc)/(ss*cc-sc*sc)
                    amplitude=math.hypot(a,b)
                    rms=math.sqrt(sum((y-a*s-b*v)**2 for y,s,v in zip(data,sines,cosines))/len(data))
                    assert abs(amplitude-0.2)<0.001 and rms<0.0003
                    fits.append(dict(amplitude=amplitude,rms_residual=rms))
                cursor=word(base+0x58)&0xffffff
                byte_cursor=call(0x1786e4,[cursor]);assert byte_cursor==cursor*channels*2
                assert call(0x179e2c,[byte_cursor])==cursor
                rate_rows.append(dict(channels=channels,pitch=pitch,original_word=get(out+8),
                                      tone_hz=hz,fits=fits,original_cursor_bytes=byte_cursor))
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
                original_volume_mix_cases=mix_rows,
                original_pitch_rate_cases=rate_rows,
                scope='complete original routing/pitch/volume helpers; maintained source DMA, nominal rate and final 2D send stage; modulation/filter/envelope and whole-device output are separate')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['xbe','library','output']:parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--dependencies',type=Path)
    args=parser.parse_args()
    result=run(args.xbe,args.library,args.dependencies)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(f"PASS: 55 original routing/pitch cases, {len(result['original_volume_mix_cases'])} volume/mix cases, {len(result['original_pitch_rate_cases'])} original pitch/rate spectra, 16 fields, 5 guards")
