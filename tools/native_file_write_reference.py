#!/usr/bin/env python3
"""Run original NTSC XAPI WriteFile up to its kernel submission boundary.

No kernel callee is replaced and no completion is invented. This fixture proves
argument/OVERLAPPED setup only, not the complete retail filesystem or IRP path.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from aot_inventory import parse_xbe
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP

def run(path):
    blob=path.read_bytes();digest=hashlib.sha256(blob).hexdigest()
    assert digest=='e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000,0x1000000);u.mem_map(0x30000000,0x10000)
    for s in parse_xbe(blob)['sections']:
        if s['raw_size'] and s['address']+s['raw_size']<=0x1010000:
            u.mem_write(s['address'],blob[s['raw_offset']:s['raw_offset']+s['raw_size']])
    def put(at,values):u.mem_write(at,struct.pack('<'+'I'*len(values),*values))
    def get(at,n):return list(struct.unpack('<'+'I'*n,bytes(u.mem_read(at,n*4))))
    stack,overlap,buffer,written=0x30008000,0x30000100,0x30001000,0x30000080
    rows=[]
    for length in (44,6000):
        for event in (0,0x30000004):
            put(overlap,[0xcccccccc,0xaabbccdd,7003,0,event]);put(written,[0xcccccccc])
            put(stack,[0x30009000,0x50000001,buffer,length,written,overlap])
            u.reg_write(UC_X86_REG_ESP,stack)
            # Stop before the original indirect CALL instruction, not after a
            # fabricated syscall return. The eight NT arguments are now live.
            u.emu_start(0xf38b3,0xf3902,count=1000)
            assert u.reg_read(UC_X86_REG_EIP)==0xf3902
            args=get(u.reg_read(UC_X86_REG_ESP),8)
            assert args[:7]==[0x50000001,event,0,overlap,overlap,buffer,length]
            assert get(args[7],2)==[7003,0] and get(written,1)==[0]
            assert get(overlap,5)==[0x103,0xaabbccdd,7003,0,event]
            rows.append(dict(length=length,event=event,offset=7003,iosb_pending=True))
    return dict(xbe_sha256=digest,cases=rows,scope='original XAPI submission ABI only; no kernel completion substitution')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--xbe',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.write_text(json.dumps(run(a.xbe),indent=2)+'\n')
    print('PASS: original XAPI byte-length/offset, optional event and OVERLAPPED submission ABI')
