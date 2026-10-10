"""Independent declared-software W/XY clipping for original mixed-W strips.

The pinned software clips W-epsilon, X+W, W-X, Y+W, W-Y in that order;
there are no geometric Z planes. Rounded distances/differences/division and
FMA intersections preserve homogeneous Z. Projection occurs before recursive
clip checks, and XY snaps only at the raster leaf. Fragment depth range is
a separate component, not a declaration of NV2A silicon depth-plane behavior.
Original near/zero-W inputs and subnormal/overflow arithmetic stay guarded.
"""
from fractions import Fraction
import math
import struct

from native_frame_inputs import require
from native_scene_pixel_reference import binary32
from native_scene_rejection_reference import W_EPSILON,retained_software_evidence,pe_bytes,expected_rejection_draw
from native_scene_clamp_reference import clamp_vertices,texture_error_bound,CLAMP_ORDER
from native_scene_diffuse_reference import consumed_profile
from native_frame_state_delivery import expected_delivery
from native_frame_target_reference import layout

CLIP_PLANES = ('W-epsilon','X+W','W-X','Y+W','W-Y')
TOLERANCE = Fraction(struct.unpack('<f',struct.pack('<f',1e-4))[0])
EXTRA_CODE_SITES = {
    'x_plus_w_distance':(0x39d294af8,'c57258c8'),
    'w_minus_x_distance':(0x39d294dd8,'c57a5cc9'),
    'y_plus_w_distance':(0x39d2950c8,'c57258c8'),
    'w_minus_y_distance':(0x39d2953b4,'c57a5cc9'),
    'rounded_distance_difference':(0x39d294847,'c4c1725ce1'),
    'rounded_intersection_division':(0x39d294859,'c5f25ed4'),
    'rounded_homogeneous_difference':(0x39d294891,'62b15c085ce0'),
    'fused_homogeneous_intersection':(0x39d294897,'62f27d0098e2'),
    'project_z_division':(0x39d295695,'c5e25eda'),
    'project_x_division':(0x39d29568d,'c5fa5ec2'),
    'project_y_division':(0x39d295691,'c5f25eca'),
    'viewport_tolerance':(0x39d2e704c,'17b7d138'),
}


def mixed_software_evidence(library):
    evidence = retained_software_evidence(library);data = library.read_bytes()
    for semantic,(address,expected) in EXTRA_CODE_SITES.items():
        actual = pe_bytes(data,address,len(bytes.fromhex(expected)))
        require(actual.hex() == expected,'Retained mixed-clip code site changed: '+semantic)
        evidence['code_sites'].append(dict(semantic=semantic,address=hex(address),bytes=actual.hex()))
    evidence.update(geometry_clip_planes=list(CLIP_PLANES),homogeneous_z_interpolated=True,
                    plane_arithmetic='binary32 distance, difference and division; FMA intersection',
                    geometric_depth_planes_present=False,projection_precedes_recursive_clip_checks=True,
                    snap_only_at_raster_leaf=True,surviving_mixed_w_pixels_qualified=False)
    return evidence


def rounded(value):
    try:return binary32(value)
    except AssertionError as exc:
        raise ValueError('Subnormal/overflow mixed-clip arithmetic requires another contract') from exc


def reconstruct(vertex,width,height):
    require(width > 0 and height > 0 and len(vertex) == 28 and
            all(math.isfinite(x) for x in vertex),'Incomplete finite mixed-clip geometry')
    v = [Fraction(x) for x in vertex]
    xy = [rounded(rounded(rounded(rounded(2*v[c])/extent)-1)*v[3])
          for c,extent in ((0,width),(1,height))]
    return xy+[rounded(v[2]*v[3]),v[3]]+v[4:]


def needs_clip(vertex):
    x,y,z,w = vertex[:4]
    lo = rounded(-w-TOLERANCE);hi = rounded(w+TOLERANCE)
    return w < W_EPSILON or x < lo or x > hi or y < lo or y > hi


def distance(vertex,plane):
    x,y,z,w = vertex[:4]
    return rounded((w-W_EPSILON,x+w,w-x,y+w,w-y)[plane])


def clip_plane(polygon,plane):
    if not polygon:return [],0
    output = [];intersections = 0;previous = polygon[-1]
    pd = distance(previous,plane)
    for current in polygon:
        cd = distance(current,plane)
        if (pd >= 0) != (cd >= 0):
            denominator = rounded(pd-cd)
            t = Fraction(0) if denominator == 0 else rounded(pd/denominator)
            require(0 <= t <= 1,'Unproved mixed-clip intersection parameter')
            output.append([rounded(a+rounded(b-a)*t) for a,b in zip(previous,current)])
            intersections += 1
        if cd >= 0:output.append(current)
        previous,pd = current,cd
    return output,intersections


def project_unsnapped(vertex,width,height):
    require(vertex[3] > 0,'Nonpositive projected mixed-clip W')
    result = list(vertex)
    for lane,extent in ((0,width),(1,height)):
        result[lane] = rounded(rounded(rounded(rounded(vertex[lane]/vertex[3])+1)*extent)*Fraction(1,2))
    result[2] = rounded(vertex[2]/vertex[3])
    return [float(x) for x in result]


