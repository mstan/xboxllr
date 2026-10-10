"""Input-only positive-W complete XY-clipping-empty validation admission.

All original finite geometry and clamped attributes are clipped through the
existing declared FMA equations before a validation-only geometry proxy is
constructed. Surviving leaves retain the exact previous admission/equations.
No original/reference/loader function executes in this module.
"""
from fractions import Fraction
import math

from native_frame_inputs import require
from native_frame_state_delivery import expected_delivery
from native_scene_clamp_reference import (clamp_vertices,texture_error_bound,
    validate_empty_inputs,CLAMP_ORDER)
from native_scene_diffuse_reference import consumed_profile
from native_scene_mixed_clip_reference import triangle_leaves
from native_scene_empty_depth_reference import expected_empty_depth_draw as previous_expected_draw


def positive_w_xy_empty_proof(vertices,width,height):
    require(type(width) is int and type(height) is int and width>0 and height>0,
            'Invalid original viewport extent')
    require(len(vertices)>=3 and all(len(v)==28 and all(math.isfinite(x) for x in v)
            for v in vertices),'Incomplete finite original strip')
    if not all(v[3]>=.001 for v in vertices):
        return None
    # This is the current corrected-generation primitive normalization, also
    # validating every original attribute before clipping can omit a sample.
    clamped=clamp_vertices(vertices)
    stats=dict(source_triangles=len(vertices)-2,homogeneous_clip_passes=0,
        homogeneous_plane_intersections=0,maximum_clip_recursion=0,
        empty_clipped_primitive_results=0,post_clip_nonpositive_w_rejections=0)
    leaves=[]
    for j in range(len(vertices)-2):
        stats['current_source_primitive']=j
        ids=[j,j+1,j+2] if j%2==0 else [j+1,j,j+2]
        leaves.extend(triangle_leaves([clamped[i] for i in ids],width,height,stats))
    stats.pop('current_source_primitive',None)
    if leaves:
        return None
    require(stats['homogeneous_clip_passes']>=stats['source_triangles'] and
            stats['post_clip_nonpositive_w_rejections']==0,
            'Positive-W whole-strip XY rejection was not proved')
    return dict(**stats,expanded_triangles=0,complete_clip_results_derived=True,
        original_depth_range=[min(v[2] for v in vertices),max(v[2] for v in vertices)],
        original_fractional_depth_vertices=sum(not float(v[2]).is_integer() for v in vertices),
        skip_fragment_depth_reason='no raster leaf survives the complete original positive-W XY clip result')


def expected_positive_w_xy_empty_draw(capture,scene,vertices,fog,targets,before,
                                     depth_arithmetic,clip_arithmetic):
    require(depth_arithmetic=='fma-b-first' and clip_arithmetic=='fma',
            'Wrong current declared software arithmetic')
    stats=positive_w_xy_empty_proof(vertices,*scene['before'][0]['shape'][:2])
    if stats is None:
        return previous_expected_draw(capture,scene,vertices,fog,targets,before,
                                      depth_arithmetic,clip_arithmetic)
    methods,fixture,pixel=expected_delivery(capture,scene)
    methods={**methods,**fixture,**pixel};consumed_profile(methods)
    require(capture['header'][2]==6 and methods[0x38c]==methods[0x390]==0x1b02 and
            methods[0x308]==1 and methods[0x39c] in (0x404,0x405),
            'Only the existing filled/culling strip profile is admitted')
    require(len(fog)==len(vertices) and all(math.isfinite(x) for x in fog),
            'Incomplete finite input fog')
    clamped=clamp_vertices(vertices)
    errors=texture_error_bound(scene['textures'][0])
    bounds=[e*Fraction(max(v[c] for v in clamped)) for e,c in zip(errors,(4,5,6))]
    # Only XYZ/W are unconsumed after the complete original clip result above.
    # Every attribute, count, fog, method, texture, target and expected root
    # remains subject to the full existing input/storage/profile validation.
    validation=[list(v) for v in clamped]
    for v in validation:
        v[:4]=[0.,0.,1.,1.]
    validate_empty_inputs(capture,scene,validation,fog,targets,before,methods)
    return dict(before),set(),dict(**stats,
        clipped_source_triangles=stats['source_triangles'],generated_clipped_vertices=0,
        triangles=0,tested_fragments=0,rejected_depth_fragments=0,
        maximum_affine_depth_error=0,covered_pixels=0,expected_written_pixels=0,
        maximum_consumed_diffuse=0,clamp_order=CLAMP_ORDER,
        input_codec_error_codes=list(map(str,errors)),
        input_prequantization_codec_error_codes=list(map(str,bounds)),
        clamped_input_components=sum(a[c]!=b[c] for a,b in zip(vertices,clamped) for c in (4,5,6)),
        codec_precision_unconsumed_because_no_raster_leaves=True,rgb_channel_bound=1,
        positive_w_xy_all_primitives_empty=True,
        positive_w_depth_domain_all_primitives_empty=any(not 0<=v[2]<=16777215 for v in vertices),
        surviving_depth_range_component_qualified=False)
