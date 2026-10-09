#!/usr/bin/env python3
"""Original Blinx audio IRQ5/PIC/kernel/ISR and DPC queue over maintained APU.

Uses owned hash-bound XBE and initialized kernel RAM. Preallocated buffer/driver
state bounds this to control/completion, not complete APU initialization or title
audio. The DPC's uncertain FE fence remains guarded; --barrier-audit reproduces
that boundary. No original callee, notifier or interrupt result is replaced.
"""
import argparse
import ctypes as c
import gc
import hashlib
import json
import os
from pathlib import Path
import struct
import traceback

from aot_inventory import parse_xbe
from native_audio_configuration_reference import IMAGE
from native_io_device_reference import Reference, SNAPSHOT_SHA
from native_vblank_reference import DisplayReference
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI,
    UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_EFLAGS)

DEVICE, OBJECT, SETTINGS = 0x10001000, 0x10002000, 0x10003000
STACK, STOP, INTERRUPT = 0x10008000, 0x10009000, 0x17de78
APU, VOICE = 0xfe800000, 71
# Revision-specific original entry bindings; never shared runtime addresses.
ENTRIES = dict(init=0x178705, filter=0x179283, start=0x179c4b,
               stop=0x179a44, isr=0x17856e, dpc=0x17838e,
               unlink=0x178d7c, completion=0x17a34b, fence=0x177df0)


class AudioHardware:
    def __init__(self, library, host, size):
        self.directories=[]
        if os.name=='nt':
            for directory in (library.parent,Path('C:/msys64/mingw64/bin')):
                self.directories.append(os.add_dll_directory(str(directory.resolve())))
        self.dll=c.CDLL(str(library.resolve()))
        signatures={
            'create':(c.c_void_p,[c.c_void_p,c.c_uint32]),
            'destroy':(None,[c.c_void_p]), 'error':(c.c_char_p,[c.c_void_p]),
            'write':(c.c_int,[c.c_void_p,c.c_uint32,c.c_int,c.c_uint32]),
            'read':(c.c_int,[c.c_void_p,c.c_uint32,c.POINTER(c.c_uint32)]),
            'pic':(None,[c.c_void_p,c.c_uint16,c.c_uint16]),
            'port':(c.c_uint32,[c.c_void_p,c.c_uint16,c.c_int,c.c_int,c.c_uint32]),
            'vector':(c.c_int,[c.c_void_p,c.c_int]), 'irq':(c.c_int,[c.c_void_p]),
            'frame':(c.c_int,[c.c_void_p,c.c_uint64]),
            'process':(c.c_int,[c.c_void_p,c.c_uint32,c.POINTER(c.c_float),c.POINTER(c.c_uint32),c.POINTER(c.c_int)]),
        }
        self.functions={}
        for name,(result,arguments) in signatures.items():
            function=getattr(self.dll,'xlle_apu_reference_'+name)
            function.restype,function.argtypes=result,arguments
            self.functions[name]=function
        self.instance=self.functions['create'](host,size)
        if not self.instance:raise RuntimeError('APU reference allocation failed')

    def call(self,name,*args):
        if name=='read':
            value=c.c_uint32()
            ok=self.functions[name](self.instance,args[0],c.byref(value));result=value.value
        else:
            result=self.functions[name](self.instance,*args)
            ok=result if name in ('write','frame','process') else True
        error=self.functions['error'](self.instance)
        if error or not ok:raise RuntimeError(error.decode() if error else 'failed APU transaction '+name)
        return result

    def close(self):
        self.functions['destroy'](self.instance)
        for directory in self.directories:directory.close()


