#!/usr/bin/env python3
"""Independent retained-input alpha-only overlays through original XDK/LLE.

Closed profile: triangle lists, constant diffuse, uniform opaque white ARGB8
mips, MODULATE alpha, LESS depth without depth writes, alpha-only color mask,
ONE/ONE add or reverse-subtract blending, disabled fog/stencil. RGB and the entire depth/stencil target must remain
exact. Expected coverage/depth/alpha never use a native or LLE after-image.
"""
import argparse
from fractions import Fraction
import json
import math
from pathlib import Path
import struct

from native_scene_reference import read_capture,read_scene
from native_scene_shader_reference import audit as shader_audit,expectation
from native_scene_composed_reference import number
from native_scene_pixel_reference import binary32,plane,software_depth,words


def expected_state(capture,scene):
    r,t=capture['render'],capture['texture']
    assert capture['header'][2]==5 and capture['header'][4]%3==0
    assert scene['header'][3:6]==capture['header'][2:5] and not scene['header'][8]
    width,height,fmt=scene['before'][0]['shape']
    assert fmt==87 and scene['before'][1]['shape']==[width,height,44]
    assert capture['viewport']==[0,0,width,height,0,0x3f800000]
    assert scene['header'][9]==0x4b7fffff
    constants=list(struct.iter_unpack('<4f',capture['constants']))
    assert list(constants[58])==[width/2,-height/2,16777215,0]
    assert list(constants[59])==[width/2+17/32,height/2+17/32,0,0]
    required={57:0x201,59:1,60:0,62:1,63:1,64:0,65:0,66:0x1d00,
        67:0x1000000,77:0,78:0,79:0,80:0,81:0,92:0,
        102:0,103:1,139:0x1b02,140:0x1b02,141:0,143:1,144:0,
        146:0x900,149:0,150:0,151:0,154:0,163:0,164:0,165:0}
    assert all(r[k]==v for k,v in required.items()),'Unsupported alpha overlay state'
    assert r[74] in (0x8006,0x800b) and r[147] in (0x900,0x901)
    assert r[159] in (0,1) and scene['packed']==[0,0xff000000,0,0x39000000]+[0]*12
    # Published NV097 method numbers; direct scalar API values are unchanged.
    direct={0x300:60,0x304:59,0x30c:143,0x310:65,0x314:102,0x32c:144,
        0x330:79,0x334:80,0x338:81,0x33c:58,0x340:61,0x344:62,0x348:63,
        0x34c:75,0x350:74,0x354:57,0x358:67,0x35c:64,0x360:73,0x364:70,
        0x368:71,0x36c:72,0x370:145,0x374:68,0x378:69,0x37c:66,
        0x38c:139,0x390:140,0x3a0:146,0x3b8:103,0x2a4:92}
    m={method:r[state] for method,state in direct.items()}
    m.update({0x308:1,0x39c:0x404 if r[147]==r[146] else 0x405,0x200:width<<16,0x204:height<<16,
        0x208:0x128,0x20c:width*4*0x10001,0x210:0x600000,0x214:0x800000,
        0x2c0:(width-1)<<16,0x2e0:(height-1)<<16,0x394:0,0x398:scene['header'][9],
        0x318:0,0x31c:0,0x320:0,0x324:0,0x328:0,0x380:0})
    assert scene['textures'][0] and not any(scene['textures'][1:])
    texture=scene['textures'][0];fmt=texture['resource'][3]
    assert (fmt>>8)&255==6 and fmt&3==1 and (fmt>>4)&15==2 and not fmt&4
    assert len(texture['mips'])==(fmt>>16)&15 and texture['resource'][4]==0
    assert texture['cpu']==b''.join(mip['data'] for mip in texture['mips'])
    for level,mip in enumerate(texture['mips']):
        assert mip['shape'][:2]==[max(1,(1<<((fmt>>20)&15))>>level),max(1,(1<<((fmt>>24)&15))>>level)]
        assert len(mip['data'])==mip['shape'][0]*mip['shape'][1]*4
        assert all(byte==255 for byte in mip['data']),'Nonuniform texture needs its own sampler contract'
    for stage in range(4):
        ts=t[stage*32:(stage+1)*32];base=0x1b00+stage*64
        assert ts[:3]==[1,1,1] and ts[3:5]==[2,2] and ts[5] in (0,2)
        assert ts[7:12]==[0,1,0,0,0] and ts[21]==0 and ts[28]==0
        if stage==0:assert [ts[i] for i in (12,14,15,16,18,19,20)]==[4,2,0,4,2,0,1]
        else:assert ts[12]==1
        bias=number(ts[6]);assert abs(bias)<16
        packed=max(-4096,min(4095,math.trunc(binary32(bias*256+Fraction(1,2)))))&8191
        m[base+8]=ts[0]|ts[1]<<8|ts[2]<<16
        m[base+12]=int(stage==0)<<30|4095<<6
        m[base+20]=(ts[3]<<24)|((6 if ts[5]==2 else ts[4])<<16)|(1<<13)|packed
    # Alpha AB=TEXTURE.a * DIFFUSE.a writes R0.a; final G=R0.a. RGB is masked.
    m.update({0x1b00:0x3000000,0x1b04:fmt,0x260:(8|16)<<24|(4|16)<<16,
        0x1e40:12<<8,0x28c:(12|16)<<8|128,0x1e60:1,0x1e70:1})
    return m


