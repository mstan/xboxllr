#!/usr/bin/env python3
"""Execute the original viewport packet emitter; no calls are substituted."""
import argparse, hashlib, json, struct
from pathlib import Path
from aot_inventory import parse_xbe
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP


def run(path):
    blob = path.read_bytes(); digest = hashlib.sha256(blob).hexdigest()
    assert digest == 'e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
    u = Uc(UC_ARCH_X86, UC_MODE_32); u.mem_map(0, 0x4000000); u.mem_map(0x30000000, 0x10000)
    for s in parse_xbe(blob)['sections']:
        if s['preload']: u.mem_write(s['address'], blob[s['raw_offset']:s['raw_offset']+s['raw_size']])
    def put(a, *v): u.mem_write(a, struct.pack('<'+'I'*len(v), *v))
    def floats(a, *v): u.mem_write(a, struct.pack('<'+'f'*len(v), *v))
    device, shader, packet, stack, end = 0x30000000, 0x30004000, 0x30005000, 0x30008000, 0x30009000
    put(0x141b78, device); put(device+0x380, shader); put(shader+4, 0x10)
    put(0x141fc4, 1); floats(device+0x510, 16777215); floats(device+0x518, 1, 1)
    floats(device+0xaa8, .53125, .53125)
    rows = []
    for vp in [(0,0,640,480,0,1), (2,3,48,40,.25,.75)]:
        u.mem_write(device+0xa90, struct.pack('<4I2f', *vp))
        for reserved in (True,False):
            put(device+8, 2 if reserved else 0x202)
            u.mem_write(packet, b'\xcc'*256); put(stack,end)
            u.reg_write(UC_X86_REG_ESP, stack); u.reg_write(UC_X86_REG_ECX, device); u.reg_write(UC_X86_REG_EDX, packet)
            u.emu_start(0x133cd0,end,count=2000)
            assert u.reg_read(UC_X86_REG_EIP)==end and u.reg_read(UC_X86_REG_ESP)==stack+4
            length=u.reg_read(UC_X86_REG_EAX)-packet
            assert length==(52 if reserved else 12)
            if reserved:
                commands=bytes(u.mem_read(packet,40))
                assert struct.unpack_from('<I',commands)[0]==0x100a20
                assert struct.unpack_from('<I',commands,20)[0]==0x100af0
                offset=struct.unpack_from('<4f',commands,4);scale=struct.unpack_from('<4f',commands,24)
                x,y,w,h,lo,hi=vp
                assert offset==(x+w/2+.53125,y+h/2+.53125,lo*16777215,0)
                assert scale==(w/2,-h/2,(hi-lo)*16777215,0)
                rows.append(dict(viewport=vp,constant58=scale,constant59=offset))
            assert bytes(u.mem_read(packet+length,4))==b'\xcc'*4
    return dict(xbe_sha256=digest,cases=rows,scope='complete original programmable viewport emitter and no-reserved branch; NV097 viewport methods update constants58/59 in existing LLE')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--xbe',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.write_text(json.dumps(run(a.xbe),indent=2)+'\n')
    print('PASS: original viewport packets, reserved/no-reserved behavior, pixel bias, depth and ABI')
