#!/usr/bin/env python3
"""Original ordinary Blinx constructors -> maintained pre-GP voice composition.
Source-first authored PCM16 stereo/ADPCM mono at44100/48000, actual default
headroom600 versus live0 override, phase0 unity/filter bypass/ordered sends,
whole original Stop/queued-work cancellation/unlink/detach/rebind/completion.
Allocated-voice/cold-RAM/synchronous-PIO/coherent-clock preconditions remain
explicit; this does not execute full allocation/configuration/ISR/DPC/title.
The required expectation artifact is written before original observations.
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


def run(xbe,library,dependencies,kernel=None,natural=False,expectations=None):
    assert not natural or kernel, 'natural completion requires the original kernel/driver owner'
    blob=xbe.read_bytes();assert hashlib.sha256(blob).hexdigest()==IMAGE
    dependency=os.add_dll_directory(str(dependencies.resolve())) if dependencies else None
    dll=c.CDLL(str(library.resolve()))
    dll.xlle_apu_reference_create.argtypes=[c.c_void_p,c.c_uint32];dll.xlle_apu_reference_create.restype=c.c_void_p
    dll.xlle_apu_reference_destroy.argtypes=[c.c_void_p]
    dll.xlle_apu_reference_error.argtypes=[c.c_void_p];dll.xlle_apu_reference_error.restype=c.c_char_p
    dll.xlle_apu_reference_write.argtypes=[c.c_void_p,c.c_uint32,c.c_int,c.c_uint32]
    dll.xlle_apu_reference_read.argtypes=[c.c_void_p,c.c_uint32,c.POINTER(c.c_uint32)]
    dll.xlle_apu_reference_quantum.argtypes=[c.c_void_p,c.c_uint32,c.POINTER(c.c_float),c.POINTER(c.c_uint32),c.POINTER(c.c_int)]
    dll.xlle_apu_reference_frame.argtypes=[c.c_void_p,c.c_uint64]
    dll.xlle_apu_reference_process.argtypes=[c.c_void_p,c.c_uint32,c.POINTER(c.c_float),c.POINTER(c.c_uint32),c.POINTER(c.c_int)]
    cpu=Uc(UC_ARCH_X86,UC_MODE_32);cpu.mem_map(0x10000,0x1000000);cpu.mem_map(0x30000000,0x10000)
    cpu.mem_map(0xfe800000,0x80000);cpu.mem_map(0x31000000,0x10000)
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
    kernel_hash=None;clock=0x0102030405060708
    if kernel:
        data=kernel.read_bytes();kernel_hash=hashlib.sha256(data).hexdigest()
        assert kernel_hash=='827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c'
        word=lambda off:struct.unpack_from('<I',data,off)[0]
        exports=word(word(0x3c)+120);entry=0x80010000+word(word(exports+28)+4*(128-word(exports+16)))
        cpu.mem_map(0x80010000,0x70000);cpu.mem_write(0x80010000,data)
        assert get(0x189e88)==0x80000080;put(0x189e88,entry) # bind real original kernel export
        put(0x80042ec0,clock);put(0x80042ec4,clock>>32);put(0x80042ec8,clock>>32)
    def call(address,args,context=None):
        put(stack,end)
        for n,arg in enumerate(args):put(stack+4+n*4,arg)
        put(stack+4+len(args)*4,0xabcdef01)
        saved=[(r,cpu.reg_read(r)) for r in (UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI)]
        cpu.reg_write(UC_X86_REG_ECX,obj if context is None else context);cpu.reg_write(UC_X86_REG_ESP,stack)
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
    cpu.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,transaction,begin=0xfe800000,end=0xfe87ffff)
    # Two virtual mappings of real device-visible RAM. Original guest stores
    # and device DMA share these bytes; no completion record is synthesized.
    notify_physical=0x4000+16*(2+71*4);notifier_accesses=[]
    def shared(uc,access,address,size,value,_):
        assert instance
        offset=notify_physical+address-notifiers if address<0x31000000 else 0x1000+address-0x31000000
        if address<0x31000000:
            assert (address-notifiers)%16==15 and size==1, 'notifier payload access outside qualified status contract'
            notifier_accesses.append((access,address-notifiers,size))
        if access==UC_MEM_READ:cpu.mem_write(address,bytes(ram[offset:offset+size]))
        else:ram[offset:offset+size]=value.to_bytes(size,'little')
    cpu.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,shared,begin=notifiers,end=notifiers+63)
    cpu.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,shared,begin=0x31000000,end=0x31007fff)
    # These expectations exist before any original constructor/processing call.
    # PCM spectra use authored quantized tones; the ADPCM zero-nibble blocks
    # have a constant header predictor (step index zero rounds delta to zero).
    import math
    import numpy as np
    from unicorn.x86_const import UC_X86_REG_FPCW
    from unicorn import UC_HOOK_MEM_INVALID
    def invalid(uc,access,address,size,value,_):
        print('invalid original memory',hex(uc.reg_read(UC_X86_REG_EIP)),access,hex(address),size,hex(value),flush=True)
        return False
    cpu.hook_add(UC_HOOK_MEM_INVALID,invalid)
    cpu.reg_write(UC_X86_REG_FPCW,0x027f)
    frames=2048;voice=71;base=0x1000+voice*128
    out,wfex,outer=0x30006000,0x30006100,0x30006200
    golden=[]
    constant=struct.unpack('<f',cpu.mem_read(0x1cbf6c,4))[0]
    for kind,channels,tag,bits,align in [('pcm16',2,1,16,4),('adpcm4',1,0x69,4,36)]:
        for frequency in (44100,48000):
            ratio=float(np.float32(frequency*constant))
            pitch=0 if frequency==48000 else round(4096*math.log2(ratio))
            assert abs((4096*math.log2(ratio))-pitch)<0.49
            positive=(b''.join(struct.pack('<h',round(0.2*32768*math.sin(math.tau*n/64+ch*math.pi/2))) for n in range(frames) for ch in range(channels))
                      if kind=='pcm16' else (struct.pack('<hBB',8192,0,0)+bytes(32))*(frames//64))
            negative=(struct.pack('<hh',-8192,-12288)*frames
                      if kind=='pcm16' else (struct.pack('<hBB',-8192,0,0)+bytes(32))*(frames//64))
            golden.append(dict(kind=kind,channels=channels,tag=tag,bits=bits,alignment=align,
                frequency=frequency,pitch=pitch,frequency_ratio_binary32=ratio,
                nominal_hz=48000*2**(pitch/4096)/64,
                default_user_headroom=600,default_attenuation=384,default_gain=10**(-384/1280),
                movie_user_headroom=0,movie_gain=1.0,apu_format_headroom=7,
                representative_live_frequency_override=48000 if kind=='pcm16' and frequency==44100 else None,
                frequency_override_pitch=0,frequency_override_hz=750.0,
                bins=[0,1],source_frames=frames,encoded_bytes=len(positive),
                positive_sha256=hashlib.sha256(positive).hexdigest(),negative_sha256=hashlib.sha256(negative).hexdigest(),
                positive=positive,negative=negative))
    expectation=[{k:v for k,v in row.items() if k not in ('positive','negative')} for row in golden]
    assert expectations is not None, 'explicit source-first expectation artifact required'
    expectation_path=Path(expectations)
    encoded=json.dumps(expectation,indent=2)+'\n'
    if expectation_path.exists():assert expectation_path.read_text()==encoded
    else:expectation_path.write_text(encoded)
    rows=[];faults=[]
    def fitted(data,hz,gain,kind,negative=False):
        # The same source-bound tolerance as the maintained original pitch/rate
        # fixture. No nominal source is taken from a device observation.
        steady=np.asarray(data,dtype=np.float64)[512:]
        assert len(steady)>=512 and steady.shape[1]==2
        if kind in ('adpcm4','pcm16-constant'):
            target=(np.array([-0.25,-0.375]) if kind=='pcm16-constant' else (-0.25 if negative else 0.25))*gain
            error=float(np.max(np.abs(steady-target)))
            assert error<0.00003,('constant source/gain',target,error)
            return dict(target=target.tolist() if hasattr(target,'tolist') else target,max_error=error)
        t=np.arange(len(steady),dtype=np.float64)
        design=np.column_stack((np.sin(math.tau*hz*t/48000),np.cos(math.tau*hz*t/48000)))
        fits=[]
        for channel in (0,1):
            a,b=np.linalg.lstsq(design,steady[:,channel],rcond=None)[0]
            amp=math.hypot(a,b);rms=float(np.sqrt(np.mean((steady[:,channel]-design@np.array([a,b]))**2)))
            assert abs(amp-0.2*gain)<0.001 and rms<0.0003,('tone gain/rate',channel,amp,gain,rms)
            fits.append(dict(amplitude=amp,rms_residual=rms))
        # Stereo source quadrature and ordered send slots survive the pipeline.
        a0,b0=np.linalg.lstsq(design,steady[:,0],rcond=None)[0]
        a1,b1=np.linalg.lstsq(design,steady[:,1],rcond=None)[0]
        assert abs(a0*a1+b0*b1)<0.0001
        return dict(fits=fits)
    for life in range(3):
      for g in golden:
        channels=g['channels'];frequency=g['frequency'];kind=g['kind'];payload=g['positive']
        cpu.mem_write(obj,bytes(0x168));cpu.mem_write(settings,bytes(0xdc));cpu.mem_write(device,bytes(0x1000))
        # Explicit precondition: original 175F5F requests zeroed class storage;
        # 1771A6 requests a 0x168-byte derived voice. No meaningful defaults are
        # assigned here: complete original constructors establish them below.
        # Existing real device identity (178236) and settings allocation prefix
        # (1771E4/1771EB); original AddRef is reached through these real vtables.
        put(device,0x1cbfbc);put(device+4,1);put(settings,0x1cbfac);put(settings+4,1)
        avg=frequency*channels*2 if kind=='pcm16' else frequency*36//64
        descriptor=struct.pack('<HHIIHHH',g['tag'],channels,frequency,avg,g['alignment'],g['bits'],0 if kind=='pcm16' else 2)
        if kind=='adpcm4':descriptor+=struct.pack('<H',64)
        cpu.mem_write(wfex,descriptor)
        assert call(0x176eec,[0,wfex,0],settings)==0
        assert get(settings+0x20)==600 and get(settings+0x1c)==0xfffffda8
        assert get(settings+0x18)==(g['pitch']&0xffffffff)
        assert get(settings+0x24)==2 and cpu.mem_read(settings+0x28,2)==b'\x00\x01'
        assert get(settings+0x30)==0 and get(settings+0x34)==0
        assert call(0x17a476,[device,settings])==obj
        assert get(obj)==0x1cbfec and get(obj+0x78)==settings and get(obj+0x90)==settings
        assert get(settings+4)==3 and get(device+4)==2
        assert call(0x178705,[0])==0 and call(0x179283,[0])==0
        default_format=get(obj+0x14)
        assert ((default_format>>13)&7)==7 and ((default_format>>27)&1)==channels-1
        assert get(obj+0x18)&0x30000==0 and get(obj+0x40)==0
        assert all(get(obj+offset)==0 for offset in (0x18,0x1c,0x20,0x24,0x28))
        cpu.mem_write(obj+0xc,struct.pack('<H',voice));cpu.mem_write(obj+0x12,b'\x01\x00')
        cpu.mem_write(obj+0x64,b'\x01')
        put(obj+0x68,notifiers);put(obj+0x6c,4);cpu.mem_write(notifiers,b'\xa5'*64)
        owner=device+0x728;put(owner,owner);put(owner+4,owner)
        # Actual device constructor 177C28..177C6D: empty ordinary voice and
        # deferred-work heads. This fixture does not execute device allocation.
        for offset in (0x730,0x738,0x740,0x748,0x750):
            head=device+offset;put(head,head);put(head+4,head)
        put(outer+0xc,obj);put(outer+0x10,settings)
        # Execute actual caller-owned buffer attachment. The allocation branch
        # is deliberately outside this fixture; no allocator is replaced.
        assert call(0x176fa7,[0x32000000,len(payload)],settings)==0
        assert get(settings+0xb8)==0x32000000 and get(settings+0xbc)==len(payload)
        assert get(settings+8)&0x80000000
        ram=(c.c_ubyte*0x10000)();instance=dll.xlle_apu_reference_create(ram,len(ram));assert instance
        ram[notify_physical:notify_physical+64]=b'\xa5'*64
        def word(a):return struct.unpack_from('<I',ram,a)[0]
        def store(a,v):struct.pack_into('<I',ram,a,v&0xffffffff)
        def write(a,v):assert dll.xlle_apu_reference_write(instance,0xfe800000+a,4,v),dll.xlle_apu_reference_error(instance)
        def read(a):
            value=c.c_uint32();assert dll.xlle_apu_reference_read(instance,0xfe800000+a,c.byref(value)),dll.xlle_apu_reference_error(instance)
            return value.value
        def put_payload(data):
            assert len(data)<=8192
            ram[0xa000:0xa000+len(data)]=data
        def configuration(loop):
            put(obj+0x14,default_format|((1<<25) if loop else 0))
            put(out,0);put(out+4,get(obj+0x14));cpu.mem_write(out+0x100,b'\xa5'*36)
            call(0x1789a0,[out,out+4]);call(0x178a7b,[out+0x100]);call(0x178c97,[out+8])
            route,fmt,pitch=get(out),get(out+4),get(out+8)
            assert route==0x14830820 # default [0,1], then unused [2,3,4,5,6,7]
            assert fmt&0x3ff==6|(7<<5) and pitch==((g['pitch']&0xffff)<<16)
            attenuation=0 if get(settings+0x20)==0 else 384
            expected=[(attenuation<<4)|(attenuation<<20)|0x000f000f,0xffffffff,0xffffffff]
            packed=[get(out+0x100+n*12) for n in range(3)]
            assert packed==expected,(packed,expected)
            write(0x202f8,voice);write(0x20300,route);write(0x20304,fmt)
            for n,value in enumerate(packed):write(0x20360+4*n,value)
            write(0x2037c,pitch);write(0x203a0,0);write(0x203a4,0);write(0x203d8,0);write(0x203dc,frames-1)
            return dict(route=route,format=fmt,pitch=pitch,packed_volume=packed)
        count=c.c_uint32();done=c.c_int();bins=(c.c_float*1024)()
        def block():
            c.memset(bins,0,c.sizeof(bins))
            assert dll.xlle_apu_reference_process(instance,voice,bins,c.byref(count),c.byref(done)),dll.xlle_apu_reference_error(instance)
            values=np.ctypeslib.as_array(bins).reshape(32,32)
            assert np.count_nonzero(values[2:])==0,'unused routing bins must stay untouched zero'
            assert np.count_nonzero(values[:2,count.value:])==0,'no output padding after finite completion'
            return values[:2,:count.value].T.copy()
        def stage(blocks):
            values=[]
            for _ in range(blocks):
                values.append(block());assert count.value==32 and not done.value
                assert ram[notify_physical+15]==0x80 and word(base+0x54)&(1<<21)
                assert word(base+0x58)>>24==255 and word(base+0x5c)>>24==255
                assert word(base+0x34)==0 and (word(base+0x54)&0xff000000)==0
            return np.concatenate(values)
        def unlink(natural,frame):
            cursor=word(base+0x58);assert not word(base+0x54)&(1<<21)
            assert call(0x179e0e,[0],obj+0x68)==1 and all(call(0x179e0e,[n],obj+0x68)==0 for n in (1,2,3))
            assert read(0x2054)==voice
            inactive=block();assert count.value==0 and len(inactive)==0 and word(base+0x58)==cursor
            write(0x1504,1<<15);write(0x2000,8)
            assert dll.xlle_apu_reference_frame(instance,frame),dll.xlle_apu_reference_error(instance)
            assert read(0x1300)==0x8000 and read(0x1304)==voice
            put(0x17df28,0x31000000);call(0x178d7c,[0])
            assert get(owner)==owner and get(owner+4)==owner and read(0x2054)==0xffff
            if natural:
                assert ram[notify_physical+63]==1 and call(0x17a34b,[])==1
                assert ram[notify_physical+63]==0x80
            assert int.from_bytes(cpu.mem_read(obj+0x12,2),'little')==1
            assert cpu.mem_read(obj+0x65,1)==b'\xff'
            write(0x1100,0);write(0x1000,0xffffffff)
            assert dll.xlle_apu_reference_frame(instance,frame+1) and read(0x1000)==0
            return cursor
        try:
            write(0x202c,0x1000);write(0x2030,0x9000);write(0x115c,0x4000)
            store(0x9000,0xa000);store(0x9008,0xb000);put_payload(payload)
            for a in (0x2054,0x2060,0x206c):write(a,0xffff)
            initial=configuration(True);transactions.clear();call(0x179c4b,[])
            assert get(owner)==obj+0x4c and int.from_bytes(cpu.mem_read(obj+0x12,2),'little')==3
            assert word(base+0x54)==1<<21
            assert bytes(ram[notify_physical:notify_physical+64])==b''.join(b'\xa5'*15+b'\x80' for _ in range(4))
            # Guard red cases use independent fresh device instances on a copy
            # of the real constructor/start-produced RAM. No live source is
            # overwritten and no failing device can be reused as a pass.
            if life==0:
                snapshot=bytes(ram)
                for name,field,value in [('nondefault APU headroom',4,word(base+4)&~(7<<13)),('unowned filter',0x18,word(base+0x18)|(1<<16))]:
                    damaged=(c.c_ubyte*len(ram)).from_buffer_copy(snapshot)
                    struct.pack_into('<I',damaged,base+field,value)
                    guard=dll.xlle_apu_reference_create(damaged,len(damaged));assert guard
                    try:
                        assert dll.xlle_apu_reference_write(guard,0xfe80202c,4,0x1000)
                        before=bytes(damaged);badcount=c.c_uint32(99);baddone=c.c_int(99)
                        assert not dll.xlle_apu_reference_process(guard,voice,bins,c.byref(badcount),c.byref(baddone))
                        assert bytes(damaged)==before and badcount.value==0 and baddone.value==0
                        assert not dll.xlle_apu_reference_process(guard,voice,bins,c.byref(badcount),c.byref(baddone))
                        faults.append(dict(kind=kind,frequency=frequency,name=name,unchanged_ram=True,persistent=True))
                    finally:dll.xlle_apu_reference_destroy(guard)
            default_data=stage(64);default_fit=fitted(default_data,g['nominal_hz'],g['default_gain'],kind)
            # Negative oracle control: confusing default600 with movie0 must
            # reject this independent fit, even though both modes are supported.
            try:fitted(default_data,g['nominal_hz'],1,kind)
            except AssertionError:pass
            else:raise AssertionError('incorrect movie gain accepted for original default')
            transactions.clear();assert call(0x176366,[outer,0])==0
            assert get(settings+0x20)==0 and get(settings+0x1c)==0
            emitted=[(a,v) for op,a,v in transactions if op=='write']
            assert [a for a,v in emitted]==[0xfe8202f8,0xfe8202fc,0xfe820360,0xfe820364,0xfe820368,0xfe8202fc]
            movie_data=stage(64);movie_fit=fitted(movie_data,g['nominal_hz'],1,kind)
            live_frequency=None
            if life==0 and kind=='pcm16' and frequency==44100:
                # Creation frequency is not effective playback rate. Execute
                # the full original public wrapper on its real +1C interface,
                # without rebinding source or resetting its rate owner.
                cursor=word(base+0x58);source_before=bytes(ram[0xa000:0xc000]);transactions.clear()
                assert call(0x176d56,[outer+0x1c,48000])==0
                assert get(settings+0x10)==44100 and get(settings+0x18)==0
                assert word(base+0x7c)>>16==0 and word(base+0x58)==cursor
                assert bytes(ram[0xa000:0xc000])==source_before
                change=[(a,v) for op,a,v in transactions if op=='write']
                assert change==[(0xfe8202f8,voice),(0xfe82037c,0)]
                fit48000=fitted(stage(64),750,1,kind)
                # Original frequency0 restores the immutable creation rate.
                assert call(0x176d56,[outer+0x1c,0])==0
                assert get(settings+0x10)==44100 and get(settings+0x18)==(g['pitch']&0xffffffff)
                live_frequency=dict(creation_frequency=44100,effective_frequency=48000,
                    actual_pitch=0,original_commands=change,fit=fit48000,
                    source_and_cursor_unchanged_by_setter=True,creation_rate_restored_by_original_zero=True)
            before_cursor=word(base+0x58);transactions.clear();call(0x179a44,[0])
            assert [a for op,a,v in transactions if op=='write']==[0xfe8202f8,0xfe820304,0xfe820128]
            assert word(base+0x58)==before_cursor and ram[notify_physical+15]==1
            assert int.from_bytes(cpu.mem_read(obj+0x80,8),'little')==clock
            assert get(device+0x750)==obj+0x98 and get(device+0x754)==obj+0x98
            assert get(obj+0xa0)==7,'real original Stop owns a deferred work item'
            stopped_cursor=unlink(False,1)
            # Cancel through the complete original derived command owner before
            # reusing the buffer. The full dispatcher/callback remains A05;
            # no pending stop may silently target the rebound source.
            call(0x17a45c,[0]) # original cancellation is void; verify ownership below
            assert get(device+0x750)==device+0x750 and get(device+0x754)==device+0x750
            assert get(obj+0x98)==obj+0x98 and get(obj+0x9c)==obj+0x98 and get(obj+0xa0)==5
            stable=bytes(cpu.mem_read(settings+0xc,0xa8))
            assert call(0x176fa7,[0,0],settings)==0
            assert get(settings+0xb8)==0 and get(settings+0xbc)==0
            assert bytes(cpu.mem_read(settings+0xc,0xa8))==stable
            assert call(0x176fa7,[0x32002000,len(g['negative'])],settings)==0
            assert get(settings+0xb8)==0x32002000 and get(settings+0xbc)==len(g['negative'])
            assert bytes(cpu.mem_read(settings+0xc,0xa8))==stable
            put_payload(g['negative']);finite=configuration(False);call(0x179c4b,[])
            finite_values=[];last=0
            # 2048 input frames, maximum ratio<1.09 for this source-bound pitch;
            # 128 blocks (4096 output frames) is a resource cap, not an accepted
            # timing tolerance. Exact final CBO and real DONE are mandatory.
            for quantum in range(128):
                value=block();finite_values.append(value);last=count.value
                if done.value:break
                assert ram[notify_physical+15]==0x80 and word(base+0x54)&(1<<21)
            assert done.value and ram[notify_physical+15]==1
            assert word(base+0x58)&0xffffff==frames-1
            final_data=np.concatenate(finite_values)
            nominal_frames=frames*math.exp2(-g['pitch']/4096)
            assert abs(len(final_data)-nominal_frames)<=1 # existing finite SRC contract
            assert last>0 or (len(finite_values)>=2 and len(finite_values[-2])==32)
            # Finite tail has the donor's declared filter drain; analyze only
            # the independently known interior, excluding both endpoint transients.
            final_fit=fitted(final_data[:-128],g['nominal_hz'],1,'pcm16-constant' if kind=='pcm16' else kind,negative=True)
            assert np.max(final_data[512:-128])<0,'old positive source/history survived rebind'
            final_cursor=unlink(True,3)
            rows.append(dict(life=life,kind=kind,frequency=frequency,constructor_pitch=g['pitch'],
                default_format=default_format,default_configuration=initial,
                default_fit=default_fit,movie_fit=movie_fit,original_headroom_update=emitted,
                live_frequency_override=live_frequency,
                stopped_cursor=stopped_cursor,finite_configuration=finite,finite_frames=len(final_data),
                last_output_count=last,finite_cursor=final_cursor,finite_fit=final_fit,
                original_stop_and_unlink=True,original_work_retention_and_cancel=True,
                original_detach_rebind=True,real_finite_done=True,
                envelope_phase0_unity=True,filter_bypass=True,ordered_bins=[0,1],unused_bins_zero=True))
            print('PASS life',life,kind,frequency,'pitch',g['pitch'],'finite',len(final_data),flush=True)
        finally:dll.xlle_apu_reference_destroy(instance);instance=None
    if dependency:dependency.close()
    return dict(xbe_sha256=IMAGE,kernel_sha256=kernel_hash,library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
        expectation_sha256=hashlib.sha256(expectation_path.read_bytes()).hexdigest(),cases=rows,guards=faults,
        source_gain_oracle_faults=len(rows),scope='whole original settings/derived voice constructors and init/filter-bypass defaults; original helper configuration bridge; actual maintained source/rate/phase0 amplitude/ordered-send pipeline; live original headroom600 to0 setter; original static Stop/inactive-frame/unlink, caller-owned detach/rebind and finite completion consumer; explicit allocated voice/coherent clock/synchronous PIO preconditions; no full DirectSound allocation/configuration emitter, ISR/DPC/SSL/title/effects/silicon precision qualification')

if __name__=='__main__':
    p=argparse.ArgumentParser(description='Private original-default A02 composed voice fixture')
    for arg in ('xbe','library','kernel','output'):p.add_argument('--'+arg,type=Path,required=True)
    p.add_argument('--dependencies',type=Path)
    p.add_argument('--expectations',type=Path,required=True);args=p.parse_args()
    result=run(args.xbe,args.library,args.dependencies,args.kernel,expectations=args.expectations)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS:',len(result['cases']),'original-default composed lives;',len(result['guards']),'real guards')
