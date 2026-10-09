#!/usr/bin/env python3
"""Qualify original DXT1-noise setter/service transport over maintained NV2A.

The preowned graphics device/pushbuffer bounds this to control delivery. Enabled
noise pixels, original allocation/startup and kernel ISR/DPC delivery are separate
gates. Original setter and complete graphics service execute without substituted
returns, instruction hooks or transport acknowledgements.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path

from native_vertex_fetch_reference import Reference, XBE_SHA, OBJECT, FRAME
from unicorn.x86_const import UC_X86_REG_ECX


def run(args):
    rows=[]
    for lifetime in range(8):
        ref=Reference(args)
        # Original setter conditions the effective enable on target format.
        # Public LIN_A8R8G8B8 (12) enables it; LIN_R5G6B5 (11) does not. This
        # is preowned target metadata, not a replacement allocation routine.
        target_format=(0x12,0x11)[lifetime%2]
        ref.put(OBJECT+0x1c00,0x01050001,FRAME,0,
            (target_format<<8)|0x10029,0x0303f03f)  # 64x64, 256-byte linear pitch
        ref.put(ref.device+0x21b4,OBJECT+0x1c00)
        read=ref.lib.xlle_nv2a_reference_read
        read.restype=ctypes.c_uint32
        read.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_int]
        accesses=[]
        def mmio_read(cpu,offset,size,data):
            value=read(ref.gpu,0xfd000000+offset,size)
            accesses.append(['read',hex(offset),size,hex(value)])
            if ref.lib.xlle_nv2a_reference_error(ref.gpu):cpu.emu_stop()
            return value
        def mmio_write(cpu,offset,size,value,data):
            accesses.append(['write',hex(offset),size,hex(value)])
            ref.lib.xlle_nv2a_reference_write(ref.gpu,0xfd000000+offset,size,value)
            if ref.lib.xlle_nv2a_reference_error(ref.gpu):cpu.emu_stop()
        ref.cpu.mmio_map(0xfd000000,0x1000000,mmio_read,None,mmio_write,None)
        ref.calls['original_graphics_service']=0x13b950
        ref.put(ref.device+0x23c0,0xfd000000)
        ref.write(0x140,1);ref.write(0x400140,0x100000)
        def observe_controls():
            values=[]
            for index in (0xe00050,0xdf0008):
                ref.write(0x400750,index)
                values.append(read(ref.gpu,0xfd400754,4));ref.check()
                assert read(ref.gpu,0xfd400750,4)==index+4
            return values
        try:
            previous=0
            assert ref.get(0x141d88+159*4)[0]==previous
            for value in (0,1,1,0,0,1,0,1,0,0,1,1,0,1,1,0):
                effective=value if target_format==0x12 else 0
                begin=ref.get(ref.device)[0]
                ref.call('D3DDevice_SetRenderStateNotInline',[159,value])
                end=ref.get(ref.device)[0]
                packets=ref.get(begin,(end-begin)//4)
                assert packets==([] if effective==previous else
                    [0x40110,0,0x40100,(effective<<5)|8]),packets
                assert ref.get(0x141d88+159*4)[0]==value
                assert ref.get(ref.device+8)[0]&1==effective
                # Caller emission alone must not publish the new GPU state.
                assert observe_controls()==[previous,previous]
                if not packets:
                    assert read(ref.gpu,0xfd400100,4)==0
                    rows.append(dict(lifetime=lifetime,target_format=target_format,
                        value=value,effective=effective,previous=previous,
                        packets=[],original_service_mmio=[],controls=[effective,effective],
                        command_omitted=True,fifo_retired=True))
                    continue
                ref.kick()
                assert read(ref.gpu,0xfd400100,4)==0x100000
                assert read(ref.gpu,0xfd400708,4)==(effective<<5)|8
                assert observe_controls()==[previous,previous]
                start=len(accesses)
                try:
                    ref.call('original_graphics_service',[],
                        ((UC_X86_REG_ECX,ref.device+0x23c0),))
                except AssertionError:
                    ref.check()
                    raise
                ref.check()
                service=accesses[start:]
                writes=[item for item in service if item[0]=='write' and
                    item[1] in ('0x400750','0x400754')]
                assert writes==[
                    ['write','0x400750',4,'0xe00050'],['write','0x400754',4,hex(effective)],
                    ['write','0x400750',4,'0xdf0008'],['write','0x400754',4,hex(effective)]],writes
                assert read(ref.gpu,0xfd400100,4)==0
                assert read(ref.gpu,0xfd800044,4)==end
                assert observe_controls()==[effective,effective]
                rows.append(dict(lifetime=lifetime,target_format=target_format,
                    value=value,effective=effective,previous=previous,
                    packets=[hex(word) for word in packets],original_service_mmio=service,
                    controls=[effective,effective],command_omitted=False,fifo_retired=True))
                previous=effective
        finally:ref.close()
    return dict(xbe_sha256=XBE_SHA,cases=rows,
        scope='original boolean setter and complete graphics service/control transport; enabled noise pixels remain unqualified')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','binding','library','dependencies','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    dependency_handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    result=run(args)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    services=sum(not row['command_omitted'] for row in result['cases'])
    print(f"PASS: {len(result['cases'])} original DXT1-noise setter calls, "
        f"{services} complete graphics services across eight device lifetimes")
