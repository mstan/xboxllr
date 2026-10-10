"""Independent pre-clip primary-color clamp and input-bound BC1 RGB equations.

ARB_vertex_program's primitive assembly order applies color clamping before
attribute clipping (section 2.14.4.4). This is the declared published-pipeline
contract; an agreeing software result does not qualify NV2A silicon ordering.
MSB-expanded endpoint/nearest-third/floored-half codec choices are declared
software facts. Exact input-selected texel errors must be below one code in
every channel, so convex filtering and clamped diffuse keep the old one-code
RGB interval. Unsupported textures fail; no observed images supply bounds.
"""
import copy
from fractions import Fraction
import math
import struct

import numpy as np

from native_frame_inputs import require
from native_frame_state_delivery import expected_delivery
from native_scene_diffuse_reference import consumed_profile,promote
from native_scene_pixel_reference import bc1_images,plane,sample
from native_scene_producer_reference import expected_draw
from native_scene_viewport_reference import clipped_triangles

CLAMP_SOURCE = 'https://registry.khronos.org/OpenGL/extensions/ARB/ARB_vertex_program.txt'
CLAMP_ORDER = 'vertex-primary-clamp-before-attribute-clipping'


def clamp_vertices(vertices):
    require(all(len(v) == 28 and all(math.isfinite(x) for x in v) and
                v[7] >= 1 and v[8:12] == [0,0,0,0] for v in vertices),
            'Unmodelled finite primary/alpha/secondary profile')
    result = [list(v) for v in vertices]
    for v in result:
        v[4:8] = [min(1,max(0,x)) for x in v[4:8]]
    return result