def projective_triangles(vertices,width,height):
    """Exact screen-plane intersections, independent of the runtime clipper.

    Window Z and inverse W are affine on the projected edge. Intersecting the
    screen edge therefore preserves the authored Z plane; interpolating Z
    with a homogeneous edge parameter would not. Round the divided clipped
    vertex to binary32 before the separate four-bit raster setup.
    """
    for start in range(0,len(vertices),3):
        polygon=[[Fraction(v[i]) for i in range(3)]+[1/Fraction(v[3])]
            for v in vertices[start:start+3]]
        for axis,limit,sign in ((0,0,1),(0,width,-1),(1,0,1),(1,height,-1)):
            if not polygon:break
            output=[];previous=polygon[-1];distance=sign*(previous[axis]-limit)
            for current in polygon:
                d=sign*(current[axis]-limit)
                if (d>=0)!=(distance>=0):
                    t=distance/(distance-d)
                    output.append([a+(b-a)*t for a,b in zip(previous,current)])
                if d>=0:output.append(current)
                previous=current;distance=d
            polygon=output
        polygon=[[binary32(v[0]),binary32(v[1]),binary32(v[2]),binary32(1/v[3])] for v in polygon]
        for index in range(1,len(polygon)-1):yield [polygon[0],polygon[index],polygon[index+1]]


def expected_targets(capture,scene,arithmetic):
    m=expected_state(capture,scene);vertices,fog,_=expectation(capture)
    assert all(v[3]>0 and 0<=v[2]<=16777215 and v[2].is_integer() for v in vertices)
    alpha=vertices[0][7]
    assert 0<=alpha<=1 and all(v[7]==alpha and v[15]==1 for v in vertices),'Varying alpha/Q needs a separate profile'
    width,height,_=scene['before'][0]['shape'];color=words(scene['before'][0]);depth=words(scene['before'][1],True)
    # Independent exact binary32 proof of the byte-domain additive operation
    # for every possible target alpha, before reading any observed output.
    alpha_updates=[]
    for destination in range(256):
        source=Fraction(alpha)*(1 if capture['render'][74]==0x8006 else -1)
        summed=max(Fraction(0),min(Fraction(1),binary32(source+binary32(Fraction(destination,255)))))
        alpha_updates.append(math.floor(binary32(summed*255)+Fraction(1,2)))
    current_alpha=[word>>24 for word in color]
    written=set();covered=set();triangles=0;rejected=passed=0
    for vv in projective_triangles(vertices,width,height):
        pts=[(math.floor(v[0]*16+Fraction(1,2)),math.floor(v[1]*16+Fraction(1,2))) for v in vv]
        (ax,ay),(bx,by),(cx,cy)=pts;det=(bx-ax)*(cy-ay)-(cx-ax)*(by-ay)
        if not det or (det>0)==(m[0x39c]==0x404):continue
        if det<0:vv[1],vv[2]=vv[2],vv[1];pts[1],pts[2]=pts[2],pts[1]
        area=abs(det);triangles+=1
        bases=[plane(pts,[int(i==k) for i in range(3)]) for k in range(3)]
        xmin=max(0,math.ceil((min(p[0] for p in pts)-8)/16));xmax=min(width-1,math.floor((max(p[0] for p in pts)-8)/16))
        ymin=max(0,math.ceil((min(p[1] for p in pts)-8)/16));ymax=min(height-1,math.floor((max(p[1] for p in pts)-8)/16))
        for y in range(ymin,ymax+1):
            for x in range(xmin,xmax+1):
                px,py=x*16+8,y*16+8;inside=True
                for i in range(3):
                    x0,y0=pts[i];x1,y1=pts[(i+1)%3];cross=(x1-x0)*(py-y0)-(y1-y0)*(px-x0)
                    if cross<0 or (cross==0 and not (y1<y0 or (y1==y0 and x1>x0))):inside=False;break
                if not inside:continue
                index=y*width+x;covered.add(index)
                weights=[(p[0]+p[1]*px+p[2]*py)*area for p in bases]
                z=software_depth(weights,area,[v[2] for v in vv],arithmetic)
                if z>=depth[index]>>8:rejected+=1;continue
                written.add(index);passed+=1;current_alpha[index]=alpha_updates[current_alpha[index]]
    alpha_byte=math.floor(binary32(Fraction(alpha)*255)+Fraction(1,2))
    expected=[(word&0xffffff)|(current_alpha[i]<<24) for i,word in enumerate(color)]
    return expected,depth,dict(triangles=triangles,covered_pixels=len(covered),expected_written_pixels=len(written),
        rejected_depth_fragments=rejected,passed_fragments=passed,source_alpha_byte=alpha_byte,
        target_pixels_checked=len(color),untouched_pixels_checked=len(color)-len(written),
        blend_equation='add' if capture['render'][74]==0x8006 else 'reverse-subtract')


