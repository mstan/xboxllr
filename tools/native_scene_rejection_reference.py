"""Input-only whole-strip rejection for strictly behind-camera geometry.

D3D11.3 section 15.4 requires W>0 and -W<=X,Y<=W. W is affine
during clipping: a triangle whose three W values are strictly negative has
no point in this volume. The retained software's first plane is W-epsilon;
its three negative distances have no classification transitions, append no
vertices and return before rasterization. This is a software component,
not an NV2A silicon or mixed-W/depth-plane clipping qualification.
"""
from fractions import Fraction
import hashlib
import math
import struct

from native_frame_inputs import require
from native_frame_state_delivery import expected_delivery
from native_frame_target_reference import layout
from native_scene_clamp_reference import (expected_clamp_draw,clamp_vertices,
    texture_error_bound,precision_domain,CLAMP_ORDER)
from native_scene_diffuse_reference import consumed_profile
from native_scene_pixel_reference import binary32

CLIP_SOURCE = 'https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm'
LIBRARY_SHA256 = '9fdec5189881eabf77d00a52a8442ee57f5f2fe8199d7c22335594ee28cedf95'
W_EPSILON_BITS = 0x358637bd
W_EPSILON = Fraction(struct.unpack('<f',struct.pack('<I',W_EPSILON_BITS))[0])
# Exact retained software sites, inspected before any new pixel comparison.
# The rejection path is not inferred from observed write counts/images.
CODE_SITES = {
    'load_positive_w_threshold':(0x39d294720,'c5fa101d20290500'),
    'compare_threshold_to_w':(0x39d294728,'62b17c082fd8'),
    'skip_w_plane_if_threshold_le_w':(0x39d29472e,'0f86cc100000'),
    'previous_w_minus_threshold':(0x39d2947d5,'62715e005ccb'),
    'current_w_minus_threshold':(0x39d294831,'c57a5ccb'),
    'same_classification_skips_intersection':(0x39d294841,'0f8453010000'),
    'append_only_nonnegative_distance':(0x39d29499a,'c441782fca0f83cb170000'),
    'empty_polygon_check_and_return_branch':(0x39d2949ff,'4839f50f84750d0000'),
    'return_after_cleanup':(0x39d2957f3,'c3'),
    'positive_w_threshold':(0x39d2e7048,'bd378635'),
}


def pe_bytes(data,address,count):
    """Read fixed virtual-address evidence from an AMD64 PE image on disk."""
    require(data[:2] == b'MZ','Not a PE image')
    pe = struct.unpack_from('<I',data,0x3c)[0]
    require(data[pe:pe+4] == b'PE\0\0' and
            struct.unpack_from('<H',data,pe+4)[0] == 0x8664,'Not an AMD64 PE image')
    sections = struct.unpack_from('<H',data,pe+6)[0]
    optional_size = struct.unpack_from('<H',data,pe+20)[0]
    optional = pe+24
    require(struct.unpack_from('<H',data,optional)[0] == 0x20b,'Not PE32+')
    base = struct.unpack_from('<Q',data,optional+24)[0]
    rva = address-base
    for n in range(sections):
        section = optional+optional_size+40*n
        virtual_size,virtual_address,size,offset = struct.unpack_from('<IIII',data,section+8)
        if virtual_address <= rva and rva+count <= virtual_address+min(virtual_size,size):
            return data[offset+rva-virtual_address:offset+rva-virtual_address+count]
    raise ValueError('Evidence address has no supplied PE storage')


def retained_software_evidence(library):
    data = library.read_bytes();digest = hashlib.sha256(data).hexdigest()
    require(digest == LIBRARY_SHA256,'W-rejection evidence is for another retained DLL')
    rows = []
    for semantic,(address,expected) in CODE_SITES.items():
        actual = pe_bytes(data,address,len(bytes.fromhex(expected)))
        require(actual.hex() == expected,'Retained W-rejection code site changed: '+semantic)
        rows.append(dict(semantic=semantic,address=hex(address),bytes=actual.hex()))
    return dict(library_sha256=digest,clip_source=CLIP_SOURCE,
                selected_from_disassembly_before_pixels=True,
                first_clip_plane='binary32(W - binary32(1e-6)) >= 0',
                positive_w_threshold_bits=hex(W_EPSILON_BITS),
                positive_w_threshold=float(W_EPSILON),code_sites=rows,
                empty_polygon_returns_before_rasterization=True,
                source_location='src/machine/nv2a.cpp:14806',
                exact_hardware_rejection_qualified=False)