def texture_error_bound(texture):
    """Exact encoded-input-selected palette error, without maintained decoding."""
    require((texture['resource'][3] >> 8)&255 == 12 and
            texture['cpu'] == b''.join(m['data'] for m in texture['mips']),
            'Not complete supplied BC1 storage')
    worst = [Fraction(0)]*3
    for mip in texture['mips']:
        width,height = mip['shape'][:2]
        require(width > 0 and height > 0 and
                len(mip['data']) == ((width+3)//4)*((height+3)//4)*8,
                'Incomplete tightly packed BC1 mip')
        for e0,e1,selectors in struct.iter_unpack('<HHI',mip['data']):
            components = lambda e:[(e >> shift)&((1 << bits)-1) for shift,bits in ((11,5),(5,6),(0,5))]
            a,b = components(e0),components(e1)
            ai = [Fraction(255*v,(1 << bits)-1) for v,bits in zip(a,(5,6,5))]
            bi = [Fraction(255*v,(1 << bits)-1) for v,bits in zip(b,(5,6,5))]
            ap = [promote(v,bits) for v,bits in zip(a,(5,6,5))]
            bp = [promote(v,bits) for v,bits in zip(b,(5,6,5))]
            ideal = [ai,bi,[(2*x+y)/3 if e0 > e1 else (x+y)/2 for x,y in zip(ai,bi)],
                     [(x+2*y)/3 if e0 > e1 else Fraction(0) for x,y in zip(ai,bi)]]
            encoded = [ap,bp,[(2*x+y+1)//3 if e0 > e1 else (x+y)//2 for x,y in zip(ap,bp)],
                       [(x+2*y+1)//3 if e0 > e1 else 0 for x,y in zip(ap,bp)]]
            for selector in {(selectors >> (2*i))&3 for i in range(16)}:
                require(e0 > e1 or selector != 3, 'Transparent BC1 outside saturated-alpha profile')
                worst = [max(m,abs(i-j)) for m,i,j in zip(worst,ideal[selector],encoded[selector])]
    return worst


def precision_domain(errors,vertices):
    """Vertex maxima bound all perspective/clip convex primary interpolations."""
    require(len(errors) == 3 and vertices, 'Incomplete precision inputs')
    maxima = [max(v[c] for v in vertices) for c in (4,5,6)]
    bounds = [e*Fraction(d) for e,d in zip(errors,maxima)]
    require(all(x < 1 for x in bounds), 'Input palette/diffuse cannot prove one-code bound')
    return bounds


def triangle_colors(vertices,images,methods,width,written,metrics):
    """Perspective primary/UV planes, gradients and the guarded MODULATE equation."""
    require(len(vertices) == 3 and all(0 <= v[c] <= 1 for v in vertices for c in (4,5,6)),
            'Expected clamped finite triangle')
    vv = list(vertices);points = [(int(v[0]*16),int(v[1]*16)) for v in vv]
    (ax,ay),(bx,by),(cx,cy) = points
    det = (bx-ax)*(cy-ay)-(cx-ax)*(by-ay)
    if det < 0:vv[1],vv[2] = vv[2],vv[1];points[1],points[2] = points[2],points[1]
    require(det or not written, 'Degenerate triangle wrote a pixel')
    if not written:return {}
    bases = [plane(points,[int(i == k) for i in range(3)]) for k in range(3)]
    reciprocal_w = [1/v[3] for v in vv]
    filter_word,control = methods[0x1b14],methods[0x1b0c]
    bias = filter_word&8191;bias = (bias-8192 if bias&4096 else bias)/256
    low,high = ((control >> 18)&4095)/256,((control >> 6)&4095)/256
    colors = {}
    for index in written:
        px,py = (index%width)*16+8,(index//width)*16+8
        lambdas = [p[0]+p[1]*px+p[2]*py for p in bases]
        require(all(l >= 0 for l in lambdas), 'Written site outside expected triangle')
        weights = [float(l)*q for l,q in zip(lambdas,reciprocal_w)];total = sum(weights)
        uv = [sum(w*v[c] for w,v in zip(weights,vv))/total for c in (12,13)]
        gradients = []
        for axis in (1,2):
            dw = [float(p[axis])*16*q for p,q in zip(bases,reciprocal_w)]
            gradients.append([(sum(w*v[c] for w,v in zip(dw,vv))-t*sum(dw))/total*extent
                              for c,t,extent in zip((12,13),uv,images[0][:2])])
        rho = max(math.hypot(*g) for g in gradients)
        lod = max(low,min(high,math.log2(rho)+bias if rho else -1000))
        texel = sample(images,*uv,lod)
        diffuse = [sum(w*v[c] for w,v in zip(weights,vv))/total for c in (4,5,6)]
        metrics['maximum_consumed_diffuse'] = max(metrics['maximum_consumed_diffuse'],max(diffuse))
        # Final color-sum and final-output clamps are identity for T,V in [0,1].
        colors[index] = [math.floor(min(255,max(0,t*d))+.5) for t,d in zip(texel,diffuse)]
    return colors


def expected_clamp_draw(capture,scene,vertices,fog,targets,before,depth_arithmetic,clip_arithmetic):
    clamped = clamp_vertices(vertices)
    errors = texture_error_bound(scene['textures'][0])
    bounds = precision_domain(errors,clamped)
    methods,fixture,pixel = expected_delivery(capture,scene)
    methods = {**methods,**fixture,**pixel};consumed_profile(methods)
    triangles,stats = clipped_triangles(clamped,*scene['before'][0]['shape'][:2],clip_arithmetic)
    images = bc1_images(scene['textures'][0])
    storage = dict(before);changed = set();covered = tested = rejected = counted = 0;error = 0
    metrics = {'maximum_consumed_diffuse':0}
    for triangle in triangles:
        part_capture,part_scene = copy.deepcopy((capture,scene))
        part_capture['header'][4] = part_scene['header'][5] = 3
        half = [list(v) for v in triangle]
        for v in half:
            for lane in (4,5,6):v[lane] /= 2
        storage,writes,row = expected_draw(part_capture,part_scene,half,[0.,0.,0.],
                                          targets,storage,depth_arithmetic)
        color_id = scene['frame_resource_plan']['target_ids'][0]
        color = np.frombuffer(storage[color_id],dtype='<u4').copy()
        for index,rgb in triangle_colors(triangle,images,methods,scene['before'][0]['shape'][0],writes,metrics).items():
            color[index] = (int(color[index])&0xff000000) | sum(v << s for v,s in zip(rgb,(16,8,0)))
        storage[color_id] = color.tobytes()
        changed.update(writes);covered += row['covered_pixels'];tested += row['tested_fragments']
        rejected += row['rejected_depth_fragments'];counted += row['triangles']
        error = max(error,row['maximum_affine_depth_error'])
    return storage,changed,dict(**stats,triangles=counted,tested_fragments=tested,
                               rejected_depth_fragments=rejected,maximum_affine_depth_error=error,
                               covered_pixels=covered,expected_written_pixels=len(changed),**metrics,
                               clamp_order=CLAMP_ORDER,input_codec_error_codes=list(map(str,errors)),
                               input_prequantization_codec_error_codes=list(map(str,bounds)),
                               clamped_input_components=sum(a[c] != b[c] for a,b in zip(vertices,clamped) for c in (4,5,6)),
                               rgb_channel_bound=1)
