#!/usr/bin/env python3
"""Decode FBD1 passive storage observations. Preview is BEFORE display processing."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

def decode(data):
    if len(data)<1860:raise ValueError('truncated FBD1 header')
    h=struct.unpack_from('<32I',data)
    if h[:3]!=(0x31444246,1,1860) or h[30:]!=(1,0):raise ValueError('unsupported FBD1 stage/version')
    if h[3] not in (0,1) or len(data)!=1860+h[4]:raise ValueError('invalid FBD1 payload')
    width,height,depth=h[7:10]
    if h[3] and (not 0<width<=1920 or not 0<height<=1080 or depth not in (16,32) or h[4]!=width*height*(depth//8)):
        raise ValueError('invalid copied storage geometry')
    if not h[3] and h[4]:raise ValueError('failed copy contains pixels')
    commands=struct.unpack_from('<11I',data,128)
    offsets=(0x630,0x680,0x684,0x688,0x68c,0x690,0x84c,0x898,0x89c,0x8c0,0x8c4)
    metadata=dict(stage='framebuffer-before-display-processing',qualified=False,storage_copied=bool(h[3]),
        start=h[5],pitch=h[6],width=width,height=height,depth=depth,rgb565=bool(h[10]),
        general_control=h[11],screen_off=bool(h[12]),dac_started=bool(h[13]),dac_complete=bool(h[14]),dac_identity=bool(h[15]),
        display_commands={f'{offset:03x}':commands[i] for i,offset in enumerate(offsets) if h[16]&(1<<i)},
        encoder_programmed=bool(h[17]),encoder_timing_known=bool(h[18]),encoder_field_known=bool(h[19]),
        vblanks=h[20]|h[21]<<32,scanout_starts=h[22]|h[23]<<32,methods=h[24]|h[25]<<32,
        draw_published=h[26]|h[27]<<32,draw_retired=h[28]|h[29]<<32,
        tiles=[list(struct.unpack_from('<4I',data,172+16*i)) for i in range(8)],
        encoder={f'{i:02x}':dict(value=data[1068+i],known_mask=data[1324+i]) for i in range(256) if data[1324+i]},
        extensions={f'{0xda+2*i:02x}':data[1584+i] for i in range(18) if struct.unpack_from('<I',data,1580)[0]&(1<<i)},
        error=data[1604:1860].split(b'\0',1)[0].decode('utf-8'),sha256=hashlib.sha256(data).hexdigest())
    rgb=bytearray()
    if h[3]:
        source=data[1860:]
        for at in range(0,len(source),depth//8):
            if depth==32:rgb.extend((source[at+2],source[at+1],source[at]))
            else:
                value=source[at]|source[at+1]<<8
                rgb.extend(((value>>(11 if h[10] else 10)&31)*255//31,
                    (value>>5&(63 if h[10] else 31))*255//(63 if h[10] else 31),(value&31)*255//31))
    return metadata,bytes(rgb)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('snapshot',type=Path);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();metadata,rgb=decode(args.snapshot.read_bytes())
    with args.output.with_suffix('.json').open('x') as f:json.dump(metadata,f,indent=2);f.write('\n')
    if rgb:
        with args.output.with_suffix('.ppm').open('xb') as f:
            f.write(f'P6\n# BEFORE display processing; NOT qualified output\n{metadata["width"]} {metadata["height"]}\n255\n'.encode()+rgb)
    print(json.dumps(metadata,indent=2))
