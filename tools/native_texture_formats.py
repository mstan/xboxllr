"""Authored packed/compressed texels and rectangular mip-layout expectations."""
import struct


def mip_bytes(fmt,width,height):
    return width*height*2 if fmt==4 else ((width+3)//4)*((height+3)//4)*16


def image_chain(fmt,width,height):
    images=[];encoded=bytearray()
    rgb=((255,0,0),(0,255,0),(0,0,255),(255,255,255))
    endpoints=(0xf800,0x07e0,0x001f,0xffff)
    for level in range(max(width,height).bit_length()):
        w=max(1,width>>level);h=max(1,height>>level);pixels=[None]*(w*h)
        if fmt==4:
            words=[0]*(w*h)
            for y in range(h):
                for x in range(w):
                    r=(x+3*level)%16;g=(3*y+level)%16
                    b=(x+2*y+5*level)%16;a=(x+3*y+level)%16
                    address=shift=0
                    for bit in range(max(w,h).bit_length()-1):
                        if 1<<bit<w:address|=(x>>bit&1)<<shift;shift+=1
                        if 1<<bit<h:address|=(y>>bit&1)<<shift;shift+=1
                    words[address]=b|g<<4|r<<8|a<<12
                    pixels[y*w+x]=(r*17,g*17,b*17,a*17)
            encoded.extend(struct.pack('<'+'H'*len(words),*words))
        else:
            for by in range((h+3)//4):
                for bx in range((w+3)//4):
                    first=(level+bx+2*by)%4;second=(first+1)%4
                    p0,p1=rgb[first],rgb[second]
                    colors=(p0,p1,tuple((2*a+b)/3 for a,b in zip(p0,p1)),
                            tuple((a+2*b)/3 for a,b in zip(p0,p1)))
                    color_bits=alpha_bits=0
                    a0,a1=(231,14) if (level+bx+by)%2==0 else (14,231)
                    if a0>a1:
                        alpha=(a0,a1)+tuple(((7-i)*a0+i*a1)/7 for i in range(1,7))
                    else:
                        alpha=(a0,a1)+tuple(((5-i)*a0+i*a1)/5 for i in range(1,5))+(0,255)
                    for y in range(4):
                        for x in range(4):
                            texel=y*4+x;ci=(x+2*y+level)%4
                            ai=(x+3*y+level+bx+by)%16 if fmt==0xe else (x+2*y+level)%8
                            color_bits|=ci<<(texel*2)
                            alpha_bits|=ai<<(texel*(4 if fmt==0xe else 3))
                            if bx*4+x<w and by*4+y<h:
                                pixels[(by*4+y)*w+bx*4+x]=colors[ci]+(ai*17 if fmt==0xe else alpha[ai],)
                    if fmt==0xe:encoded.extend(struct.pack('<Q',alpha_bits))
                    else:encoded.extend(bytes((a0,a1))+alpha_bits.to_bytes(6,'little'))
                    encoded.extend(struct.pack('<HHI',endpoints[first],endpoints[second],color_bits))
        images.append((w,h,pixels))
    return images,bytes(encoded)


def cases():
    for fmt in (4,0xe,0xf):
        for width,height in ((8,8),(8,16),(16,8)):
            for w,q in (([1,1,1],[1,1,1]),([1,2,4],[1,1,1]),([1,2,4],[4,1,2])):
                for mag,minf,mip in ((1,1,0),(2,2,0),(1,2,0),(2,1,0),
                                    (1,1,1),(2,2,1),(1,1,2),(2,2,2)):
                    for rho in (.25,2,8):
                        yield dict(format=fmt,base=width,height=height,dxt=False,pattern=True,
                            mag=mag,min=minf,mip=mip,rho=rho,bias=.125 if mip==1 else 0,
                            maxlevel=0,u=1,v=1,origin=[.09423828125,.34423828125],w=w,q=q,projected=True)
                for u,v in ((1,3),(3,1),(4,1),(1,4),(4,3),(3,4)):
                    for mip in (0,2):
                        yield dict(format=fmt,base=width,height=height,dxt=False,pattern=True,
                            mag=2,min=2,mip=mip,rho=2,bias=.25 if mip else -.25,
                            maxlevel=1 if mip else 0,u=u,v=v,origin=[-.34326171875,.90673828125],
                            w=w,q=q,projected=True)


def codec_cases():
    # Nearly constant coordinates at exact texel centers isolate block decode
    # from interpolation/filter/LOD, with every texel in four authored blocks.
    for fmt in (4,0xe,0xf):
        for y in range(8):
            for x in range(8):
                yield dict(format=fmt,base=8,height=8,dxt=False,pattern=True,
                    mag=1,min=1,mip=0,rho=2**-20,bias=0,maxlevel=0,u=3,v=3,
                    origin=[(x+.5)/8,(y+.5)/8],w=[1,1,1],q=[1,1,1],projected=True)


def precision_chain(fmt,width,height):
    """Independent D3D11.3 19.5.2 decoder budgets in channel-byte units.

    Each derived BC2/3 channel has absolute_error=1 plus .03*endpoint span.
    Exact zero/one entries and authored UNORM endpoints add no decode error.
    These are host-API bounds, not an NV2A precision golden.
    """
    rgb=((255,0,0),(0,255,0),(0,0,255),(255,255,255))
    images=[]
    for level in range(max(width,height).bit_length()):
        w=max(1,width>>level);h=max(1,height>>level);budgets=[]
        for y in range(h):
            for x in range(w):
                if fmt==4:
                    budgets.append((0,0,0,0));continue
                bx=x//4;by=y//4;first=(level+bx+2*by)%4
                p0,p1=rgb[first],rgb[(first+1)%4]
                ci=(x%4+2*(y%4)+level)%4
                color=tuple(1+.03*abs(a-b) if ci>=2 and a!=b else 0
                            for a,b in zip(p0,p1))
                ai=(x%4+2*(y%4)+level)%8
                high=(level+bx+by)%2==0
                alpha=1+.03*(231-14) if fmt==0xf and ai>=2 and (high or ai<6) else 0
                budgets.append(color+(alpha,))
        images.append((w,h,budgets))
    return images
