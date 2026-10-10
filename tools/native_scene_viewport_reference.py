"""Input-only homogeneous viewport clipping for the closed BC1 producer model.

Plane intersections and perspective division follow D3D11.3 sections 15.4,
15.5 and 15.6. Four-bit snapping and numeric arithmetic are explicit software
contracts, not NV2A silicon precision claims. The retained -march=native DLL
uses binary32 reconstruction/division and rounded differences followed by FMA
for clipping interpolation; that graph is declared from disassembly before
pixel comparisons. Original/observed renderer vertices and after-images never
supply expected clipped vertices. Positive W and in-range Z only; near-W and
depth-plane clipping retain separate contracts.
"""
import copy
from fractions import Fraction
import math

from native_frame_inputs import require
from native_scene_pixel_reference import binary32
from native_scene_producer_reference import expected_draw

SOURCE = 'https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm'


def rounded(value):
    return binary32(Fraction(value))


def lerp(a,b,t):
    """Declared RN difference then RN fused multiply/add, with rational ties."""
    return rounded(Fraction(a)+rounded(Fraction(b)-Fraction(a))*Fraction(t))


def homogeneous(vertex,width,height):
    v = [Fraction(x) for x in vertex]
    require(len(v) == 28 and v[3] >= Fraction(1,1000) and
            0 <= v[2] <= 16777215, 'Near-W/depth clipping outside viewport contract')
    require(width > 0 and height > 0, 'Invalid viewport')
    coordinates = []
    for lane,extent in ((0,width),(1,height)):
        normalized = rounded(rounded(2*v[lane])/extent)
        coordinates.append(rounded(rounded(normalized-1)*v[3]))
    return coordinates+[rounded(v[2]*v[3]),v[3]]+v[4:]


def plane_distance(vertex,lane,side):
    require(lane in (0,1) and side in (-1,1), 'Invalid XY clip plane')
    return rounded(vertex[3]+side*vertex[lane])


def clip_polygon(vertices,lane,side):
    if not vertices:return []
    output = []
    prev = vertices[-1]
    previous = plane_distance(prev,lane,side)
    for cur in vertices:
        current = plane_distance(cur,lane,side)
        if (previous >= 0) != (current >= 0):
            denominator = rounded(previous-current)
            require(denominator != 0, 'Undefined clipping intersection')
            t = rounded(previous/denominator)
            require(0 <= t <= 1, 'Intersection outside segment')
            output.append([lerp(a,b,t) for a,b in zip(prev,cur)])
        if current >= 0:output.append(cur)
        prev,previous = cur,current
    return output


def snap(value):
    scaled = Fraction(value)*16
    return Fraction((-1 if scaled < 0 else 1)*math.floor(abs(scaled)+Fraction(1,2)),16)


def project(vertex,width,height):
    require(vertex[3] > 0, 'Nonpositive clipped W')
    result = list(vertex)
    for lane,extent in ((0,width),(1,height)):
        xy = rounded(rounded(rounded(vertex[lane]/vertex[3])+1)*extent)
        result[lane] = snap(rounded(xy*Fraction(1,2)))
    result[2] = rounded(vertex[2]/vertex[3])
    return [float(x) for x in result]


def clipped_triangles(vertices,width,height,clip_arithmetic):
    require(clip_arithmetic == 'fma', 'Declare the disassembly-proven clipping arithmetic')
    require(len(vertices) >= 3 and all(len(v) == 28 and all(math.isfinite(x) for x in v) for v in vertices),
            'Incomplete finite producer geometry')
    triangles = [];clipped = 0;generated = 0
    for j in range(len(vertices)-2):
        indices = [j,j+1,j+2] if j%2 == 0 else [j+1,j,j+2]
        original = [vertices[i] for i in indices]
        require(all(v[3] >= .001 and 0 <= v[2] <= 16777215 for v in original),
                'Near-W/depth clipping outside viewport contract')
        if all(0 <= v[0] <= width and 0 <= v[1] <= height for v in original):
            triangles.append(original)
            continue
        polygon = [homogeneous(v,width,height) for v in original]
        # The retained software tolerance is only an early-out guard. Require
        # the numeric clip classification to be safely beyond that guard.
        outside = [max(abs(p[0])-p[3],abs(p[1])-p[3]) for p in polygon]
        require(max(outside) > Fraction(1,10000), 'Near guard-band boundary needs a separate contract')
        clipped += 1
        for lane,side in ((0,1),(0,-1),(1,1),(1,-1)):
            polygon = clip_polygon(polygon,lane,side)
        projected = [project(v,width,height) for v in polygon]
        generated += len(projected)
        for k in range(1,len(projected)-1):
            triangles.append([projected[0],projected[k],projected[k+1]])
    return triangles,dict(source_triangles=len(vertices)-2,clipped_source_triangles=clipped,
                          generated_clipped_vertices=generated,expanded_triangles=len(triangles))


def expected_viewport_draw(capture,scene,vertices,fog,targets,before,depth_arithmetic,clip_arithmetic):
    triangles,stats = clipped_triangles(vertices,*scene['before'][0]['shape'][:2],clip_arithmetic)
    storage = dict(before);changed = set();covered = tested = rejected = counted = 0;error = 0
    for triangle in triangles:
        # Independent decomposition is an equation input, not a substitute
        # original submission. The original draw's whole shader/state contract
        # is still checked separately against its unmodified input and output.
        part_capture,part_scene = copy.deepcopy((capture,scene))
        part_capture['header'][4] = part_scene['header'][5] = 3
        storage,writes,row = expected_draw(part_capture,part_scene,triangle,[0.,0.,0.],
                                          targets,storage,depth_arithmetic)
        changed.update(writes)
        covered += row['covered_pixels'];tested += row['tested_fragments']
        rejected += row['rejected_depth_fragments'];counted += row['triangles']
        error = max(error,row['maximum_affine_depth_error'])
    return storage,changed,dict(**stats,triangles=counted,tested_fragments=tested,
                               rejected_depth_fragments=rejected,maximum_affine_depth_error=error,
                               covered_pixels=covered,expected_written_pixels=len(changed))
