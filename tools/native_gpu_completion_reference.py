#!/usr/bin/env python3
"""Execute original XDK query/callback commands through the maintained NV2A.

Private owned NTSC inputs only. Preowned device/report/pushbuffer storage bounds
this fixture to the original fast paths; no allocator or kick is substituted.
This fixture is not the still-required selectable title integration route.
"""
import argparse
import ctypes
import gc
import hashlib
import json
import os
from pathlib import Path
import struct
import traceback

from aot_inventory import parse_xbe
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_MEM
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_ALL
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP)

XBE_SHA = 'e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
GPU = 0xfd000000
DEVICE, PUSH, REPORT = 0x02000000, 0x02100000, 0x00800000
STACK, STOP, OUT = 0x70008000, 0x70009000, 0x70002000


class Reference:
    def __init__(self, args):
        blob=args.xbe.read_bytes()
        assert hashlib.sha256(blob).hexdigest()==XBE_SHA, 'unreviewed NTSC input'
        self.symbols={item['name']:item['address'] for item in json.loads(args.symbols.read_text())['symbols']}
        self.functions={int(item['start'],16):item for item in json.loads(args.functions.read_text())}
        self.ram=ctypes.create_string_buffer(64*1024*1024)
        self.cpu=Uc(UC_ARCH_X86,UC_MODE_32)
        for address in (0,0x80000000):
            self.cpu.mem_map_ptr(address,len(self.ram),UC_PROT_ALL,ctypes.addressof(self.ram))
        self.cpu.mem_map(0x70000000,0x10000)
        for section in parse_xbe(blob)['sections']:
            if section['preload'] and section['raw_size']:
                self.cpu.mem_write(section['address'],blob[section['raw_offset']:section['raw_offset']+section['raw_size']])
        self.lib=ctypes.CDLL(str(args.library.resolve()))
        for name,result,params in (
            ('create',ctypes.c_void_p,[]),('destroy',None,[ctypes.c_void_p]),
            ('memory',None,[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint32]),
            ('read',ctypes.c_uint32,[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_int]),
            ('write',None,[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32]),
            ('error',ctypes.c_char_p,[ctypes.c_void_p]),('irq',ctypes.c_int,[ctypes.c_void_p]),
            ('frame',None,[ctypes.c_void_p,ctypes.c_uint64])):
            fn=getattr(self.lib,'xlle_nv2a_reference_'+name);fn.restype=result;fn.argtypes=params
        self.gpu=self.lib.xlle_nv2a_reference_create()
        assert self.gpu, 'NV2A allocation failed'
        self.lib.xlle_nv2a_reference_memory(self.gpu,ctypes.addressof(self.ram),len(self.ram))
        self.failure=None;self.mmio=[];self.last_call=None
        self.cpu.mmio_map(GPU,0x1000000,self.read_mmio,None,self.write_mmio,None)
        self.dis=Cs(CS_ARCH_X86,CS_MODE_32);self.dis.detail=True
        end=self.instructions(self.symbols['D3DDevice_EndVisibilityTest'])
        self.kick=int([i.op_str for i in end if i.mnemonic=='call'][-1],0)
        begin=self.instructions(self.symbols['D3DDevice_BeginVisibilityTest'])
        globals_=[op.mem.disp for i in begin for op in i.operands
                  if op.type==X86_OP_MEM and not op.mem.base and not op.mem.index]
        assert self.symbols['D3D_g_pDevice'] in globals_
        self.put(self.symbols['D3D_g_pDevice'],DEVICE)
        self.put(DEVICE,PUSH,PUSH+0x10000,0)
        # Reviewed private layout for this hash-bound XDK revision only.
        self.put(DEVICE+0x23bc,GPU+0x800000,GPU)
        for page in range(3):self.put(DEVICE+0x39c+page*4,0x80000000+REPORT+page*4096)
        self.configure()

    def instructions(self,address):
        size=self.functions[address]['size']
        return list(self.dis.disasm(bytes(self.cpu.mem_read(address,size)),address))

    def check(self):
        error=self.lib.xlle_nv2a_reference_error(self.gpu)
        if error:raise RuntimeError(error.decode())
        if self.failure:raise RuntimeError(self.failure)

    def read_mmio(self,cpu,offset,size,data):
        value=self.lib.xlle_nv2a_reference_read(self.gpu,GPU+offset,size)
        self.mmio.append(['read',offset,size,value])
        error=self.lib.xlle_nv2a_reference_error(self.gpu)
        if error:self.failure=error.decode();cpu.emu_stop()
        return value

    def write_mmio(self,cpu,offset,size,value,data):
        self.mmio.append(['write',offset,size,value])
        self.lib.xlle_nv2a_reference_write(self.gpu,GPU+offset,size,value)
        error=self.lib.xlle_nv2a_reference_error(self.gpu)
        if error:self.failure=error.decode();cpu.emu_stop()

    def write(self,offset,value):
        self.lib.xlle_nv2a_reference_write(self.gpu,GPU+offset,4,value);self.check()

    def read(self,offset):
        value=self.lib.xlle_nv2a_reference_read(self.gpu,GPU+offset,4);self.check();return value

    def put(self,address,*values):
        self.cpu.mem_write(address,struct.pack('<'+'I'*len(values),*[value&0xffffffff for value in values]))

    def get(self,address,count=1):
        return list(struct.unpack('<'+'I'*count,self.cpu.mem_read(address,count*4)))

    def emit(self,method,*values):
        cursor=self.get(DEVICE)[0]
        assert cursor+4+4*len(values)<PUSH+0x10000,'fixture pushbuffer exhausted'
        self.put(cursor,(len(values)<<18)|method,*values)
        self.put(DEVICE,cursor+4+4*len(values))

    def call(self,name,args=()):
        address=self.kick if name=='kick' else self.symbols[name];self.last_call=name
        self.put(STACK,STOP,*args);self.cpu.reg_write(UC_X86_REG_ESP,STACK)
        saved=((UC_X86_REG_EBX,123),(UC_X86_REG_ESI,456),(UC_X86_REG_EDI,789),(UC_X86_REG_EBP,876))
        for reg,value in saved:self.cpu.reg_write(reg,value)
        self.cpu.reg_write(UC_X86_REG_ECX,DEVICE)
        self.cpu.emu_start(address,STOP,count=20000);self.check()
        assert self.cpu.reg_read(UC_X86_REG_EIP)==STOP, f'{name} did not return'
        assert self.cpu.reg_read(UC_X86_REG_ESP)==STACK+4+len(args)*4, name
        assert all(self.cpu.reg_read(reg)==value for reg,value in saved),name
        return self.cpu.reg_read(UC_X86_REG_EAX)

    def configure(self):
        for offset,value in ((0x700008,1),(0x70000c,0x80010020),(0x700200,0x97),
            (0x700010,2),(0x700014,0x80010030),(0x700300,0x3d),(0x700304,len(self.ram)-1),(0x700308,0),
            (0x700180,0x3d),(0x700184,len(self.ram)-1),(0x700188,0),
            (0x002504,1),(0x003204,0x100),(0x00322c,0x18),
            (0x003240,PUSH),(0x003244,PUSH),(0x003220,1),(0x003250,1),(0x003200,1),
            (0x000140,1),(0x400140,1<<20)):
            self.write(offset,value)
        for method,value in ((0,1),(0x0194,2),(0x0198,2),(0x01a8,2),
            (0x0294,0x00020001),(0x1e94,0),(0x0200,64<<16),(0x0204,64<<16),
            (0x0208,0x128),(0x020c,0x01000100),(0x0210,0x600000),(0x0214,0x604000),
            (0x0290,0x00100001),(0x02c0,63<<16),(0x02e0,63<<16),
            (0x033c,0x207),(0x0350,0x8006),(0x0354,0x201),(0x0358,0x01010101),
            (0x035c,1),(0x030c,1),(0x037c,0x1d01),(0x038c,0x1b02),(0x0390,0x1b02),
            (0x0394,0),(0x0398,0x4b7fffff),(0x039c,0x405),(0x03a0,0x900),(0x1d78,1),
            (0x1e78,0x00210000),(0x1e60,1),(0x0ac0,0x04200000),(0x0260,0x14200000),
            (0x1e40,0xc00),(0x0aa0,0xc00),(0x0288,0xc),(0x028c,0x1c80)):
            self.emit(method,value)
        for i in range(16):self.emit(0x0680+i*4,0x3f800000 if i%5==0 else 0)
        self.cpu.mem_write(0x604000,b'\xff'*(64*64*4))
        self.call('kick');self.lib.xlle_nv2a_reference_frame(self.gpu,120)

    def quad(self,left,top,right,bottom,z):
        bits=lambda value:struct.unpack('<I',struct.pack('<f',value))[0]
        self.emit(0x17fc,5)
        for x,y in ((left,top),(right,top),(left,bottom),(right,top),(right,bottom),(left,bottom)):
            self.emit(0x1518,bits(x),bits(y),bits(z));self.emit(0x194c,0xffffffff);self.emit(0x1524,bits(1))
        self.emit(0x17fc,0)

    def qualify(self,rows):
        for index,geometry,expected in ((0,(0,0,20,20,50),400),
                (257,(0,0,20,20,60),0),(655,(10,0,30,20,60),200),
                (257,None,0)):
            self.call('D3DDevice_BeginVisibilityTest')
            if geometry:self.quad(*geometry)
            # The actual puller gate holds the queued draw/report. No result is
            # injected and no original function is bypassed to create pending.
            self.write(0x400720,0)
            assert self.call('D3DDevice_EndVisibilityTest',[index])==0
            slot=REPORT+(index>>8)*4096+(index&255)*16
            assert self.get(slot+12)==[0xffffffff]
            self.put(OUT,0xcccccccc,0xdddddddd,0xeeeeeeee)
            assert self.call('D3DDevice_GetVisibilityTestResult',[index,OUT,OUT+4])==0x88760828
            assert self.get(OUT,3)==[0xcccccccc,0xdddddddd,0xeeeeeeee]
            self.write(0x400720,1)
            assert self.get(slot,4)==[2000000000,0,expected,0]
            for optional in (True,False,True):
                self.put(OUT,0xcccccccc,0xdddddddd,0xeeeeeeee)
                assert self.call('D3DDevice_GetVisibilityTestResult',[index,OUT,OUT+4 if optional else 0])==0
                assert self.get(OUT,3)==[expected,2000000000 if optional else 0xdddddddd,0 if optional else 0xeeeeeeee]
            rows.append(dict(case='complete-original-visibility',index=index,samples=expected,
                             report=self.get(slot,4),pending_outputs_preserved=True,repeated_reads=3))
        for kind in (0,1):self.call('D3DDevice_InsertCallback',[kind,0x70003000+kind*16,0x12345678+kind])
        self.emit(0x1d90,0xdeadbeef);end=self.get(DEVICE)[0];self.call('kick')
        for kind in (0,1):
            assert self.lib.xlle_nv2a_reference_irq(self.gpu)
            assert self.read(0x400708)==6+kind
            assert self.read(0x401a88)==0x70003000+kind*16
            assert self.read(0x40186c)==0x12345678+kind
            # PFIFO may prefetch through PUT while the PGRAPH puller is held.
            # PARAMETER_B proves the later marker has not executed at the trap.
            rows.append(dict(case='original-callback-trap',type=kind,routine=self.read(0x401a88),
                             context=self.read(0x40186c),dma_get=self.read(0x800044)))
            self.write(0x400100,1<<20)
        assert not self.lib.xlle_nv2a_reference_irq(self.gpu)
        assert self.read(0x40186c)==0xdeadbeef and self.read(0x800044)==end
        # Original fences publish real semaphore DMA; a paused hardware puller
        # keeps BlockOnFence in original code until that work can execute.
        semaphore=REPORT+0x3000
        self.put(DEVICE+0x30,2,0x80000000+semaphore)
        self.emit(0x01a4,2);self.emit(0x1d6c,semaphore);self.call('kick')
        for cycle in range(3):
            before=self.get(semaphore)[0];self.write(0x400720,0)
            token=self.call('D3DDevice_InsertFence')
            assert token==2+cycle*2 and self.get(semaphore)==[before]
            self.put(STACK,STOP,token);self.cpu.reg_write(UC_X86_REG_ESP,STACK)
            saved={reg:self.cpu.reg_read(reg) for reg in (UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP)}
            self.last_call='D3DDevice_BlockOnFence-pending'
            self.cpu.emu_start(self.symbols['D3DDevice_BlockOnFence'],STOP,count=1000);self.check()
            pending_pc=self.cpu.reg_read(UC_X86_REG_EIP)
            assert pending_pc!=STOP and self.get(semaphore)==[before]
            self.write(0x400720,1);assert self.get(semaphore)==[token]
            self.cpu.emu_start(pending_pc,STOP,count=20000);self.check()
            assert self.cpu.reg_read(UC_X86_REG_EIP)==STOP and self.cpu.reg_read(UC_X86_REG_ESP)==STACK+8
            assert all(self.cpu.reg_read(reg)==value for reg,value in saved.items())
            self.call('D3DDevice_BlockOnFence',[token])
            rows.append(dict(case='original-fence-wait',token=token,pending_pc=pending_pc,
                             semaphore=self.get(semaphore)[0],completed_repeat=True))
        # Pixel evidence accompanies nonzero sample counts; the real rasterizer
        # changed color and depth storage, not merely a query bookkeeping field.
        assert any(self.cpu.mem_read(0x600000,64*64*4))
        assert bytes(self.cpu.mem_read(0x604000,64*64*4))!=b'\xff'*(64*64*4)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','symbols','functions','library','dependencies','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    result=dict(xbe_sha256=XBE_SHA,library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest(),
                scope='original fast paths + real PFIFO/software raster/report/trap; not title integration',cases=[],error=None)
    with os.add_dll_directory(str(args.dependencies.resolve())):
        result['lifetimes']=[]
        for lifetime in range(3):
            ref=None;rows=[];result['lifetimes'].append(rows)
            try:
                ref=Reference(args);ref.qualify(rows)
            except Exception as exc:
                result.update(error=f'{type(exc).__name__}: {exc}',traceback=traceback.format_exc())
            finally:
                if ref:
                    names=['D3DDevice_BeginVisibilityTest','D3DDevice_EndVisibilityTest',
                           'D3DDevice_GetVisibilityTestResult','D3DDevice_InsertCallback',
                           'D3DDevice_InsertFence','D3DDevice_BlockOnFence']
                    functions={name:ref.symbols[name] for name in names};functions['kick']=ref.kick
                    result.update(last_call=ref.last_call,mmio=ref.mmio,functions={name:dict(address=address,
                        sha256=hashlib.sha256(bytes(ref.cpu.mem_read(address,ref.functions[address]['size']))).hexdigest())
                        for name,address in functions.items()})
                    ref.lib.xlle_nv2a_reference_destroy(ref.gpu)
                ref=None;gc.collect()
            if result['error']:break
        # Keep one set of distinct cases; lifetimes retain every repetition.
        result['cases']=result['lifetimes'][0]
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({key:result.get(key) for key in ('error','last_call')}))
    return int(result['error'] is not None)


if __name__=='__main__':
    raise SystemExit(main())