def behind_camera_proof(vertices):
    """Prove every whole strip primitive empty; reject partial/mixed-W strips."""
    require(len(vertices) >= 3 and all(len(v) == 28 and
            all(math.isfinite(x) for x in v) for v in vertices),'Incomplete finite strip geometry')
    require(all(v[3] < 0 for v in vertices),'Whole-strip strictly negative W is required')
    primitives = []
    for j in range(len(vertices)-2):
        indices = [j,j+1,j+2] if j%2 == 0 else [j+1,j,j+2]
        weights = [Fraction(vertices[i][3]) for i in indices]
        distances = [binary32(w-W_EPSILON) for w in weights]
        require(max(weights) < 0 and max(distances) < 0,'Unproved whole-primitive W rejection')
        # For convex coefficients lambda_i>=0, sum(lambda_i)=1, W is in
        # [min(weights),max(weights)]<0. All software classifications are
        # outside, so no edge intersection and no appended vertex exists.
        primitives.append(dict(indices=indices,minimum_w=float(min(weights)),
            maximum_w=float(max(weights)),maximum_software_plane_distance=float(max(distances)),
            classification_transitions=0,expected_clipped_vertices=0))
    return dict(proof='affine W convex hull strictly below zero',
                every_whole_primitive_rejected=True,primitives=primitives,
                rejected_whole_primitives=len(primitives),maximum_w=max(v[3] for v in vertices))


def expected_rejection_draw(capture,scene,vertices,fog,targets,before,
                            depth_arithmetic,clip_arithmetic):
    if not all(v[3] < 0 for v in vertices):
        # Existing positive-W/in-range-depth model keeps all its old guards.
        return expected_clamp_draw(capture,scene,vertices,fog,targets,before,
                                   depth_arithmetic,clip_arithmetic)
    proof = behind_camera_proof(vertices)
    require(clip_arithmetic == 'fma' and depth_arithmetic in ('separate','fma-b-first'),
            'Undeclared software arithmetic')
    methods,fixture,pixel = expected_delivery(capture,scene)
    methods = {**methods,**fixture,**pixel};consumed_profile(methods)
    require(capture['header'][2] == 6 and methods[0x38c] == methods[0x390] == 0x1b02 and
            methods[0x308] == 1 and methods[0x39c] in (0x404,0x405),
            'Only the closed filled/culling strip profile is admitted')
    # The consumed profile already proves density-zero fog; the finite
    # shader fog values are unconsumed and need not themselves be zero.
    require(len(fog) == len(vertices) and all(math.isfinite(x) for x in fog),
            'Incomplete finite input fog')
    clamped = clamp_vertices(vertices)
    errors = texture_error_bound(scene['textures'][0]);bounds = precision_domain(errors,clamped)
    pair = scene['frame_resource_plan']['target_ids']
    for slot,target_id in enumerate(pair):
        width,height,pitch = layout(targets[target_id])
        require(pitch == width*4 and scene['before'][slot]['shape'][2] in
                ((44,45) if slot else (87,)),'Producer target representation outside contract')
        require(len(before[target_id]) == pitch*height,'Incomplete expected predecessor storage')
    # Preserve every owned root, including target A/S/padding. Supplied
    # predecessors are the independent expected chain, not renderer images.
    return dict(before),set(),dict(source_triangles=len(vertices)-2,
        clipped_source_triangles=len(vertices)-2,generated_clipped_vertices=0,
        expanded_triangles=0,triangles=0,tested_fragments=0,rejected_depth_fragments=0,
        maximum_affine_depth_error=0,covered_pixels=0,expected_written_pixels=0,
        maximum_consumed_diffuse=0,clamp_order=CLAMP_ORDER,
        input_codec_error_codes=list(map(str,errors)),
        input_prequantization_codec_error_codes=list(map(str,bounds)),
        clamped_input_components=sum(a[c] != b[c] for a,b in zip(vertices,clamped) for c in (4,5,6)),
        rgb_channel_bound=1,behind_camera_rejection=proof)
