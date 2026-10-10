"""Input-derived BC1 producer RGB through diffuse 3/4, with the same one-code bound.

The NV register equations reduce this consumed profile to texture * primary:
one general RGB/alpha MODULATE stage, saturated primary alpha, zero secondary,
and density-zero fog. Final color-sum/output clamps cannot change RGB in this
domain. Expected coverage/depth reuse the earlier independent equations with
RGB halved; those temporary RGB values are discarded, and full RGB is derived
here from the original input colors. No maintained raster/codec or after-image
supplies expectations. Exact silicon arithmetic and other profiles are guarded.
"""
import copy
from fractions import Fraction
from functools import lru_cache
import math

import numpy as np

from native_frame_inputs import require
from native_frame_state_delivery import expected_delivery
from native_scene_pixel_reference import bc1_images,plane,sample
from native_scene_producer_reference import expected_draw
from native_scene_viewport_reference import clipped_triangles

SOURCES = (
    'https://registry.khronos.org/OpenGL/extensions/NV/NV_register_combiners.txt',
    'https://registry.khronos.org/OpenGL/extensions/ARB/ARB_vertex_program.txt',
    'https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm',
)
DIFFUSE_LIMIT = Fraction(3,4)


def promote(value,bits):
    """D3D11.3 section 19.5.3 MSB-extension promotion to UNORM8."""
    require(bits in (5,6) and 0 <= value < 1 << bits, 'Invalid BC1 component')
    return (value << (8-bits)) | (value >> (2*bits-8))


@lru_cache(maxsize=1)
def codec_error_bound():
    """Exhaust all channel pairs, independently of texture or observed pixels.

    Explicit software codec choices: MSB-extension endpoints, nearest third
    palettes, floored half palettes. These are allowed by sections 19.5.2/.6;
    the proof qualifies this software contract, not all conforming hardware.
    Convex spatial/mip filtering preserves the maximum error. Multiplication
    by diffuse <=3/4 leaves 105/124 (<1) code before final UNORM rounding, so
    integer results differ by at most one code. No empirical maximum is used.
    """
    bounds = {}
    for bits in (5,6):
        ideal = [Fraction(255*x,(1 << bits)-1) for x in range(1 << bits)]
        expanded = [promote(x,bits) for x in range(1 << bits)]
        maximum = max(abs(x-y) for x,y in zip(expanded,ideal))
        for x in range(1 << bits):
            for y in range(1 << bits):
                for a,b,d,rounding in ((2,1,3,1),(1,2,3,1),(1,1,2,0)):
                    value = (a*expanded[x]+b*expanded[y]+rounding)//d
                    reference = (a*ideal[x]+b*ideal[y])/d
                    maximum = max(maximum,abs(value-reference))
        bounds[bits] = maximum
    require(max(bounds.values())*DIFFUSE_LIMIT < 1, 'One-code proof exhausted')
    return bounds


def diffuse_domain(vertices):
    require(all(len(v) == 28 and all(math.isfinite(x) for x in v) and
                all(0 <= v[c] <= DIFFUSE_LIMIT for c in (4,5,6)) and
                v[7] >= 1 and v[8:12] == [0,0,0,0] for v in vertices),
            'Diffuse/alpha/secondary profile outside derived one-code domain')


def consumed_profile(methods):
    """Guard every equation used to eliminate fog, alpha and final clamps."""
    expected = {0x1e60:1,0x1e70:1,0xac0:0x08040000,0x260:0x18140000,
                0x1e40:0xc00,0xaa0:0xc00,0x288:0x130e0300,0x28c:0x1c80,
                0x2a4:1,0x29c:0x800,0x9c0:0x3fc00000,0x9c4:0x80000000,0x9c8:0,
                0x300:0,0x310:0,0x314:0,0x358:0x10101}
    require(all(methods[k] == v for k,v in expected.items()),
            'Consumed combiner/fog/kill/blend/write-mask equation changed')
    require(all(methods[base+4*stage] == 0 for base in (0xac0,0x260,0x1e40,0xaa0)
                for stage in range(1,8)), 'Unexpected additional combiner stage')


def modulate(texel,diffuse):
    """R0=T0*V0; secondary=0, fog=1, final clamps to UNORM, mask keeps A."""
    require(len(texel) == len(diffuse) == 3 and
            all(math.isfinite(x) and 0 <= x <= 255 for x in texel) and
            all(math.isfinite(x) and 0 <= x <= DIFFUSE_LIMIT for x in diffuse),
            'Invalid consumed MODULATE inputs')
    return [math.floor(min(255,max(0,t*d))+.5) for t,d in zip(texel,diffuse)]


