"""Positive-W, out-of-range-depth whole-draw rejection after full clip proof.

Original window depths outside D24 do not imply rejection. Only complete
input-derived homogeneous W/XY clip results with no surviving raster leaves
allow expected predecessor preservation. Any surviving depth-boundary geometry
remains guarded. The original near/zero-W and existing mixed/clamp contracts
are unchanged; no actual renderer output chooses an expectation.
"""
import math

from native_frame_inputs import require
from native_frame_state_delivery import expected_delivery
from native_frame_target_reference import layout
from native_scene_clamp_reference import clamp_vertices,texture_error_bound,precision_domain,CLAMP_ORDER
from native_scene_diffuse_reference import consumed_profile
from native_scene_mixed_clip_reference import triangle_leaves,expected_empty_mixed_draw


def empty_positive_depth_proof(vertices,width,height):
    require(len(vertices) >= 3 and all(len(v) == 28 and all(math.isfinite(x) for x in v)
            and v[3] >= .001 for v in vertices),'Near/zero/nonpositive-W original geometry remains guarded')
    require(any(not 0 <= v[2] <= 16777215 for v in vertices),'No original depth-boundary domain')
    stats = dict(source_triangles=len(vertices)-2,homogeneous_clip_passes=0,
                 homogeneous_plane_intersections=0,maximum_clip_recursion=0,
                 empty_clipped_primitive_results=0,post_clip_nonpositive_w_rejections=0)
    leaves = []
    for j in range(len(vertices)-2):
        stats['current_source_primitive'] = j
        ids = [j,j+1,j+2] if j%2 == 0 else [j+1,j,j+2]
        leaves.extend(triangle_leaves([vertices[i] for i in ids],width,height,stats))
    stats.pop('current_source_primitive',None)
    require(not leaves and stats['homogeneous_clip_passes'] >= stats['source_triangles'],
            'Surviving out-of-range-depth geometry needs independent fragment-depth/raster equations')
    require(stats['post_clip_nonpositive_w_rejections'] == 0,
            'Positive-W clip arithmetic lost positivity')
    return dict(**stats,expanded_triangles=0,complete_clip_results_derived=True,
        original_out_of_range_depth_vertices=sum(not 0 <= v[2] <= 16777215 for v in vertices),
        original_depth_range=[min(v[2] for v in vertices),max(v[2] for v in vertices)],
        skip_fragment_depth_reason='no raster leaf survives the complete W/XY clip result')


def expected_empty_depth_draw(capture,scene,vertices,fog,targets,before,
                              depth_arithmetic,clip_arithmetic):
    require(len(vertices) >= 3 and all(len(v) == 28 for v in vertices),'Incomplete original strip')
    if not (all(v[3] >= .001 for v in vertices) and any(not 0 <= v[2] <= 16777215 for v in vertices)):
        return expected_empty_mixed_draw(capture,scene,vertices,fog,targets,before,
                                         depth_arithmetic,clip_arithmetic)
    require(clip_arithmetic == 'fma','Undeclared homogeneous clipping arithmetic')
    stats = empty_positive_depth_proof(vertices,*scene['before'][0]['shape'][:2])
    methods,fixture,pixel = expected_delivery(capture,scene)
    methods = {**methods,**fixture,**pixel};consumed_profile(methods)
    require(capture['header'][2] == 6 and methods[0x38c] == methods[0x390] == 0x1b02 and
            methods[0x308] == 1 and methods[0x39c] in (0x404,0x405),
            'Only the closed filled/culling strip profile is admitted')
    require(len(fog) == len(vertices) and all(math.isfinite(x) for x in fog),
            'Incomplete finite input fog')
    clamped = clamp_vertices(vertices)
    errors = texture_error_bound(scene['textures'][0]);bounds = precision_domain(errors,clamped)
    for slot,target_id in enumerate(scene['frame_resource_plan']['target_ids']):
        width,height,pitch = layout(targets[target_id])
        require(pitch == width*4 and scene['before'][slot]['shape'][2] in
                ((44,45) if slot else (87,)) and len(before[target_id]) == pitch*height,
                'Producer target representation outside contract')
    return dict(before),set(),dict(**stats,clipped_source_triangles=stats['source_triangles'],
        generated_clipped_vertices=0,triangles=0,tested_fragments=0,rejected_depth_fragments=0,
        maximum_affine_depth_error=0,covered_pixels=0,expected_written_pixels=0,
        maximum_consumed_diffuse=0,clamp_order=CLAMP_ORDER,
        input_codec_error_codes=list(map(str,errors)),
        input_prequantization_codec_error_codes=list(map(str,bounds)),
        clamped_input_components=sum(a[c] != b[c] for a,b in zip(vertices,clamped) for c in (4,5,6)),
        rgb_channel_bound=1,positive_w_depth_domain_all_primitives_empty=True,
        surviving_depth_range_component_qualified=False)
