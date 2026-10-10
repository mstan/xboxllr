"""Input-only surviving mixed-W clipping into the existing positive-W equations.

The whole strip's W/XY clip and recursive fan order are independently derived
before four-bit snapping. Every resulting leaf must fit the unchanged finite,
positive-W, integer/in-range D24, opaque BC1/LESS coverage and color contracts.
Original RGB is restricted to [0,1], so the earlier published clamp order and
the retained software's post-clip clamp give the same RGB inputs. Saturated
alpha and zero specular retain their existing guards. No observed renderer
vertices or after-images supply expectations; unsupported leaves still fail.
"""
import copy
from fractions import Fraction
import hashlib
import math

from native_frame_inputs import require
from native_frame_state_delivery import expected_delivery
from native_scene_clamp_reference import (expected_clamp_draw,clamp_vertices,
    texture_error_bound,precision_domain)
from native_scene_diffuse_reference import consumed_profile
from native_scene_positive_xy_empty_reference import expected_positive_w_xy_empty_draw as expected_empty_depth_draw
from native_scene_mixed_clip_reference import mixed_triangles,mixed_software_evidence
from native_scene_rejection_reference import pe_bytes
from native_scene_viewport_reference import snap

CAPTURE_1943_SHA256 = '3c7da6fc564c57ceec1db134615d0bee45bd28ef84e2168795dc04f089872165'
SCENE_1943_SHA256 = '9c9280e8fb94e5a663518ceb87f05f0abc5ea40fb8b6acdf04f1f9db7041c644'
CAPTURE_1969_SHA256 = '9396f9f543470062a6ff5620b9a6fc6daa8fbfc8fa3480956bda7bcdf59610b2'
SCENE_1969_SHA256 = 'fba0c91b532fef2c8c4d718fa79f2422bfa434849a9e0a9cf5b3a02118f7bd80'
CAPTURE_1983_SHA256 = '760b08a25b5b828d4d3c784af236923a7567f6d00126c31c921017d05052f53a'
SCENE_1983_SHA256 = '1045596a8e051402cdaf752a8ae56e2e56826bf9493d21178916b8d7c5ff1e90'
SURVIVING_INPUT_SHA256 = {
    1943:(CAPTURE_1943_SHA256,SCENE_1943_SHA256),
    1969:(CAPTURE_1969_SHA256,SCENE_1969_SHA256),
    1983:(CAPTURE_1983_SHA256,SCENE_1983_SHA256),
}
SOURCE_SHA256 = '24669d0fc03643e001373314d03258059777f441865ecfa846e4435b67b9af86'
# Inspected in the pinned DLL before original draw1943 pixel observation.
SURVIVING_CODE_SITES = {
    'recursive_fan_index_increment':(0x39d29575f,'49ffc6'),
    'recursive_fan_count_compare':(0x39d295765,'4d39e6'),
    'recursive_fan_next_in_order':(0x39d295768,'72d6'),
    'xy_binary32_times_sixteen':(0x39d295949,'c5fa59c7'),
    'xy_nearest_half_away_llroundf':(0x39d295950,'e85bf70300'),
    'xy_scale_sixteen_constant':(0x39d2e62a4,'00008041'),
    'front_cull_rejects_positive_area':(0x39d296653,'4883bc24d000000000'),
    'front_cull_positive_area_branch':(0x39d29665c,'0f8f3efaffff'),
    'coverage_negative_first_edge':(0x39d296c9b,'48837c24600078dd'),
    'coverage_zero_edge_excluded_unless_top_left':(0x39d296ca3,'0f94c08444247075d4'),
    'depth_b_rounded_product':(0x39d296d23,'c5ea595108'),
    'depth_fused_a_plus_b':(0x39d296d30,'c4e269994308'),
    'depth_fused_c_addition':(0x39d296d3a,'c4e279994908'),
    'depth_inverse_area_and_zero_offset_fma':(0x39d296d52,'c4c27999cf'),
    'depth_clip_min_comparison':(0x39d296d74,'c5f82fc1'),
    'depth_clip_max_comparison':(0x39d296d85,'c5f82f0a'),
    'depth_floor_binary32':(0x39d296db5,'62f37d080ac009'),
    'depth_integer_conversion':(0x39d296dbc,'62f17e0878d8'),
}


def surviving_software_evidence(library,source):
    if library is not None:
        from native_scene_software_evidence import CORRECTED_LIBRARY_SHA256,software_evidence
        if hashlib.sha256(library.read_bytes()).hexdigest() == CORRECTED_LIBRARY_SHA256:
            return software_evidence(library,source)
    evidence = mixed_software_evidence(library)
    require(hashlib.sha256(source.read_bytes()).hexdigest() == SOURCE_SHA256,
            'Surviving mixed proof requires the pinned retained source')
    data = library.read_bytes()
    for semantic,(address,expected) in SURVIVING_CODE_SITES.items():
        actual = pe_bytes(data,address,len(bytes.fromhex(expected)))
        require(actual.hex() == expected,'Changed surviving clip/raster/depth site: '+semantic)
        evidence['code_sites'].append(dict(semantic=semantic,address=hex(address),bytes=actual.hex()))
    evidence.update(source_sha256=SOURCE_SHA256,source_path=str(source),
        clip_fan_source='src/machine/nv2a.cpp:14833',
        snap_cull_source='src/machine/nv2a.cpp:14837',
        top_left_source='src/machine/nv2a.cpp:14920',
        depth_source='src/machine/nv2a.cpp:15121',
        primary_clamp_source='src/machine/nv2a.cpp:14961',
        rgb_clamp_order_equivalent_only_for_original_rgb_in_unit_interval=True,
        surviving_signed_depth_range_qualified=False,
        selected_before_original_draw1943_pixel_observation=True)
    return evidence