class AudioReference(DisplayReference):
    def __init__(self,args):
        # Reuse the qualified captured-kernel mappings and interrupt entry/ports.
        Reference.__init__(self,args.snapshot)
        self.hardware=AudioHardware(args.library,self.host,len(self.ram))
        self.hardware.call('pic',self.word(0x8003d88c)&65535,self.word(0x8003d890)&65535)
        self.enable_kernel_segments()
        blob=args.xbe.read_bytes();image=parse_xbe(blob)
        if image['sha256']!=IMAGE:raise ValueError('unreviewed original audio image')
        self.cpu.mem_map(image['base'],(image['image_size']+4095)&~4095)
        self.cpu.mem_write(image['base'],blob[:struct.unpack_from('<I',blob,0x108)[0]])
        for section in image['sections']:
            if section['raw_size']:
                self.cpu.mem_write(section['address'],blob[section['raw_offset']:section['raw_offset']+section['raw_size']])
        thunk=struct.unpack_from('<I',blob,0x158)[0]^0x5b6d40b6;self.ordinals={}
        for index in range(512):
            address=thunk+4*index;ordinal=self.get(address)
            if not ordinal:break
            if ordinal&0xffff0000!=0x80000000:raise ValueError('invalid original import')
            self.ordinals[address]=ordinal&0xffff
            self.put(address,self.base+self.word(self.export_table+4*((ordinal&0xffff)-self.ordinal_base)))
        else:raise ValueError('unterminated original imports')
        self.cpu.mmio_map(APU,0x80000,self.mmio_read,None,self.mmio_write,None)
        self.executed={name:0 for name in ENTRIES};self.accesses=[];self.allocations=[]
        self.notifier_accesses=[];self.connected=False
        for name,address in ENTRIES.items():
            self.cpu.hook_add(UC_HOOK_CODE,self.record_entry,name,address,address)

    def record_entry(self,cpu,address,size,name):self.executed[name]+=1
    def put(self,address,*values):
        self.map_owned(address,4*len(values))
        self.cpu.mem_write(address,struct.pack('<'+'I'*len(values),*values))
    def get(self,address):
        self.map_owned(address,4)
        return struct.unpack('<I',self.cpu.mem_read(address,4))[0]
    def map_owned(self,address,size):
        if address<0x80000000:return
        for page in range(address&~4095,(address+size+4095)&~4095,4096):
            if page not in self.pages and not self.map_page(self.cpu,0,page,4096,0,None):
                raise RuntimeError(self.failure)
    def mmio_read(self,cpu,offset,size,data):
        try:
            if size!=4:raise ValueError('unqualified APU read width')
            value=self.hardware.call('read',APU+offset)
            self.accesses.append(['read',offset,value]);return value
        except Exception as error:
            self.accesses.append(['rejected_read',offset,size]);self.stop(str(error));return 0
    def mmio_write(self,cpu,offset,size,value,data):
        self.accesses.append(['write',offset,value])
        try:self.hardware.call('write',APU+offset,size,value)
        except Exception as error:self.stop(str(error))
    def write(self,offset,value):self.hardware.call('write',APU+offset,4,value)
    def read(self,offset):return self.hardware.call('read',APU+offset)
    def original(self,name,*args,context=OBJECT):
        self.put(STACK,STOP,*args,0xabcdef01)
        saved={reg:0x123400+i for i,reg in enumerate((UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI))}
        for reg,value in saved.items():self.cpu.reg_write(reg,value)
        self.cpu.reg_write(UC_X86_REG_ECX,context);self.cpu.reg_write(UC_X86_REG_ESP,STACK)
        self.cpu.reg_write(UC_X86_REG_EFLAGS,0x202);self.failure=None
        self.cpu.emu_start(ENTRIES[name],STOP,count=1000000)
        self.check_return()
        assert self.cpu.reg_read(UC_X86_REG_ESP)==STACK+4*(1+len(args))
        assert self.get(STACK+4*(1+len(args)))==0xabcdef01
        assert all(self.cpu.reg_read(reg)==value for reg,value in saved.items())
        return self.cpu.reg_read(UC_X86_REG_EAX)
    def check_return(self):
        if self.failure or self.cpu.reg_read(UC_X86_REG_EIP)!=STOP:
            raise RuntimeError(self.failure or f'original call did not return at {self.cpu.reg_read(UC_X86_REG_EIP):08x}')
    def allocate(self,size):
        address=self.call(165,size)
        assert address;self.allocations.append(address);self.map_owned(address,size)
        self.cpu.mem_write(address,bytes(size))
        return address,self.call(173,address)
    def connect(self):
        assert self.call(98,INTERRUPT)&255;self.connected=True
    def disconnect(self):
        assert self.call(100,INTERRUPT)&255;self.connected=False
    def notifier_access(self,cpu,access,address,size,value,user):
        if (address-self.notifier)%16!=15 or size!=1:
            self.stop('notifier payload outside qualified status contract');return
        self.notifier_accesses.append((access,address-self.notifier,size))

    def setup(self,channels):
        self.baseline_statistics=self.statistics()
        self.voice_table,voice_pa=self.allocate(0x8000)
        notify_table,notify_pa=self.allocate(0x5000)
        sge,sge_pa=self.allocate(4096);source,source_pa=self.allocate(4096)
        self.fence,fence_pa=self.allocate(4096)
        self.notifier=notify_table+16*(2+VOICE*4)
        self.cpu.mem_write(self.notifier,b'\xa5'*64)
        self.cpu.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,self.notifier_access,None,self.notifier,self.notifier+63)
        self.put(0x17df28,self.voice_table);self.put(0x17df18,self.fence);self.put(0x17df1c,fence_pa)
        self.put(sge,source_pa,0)
        for frame in range(128):
            self.cpu.mem_write(source+frame*channels*2,struct.pack('<'+'h'*channels,*[4096*(c+1) for c in range(channels)]))
        self.put(OBJECT,0x1cbfec);self.put(OBJECT+8,DEVICE)
        self.put(OBJECT+0x78,SETTINGS);self.put(OBJECT+0x90,SETTINGS)
        self.put(SETTINGS+0xc,1|(channels<<16)|(16<<24));self.put(SETTINGS+0x14,channels*2)
        assert self.original('init',0)==0;assert self.original('filter',0)==0
        self.cpu.mem_write(OBJECT+0xc,struct.pack('<H',VOICE));self.cpu.mem_write(OBJECT+0x12,b'\x01\x00')
        self.put(OBJECT+0x68,self.notifier,4)
        for offset in range(0x728,0x758,8):self.put(DEVICE+offset,DEVICE+offset,DEVICE+offset)
        self.put(DEVICE+0x328+VOICE*4,OBJECT)
        self.write(0x202c,voice_pa);self.write(0x2030,sge_pa);self.write(0x115c,notify_pa)
        for offset in (0x2054,0x2060,0x206c):self.write(offset,0xffff)
        self.write(0x202f8,VOICE);self.write(0x20304,self.get(OBJECT+0x14));self.write(0x203dc,127)
        self.call(107,DEVICE+0x768,ENTRIES['dpc'],DEVICE)
        # Original HalGetInterruptVector and initialization/connection, with
        # the same level-triggered/shared parameters emitted by the real owner.
        vector=self.call(44,5,0x10000400)
        self.vector=vector;irql=self.cpu.mem_read(0x10000400,1)[0]
        assert vector==0x35
        self.call(109,INTERRUPT,ENTRIES['isr'],DEVICE,vector,irql,0,1);self.connect()
        self.write(0x1504,1<<15);self.write(0x2000,8);self.write(0x1004,0x71)
        self.original('start')

    def statistics(self):
        self.put(0x10000500,36,*([0]*8))
        assert self.call(181,0x10000500)==0
        return [self.get(0x10000500+4*n) for n in range(9)]

    def irql(self,value):
        current=self.word(0x8003da60)&255
        thunk=0x189fa8 if value>current else 0x189ef0
        self.cpu.reg_write(UC_X86_REG_ECX,value)
        result=self.call(self.ordinals[thunk])
        assert self.word(0x8003da60)&255==value
        return result

    def deliver(self):
        assert self.hardware.call('vector',0)==self.vector
        registers=(UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI)
        saved={reg:0xabcdef00+i for i,reg in enumerate(registers)}
        for reg,value in saved.items():self.cpu.reg_write(reg,value)
        entry=self.inject(self.vector,STOP,STACK,0x202)
        assert self.hardware.call('vector',1)==self.vector
        self.failure=None;self.cpu.emu_start(entry,STOP,count=1000000);self.check_return()
        assert all(self.cpu.reg_read(reg)==value for reg,value in saved.items())
        assert self.cpu.reg_read(UC_X86_REG_ESP)==STACK and self.cpu.reg_read(UC_X86_REG_EFLAGS)==0x202
        assert self.word(0x8003da60)&255==2

    def qualify(self,channels,scenario,barrier_audit=False):
        self.setup(channels)
        if scenario=='disconnected':self.disconnect()
        if scenario=='masked':self.write(0x1004,0)
        self.irql(2)
        bins=(c.c_float*1024)();count=c.c_uint32();done=c.c_int();total=0
        for quantum in range(32):
            self.hardware.call('process',VOICE,bins,c.byref(count),c.byref(done));total+=count.value
            if scenario=='command-stop':
                assert not done.value and count.value==32
                self.original('stop',0);break
            if done.value:break
        assert (scenario=='command-stop' and total==32) or (done.value and total==128)
        if scenario=='notification-before-frame':
            self.deliver()
            assert self.executed['unlink']==0 and self.executed['dpc']==0
        self.hardware.call('frame',1)
        assert self.executed['dpc']==0 and self.executed['completion']==0
        if scenario in ('masked','disconnected'):
            assert self.hardware.call('vector',0)==-1 and self.executed['isr']==0
            if scenario=='masked':self.write(0x1004,0x71)
            else:self.connect()
        deliveries=1 if scenario=='notification-before-frame' else 0
        while self.hardware.call('vector',0)!=-1:
            deliveries+=1;assert deliveries<=3
            self.deliver()
        assert not self.hardware.call('irq') and self.hardware.call('vector',0)==-1
        assert self.get(DEVICE+0x728)==DEVICE+0x728
        assert self.executed['unlink']==1 and self.executed['dpc']==0 and self.executed['completion']==0
        if scenario=='command-stop':
            assert self.get(DEVICE+0x740)==DEVICE+0x740
            assert int.from_bytes(self.cpu.mem_read(OBJECT+0x12,2),'little')==1
        else:
            assert self.get(DEVICE+0x740)==OBJECT+0x54
            assert self.cpu.mem_read(self.notifier+63,1)==b'\x01'
            assert int.from_bytes(self.cpu.mem_read(OBJECT+0x12,2),'little')==3
        # The original ISR really queued this DPC; a duplicate queue request
        # must be rejected by the original kernel. Do not execute the unknown
        # FE fence by accepting register-file defaults.
        assert self.call(self.ordinals[0x189fc4],DEVICE+0x768,0,0)==0
        pending=dict(flags=int.from_bytes(self.cpu.mem_read(OBJECT+0x12,2),'little'),
                     software_status=self.cpu.mem_read(self.notifier+63,1)[0],
                     source_cursor=self.get(self.voice_table+VOICE*128+0x58)&0xffffff)
        if barrier_audit:self.irql(0) # Deliberately reaches the strict unresolved guard.
        # Explicit fixture teardown cancels real queued work before freeing its
        # inputs. Cancellation does not claim that deferred completion ran.
        assert self.call(self.ordinals[0x189ffc],DEVICE+0x768)&255
        assert not self.call(self.ordinals[0x189ffc],DEVICE+0x768)&255
        self.irql(0)
        self.hardware.call('frame',2)
        assert self.hardware.call('vector',0)==-1 and self.executed['unlink']==1
        self.disconnect()
        self.write(0x1004,0);self.write(0x2000,0)
        for allocation in reversed(self.allocations):self.call(171,allocation)
        assert self.statistics()==self.baseline_statistics
        return dict(channels=channels,scenario=scenario,output_frames_before_irq=total,deliveries=deliveries,
                    executed=self.executed,pending=pending,original_dpc_cancelled_for_teardown=True,
                    allocation_statistics_restored=True,notifier_accesses=self.notifier_accesses)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','snapshot','library','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--barrier-audit',action='store_true')
    args=parser.parse_args()
    result=dict(xbe_sha256=IMAGE,snapshot_sha256=SNAPSHOT_SHA,
        library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest(),cases=[],error=None,
        scope='Original IRQ5/PIC/kernel gate/ISR/IRET and real DPC queue ownership; preowned ordinary buffer; DPC completion remains guarded by unknown FE fence/control; no full audio backend or title claim')
    scenarios=('combined','notification-before-frame','masked','disconnected','command-stop')
    configurations=[(channels,scenario) for channels in (1,2) for scenario in scenarios for _ in range(3)]
    if args.barrier_audit:configurations=configurations[:1]
    for channels,scenario in configurations:
        ref=None
        try:
            ref=AudioReference(args);result['cases'].append(ref.qualify(channels,scenario,args.barrier_audit))
        except Exception as error:
            result.update(error=f'{type(error).__name__}: {error}',traceback=traceback.format_exc())
            if ref is not None:result['stopped_pc']=hex(ref.cpu.reg_read(UC_X86_REG_EIP))
        finally:
            if ref is not None:
                result['last_mmio']=ref.accesses[-40:];result['last_executed']=ref.executed
                ref.hardware.close()
            ref=None;gc.collect()
        if result['error']:break
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(error=result['error'],cases=len(result['cases']))))
    if result['error']:print(result['traceback'])
    return int(result['error'] is not None)


if __name__=='__main__':raise SystemExit(main())
