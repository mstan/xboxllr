#!/usr/bin/env python3
"""Execute the complete original NTSC LogicOp setter with a real packet buffer."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from aot_inventory import parse_xbe
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_ESI

def run(path):
    blob=path.read_bytes();digest=hashlib.sha256(blob).hexdigest()
    assert digest=='e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000,0x1000000)
    for s in parse_xbe(blob)['sections']:
        if s['raw_size'] and s['address']+s['raw_size']<=0x1010000:
            u.mem_write(s['address'],blob[s['raw_offset']:s['raw_offset']+s['raw_size']])
    u.mem_map(0x30000000,0x10000)
    device,ring,stack,end=0x30000000,0x30001000,0x30008000,0x30009000
    write=lambda address,value:u.mem_write(address,struct.pack('<I',value))
    read=lambda address:struct.unpack('<I',u.mem_read(address,4))[0]
    write(0x141b78,device);write(device+4,ring+256)
    rows=[]
    for value in (0x1503,0,0x1503,0x1506,0):
        write(device,ring);u.mem_write(ring-4,b'\xcd'*24)
        write(0x141fdc,0xaabbccdd);write(0x141fe4,0xaabbccdd)
        write(stack,end);write(stack+4,value)
        u.reg_write(UC_X86_REG_ESI,0x12345678);u.reg_write(UC_X86_REG_ESP,stack)
        u.emu_start(0x1337c0,end,count=100)
        expected=[0x817bc,1,value] if value else [0x417bc,0]
        assert u.reg_read(UC_X86_REG_EIP)==end and u.reg_read(UC_X86_REG_ESP)==stack+8
        assert u.reg_read(UC_X86_REG_ESI)==0x12345678
        assert read(device)==ring+4*len(expected)
        assert [read(ring+4*i) for i in range(len(expected))]==expected
        assert read(ring-4)==0xcdcdcdcd and read(ring+4*len(expected))==0xcdcdcdcd
        assert read(0x141fe0)==value and read(0x141fdc)==read(0x141fe4)==0xaabbccdd
        rows.append(dict(argument=value,packet=expected,cache=value))
    return dict(xbe_sha256=digest,cases=rows,scope='complete setter with available FIFO space; pixel behavior checked separately')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--xbe',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();a.output.write_text(json.dumps(run(a.xbe),indent=2)+'\n')
    print('PASS: original logic-op packets, enable/disable, cache, stdcall and output extents')
