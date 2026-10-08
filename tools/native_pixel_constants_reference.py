#!/usr/bin/env python3
"""Run the complete original constant setter/getter with private XDK input.

The original owns quantization, stage mapping and emitted NV097 packets.
This fixture maps memory/FIFO space; it hooks no guest function bodies.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from aot_inventory import parse_xbe
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EIP,UC_X86_REG_ESP,UC_X86_REG_MXCSR

def run(path):
    blob=path.read_bytes();digest=hashlib.sha256(blob).hexdigest()
    assert digest=='e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
    u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000,0x1000000)
    for s in parse_xbe(blob)['sections']:
        if s['raw_size'] and s['address']+s['raw_size']<=0x1010000:
            u.mem_write(s['address'],blob[s['raw_offset']:s['raw_offset']+s['raw_size']])
    u.mem_map(0x30000000,0x10000)
    device,ring,shader,definition,inputs,output,stack,end=[0x30000000+n for n in (0,0x2000,0x3000,0x3100,0x4000,0x5000,0x8000,0x9000)]
    write=lambda a,v:u.mem_write(a,struct.pack('<I',v))
    read=lambda a:struct.unpack('<I',u.mem_read(a,4))[0]
    def call(address,args):
        write(stack,end)
        for i,value in enumerate(args):write(stack+4+i*4,value)
        u.reg_write(UC_X86_REG_ESP,stack);u.emu_start(address,end,count=20000)
        assert u.reg_read(UC_X86_REG_EIP)==end and u.reg_read(UC_X86_REG_ESP)==stack+4+4*len(args)
    write(0x141b78,device);write(device+0x370,shader);write(shader+8,definition)
    write(device+4,ring+0x1000);u.reg_write(UC_X86_REG_MXCSR,0x1f80)
    rows=[]
    values=[(.25,.5,.75,1),(-2,2,0,.5),(.3,.1,.9,.7),(.8,.4,.2,.6)]
    for first,count,m0,m1,mf in [(0,4,0x32103210,0x10101010,0x10),(7,2,0x77778888,0x88887777,0x87),(0,0,0,0,0)]:
        write(device,ring);write(definition+228,m0);write(definition+232,m1);write(definition+236,mf)
        u.mem_write(0x141d88,b'\xcd'*(166*4));u.mem_write(device+0x4a8,b'\xcd'*72)
        data=struct.pack('<16f',*(v for row in values for v in row));u.mem_write(inputs,data)
        call(0x1367e0,[first,inputs,count])
        expected_packets=[];packed=[]
        for i in range(count):
            channels=[round(max(0,min(1,v))*255) for v in values[i]]
            # FLOAT multiply before conversion; half-way rounding tolerance below.
            observed=read(device+0x4ac+(first+i)*4)
            for shift,channel in zip((16,8,0,24),channels):assert abs(((observed>>shift)&255)-channel)<=1
            packed.append(observed)
            for bank,mapping in enumerate((m0,m1)):
                for stage in range(8):
                    if ((mapping>>(stage*4))&15)==first+i:
                        assert read(0x141d88+(10+bank*8+stage)*4)==observed
                        expected_packets.extend((0x40a60+bank*32+stage*4,observed))
            for bank in range(2):
                if ((mf>>(bank*4))&15)==first+i:
                    assert read(0x141d88+(43+bank)*4)==observed
                    expected_packets.extend((0x41e20+bank*4,observed))
            if first+i==0:expected_packets.extend((0xc181c,*struct.unpack('<3I',data[:12])))
        assert read(device)==ring+len(expected_packets)*4
        if expected_packets:
            actual=list(struct.unpack('<'+'I'*len(expected_packets),u.mem_read(ring,len(expected_packets)*4)))
            assert actual==expected_packets
        assert read(device+0x4a8)==read(device+0x4ec)==0xcdcdcdcd
        u.mem_write(output-4,b'\xcd'*(count*16+8));call(0x136270,[first,output,count])
        floats=list(struct.unpack('<'+'f'*(count*4),u.mem_read(output,count*16))) if count else []
        for i,p in enumerate(packed):
            for c,shift in enumerate((16,8,0,24)):assert abs(floats[i*4+c]-((p>>shift)&255)/255)<1e-7
        assert read(output-4)==read(output+count*16)==0xcdcdcdcd
        rows.append(dict(first=first,count=count,mapping=[m0,m1,mf],packed=packed,packets=expected_packets,queried=floats))
    return dict(xbe_sha256=digest,cases=rows,eye_vector_method='0x181c',eye_vector_scope='retained; constant-eye reflection remains unqualified')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--xbe',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.write_text(json.dumps(run(a.xbe),indent=2)+'\n')
    print('PASS: original mapped constants, packed cache, eye-vector packet, query, bounds and ABI')
