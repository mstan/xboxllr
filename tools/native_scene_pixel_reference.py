#!/usr/bin/env python3
"""Independent post-transform pixel contract for retained Blinx triangle strips.

Device-observed transformed vertices are explicit INPUTS to this component
fixture. They do not qualify the shader that produced them. Expected coverage,
depth, texture samples and modulated colors are computed here without calling
the maintained rasterizer/decoder or using either route's after-image.

Scope is deliberately closed: single opaque BC1 texture, repeat/trilinear,
smooth shading, LESS depth, RGB/depth writes with preserved alpha/stencil,
saturated source alpha/fog and zero
specular. Other states fail rather than being silently approximated. This is
the noise-disabled software contract, not a native or Xbox hardware golden.
"""
import argparse
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import struct

from native_scene_reference import read_capture, read_scene


def words(target, depth=False):
    values=list(struct.unpack('<'+'I'*(len(target['data'])//4),target['data']))
    fmt=target['shape'][2]
    if depth:
        assert fmt in (44,45)
        return [((v&0xffffff)<<8)|(v>>24) for v in values]
    assert fmt==87,'This closed fixture requires native BGRA storage'
    return values


def bc1_images(texture):
    """Published BC1 endpoint/selector equations; retain ideal channel values.

    Microsoft BC1 documentation and D3D11.3 section 19.5.2 describe 565 UNORM
    endpoints and 1/3 or 1/2 palette interpolation. No production codec imports.
    Exact silicon endpoint/palette rounding remains outside this contract.
    """
    assert (texture['resource'][3]>>8)&255==12
    assert texture['cpu']==b''.join(m['data'] for m in texture['mips'])
    images=[]
    for mip in texture['mips']:
        width,height=mip['shape'][:2]
        pixels=[None]*(width*height)
        for block,(e0,e1,selectors) in enumerate(struct.iter_unpack('<HHI',mip['data'])):
            rgb=lambda v:[255*((v>>shift)&mask)/mask for shift,mask in ((11,31),(5,63),(0,31))]
            a,b=rgb(e0),rgb(e1)
            if e0>e1:
                palette=[a,b,[(2*x+y)/3 for x,y in zip(a,b)],[(x+2*y)/3 for x,y in zip(a,b)]]
            else:
                palette=[a,b,[(x+y)/2 for x,y in zip(a,b)],None]
            bx=block%((width+3)//4);by=block//((width+3)//4)
            for i in range(16):
                x=bx*4+i%4;y=by*4+i//4
                if x>=width or y>=height:continue
                color=palette[(selectors>>(2*i))&3]
                assert color is not None,'Transparent BC1 needs a separate alpha/kill contract'
                pixels[y*width+x]=color
        assert all(p is not None for p in pixels)
        images.append((width,height,pixels))
    return images


def plane(points, values):
    """Solve an affine plane on the integer subpixel grid using exact rationals."""
    (x0,y0),(x1,y1),(x2,y2)=points
    det=(x1-x0)*(y2-y0)-(x2-x0)*(y1-y0)
    dx=Fraction((values[1]-values[0])*(y2-y0)-(values[2]-values[0])*(y1-y0),det)
    dy=Fraction((x1-x0)*(values[2]-values[0])-(x2-x0)*(values[1]-values[0]),det)
    return values[0]-dx*x0-dy*y0,dx,dy


def binary32(value):
    """Exact rational round-to-nearest/even for this fixture's normal values.

    This is a numerical contract, not a call into the renderer. Integer
    arithmetic determines tie ownership independently of host float math.
    """
    value=Fraction(value)
    if not value:return value
    sign=-1 if value<0 else 1;value=abs(value)
    n,d=value.numerator,value.denominator
    exponent=n.bit_length()-d.bit_length()
    scale=Fraction(2)**exponent
    if value<scale:exponent-=1
    assert -126<=exponent<=127
    unit=Fraction(2)**(exponent-23)
    scaled=value/unit;integer,remainder=divmod(scaled.numerator,scaled.denominator)
    if 2*remainder>scaled.denominator or (2*remainder==scaled.denominator and integer%2):integer+=1
    return sign*integer*unit


def software_depth(weights, area, z, arithmetic):
    # Exact rational rounding resolves LESS-test boundary cases. The rounding
    # graph must be declared from compiler/disassembly evidence. The retained
    # -march=native DLL rounds the B product then fuses A and C multiply/adds;
    # a build without contraction has the separate left-associated graph.
    assert all(w.denominator==1 and abs(w)<2**24 for w in weights)
    products=[w*int(v) for w,v in zip(weights,z)]
    if arithmetic=='fma-b-first':
        total=binary32(products[2]+binary32(products[0]+binary32(products[1])))
    else:
        rounded=[binary32(p) for p in products]
        total=binary32(binary32(rounded[0]+rounded[1])+rounded[2])
    return math.floor(binary32(total*binary32(Fraction(1,area))))


def sample(images, u, v, lod):
    # Closed fixture uses repeat addressing and linear magnification/trilinear
    # minification. Each filter is a convex combination of ideal BC1 colors.
    last=len(images)-1;level=max(0,min(last,lod));first=math.floor(level)
    mix=level-first
    def bilinear(mip):
        width,height,pixels=images[mip]
        xx=(u%1)*width-.5;yy=(v%1)*height-.5
        x,y=math.floor(xx),math.floor(yy);fx=xx-x;fy=yy-y
        return [sum(pixels[((y+dy)%height)*width+(x+dx)%width][c]*
            (fx if dx else 1-fx)*(fy if dy else 1-fy)
            for dx in (0,1) for dy in (0,1)) for c in range(3)]
    a=bilinear(first);b=bilinear(min(last,first+1))
    return [(1-mix)*x+mix*y for x,y in zip(a,b)]


def contract(capture, scene, observation):
    assert observation['noise_disabled_fixture'] and observation['complete_draw_executed']
    assert observation['vertex_capture_sha256']==capture['sha256']
    assert observation['scene_capture_sha256']==scene['sha256']
    m={int(k,16):v for k,v in observation['methods'].items()}
    # These are supplied raster-state inputs. Assertions bound the oracle's
    # domain; they are not an independent qualification of the original setters.
    expected={0x208:0x128,0x300:0,0x304:1,0x308:1,0x30c:1,0x310:0,
        0x318:0,0x31c:0,0x354:0x201,0x358:0x10101,0x35c:1,
        0x37c:0x1d01,0x380:0,0x38c:0x1b02,0x390:0x1b02,0x3a0:0x900,
        0x288:0x130e0300,0x28c:0x1c80,0xac0:0x08040000,0x260:0x18140000,
        0xaa0:0xc00,0x1e40:0xc00,0x1e60:1,0x1e70:1,0x1b08:0x10101,
        0x1b48:0x10101,0x1b88:0x10101,0x1bc8:0x10101,
        0x394:0,0x398:0x4b7fffff}
    assert all(m[k]==v for k,v in expected.items()),{hex(k):hex(m[k]) for k,v in expected.items() if m[k]!=v}
    assert m[0x39c] in (0x404,0x405)
    assert all(m[k]==0 for k in (0x314,0x320,0x324,0x328,0x32c,0x330,0x334,0x338))
    assert m[0x344]==0x302 and m[0x348]==0x303 and m[0x350]==0x8006
    assert m[0x2a4]==1 and m[0x29c]==0x800
    # This retained linear fog is >=1 everywhere: p0=1.5, p1=-0.
    assert m[0x9c0]==0x3fc00000 and m[0x9c4]==0x80000000 and m[0x9c8]==0
    assert capture['header'][2]==6 and not scene['header'][8]
    assert scene['textures'][0] and not any(scene['textures'][1:])
    vertices=observation['vertices']
    assert len(vertices)==capture['header'][4] and all(len(v)==28 for v in vertices)
    assert len(observation['fog'])==len(vertices) and all(math.isfinite(f) for f in observation['fog'])
    assert all(all(math.isfinite(x) for x in v) and v[3]>0 and
        0<=v[2]<=16777215 and v[2].is_integer() and v[7]>=1 and
        v[8:12]==[0,0,0,0] and v[15]==1 for v in vertices)
    assert all(0<=v[c]<=.5 for v in vertices for c in (4,5,6))
    assert all(v[c]*16==math.trunc(v[c]*16) for v in vertices for c in (0,1))
    filter_word=m[0x1b14];control=m[0x1b0c]
    assert filter_word>>24==2 and (filter_word>>16)&255==6 and not control&0x30
    bias=filter_word&8191;bias=(bias-8192 if bias&4096 else bias)/256
    min_lod=((control>>18)&4095)/256;max_lod=((control>>6)&4095)/256
    assert min_lod<=max_lod
    width,height,_=scene['before'][0]['shape']
    assert scene['before'][1]['shape'][:2]==[width,height]
    assert m[0x200]==width<<16 and m[0x204]==height<<16
    assert m[0x2c0]==(width-1)<<16 and m[0x2e0]==(height-1)<<16
    assert m[0x20c]==width*4*0x10001
    return m,vertices,bias,min_lod,max_lod


def expected_targets(capture, scene, observation, arithmetic):
    m,vertices,bias,min_lod,max_lod=contract(capture,scene,observation)
    images=bc1_images(scene['textures'][0]);width,height,_=scene['before'][0]['shape']
    before_color=words(scene['before'][0]);before_depth=words(scene['before'][1],True)
    colors={};depths={};covered=set();triangles=0;depth_tested=depth_rejected=0
    ideal_depth_error=0
    # An independently derived binary32 forward-error interval: three products,
    # two adds and one reciprocal/multiply path give gamma(6). Grid weights and
    # area in these inputs must be exactly representable as binary32 integers.
    # gamma(6) covers reciprocal rounding in the <=6 roundings on each path;
    # flooring the endpoints accounts for conversion to integer depth.
    u=Fraction(1,2**24);gamma=6*u/(1-6*u)
    for j in range(len(vertices)-2):
        indices=[j,j+1,j+2] if j%2==0 else [j+1,j,j+2]
        vv=[vertices[i] for i in indices]
        pts=[(int(v[0]*16),int(v[1]*16)) for v in vv]
        (ax,ay),(bx,by),(cx,cy)=pts
        det=(bx-ax)*(cy-ay)-(cx-ax)*(by-ay)
        if not det or (det>0)==(m[0x39c]==0x404):continue
        assert abs(det)<2**24
        triangles+=1
        if det<0:vv[1],vv[2]=vv[2],vv[1];pts[1],pts[2]=pts[2],pts[1]
        assert len({v[2] for v in vv})>1,'Constant-depth optimization needs its own numerical contract'
        zplane=plane(pts,[int(v[2]) for v in vv])
        bases=[plane(pts,[int(i==k) for i in range(3)]) for k in range(3)]
        area=abs(det)
        invw=[1/v[3] for v in vv]
        xmin=max(0,math.ceil((min(p[0] for p in pts)-8)/16))
        xmax=min(width-1,math.floor((max(p[0] for p in pts)-8)/16))
        ymin=max(0,math.ceil((min(p[1] for p in pts)-8)/16))
        ymax=min(height-1,math.floor((max(p[1] for p in pts)-8)/16))
        for y in range(ymin,ymax+1):
            for x in range(xmin,xmax+1):
                px,py=x*16+8,y*16+8
                # Exact directed half-plane membership and top/left ownership.
                inside=True
                for i in range(3):
                    x0,y0=pts[i];x1,y1=pts[(i+1)%3]
                    cross=(x1-x0)*(py-y0)-(y1-y0)*(px-x0)
                    if cross<0 or (cross==0 and not (y1<y0 or (y1==y0 and x1>x0))):inside=False;break
                if not inside:continue
                index=y*width+x;covered.add(index)
                depth_tested+=1
                ideal=zplane[0]+zplane[1]*px+zplane[2]*py
                radius=gamma*ideal  # all nonnegative weighted vertex depths
                incoming=(max(0,math.floor(ideal-radius)),min(16777215,math.floor(ideal+radius)))
                lambdas=[p[0]+p[1]*px+p[2]*py for p in bases]
                z=software_depth([l*area for l in lambdas],area,[v[2] for v in vv],arithmetic)
                assert incoming[0]<=z<=incoming[1],'Exact software result violates independent affine error bound'
                ideal_depth_error=max(ideal_depth_error,abs(float(ideal)-z))
                stored=depths.get(index,(before_depth[index]>>8,)*2)
                if z>=stored[0]:depth_rejected+=1;continue
                weights=[float(l)*q for l,q in zip(lambdas,invw)]
                denominator=sum(weights)
                uv=[sum(w*v[c] for w,v in zip(weights,vv))/denominator for c in (12,13)]
                gradients=[]
                for axis in (1,2):
                    dw=[float(p[axis])*16*q for p,q in zip(bases,invw)]
                    gradients.append([(sum(w*v[c] for w,v in zip(dw,vv))-t*sum(dw))/denominator*extent
                        for c,t,extent in zip((12,13),uv,images[0][:2])])
                rho=max(math.hypot(*g) for g in gradients)
                lod=max(min_lod,min(max_lod,math.log2(rho)+bias if rho else -1000))
                texel=sample(images,*uv,lod)
                diffuse=[sum(w*v[c] for w,v in zip(weights,vv))/denominator for c in (4,5,6)]
                # RGB write mask 0x00010101 leaves target alpha untouched.
                colors[index]=[math.floor(t*d+.5) for t,d in zip(texel,diffuse)]+[before_color[index]>>24]
                depths[index]=(z,z)
    assert triangles and colors,'No-op captures cannot qualify this exercised component'
    return before_color,before_depth,colors,depths,covered,dict(triangles=triangles,
        tested_fragments=depth_tested,rejected_depth_fragments=depth_rejected,
        maximum_affine_depth_error=ideal_depth_error)


def audit(captures, diagnostics, number, arithmetic):
    path=captures/f'draw-{number}.bin';capture=read_capture(path)
    scene=read_scene(captures/f'scene-{number}.bin')
    observation_path=diagnostics/f'draw-{number}.aot-private.json'
    observation=json.loads(observation_path.read_text())
    before_color,before_depth,colors,depths,covered,stats=expected_targets(capture,scene,observation,arithmetic)
    actual_color=list(struct.unpack('<'+'I'*len(before_color),(diagnostics/f'draw-{number}-color.bin').read_bytes()))
    actual_depth=list(struct.unpack('<'+'I'*len(before_depth),(diagnostics/f'draw-{number}-depth.bin').read_bytes()))
    failures=[];maximum_color=0;maximum_depth=0;coverage_failures=0
    for i,(color,depth) in enumerate(zip(actual_color,actual_depth)):
        if i not in colors:
            if color!=before_color[i] or depth!=before_depth[i]:
                coverage_failures+=1;failures.append([i%scene['before'][0]['shape'][0],i//scene['before'][0]['shape'][0],'untouched'])
            continue
        expected=colors[i]
        got=[(color>>shift)&255 for shift in (16,8,0,24)]
        error=max(abs(a-b) for a,b in zip(got[:3],expected[:3]));maximum_color=max(maximum_color,error)
        lo,hi=depths[i];z=depth>>8
        maximum_depth=max(maximum_depth,max(lo-z,z-hi,0))
        if error>1 or got[3]!=expected[3] or not lo<=z<=hi or (depth&255)!=(before_depth[i]&255):
            failures.append([i%scene['before'][0]['shape'][0],i//scene['before'][0]['shape'][0],got,expected,z,[lo,hi]])
    return dict(draw=number,vertex_capture_sha256=capture['sha256'],scene_capture_sha256=scene['sha256'],
        observation_sha256=hashlib.sha256(observation_path.read_bytes()).hexdigest(),
        library_sha256=observation['library_sha256'],
        depth_arithmetic=arithmetic,
        **stats,covered_pixels=len(covered),expected_written_pixels=len(colors),
        target_pixels_checked=len(actual_color),untouched_pixels_checked=len(actual_color)-len(colors),
        color_channel_bound=1,maximum_color_channel_error=maximum_color,
        depth_bound='exact rational nearest/even binary32 weighted interpolation and floor, additionally bounded by affine gamma(6); software contract only',
        maximum_depth_interval_violation=maximum_depth,coverage_failures=coverage_failures,
        failure_count=len(failures),first_failures=failures[:12],
        post_transform_pixel_qualified=not failures,
        complete_scene_pixel_qualified=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--draws',type=int,nargs='+',default=[2,3,4,5,6])
    parser.add_argument('--depth-arithmetic',required=True,choices=('separate','fma-b-first'),
        help='Declare the compiled rounding graph from build/disassembly evidence, never select it by pixel agreement')
    args=parser.parse_args()
    rows=[audit(args.captures,args.diagnostics,n,args.depth_arithmetic) for n in args.draws]
    args.output.write_text(json.dumps(dict(draws=rows,
        scope='Independent post-transform noise-disabled software pixel component; supplied device vertices/state do not qualify shader/setters or enabled-noise scenes'),indent=2)+'\n')
    for row in rows:print(json.dumps(row),flush=True)
    raise SystemExit(0 if rows and all(r['post_transform_pixel_qualified'] for r in rows) else 1)