def admitted_leaf(triangle,width,height,cull):
    require(width > 0 and height > 0 and cull in (0x404,0x405),
            'Invalid surviving leaf viewport/cull inputs')
    require(len(triangle) == 3 and all(len(v) == 28 and all(math.isfinite(x) for x in v)
            and v[3] >= .001 and 0 <= v[0] <= width and 0 <= v[1] <= height
            and 0 <= v[2] <= 16777215 and v[2] == math.trunc(v[2])
            and v[7] >= 1 and v[8:12] == [0,0,0,0] and v[15] == 1
            and all(0 <= v[c] <= 1 for c in (4,5,6)) for v in triangle),
            'Surviving leaf outside unchanged positive-W/raster/depth/primary/texture contract')
    result = [list(v) for v in triangle]
    for v in result:
        v[:2] = [float(snap(Fraction(x))) for x in v[:2]]
    a,b,c = result
    area = (b[0]-a[0])*(c[1]-a[1])-(c[0]-a[0])*(b[1]-a[1])
    active = bool(area) and (area > 0) != (cull == 0x404)
    if active:
        require(abs(area*256) < 2**24,'Surviving leaf outside exact small-grid edge contract')
        require(len({v[2] for v in result}) > 1,
                'Surviving constant-depth optimization remains guarded')
    return result,dict(snapped_area_16=int(area*256),survives_cull=active,
                       integer_in_range_vertex_depth=True,minimum_w=min(v[3] for v in result))


def expected_surviving_mixed_draw(capture,scene,vertices,fog,targets,before,
                                  depth_arithmetic,clip_arithmetic):
    require(clip_arithmetic == 'fma' and depth_arithmetic == 'fma-b-first',
            'Surviving mixed requires the pinned clipping/depth graph')
    require(all(len(v) == 28 and all(math.isfinite(x) for x in v) and
                all(0 <= v[c] <= 1 for c in (4,5,6)) and v[15] == 1 for v in vertices),
            'Mixed survivor primary clamp order requires original RGB in [0,1]')
    clamped = clamp_vertices(vertices)
    errors = texture_error_bound(scene['textures'][0]);bounds = precision_domain(errors,clamped)
    require(len(fog) == len(vertices) and all(math.isfinite(x) for x in fog),
            'Incomplete finite original fog')
    methods,fixture,pixel = expected_delivery(capture,scene)
    methods = {**methods,**fixture,**pixel};consumed_profile(methods)
    require(capture['header'][2] == 6 and capture['header'][4] == scene['header'][5] == len(vertices) and
            methods[0x38c] == methods[0x390] == 0x1b02 and
            methods[0x308] == 1 and methods[0x39c] in (0x404,0x405),
            'Only the closed filled/culling strip profile is admitted')
    width,height = scene['before'][0]['shape'][:2]
    triangles,stats = mixed_triangles(vertices,width,height)
    require(triangles,'This component requires a surviving mixed-W clip result')
    storage = dict(before);changed = set();proofs = []
    covered = tested = rejected = counted = 0
    error = diffuse = 0
    for index,triangle in enumerate(triangles):
        triangle,proof = admitted_leaf(triangle,width,height,methods[0x39c])
        proofs.append(dict(leaf=index,**proof))
        part_capture,part_scene = copy.deepcopy((capture,scene))
        part_capture['header'][4] = part_scene['header'][5] = 3
        # Leaf geometry is an independent equation input. Actual execution
        # remains the unchanged whole original strip and original shader/state.
        # Density-zero fog consumes no clipped fog attribute in this profile.
        storage,writes,row = expected_clamp_draw(part_capture,part_scene,triangle,[0.,0.,0.],
            targets,storage,depth_arithmetic,clip_arithmetic)
        changed.update(writes);covered += row['covered_pixels'];tested += row['tested_fragments']
        rejected += row['rejected_depth_fragments'];counted += row['triangles']
        error = max(error,row['maximum_affine_depth_error'])
        diffuse = max(diffuse,row['maximum_consumed_diffuse'])
    return storage,changed,dict(**stats,clipped_source_triangles=stats['source_triangles'],
        generated_clipped_vertices=3*len(triangles),triangles=counted,tested_fragments=tested,
        rejected_depth_fragments=rejected,maximum_affine_depth_error=error,
        covered_pixels=covered,expected_written_pixels=len(changed),maximum_consumed_diffuse=diffuse,
        leaf_admission_proofs=proofs,surviving_mixed_w_component=True,
        original_primary_rgb_in_unit_interval=True,rgb_clamp_order_equivalent_for_this_domain=True,
        input_codec_error_codes=list(map(str,errors)),
        input_prequantization_codec_error_codes=list(map(str,bounds)),
        clamped_input_components=0,
        surviving_depth_range_component_qualified=False,rgb_channel_bound=1)


def expected_bounded_surviving_draw(capture,scene,vertices,fog,targets,before,
                                   depth_arithmetic,clip_arithmetic):
    number = capture.get('input_sequence')
    if number in SURVIVING_INPUT_SHA256:
        capture_sha,scene_sha = SURVIVING_INPUT_SHA256[number]
        require(capture['sha256'] == capture_sha and scene['sha256'] == scene_sha,
                f'Wrong original draw{number} capture/scene inputs')
        return expected_surviving_mixed_draw(capture,scene,vertices,fog,targets,before,
                                            depth_arithmetic,clip_arithmetic)
    return expected_empty_depth_draw(capture,scene,vertices,fog,targets,before,
                                     depth_arithmetic,clip_arithmetic)
