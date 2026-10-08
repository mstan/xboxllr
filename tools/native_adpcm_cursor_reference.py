#!/usr/bin/env python3
"""Execute complete original MCPX byte/sample conversion helpers unhooked.

This is the buffer-unit contract, not a full original driver/audio-device run.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from aot_inventory import parse_xbe
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP

def run(path):
    blob=path.read_bytes();digest=hashlib.sha256(blob).hexdigest()
    assert digest=='e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000,0x1000000);u.mem_map(0x30000000,0x10000)
    for s in parse_xbe(blob)['sections']:
        if s['raw_size'] and s['address']+s['raw_size']<=0x1010000:
            u.mem_write(s['address'],blob[s['raw_offset']:s['raw_offset']+s['raw_size']])
    def put(a,v):u.mem_write(a,struct.pack('<I',v))
    obj,fmt,stack,end=0x30000000,0x30001000,0x30008000,0x30009000
    put(obj+0x78,fmt);rows=[]
    def call(address,value):
        put(stack,end);put(stack+4,value);put(stack+8,0xaabbccdd)
        u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,obj)
        u.emu_start(address,end,count=100)
        assert u.reg_read(UC_X86_REG_EIP)==end and u.reg_read(UC_X86_REG_ESP)==stack+8
        assert bytes(u.mem_read(stack+8,4))==b'\xdd\xcc\xbb\xaa'
        return u.reg_read(UC_X86_REG_EAX)
    for channels in (1,2):
        block=36*channels;put(fmt+0xc,0x69);put(fmt+0x14,block)
        for frames in (0,1,31,32,63,64,65,127,128,1024):
            encoded=call(0x1786e4,frames);assert encoded==frames//64*block
            rows.append(dict(channels=channels,frames=frames,encoded_bytes=encoded))
        for encoded in (0,1,block-1,block,block+1,block*2-1,block*2):
            assert call(0x179e2c,encoded)==encoded//block*64
        put(fmt+0xc,1);put(fmt+0x14,2*channels)
        assert call(0x1786e4,65)==65*2*channels and call(0x179e2c,65*2*channels)==65
    return dict(xbe_sha256=digest,cases=rows,scope='complete original conversion helpers; ADPCM quantizes to 64-frame blocks')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--xbe',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.write_text(json.dumps(run(a.xbe),indent=2)+'\n')
    print('PASS: original ADPCM byte/sample block conversion, truncation, PCM control and stack ABI')
