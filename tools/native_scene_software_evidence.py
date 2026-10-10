"""Generation-bound clip/raster/depth evidence, without renderer execution.

The two admitted DLL/source pairs retain their exact disassembly-selected sites.
The corrected generation also pins local original-primitive normalization before
attribute clipping. Both use the existing FMA clip and fma-b-first depth graph;
this module changes no numeric equations, geometry or draw admission. Existing
retained evidence functions keep their original behavior and remain usable.
"""
import argparse
import hashlib
import json
from pathlib import Path

from native_frame_inputs import require
from native_scene_rejection_reference import CODE_SITES,pe_bytes,CLIP_SOURCE,LIBRARY_SHA256
from native_scene_mixed_clip_reference import EXTRA_CODE_SITES,CLIP_PLANES
from native_scene_surviving_mixed_reference import SURVIVING_CODE_SITES,SOURCE_SHA256

CORRECTED_LIBRARY_SHA256 = 'bf0c9c51224e9fa593d83dc525bc7c588a9af02738bce35f96749df57b357e19'
CORRECTED_SOURCE_SHA256 = 'd3ad665e3c03d2d5c60e4bf5b527ef00b0b4d6180a6f1f56a959199052a03c89'
OLD_CODE_SITES = {**CODE_SITES,**EXTRA_CODE_SITES,**SURVIVING_CODE_SITES}
# Inspected in the corrected PE image before any original-scene replay using it.
# These are fixed addresses/bytes, not signatures selected from pixel agreement.
CORRECTED_CODE_SITES = {
    'load_positive_w_threshold':(0x1e83148e0,'c5fa101d60270500'),
    'compare_threshold_to_w':(0x1e83148e8,'62b17c082fd8'),
    'skip_w_plane_if_threshold_le_w':(0x1e83148ee,'0f86cc100000'),
    'previous_w_minus_threshold':(0x1e8314995,'62715e005ccb'),
    'current_w_minus_threshold':(0x1e83149f1,'c57a5ccb'),
    'same_classification_skips_intersection':(0x1e8314a01,'0f8453010000'),
    'append_only_nonnegative_distance':(0x1e8314b5a,'c441782fca0f83cb170000'),
    'empty_polygon_check_and_return_branch':(0x1e8314bbf,'4839f50f84750d0000'),
    'return_after_cleanup':(0x1e83159b3,'c3'),
    'positive_w_threshold':(0x1e8367048,'bd378635'),
    'x_plus_w_distance':(0x1e8314cb8,'c57258c8'),
    'w_minus_x_distance':(0x1e8314f98,'c57a5cc9'),
    'y_plus_w_distance':(0x1e8315288,'c57258c8'),
    'w_minus_y_distance':(0x1e8315574,'c57a5cc9'),
    'rounded_distance_difference':(0x1e8314a07,'c4c1725ce1'),
    'rounded_intersection_division':(0x1e8314a19,'c5f25ed4'),
    'rounded_homogeneous_difference':(0x1e8314a51,'62b15c085ce0'),
    'fused_homogeneous_intersection':(0x1e8314a57,'62f27d0098e2'),
    'project_z_division':(0x1e8315855,'c5e25eda'),
    'project_x_division':(0x1e831584d,'c5fa5ec2'),
    'project_y_division':(0x1e8315851,'c5f25eca'),
    'viewport_tolerance':(0x1e836704c,'17b7d138'),
    'recursive_fan_index_increment':(0x1e831591f,'49ffc6'),
    'recursive_fan_count_compare':(0x1e8315925,'4d39e6'),
    'recursive_fan_next_in_order':(0x1e8315928,'72d6'),
    'xy_binary32_times_sixteen':(0x1e8315b09,'c5fa59c7'),
    'xy_nearest_half_away_llroundf':(0x1e8315b10,'e85bf70300'),
    'xy_scale_sixteen_constant':(0x1e83662a4,'00008041'),
    'front_cull_rejects_positive_area':(0x1e8316813,'4883bc24d000000000'),
    'front_cull_positive_area_branch':(0x1e831681c,'0f8f3efaffff'),
    'coverage_negative_first_edge':(0x1e8316e5b,'48837c24600078dd'),
    'coverage_zero_edge_excluded_unless_top_left':(0x1e8316e63,'0f94c08444247075d4'),
    'depth_b_rounded_product':(0x1e8316ee3,'c5ea595108'),
    'depth_fused_a_plus_b':(0x1e8316ef0,'c4e269994308'),
    'depth_fused_c_addition':(0x1e8316efa,'c4e279994908'),
    'depth_inverse_area_and_zero_offset_fma':(0x1e8316f12,'c4c27999cf'),
    'depth_clip_min_comparison':(0x1e8316f34,'c5f82fc1'),
    'depth_clip_max_comparison':(0x1e8316f45,'c5f82f0a'),
    'depth_floor_binary32':(0x1e8316f75,'62f37d080ac009'),
    'depth_integer_conversion':(0x1e8316f7c,'62f17e0878d8'),
}
NORMALIZATION_CODE_SITES = {
    'original_vertex_copy_load':(0x1e82eaa37,'62f1fe486f02'),
    'original_vertex_copy_stack_store':(0x1e82eaa49,'62f1fe487f842420000000'),
    'flat_primary_selected_before_normalization':(0x1e82eaac6,'c5fa6f6710c5fa7f6210'),
    'flat_secondary_selected_before_normalization':(0x1e82eaad0,'c5fa6f6720c5fa7f6220'),
    'original_primary_consumed_gate':(0x1e82eaae0,'4584d20f84b7000000'),
    'original_secondary_consumed_gate':(0x1e82eab12,'84c90f84b6000000'),
    'original_primary_nan_test_and_branch':(0x1e82eaaef,'c5f82ec90f8af7000000'),
    'original_secondary_nan_test_and_branch':(0x1e82eab20,'c5f82ec00f8a16010000'),
    'original_primary_lower_clamp':(0x1e82eaaf9,'c5f2c2e305'),
    'original_primary_upper_clamp':(0x1e82eab08,'c5ea5dc0'),
    'normalized_primary_stack_store':(0x1e82eab0c,'c5fa11448210'),
    'original_secondary_lower_clamp':(0x1e82eab2a,'c5fac2e305'),
    'original_secondary_upper_clamp':(0x1e82eab39,'c5ea5dd1'),
    'normalized_secondary_stack_store':(0x1e82eab3d,'c5fa11548220'),
    'primary_nan_rgb_zero_store':(0x1e82eabfa,'c5f057c9c5fa114c8210'),
    'primary_nan_alpha_one_store':(0x1e82eacb0,'c5fa11521c'),
    'secondary_nan_rgb_zero_store':(0x1e82eac4a,'c744822000000000'),
    'secondary_nan_alpha_one_store':(0x1e82eacdf,'c5fa11522c'),
    'normalization_one_constant':(0x1e83662b8,'0000803f'),
    'normalize_all_three_original_local_copies':(0x1e82eab4c,'4883c2784c39c20f8569ffffff'),
    'dispatch_normalized_copies_to_raster':(0x1e82eab83,'ff5018'),
    'clipped_primary_rounded_difference':(0x1e8314ab4,'c5f85cc1'),
    'clipped_primary_fma_intersection':(0x1e8314ab8,'c4e27198c2'),
    'clipped_secondary_rounded_difference':(0x1e8314ad6,'c5f85cc1'),
    'clipped_secondary_fma_intersection':(0x1e8314ada,'c4e27198c2'),
}
GENERATIONS = {
    LIBRARY_SHA256:dict(name='retained-postclip-colors',source_sha256=SOURCE_SHA256,
        sites=OLD_CODE_SITES,color_normalization='after-clipping-at-raster-leaf'),
    CORRECTED_LIBRARY_SHA256:dict(name='original-preclip-colors',source_sha256=CORRECTED_SOURCE_SHA256,
        sites={**CORRECTED_CODE_SITES,**NORMALIZATION_CODE_SITES},
        color_normalization='local-original-primitive-before-attribute-clipping'),
}


