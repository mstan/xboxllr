#!/usr/bin/env python3
"""Original texture setters/draws and independent finite 2D sampling pixels.

Resources start preowned; original XDK callees execute without hooks. Authored
texture images, gradients and sampler equations supply expectations, independently
of the maintained texture decoder. Silicon LOD/rounding remains a hardware probe.
"""
import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import struct

from native_vertex_fetch_reference import Reference, OBJECT, STREAMS, FRAME, XBE_SHA
import native_texture_formats


def bits(value):
    return struct.unpack('<I',struct.pack('<f',value))[0]


def program(implicit_q=False):
    return [0,0x0020001b,0x08000000,0x0000e800 if implicit_q else 0x0000f800,
            0,0x0020021b,0x08000000,0x0000c849 if implicit_q else 0x0000f849]


def image_chain(dxt,pattern,base=8):
    images=[];encoded=bytearray()
    colors=((255,0,0,255),(0,255,0,255),(0,0,255,255),(255,255,255,255))
    endpoints=(0xf800,0x07e0,0x001f,0xffff)
    for level in range(base.bit_length()):
        size=max(1,base>>level)
        image=[colors[(level+(x+2*y if pattern else 0))%4] for y in range(size) for x in range(size)]
        if dxt:
            image=[]
            for y in range(size):
                for x in range(size):
                    image.append(colors[(level+((x//4)+2*(y//4) if pattern else 0))%4])
            for by in range((size+3)//4):
                for bx in range((size+3)//4):
                    index=(level+(bx+2*by if pattern else 0))%4
                    encoded.extend(struct.pack('<HHI',endpoints[index],0,0))
        else:
            words=[0]*len(image)
            for y in range(size):
                for x in range(size):
                    address=sum(((x>>b)&1)<<(2*b) | ((y>>b)&1)<<(2*b+1) for b in range(base.bit_length()-1))
                    r,g,b,a=image[y*size+x];words[address]=b|g<<8|r<<16|a<<24
            encoded.extend(struct.pack('<'+'I'*len(words),*words))
        images.append((size,image))
    return images,bytes(encoded)


def sample_level(image,u,v,linear,address_u,address_v,border):
    if len(image)==2:
        width,pixels=image;height=width
    else:width,height,pixels=image
    def texel(x,y):
        if (address_u==4 and not 0<=x<width) or (address_v==4 and not 0<=y<height):return border
        x=x%width if address_u==1 else max(0,min(width-1,x))
        y=y%height if address_v==1 else max(0,min(height-1,y))
        return pixels[y*width+x]
    if not linear:return texel(math.floor(u*width),math.floor(v*height))
    x=u*width-.5;y=v*height-.5;ix=math.floor(x);iy=math.floor(y);fx=x-ix;fy=y-iy
    return [sum(texel(ix+dx,iy+dy)[c]*(fx if dx else 1-fx)*(fy if dy else 1-fy)
                for dy in (0,1) for dx in (0,1)) for c in range(4)]


def projected_field(row,x,y):
    # Closed-form field for the authored right triangle. Q/W weights project
    # its two independent coordinate axes; no shader/triangle decoder is used.
    r=[q/w for q,w in zip(row['q'],row['w'])]
    dx=x-8;dy=y-8;hx=(r[1]-r[0])/48;hy=(r[2]-r[0])/48
    denominator=r[0]+hx*dx+hy*dy
    width=row.get('base',8);height=row.get('height',width)
    scale=row['rho']/width;scale_v=row['rho']/height
    u=row['origin'][0]+scale*r[1]*dx/denominator
    v=row['origin'][1]+scale_v*r[2]*dy/denominator
    ux=scale*r[1]*(denominator-dx*hx)/denominator**2
    uy=-scale*r[1]*dx*hy/denominator**2
    vx=-scale_v*r[2]*dy*hx/denominator**2
    vy=scale_v*r[2]*(denominator-dy*hy)/denominator**2
    rho=max(math.hypot(ux*width,vx*height),math.hypot(uy*width,vy*height))
    return u,v,rho


def expected_pixels(row,images,decoder_budgets=False):
    result=[]
    for y in range(10,54):
        for x in range(10,54):
            if x+y+1>=62:continue
            if row.get('projected'):
                u,v,rho=projected_field(row,x+.5,y+.5)
            else:
                u=row['origin'][0]+(x+.5-8)*row['rho']/row.get('base',8)
                v=row['origin'][1]+(y+.5-8)*row['rho']/row.get('base',8)
                rho=row['rho']
            lod=max(row['maxlevel'],math.log2(rho)+row['bias'])
            last=len(images)-1;lod=min(last,lod);linear=row['mag']==2 if lod<=0 else row['min']==2
            if lod<=0 or row['mip']==0:levels=[(0,1)]
            elif row['mip']==1:levels=[(min(last,math.floor(lod+.5)),1)]
            else:levels=[(math.floor(lod),1-(lod%1)),(min(last,math.floor(lod)+1),lod%1)]
            border=(0,0,0,0) if decoder_budgets else (18,52,86,127)
            colors=[(sample_level(images[l],u,v,linear,row['u'],row['v'],border),weight) for l,weight in levels]
            values=[sum(color[c]*weight for color,weight in colors) for c in range(4)]
            result.append((y*64+x,[2+math.ceil(v) for v in values] if decoder_budgets else [round(v) for v in values]))
    return result


def cases():
    for mip in (0,1,2):
        for filt in (1,2):
            yield dict(dxt=False,xrgb=True,pattern=True,mag=filt,min=filt,mip=mip,rho=2,bias=0,maxlevel=0,u=1,v=1,origin=[.09375,.34375])
    for dxt in (False,True):
        for pattern in (False,True):
            for mag,minf,mip in ((1,1,0),(2,1,0),(1,2,0),(2,2,0),(1,1,1),(2,2,1),(1,1,2),(2,2,2)):
                for rho,bias,maxlevel in ((.25,0,0),(2,0,0),(4,-1.25,0),(2,.25,0),(8,0,0),(1,0,2)):
                    yield dict(dxt=dxt,pattern=pattern,mag=mag,min=minf,mip=mip,rho=rho,bias=bias,maxlevel=maxlevel,u=1,v=1,origin=[.09375,.34375])
        for u,v in ((1,3),(3,1),(4,1),(1,4),(4,3),(3,4)):
            for filt in (1,2):
                yield dict(dxt=dxt,pattern=True,mag=filt,min=filt,mip=0,rho=.25,bias=0,maxlevel=0,u=u,v=v,origin=[-.34375,.90625])
        for rho,bias,maxlevel in ((4,-1.2,0),(32,0,0),(128,0,0),(8,.25,0),(1,0,5),(1,0,7)):
            for mip in (1,2):
                yield dict(dxt=dxt,pattern=False,mag=2,min=2,mip=mip,rho=rho,bias=bias,maxlevel=maxlevel,u=1,v=1,origin=[.09375,.34375],base=128)
        # Q/W constant makes the projected field affine while exercising both
        # perspective interpolation and the post-interpolation Q division.
        for w,q in (([1,2,4],[.5,1,2]),([2,4,8],[1,2,4]),([1,1,1],[2,2,2])):
            for rho in (.25,2):
                yield dict(dxt=dxt,pattern=True,mag=2,min=2,mip=2,rho=rho,bias=0,maxlevel=0,u=1,v=1,origin=[.09375,.34375],w=w,q=q)


def projected_cases():
    for w,q in (([1,1,1],[1,2,4]),([1,2,4],[1,1,1]),
                ([1,2,4],[4,1,2]),([2,4,8],[.5,1,1])):
        for fmt in ('argb','xrgb','dxt'):
            for mag,minf,mip in ((1,1,0),(2,2,0),(1,2,0),(2,1,0),
                                (1,1,1),(2,2,1),(1,1,2),(2,2,2)):
                for rho in (.25,2,8):
                    yield dict(dxt=fmt=='dxt',xrgb=fmt=='xrgb',pattern=True,
                        mag=mag,min=minf,mip=mip,rho=rho,bias=.125 if mip==1 else 0,maxlevel=0,
                        # Offset exact nearest-texel ties; silicon boundary
                        # rounding remains a separate deferred question.
                        u=1,v=1,origin=[.09423828125,.34423828125],w=w,q=q,projected=True)
            for u,v in ((1,3),(3,1),(4,1),(1,4),(4,3),(3,4)):
                for mip in (0,2):
                    yield dict(dxt=fmt=='dxt',xrgb=fmt=='xrgb',pattern=True,
                        mag=2,min=2,mip=mip,rho=2,bias=.25 if mip else -.25,
                        maxlevel=1 if mip else 0,u=u,v=v,
                        origin=[-.34326171875,.90673828125],w=w,q=q,projected=True)


def run(args):
    ref=Reference(args);rows=[];compared=0
    count=ref.lib.xlle_nv2a_reference_draw_count
    count.argtypes=[ctypes.c_void_p,ctypes.c_uint32];count.restype=ctypes.c_uint64
    try:
        ref.emit(0x184,2);ref.emit(0x188,2)  # preowned texture DMA contexts
        ref.resource(OBJECT+0x1000,STREAMS);ref.set_stream(0,OBJECT+0x1000,32)
        ref.put(OBJECT,1,0x10,0,0,0)
        for i in range(16):ref.put(OBJECT+0x14+i*16,0,0,2,0)
        ref.put(OBJECT+0x14,0,0,0x42);ref.put(OBJECT+0x24,0,16,0x42)
        ref.bind_program(program(args.implicit_q))
        surface=OBJECT+0x1c00;ref.put(surface,0x01050001,FRAME,0,0x00010c29,0x0303f03f)
        for offset in (0x21b4,0x21c0,0x21c4):ref.put(ref.device+offset,surface)
        ref.cpu.mem_write(0x141d88,bytes(166*4))
        pixel=OBJECT+0x2800;state=[0]*60
        state[0]=0x18200000;state[34]=0x08200000;state[26]=state[45]=0xc00;state[53]=1
        state[8]=0xc;state[9]=0x1c00;state[54]=1
        ref.put(pixel,1,1,pixel+12,*state);ref.call('D3DDevice_SetPixelShader',[pixel])
        samples=list(native_texture_formats.codec_cases() if args.codec_only else
                     native_texture_formats.cases() if args.formats_only else
                     projected_cases() if args.projected_only else cases())
        if args.implicit_q:
            # A small sampling corpus tests observable default Q/W. Poisoned
            # input W/Q must be ignored; coordinates/expectations stay authored.
            samples=[row for row in samples if row.get('w',[1,1,1])==[1,1,1]
                and row.get('q',[1,1,1])==[1,1,1]][::32]
        for row in samples:
            base=row.get('base',8);height=row.get('height',base);log=base.bit_length()-1
            images,encoded=case_images(row)
            if row.get('xrgb'):
                encoded=bytearray(encoded);encoded[3::4]=bytes([23])*(len(encoded)//4);encoded=bytes(encoded)
            data=0x3000000;ref.cpu.mem_write(data,encoded)
            color=row.get('format',0xc if row['dxt'] else 7 if row.get('xrgb') else 6)
            header=OBJECT+0x2000;fmt=((height.bit_length()-1)<<24)|(log<<20)|(len(images)<<16)|0x29|(color<<8)
            ref.put(header,0x010c0001,data,0,fmt,0)
            ref.call('D3DDevice_SetTexture',[0,header])
            for stage_state,value in ((0,row['u']),(1,row['v']),(2,1),(3,row['mag']),(4,row['min']),
                    (5,row['mip']),(6,bits(row['bias'])),(7,row['maxlevel']),(8,1),(29,0x7f123456)):
                ref.call('D3DDevice_SetTextureStageStateNotInline',[0,stage_state,value])
            for i,(x,y) in enumerate(((8,8),(56,8),(8,56)),1):
                w=row.get('w',[1,1,1])[i-1];q=row.get('q',[1,1,1])[i-1]
                ref.floats(STREAMS+i*32,x,y,10,31 if args.implicit_q else w,
                    (row['origin'][0]+(x-8)*row['rho']/base)*q,
                    (row['origin'][1]+(y-8)*row['rho']/height)*q,0,31 if args.implicit_q else q)
            before=count(ref.gpu,1)
            try:_,methods=ref.draw(1,state_dirty=None)
            except AssertionError:
                value=ref.lib.xlle_nv2a_reference_method_value
                value.argtypes=[ctypes.c_void_p,ctypes.c_uint32];value.restype=ctypes.c_uint32
                print({hex(m):hex(value(ref.gpu,m)) for m in range(0x1b00,0x1b20,4)})
                raise
            ref.check()
            assert count(ref.gpu,1)==before+1,(row,methods)
            if args.inspect:
                print(json.dumps(dict(row=row,methods={hex(k):hex(v) for k,v in methods.items() if 0x1b00<=k<0x1b40})))
                break
            expected=expected_pixels(row,images)
            actual=struct.unpack('<4096I',ref.cpu.mem_read(FRAME,16384))
            for index,rgba in expected:
                word=actual[index]
                assert all(abs((word>>shift&255)-want)<=1 for shift,want in zip((16,8,0,24),rgba)),(row,index,hex(word),rgba,{hex(k):hex(v) for k,v in methods.items() if 0x1b00<=k<0x1b40})
            compared+=len(expected)
            _,_=ref.draw(1,state_dirty=None);ref.check()
            repeated=bytes(ref.cpu.mem_read(FRAME,16384))
            assert repeated==struct.pack('<4096I',*actual),'Persistent original draw changed pixels'
            compared+=len(expected)
            rows.append(dict(**row,methods={hex(k):hex(v) for k,v in methods.items() if 0x1b00<=k<0x1b40}))
        ref.set_stream(0,0,0)
    finally:ref.close()
    return dict(xbe_sha256=XBE_SHA,cases=rows,original_draws=2*len(rows),interior_pixels=compared,
        reference_mode='codec' if args.codec_only else 'formats' if args.formats_only else 'projected' if args.projected_only else 'implicit-q' if args.implicit_q else 'affine')


def case_images(row):
    if 'format' in row:
        return native_texture_formats.image_chain(row['format'],row['base'],row['height'])
    return image_chain(row['dxt'],row['pattern'],row.get('base',8))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','binding','library','dependencies','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--inspect',action='store_true')
    parser.add_argument('--native-cases',type=Path)
    modes=parser.add_mutually_exclusive_group()
    modes.add_argument('--implicit-q',action='store_true')
    modes.add_argument('--projected-only',action='store_true')
    modes.add_argument('--formats-only',action='store_true')
    modes.add_argument('--codec-only',action='store_true')
    parser.add_argument('--native-bc-precision',action='store_true',help='Separate host-API BC2/3 decode intervals; does not change the LLE pixel bound')
    args=parser.parse_args();handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    format_mode=args.formats_only or args.codec_only
    if args.native_bc_precision and not format_mode:parser.error('--native-bc-precision requires --formats-only or --codec-only')
    result=run(args);args.output.write_text(json.dumps(result,indent=2)+'\n')
    if args.native_cases:
        with args.native_cases.open('wb') as file:
            file.write(struct.pack('<4I',0x54584c58,3 if args.native_bc_precision else 2 if format_mode else 1,len(result['cases']),
                1 if args.implicit_q else 2 if args.projected_only or format_mode else 0))
            for row in result['cases']:
                base=row.get('base',8);images,encoded=case_images(row)
                if row.get('xrgb'):
                    encoded=bytearray(encoded);encoded[3::4]=bytes([23])*(len(encoded)//4);encoded=bytes(encoded)
                expected=expected_pixels(row,images)
                if args.native_bc_precision:
                    budgets=native_texture_formats.precision_chain(row['format'],base,row['height'])
                    limits=expected_pixels(row,budgets,decoder_budgets=True)
                meta=[row.get('format',2 if row.get('xrgb') else row['dxt']),row['mag'],row['min'],row['mip'],row['maxlevel'],row['u'],row['v'],len(expected),base,len(images)]
                if format_mode:meta.append(row['height'])
                file.write(struct.pack('<'+'I'*len(meta),*meta))
                file.write(struct.pack('<4f',row['rho'],row['bias'],*row['origin']))
                file.write(struct.pack('<6f',*row.get('w',[1,1,1]),*row.get('q',[1,1,1])))
                cursor=0
                for level,image in enumerate(images):
                    size=image[0]
                    length=native_texture_formats.mip_bytes(row['format'],image[0],image[1]) if format_mode else ((size+3)//4)**2*8 if row['dxt'] else size*size*4
                    data=encoded[cursor:cursor+length];cursor+=length
                    file.write(struct.pack('<I',len(data)));file.write(data)
                for pixel,(index,rgba) in enumerate(expected):
                    file.write(struct.pack('<I4B',index,*rgba))
                    if args.native_bc_precision:
                        assert limits[pixel][0]==index
                        file.write(struct.pack('<4B',*limits[pixel][1]))
    print(f"PASS: {len(result['cases'])} independent texture cases / {result['interior_pixels']} interior pixels")