def triangle_rgb(vertices,images,methods,width,written,metrics=None):
    """Full unrounded diffuse and perspective texture equations at written sites."""
    diffuse_domain(vertices);consumed_profile(methods)
    require(len(vertices) == 3, 'Expected one independently clipped triangle')
    vv = list(vertices)
    points = [(int(v[0]*16),int(v[1]*16)) for v in vv]
    (ax,ay),(bx,by),(cx,cy) = points
    det = (bx-ax)*(cy-ay)-(cx-ax)*(by-ay)
    if det < 0:
        vv[1],vv[2] = vv[2],vv[1];points[1],points[2] = points[2],points[1]
    require(det or not written, 'Degenerate triangle wrote a pixel')
    if not written:return {}
    bases = [plane(points,[int(i == k) for i in range(3)]) for k in range(3)]
    reciprocal_w = [1/v[3] for v in vv]
    filter_word,control = methods[0x1b14],methods[0x1b0c]
    bias = filter_word & 8191;bias = (bias-8192 if bias & 4096 else bias)/256
    min_lod,max_lod = ((control >> 18)&4095)/256,((control >> 6)&4095)/256
    colors = {}
    for index in written:
        px,py = (index%width)*16+8,(index//width)*16+8
        lambdas = [p[0]+p[1]*px+p[2]*py for p in bases]
        require(all(l >= 0 for l in lambdas), 'Written site outside expected triangle')
        weights = [float(l)*q for l,q in zip(lambdas,reciprocal_w)]
        denominator = sum(weights)
        uv = [sum(w*v[c] for w,v in zip(weights,vv))/denominator for c in (12,13)]
        gradients = []
        for axis in (1,2):
            dw = [float(p[axis])*16*q for p,q in zip(bases,reciprocal_w)]
            gradients.append([(sum(w*v[c] for w,v in zip(dw,vv))-t*sum(dw))/denominator*extent
                              for c,t,extent in zip((12,13),uv,images[0][:2])])
        rho = max(math.hypot(*g) for g in gradients)
        lod = max(min_lod,min(max_lod,math.log2(rho)+bias if rho else -1000))
        texel = sample(images,*uv,lod)
        diffuse = [sum(w*v[c] for w,v in zip(weights,vv))/denominator for c in (4,5,6)]
        if metrics is not None:
            metrics['maximum_consumed_diffuse'] = max(metrics.get('maximum_consumed_diffuse',0),max(diffuse))
        colors[index] = modulate(texel,diffuse)
    return colors


def expected_diffuse_draw(capture,scene,vertices,fog,targets,before,depth_arithmetic,clip_arithmetic):
    diffuse_domain(vertices);codec_error_bound()
    methods,fixture,pixel = expected_delivery(capture,scene)
    methods = {**methods,**fixture,**pixel};consumed_profile(methods)
    triangles,stats = clipped_triangles(vertices,*scene['before'][0]['shape'][:2],clip_arithmetic)
    images = bc1_images(scene['textures'][0])
    storage = dict(before);changed = set();covered = tested = rejected = counted = 0;error = 0
    metrics = {'maximum_consumed_diffuse':0}
    for triangle in triangles:
        part_capture,part_scene = copy.deepcopy((capture,scene))
        part_capture['header'][4] = part_scene['header'][5] = 3
        # In this guarded equation RGB cannot affect alpha/kill/depth/coverage.
        # The original guest submission and shader/state audit stay unmodified.
        half = [list(v) for v in triangle]
        for v in half:
            for lane in (4,5,6):v[lane] /= 2
        storage,writes,row = expected_draw(part_capture,part_scene,half,[0.,0.,0.],
                                          targets,storage,depth_arithmetic)
        color_id = scene['frame_resource_plan']['target_ids'][0]
        color = np.frombuffer(storage[color_id],dtype='<u4').copy()
        colors = triangle_rgb(triangle,images,methods,scene['before'][0]['shape'][0],writes,metrics)
        for index,rgb in colors.items():
            color[index] = (int(color[index])&0xff000000) | sum(v << s for v,s in zip(rgb,(16,8,0)))
        storage[color_id] = color.tobytes()
        changed.update(writes);covered += row['covered_pixels'];tested += row['tested_fragments']
        rejected += row['rejected_depth_fragments'];counted += row['triangles']
        error = max(error,row['maximum_affine_depth_error'])
    return storage,changed,dict(**stats,triangles=counted,tested_fragments=tested,
                               rejected_depth_fragments=rejected,maximum_affine_depth_error=error,
                               covered_pixels=covered,expected_written_pixels=len(changed),
                               maximum_input_diffuse=max(v[c] for v in vertices for c in (4,5,6)),
                               **metrics,
                               diffuse_ceiling=float(DIFFUSE_LIMIT),rgb_channel_bound=1)