def triangle_leaves(vertices,width,height,stats,level=0):
    require(level <= 4,'Mixed clipping requires an unproved recursive pass')
    stats['maximum_clip_recursion'] = max(stats['maximum_clip_recursion'],level)
    polygon = [reconstruct(v,width,height) for v in vertices]
    if not any(needs_clip(v) for v in polygon):return [vertices]
    stats['homogeneous_clip_passes'] += 1
    for plane in range(5):
        values = [distance(v,plane) for v in polygon]
        before_count = len(polygon)
        polygon,intersections = clip_plane(polygon,plane)
        stats['homogeneous_plane_intersections'] += intersections
        stats.setdefault('clip_stage_proofs',[]).append(dict(
            source_primitive=stats.get('current_source_primitive'),recursion=level,
            plane=CLIP_PLANES[plane],before_vertices=before_count,after_vertices=len(polygon),
            intersections=intersections,minimum_distance=float(min(values)) if values else None,
            maximum_distance=float(max(values)) if values else None))
    if len(polygon) < 3:
        stats['empty_clipped_primitive_results'] += 1
        return []
    if any(v[3] <= 0 for v in polygon):
        # Actual retained software checks the complete resulting polygon
        # before constructing/dispatching its triangle fan.
        stats['post_clip_nonpositive_w_rejections'] += 1
        return []
    projected = [project_unsnapped(v,width,height) for v in polygon]
    leaves = []
    for n in range(1,len(projected)-1):
        leaves.extend(triangle_leaves([projected[0],projected[n],projected[n+1]],
                                      width,height,stats,level+1))
    return leaves


def mixed_triangles(vertices,width,height):
    require(len(vertices) >= 3 and all(len(v) == 28 and all(math.isfinite(x) for x in v)
            and abs(v[3]) >= .001 for v in vertices),'Near/zero-W original geometry remains guarded')
    require(min(v[3] for v in vertices) < 0 < max(v[3] for v in vertices),
            'This extension requires original mixed-W geometry')
    stats = dict(source_triangles=len(vertices)-2,homogeneous_clip_passes=0,
                 homogeneous_plane_intersections=0,maximum_clip_recursion=0,
                 empty_clipped_primitive_results=0,post_clip_nonpositive_w_rejections=0)
    result = []
    for j in range(len(vertices)-2):
        stats['current_source_primitive'] = j
        indices = [j,j+1,j+2] if j%2 == 0 else [j+1,j,j+2]
        result.extend(triangle_leaves([vertices[i] for i in indices],width,height,stats))
    stats.pop('current_source_primitive',None)
    stats['expanded_triangles'] = len(result)
    return result,stats


def expected_empty_mixed_draw(capture,scene,vertices,fog,targets,before,
                              depth_arithmetic,clip_arithmetic):
    if not (min(v[3] for v in vertices) < 0 < max(v[3] for v in vertices)):
        return expected_rejection_draw(capture,scene,vertices,fog,targets,before,
                                       depth_arithmetic,clip_arithmetic)
    require(clip_arithmetic == 'fma','Undeclared mixed clipping arithmetic')
    leaves,stats = mixed_triangles(vertices,*scene['before'][0]['shape'][:2])
    require(not leaves and stats['homogeneous_clip_passes'] >= stats['source_triangles'],
            'Surviving mixed-W geometry needs an independent raster/depth contract')
    methods,fixture,pixel = expected_delivery(capture,scene)
    methods = {**methods,**fixture,**pixel};consumed_profile(methods)
    require(capture['header'][2] == 6 and methods[0x38c] == methods[0x390] == 0x1b02 and
            methods[0x308] == 1 and methods[0x39c] in (0x404,0x405),
            'Only the closed filled/culling strip profile is admitted')
    require(len(fog) == len(vertices) and all(math.isfinite(x) for x in fog),
            'Incomplete finite input fog')
    clamped = clamp_vertices(vertices)
    errors = texture_error_bound(scene['textures'][0])
    # Complete clipping above proves that no raster leaf can sample this
    # texture or consume a primary color. Keep validating encoded storage and
    # recording its exact errors; a sampled-color error bound is unnecessary
    # for an empty draw. Surviving draws retain their separate precision gate.
    bounds = [e*Fraction(max(v[c] for v in clamped))
              for e,c in zip(errors,(4,5,6))]
    for slot,target_id in enumerate(scene['frame_resource_plan']['target_ids']):
        width,height,pitch = layout(targets[target_id])
        require(pitch == width*4 and scene['before'][slot]['shape'][2] in
                ((44,45) if slot else (87,)) and len(before[target_id]) == pitch*height,
                'Producer target representation outside contract')
    # Every primitive has a fully derived clip result and none dispatches
    # any raster leaf. No observed storage or depth interpolation is needed.
    return dict(before),set(),dict(**stats,clipped_source_triangles=stats['source_triangles'],
        generated_clipped_vertices=0,triangles=0,tested_fragments=0,rejected_depth_fragments=0,
        maximum_affine_depth_error=0,covered_pixels=0,expected_written_pixels=0,
        maximum_consumed_diffuse=0,clamp_order=CLAMP_ORDER,
        input_codec_error_codes=list(map(str,errors)),
        input_prequantization_codec_error_codes=list(map(str,bounds)),
        clamped_input_components=sum(a[c] != b[c] for a,b in zip(vertices,clamped) for c in (4,5,6)),
        rgb_channel_bound=1,mixed_clip_all_primitives_empty=True,
        codec_precision_unconsumed_because_no_raster_leaves=True,
        surviving_mixed_w_pixels_qualified=False)
