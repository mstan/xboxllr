#!/usr/bin/env python3
"""Independent native scene raster diagnostics after shader interval admission.

Eight-bit nearest/even screen setup and exact top-left coverage come from the
D3D11 host contract, not the software floor's four-bit grid. Production shader
observations are admitted only after independent captured-input bounds pass.
This closes a bounded noise-off native contract, not an exact NV2A golden.
"""
from fractions import Fraction
import math
import struct

from native_scene_composed_reference import expected_state
from native_scene_pixel_reference import plane,words,bc1_images,sample
from native_scene_host_precision import qualify

SPEC='https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm'
UNIT=2**-24
GAMMA8=8*UNIT/(1-8*UNIT)


def bc1_budgets(texture):
    """Per-texel/channel host BC1 bounds, including endpoint promotion.

    Section 19.5.2 gives absolute error 1 byte plus .03*endpoint span;
    section 19.5.6 defines 565 endpoint promotion and palette interpolation.
    Exact zero/one entries stay exact. These are not empirical allowances.
    """
    images=[]
    for mip in texture['mips']:
        width,height=mip['shape'][:2];pixels=[None]*(width*height)
        for block,(e0,e1,selectors) in enumerate(struct.iter_unpack('<HHI',mip['data'])):
            def endpoint(word):
                ideal=[];promoted=[]
                for shift,bits in ((11,5),(5,6),(0,5)):
                    value=(word>>shift)&((1<<bits)-1)
                    ideal.append(255*value/((1<<bits)-1))
                    promoted.append((value<<(8-bits))|(value>>(2*bits-8)))
                return ideal,promoted
            a,ap=endpoint(e0);b,bp=endpoint(e1)
            coefficients=(1,0,2/3,1/3) if e0>e1 else (1,0,.5,None)
            palette=[]
            for mix in coefficients:
                if mix is None:palette.append(None);continue
                values=[mix*x+(1-mix)*y for x,y in zip(a,b)]
                promoted=[mix*x+(1-mix)*y for x,y in zip(ap,bp)]
                palette.append([0 if value in (0,255) else abs(p-value)+1+.03*max(abs(x-y),abs(xp-yp))
                    for value,p,x,y,xp,yp in zip(values,promoted,a,b,ap,bp)])
            bx=block%((width+3)//4);by=block//((width+3)//4)
            for i in range(16):
                x=bx*4+i%4;y=by*4+i//4
                if x<width and y<height:
                    budget=palette[(selectors>>(2*i))&3];assert budget is not None
                    pixels[y*width+x]=budget
        assert all(p is not None for p in pixels);images.append((width,height,pixels))
    return images


def predict(capture,scene,raster_data):
    expected_state(capture,scene)
    assert capture['texture'][5]==2 and capture['texture'][7]==0,'Closed native fixture requires trilinear mips with base level zero'
    shader,actual=qualify(capture,raster_data,True)
    assert shader['shader_qualified'],shader
    width,height,_=scene['before'][0]['shape']
    rows=[]
    for row in actual:
        row=list(map(Fraction,row));w=row[3];assert w>0
        assert row[39]==1 and row[15]==1 and row[16:20]==[0,0,0,0] and row[20]==1,'Unsupported projected-Q/specular/alpha/fog profile'
        x=(row[0]/w+1)*width/2;y=(1-row[1]/w)*height/2
        rows.append(dict(p=(round(x*256),round(y*256)),z=row[2]/w,w=w,
            diffuse=row[12:15],uv=row[36:38],clip=row[:4],
            inside=0<=x<=width and 0<=y<=height and 0<=row[2]/w<=1))
    before_color=words(scene['before'][0]);before_depth=words(scene['before'][1],True)
    images=bc1_images(scene['textures'][0]);budgets=bc1_budgets(scene['textures'][0])
    bias=struct.unpack('<f',struct.pack('<I',capture['texture'][6]))[0]
    covered=set();fragments={};colors={};depths={};triangle_count=clipped_triangles=0
    for j in range(len(rows)-2):
        indices=[j,j+1,j+2] if j%2==0 else [j+1,j,j+2]
        vv=[rows[i] for i in indices];pts=[v['p'] for v in vv]
        # A primitive wholly beyond a single clip plane cannot touch the
        # viewport, including with an implementation's wider guard band.
        outside=[lambda v:v[0]<-v[3],lambda v:v[0]>v[3],
            lambda v:v[1]<-v[3],lambda v:v[1]>v[3],lambda v:v[2]<0,lambda v:v[2]>v[3]]
        if any(all(test(v['clip']) for v in vv) for test in outside):clipped_triangles+=1;continue
        assert all(v['inside'] for v in vv),'Partial clipping needs a separate contract'
        (ax,ay),(bx,by),(cx,cy)=pts;det=(bx-ax)*(cy-ay)-(cx-ax)*(by-ay)
        cull_front=capture['render'][147]==capture['render'][146]
        if not det or (det>0)==cull_front:continue
        if det<0:vv[1],vv[2]=vv[2],vv[1];pts[1],pts[2]=pts[2],pts[1]
        triangle_count+=1;bases=[plane(pts,[int(i==k) for i in range(3)]) for k in range(3)]
        zplane=plane(pts,[v['z']*16777215 for v in vv])
        def field(x,y):
            weights=[float(p[0]+p[1]*x+p[2]*y)/float(v['w']) for p,v in zip(bases,vv)]
            denominator=sum(weights)
            return [sum(q*float(v['uv'][c]) for q,v in zip(weights,vv))/denominator for c in range(2)]
        xmin=max(0,math.ceil((min(p[0] for p in pts)-128)/256));xmax=min(width-1,math.floor((max(p[0] for p in pts)-128)/256))
        ymin=max(0,math.ceil((min(p[1] for p in pts)-128)/256));ymax=min(height-1,math.floor((max(p[1] for p in pts)-128)/256))
        for y in range(ymin,ymax+1):
            for x in range(xmin,xmax+1):
                px,py=x*256+128,y*256+128;inside=True
                for i in range(3):
                    x0,y0=pts[i];x1,y1=pts[(i+1)%3];cross=(x1-x0)*(py-y0)-(y1-y0)*(px-x0)
                    if cross<0 or (cross==0 and not (y1<y0 or (y1==y0 and x1>x0))):inside=False;break
                if not inside:continue
                index=y*width+x;covered.add(index);ideal=zplane[0]+zplane[1]*px+zplane[2]*py
                fragments.setdefault(index,[]).append((j,float(ideal)))
                z=round(ideal)
                if z>=depths.get(index,before_depth[index]>>8):continue
                uv=field(px,py)
                # All four mathematically possible coarse derivative pairings
                # on the aligned 2x2 quad are retained as diagnostics.
                qx=(x&~1)*256+128;qy=(y&~1)*256+128
                quad=[field(qx+dx*256,qy+dy*256) for dy in (0,1) for dx in (0,1)]
                dxs=[[ (quad[2*r+1][c]-quad[2*r][c])*images[0][c] for c in range(2)] for r in (0,1)]
                dys=[[ (quad[c+2][lane]-quad[c][lane])*images[0][lane] for lane in range(2)] for c in (0,1)]
                lods=[max(0,min(len(images)-1,math.log2(max(math.hypot(*dx),math.hypot(*dy)))+bias)) for dx in dxs for dy in dys]
                weights=[float(p[0]+p[1]*px+p[2]*py)/float(v['w']) for p,v in zip(bases,vv)]
                diffuse=[sum(q*float(v['diffuse'][c]) for q,v in zip(weights,vv))/sum(weights) for c in range(3)]
                assert all(0<=d<=.5 for d in diffuse),'Closed fixture requires bounded diffuse'
                rgb=[]
                for lod in lods:
                    texel=sample(images,*uv,lod);decode=sample(budgets,*uv,lod)
                    # The independent acceptance model permits eight binary32
                    # roundings in affine/quotient interpolation. Eight-bit
                    # subtexel and mip weights each move <=1/512; the maximum
                    # byte-range slope gives 255/512 per U, V and mip axis.
                    # Three UNORM8 filtering roundings cost <=3/2 byte steps;
                    # final FLOAT->UNORM conversion permits .6 integer steps.
                    # All costs are established before reading target outputs.
                    position_cost=255*GAMMA8*(abs(uv[0])*images[0][0]+abs(uv[1])*images[0][1])
                    filter_cost=3*255/512+1.5+position_cost
                    rgb.append([(t*d-(limit+filter_cost)*d-.6,t*d+(limit+filter_cost)*d+.6)
                        for t,limit,d in zip(texel,decode,diffuse)])
                colors[index]=[(max(0,min(v[c][0] for v in rgb)),min(255,max(v[c][1] for v in rgb))) for c in range(3)]
                depths[index]=z
    return before_color,before_depth,covered,fragments,colors,depths,dict(triangles=triangle_count,
        wholly_clipped_triangles=clipped_triangles,shader=shader)


def audit(capture,scene,raster_data,color_data,depth_data):
    before_color,before_depth,covered,fragments,colors,depths,stats=predict(capture,scene,raster_data)
    assert len(color_data)==len(depth_data)==len(before_color)*4,'Incomplete target observation'
    actual_color=[v[0] for v in struct.iter_unpack('<I',color_data)]
    actual_depth=[v[0] for v in struct.iter_unpack('<I',depth_data)]
    failures=[];coverage_failures=0;maximum_depth=0;maximum_color_radius=0
    for index,(color,depth) in enumerate(zip(actual_color,actual_depth)):
        if index not in colors:
            if color!=before_color[index] or depth!=before_depth[index]:
                failures.append([index,'untouched']);coverage_failures+=1
            continue
        # Visibility and preservation remain exact requirements. No edge or
        # near-depth pixel is omitted from the comparison or treated as pass.
        if color==before_color[index] and depth==before_depth[index]:
            failures.append([index,'missing-write']);coverage_failures+=1
        if color>>24!=before_color[index]>>24:failures.append([index,'alpha'])
        if depth&255!=before_depth[index]&255:failures.append([index,'stencil'])
        z=depth>>8;ideal=depths[index]
        radius=GAMMA8*ideal+.6
        maximum_depth=max(maximum_depth,abs(z-ideal))
        if abs(z-ideal)>radius:failures.append([index,'depth',z,ideal,radius])
        for channel,(lo,hi),shift in zip(('R','G','B'),colors[index],(16,8,0)):
            got=color>>shift&255;maximum_color_radius=max(maximum_color_radius,(hi-lo)/2)
            if not lo<=got<=hi:failures.append([index,channel,got,lo,hi])
    return dict(**stats,target_pixels_checked=len(before_color),covered_pixels=len(covered),
        expected_written_pixels=len(colors),untouched_pixels_checked=len(before_color)-len(colors),
        maximum_depth_difference_steps=maximum_depth,maximum_color_interval_radius_steps=maximum_color_radius,
        depth_contract='Eight-rounding binary32 affine interpolation acceptance gamma(8), plus .6 UNORM integer steps; host contract, not exact silicon',
        color_contract='BC1 endpoint/palette per-channel spec budgets; 8-bit sampler weights; fixed-point filtering and final UNORM rounding',
        coverage_failures=coverage_failures,failure_count=len(failures),first_failures=failures[:16],
        native_noise_off_draw_qualified=not failures,host_spec_source=SPEC,
        original_noise_enabled_scene_qualified=False,complete_original_scene_qualified=False)