def generation_for(library_sha256,source_sha256):
    require(library_sha256 in GENERATIONS,'Unknown clip/raster/depth DLL generation')
    generation = GENERATIONS[library_sha256]
    require(source_sha256 == generation['source_sha256'],
            'Pinned source does not match clip/raster/depth DLL generation')
    return generation


def verify_code_sites(data,sites):
    require(sites,'Missing declared software code sites')
    rows = []
    for semantic,(address,expected) in sites.items():
        require(type(address) is int and address > 0 and expected,
                'Incomplete declared software code site: '+semantic)
        payload = bytes.fromhex(expected)
        actual = pe_bytes(data,address,len(payload))
        require(actual == payload,'Changed declared software code site: '+semantic)
        rows.append(dict(semantic=semantic,address=hex(address),bytes=actual.hex()))
    return rows


def software_evidence(library,source):
    data = library.read_bytes();source_data = source.read_bytes()
    library_digest = hashlib.sha256(data).hexdigest()
    source_digest = hashlib.sha256(source_data).hexdigest()
    generation = generation_for(library_digest,source_digest)
    rows = verify_code_sites(data,generation['sites'])
    return dict(library_sha256=library_digest,source_sha256=source_digest,
        generation=generation['name'],source_path=str(source),code_sites=rows,
        clip_source=CLIP_SOURCE,geometry_clip_planes=list(CLIP_PLANES),
        clip_arithmetic='fma',depth_arithmetic='fma-b-first',
        rounding_graph='binary32 reconstruction, distances, differences and division; RN difference then FMA intersections; binary32 projection; half-away four-bit XY snap; rounded B depth product then A/C FMA then inverse-area FMA; range check before floor',
        color_normalization=generation['color_normalization'],
        primary_secondary_normalized_before_clip=(library_digest==CORRECTED_LIBRARY_SHA256),
        geometry_and_draw_admission_changed=False,numeric_equations_changed=False,
        homogeneous_z_interpolated=True,geometric_depth_planes_present=False,
        projection_precedes_recursive_clip_checks=True,snap_only_at_raster_leaf=True,
        selected_from_disassembly_before_pixels=True,
        pixel_observation_scope='new original scene replay for corrected generation; prior authored component comparisons for retained generation',
        selected_before_original_scene_replay=(library_digest==CORRECTED_LIBRARY_SHA256),
        retained_generation_existing_proof_preserved=(library_digest==LIBRARY_SHA256),
        observed_after_image_goldens_used=False,
        original_scene_replayed_by_this_verifier=False,
        rgb_clamp_order_equivalent_only_for_original_rgb_in_unit_interval=True,
        surviving_signed_depth_range_qualified=False,complete_scene_pixels_qualified=False,
        exact_hardware_arithmetic_qualified=False)


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('library','source','output'):parser.add_argument('--'+name,type=Path,required=True)
    args = parser.parse_args();report = software_evidence(args.library,args.source)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(f"PASS: {report['generation']}, {len(report['code_sites'])} fixed sites, FMA clip/fma-b-first depth")