def audit(captures,diagnostics,number,arithmetic,chain=False,previous=None):
    c=read_capture(captures/f'draw-{number}.bin');s=read_scene(captures/f'scene-{number}.bin')
    meta=json.loads((diagnostics/f'draw-{number}.aot-private.json').read_text())
    assert meta['scene_capture_sha256']==s['sha256'] and meta['noise_disabled_fixture']
    assert meta['retained_targets']==chain
    if chain:
        before={kind:(diagnostics/f'draw-{number}-before-{kind}.bin').read_bytes() for kind in ('color','depth')}
        root={'color':s['before'][0]['data'],'depth':b''.join(struct.pack('<I',v) for v in words(s['before'][1],True))}
        assert all(before[k]==(previous or root)[k] for k in before),'Broken LLE retained-target sequence'
        s['before'][0]['data']=before['color']
        s['before'][1]['data']=b''.join(struct.pack('<I',((v>>8)&0xffffff)|((v&255)<<24)) for (v,) in struct.iter_unpack('<I',before['depth']))
    expected=expected_state(c,s);shader=shader_audit(captures,diagnostics,number)
    color,depth,stats=expected_targets(c,s,arithmetic)
    actual={int(k,16):v for k,v in meta['methods'].items()}
    state_failures=[[hex(k),actual.get(k),v] for k,v in expected.items() if actual.get(k)!=v]
    failures=[]
    for kind,want in (('color',color),('depth',depth)):
        data=(diagnostics/f'draw-{number}-{kind}.bin').read_bytes();assert len(data)==len(want)*4
        for index,((got,),value) in enumerate(zip(struct.iter_unpack('<I',data),want)):
            if got!=value:failures.append([kind,index,hex(got),hex(value)])
    return dict(draw=number,**stats,shader_compared_lanes=shader['compared_lanes'],
        shader_failure_count=shader['failure_count'],state_methods_compared=len(expected),retained_targets=chain,
        state_failure_count=len(state_failures),state_first_failures=state_failures[:12],
        failure_count=len(failures),first_failures=failures[:12],
        alpha_overlay_qualified=not failures and not state_failures and shader['shader_input_to_output_qualified'],
        complete_scene_pixel_qualified=False,native_alpha_overlay_qualified=False,
        scope='Constant-alpha uniform-white triangle-list overlay; exact software coverage/LESS visibility, alpha, RGB and depth/stencil preservation')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--draws',type=int,nargs='+',default=list(range(1,33)))
    parser.add_argument('--depth-arithmetic',required=True,choices=('separate','fma-b-first'))
    parser.add_argument('--retain-targets',action='store_true')
    args=parser.parse_args();rows=[];previous=None
    for n in args.draws:
        row=audit(args.captures,args.diagnostics,n,args.depth_arithmetic,args.retain_targets,previous);rows.append(row);print(json.dumps(row),flush=True)
        if args.retain_targets:assert row['alpha_overlay_qualified'],'Failed prefix cannot qualify later chained draws'
        previous={kind:(args.diagnostics/f'draw-{n}-{kind}.bin').read_bytes() for kind in ('color','depth')}
    args.output.write_text(json.dumps(dict(draws=rows,retained_targets=args.retain_targets),indent=2)+'\n')
    assert sum(row['expected_written_pixels'] for row in rows),'All-noop corpus cannot qualify alpha overlay pixels'
    raise SystemExit(0 if rows and all(r['alpha_overlay_qualified'] for r in rows) else 1)
