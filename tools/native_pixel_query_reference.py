#!/usr/bin/env python3
"""Execute the original NTSC bound-pixel-shader query, without hooks."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from aot_inventory import parse_xbe
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP

def run(path):
    blob=path.read_bytes();digest=hashlib.sha256(blob).hexdigest()
    assert digest=='e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000,0x1000000)
    for s in parse_xbe(blob)['sections']:
        if s['raw_size'] and s['address']+s['raw_size']<=0x1010000:
            u.mem_write(s['address'],blob[s['raw_offset']:s['raw_offset']+s['raw_size']])
    u.mem_map(0x30000000,0x10000)
    device,stack,output,end,shader=0x30000000,0x30008000,0x30007001,0x30009000,0x30006000
    write=lambda address,value:u.mem_write(address,struct.pack('<I',value))
    write(0x141b78,device)
    u.mem_write(shader,bytes(range(252)));shader_before=bytes(u.mem_read(shader,252))
    rows=[]
    for handle in (shader,0,shader):
        write(device+0x370,handle)
        write(stack,end);write(stack+4,output)
        u.mem_write(output-4,b'\xcd'*12)
        before=bytes(u.mem_read(device,0x1000))
        u.reg_write(UC_X86_REG_ESP,stack);u.emu_start(0x136250,end,count=20)
        assert u.reg_read(UC_X86_REG_EIP)==end and u.reg_read(UC_X86_REG_ESP)==stack+8
        assert bytes(u.mem_read(output-4,12))==b'\xcd'*4+struct.pack('<I',handle)+b'\xcd'*4
        assert bytes(u.mem_read(device,0x1000))==before
        assert bytes(u.mem_read(shader,252))==shader_before
        rows.append(dict(bound_handle=handle,output=handle,stack_bytes=4,shader_unchanged=True))
    return dict(xbe_sha256=digest,query='D3DDevice_GetPixelShader',cases=rows)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--xbe',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();a.output.write_text(json.dumps(run(a.xbe),indent=2)+'\n')
    print('PASS: original pixel shader query, output extent, stdcall and borrowed lifetime')
