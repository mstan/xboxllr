// Optional host acceleration seam for NV2A generic-combiner batches.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "host/nv2a_combiner_glsl.hpp"

namespace xlle {

// This is deliberately an owned snapshot.  A renderer must never retain pointers into
// Nv2a's mutable register or vertex state after submit_generic_combiner returns.
struct Nv2aGenericCombinerVertex {
    std::array<float, 4> position{};
    std::array<float, 4> diffuse{};
    std::array<float, 4> specular{};
    std::array<std::array<float, 4>, 4> texture{};
};

struct Nv2aGenericCombinerPacket {
    // A deliberately decoded, narrowly supported compute fixture.  This is not a
    // replacement for the generic state vector: it gives a host backend the exact
    // coverage/output inputs it needs without teaching host code NV097 register
    // indices.  More general generic-combiner uniforms will extend this structure.
    enum class ComputeFixture : uint8_t {
        kNone,
        kUntexturedPrimaryTriangle,
        kTextureModulateTriangle,
        kDashboardProject2dFan,
        kResidentPrimaryColorTriangles,
        kResidentConstantColorTriangles,
        kResidentTextureModulateTriangles
    };
    enum class TextureLayout : uint8_t { kLinear, kSwizzled };
    enum class TextureFormat : uint8_t { kArgb8, kA8 };
    enum class TextureMode : uint8_t { kProject2d, kCubemap };
    struct Texture {
        bool referenced = false;
        TextureLayout layout = TextureLayout::kSwizzled;
        TextureFormat format = TextureFormat::kArgb8;
        TextureMode mode = TextureMode::kProject2d;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t pitch = 0;
        uint32_t address_mode = 0;
        uint32_t border_color = 0;
        std::vector<uint8_t> input;
    };

    std::vector<Nv2aGenericCombinerVertex> vertices;
    std::vector<uint32_t> raster_state;
    std::array<Texture, 4> textures{};
    uint8_t primitive_mode = 0;
    uint32_t surface_format = 0;
    uint32_t surface_pitch = 0;
    uint32_t surface_height = 0;
    uint32_t color_address = 0;
    uint32_t depth_address = 0;
    std::vector<uint8_t> color_input;
    std::vector<uint8_t> depth_input;
    ComputeFixture compute_fixture = ComputeFixture::kNone;
    uint32_t fixture_clip_x = 0;
    uint32_t fixture_clip_y = 0;
    uint32_t fixture_clip_width = 0;
    uint32_t fixture_clip_height = 0;
    // Screen positions in NV2A's 1/16th-pixel fixed-point convention, ordered
    // A/B/C. fixture_source_argb is the already-decoded combiner output.  The
    // first fixture explicitly gates blending off; future variants add its decoded
    // blend contract here rather than reaching back into raster_state.
    std::array<int32_t, 6> fixture_xy_16{};
    uint32_t fixture_source_argb = 0;
    uint32_t fixture_constant_argb = 0;
    uint32_t fixture_factor0_stage2_argb = 0;
    uint32_t fixture_factor0_stage4_argb = 0;
    uint32_t fixture_blend_src = 0;
    uint32_t fixture_blend_dst = 0;
    uint32_t fixture_depth_func = 0x0207;
    bool fixture_blend_enabled = false;
    bool fixture_depth_test_enabled = false;
    bool fixture_depth_write_enabled = false;
    bool fixture_texture_passthrough = false;
    bool fixture_texture_constant_color_alpha = false;
    // When set, fragment output alpha = texel.a * unpack_argb(
    // fixture_constant_argb).a while RGB follows fixture_texture_passthrough.
    // Encodes the witnessed combiner pair ColorIcw0=0x08200000 /
    // AlphaIcw0=0x11180000 exactly; distinct from
    // fixture_texture_constant_color_alpha whose compute contract also
    // replaces RGB with the constant.
    bool fixture_alpha_constant_modulate = false;
    // When set, discard fragments whose packed output-alpha byte is zero
    // (witnessed as GEQUAL alpha-ref 1 on combiner output).
    bool fixture_alpha_kill_zero = false;
    bool fixture_color_format_x8 = false;
    bool fixture_use_tile_bins = false;
};

// A successful synchronous submission returns complete replacement images for the
// packet's color surface and, when present, depth surface. Nv2a validates their
// sizes and commits them through its normal guest-memory write path. This is the
// correctness-first protocol; a future resident backend must use a separate,
// explicit resolve/invalidate contract rather than weakening this one.
struct Nv2aGenericCombinerResult {
    std::vector<uint8_t> color_output;
    std::vector<uint8_t> depth_output;
    uint64_t triangles = 0;
    uint64_t fragments = 0;
    uint64_t writes = 0;
};

struct Nv2aResidentColorResolve {
    uint32_t address = 0;
    std::vector<uint8_t> data;
};

struct Nv2aResidentDepthResolve {
    uint32_t address = 0;
    std::vector<uint8_t> data;
};

// ===================== residency-break attribution (beads-xbox.1.15.10 p3) ==
// EVERY re-upload of a resident witnessed surface exists because something
// dropped that plane's residency. Which consumer did it is the whole of the
// transfer-volume question, and until now it was INFERRED from a separate
// simulation (Nv2a::GpuResidencyCensus) whose one-active-target model reported
// 647 breaks against the renderer's 1336 actual uploads. This is the direct
// measurement: the invalidator stamps the slot, the uploader reports the stamp.
//
// Always on, in Release. Cost is one 32-bit store per invalidation and one
// increment per upload -- both already off the per-fragment path.
enum Nv2aResidentInvalidateCause : uint32_t {
    kNv2aInvalidateClearSurface = 0,
    kNv2aInvalidateWitnessedCpuDraw,
    kNv2aInvalidateGenericSoftware,
    kNv2aInvalidateImageBlit,
    kNv2aInvalidateScanout,
    kNv2aInvalidateImmediateQuad,
    kNv2aInvalidateRetarget,
    kNv2aInvalidateOther,
    // Never invalidated: this is the plane's FIRST upload into the slot.
    kNv2aInvalidateCold,
    // The slot was taken over for a different surface while still holding a
    // clean copy of this one (find_reusable_resident_slot stole it).
    kNv2aInvalidateEvicted,
    kNv2aInvalidateGenericAccelerated,
    // refresh_resident_color_after_software_write could not region-upload
    // (surface not resident under that key, or backend cannot) and fell back to
    // dropping residency.
    kNv2aInvalidateRegionFallback,
    // upload_resident_color_rows invalidating OTHER slots that overlap the
    // range it just refreshed (the overlap contract).
    kNv2aInvalidateRegionOverlap,
    kNv2aInvalidateCauses,
};
inline constexpr size_t kNv2aInvalidateCauseSlots = 16;
inline constexpr size_t kNv2aInvalidateProfileSlots = 16;
inline const char* nv2a_invalidate_cause_name(size_t cause) {
    switch (cause) {
    case kNv2aInvalidateClearSurface:       return "clear_surface";
    case kNv2aInvalidateWitnessedCpuDraw:   return "witnessed_cpu_draw";
    case kNv2aInvalidateGenericSoftware:    return "generic_software";
    case kNv2aInvalidateImageBlit:          return "image_blit";
    case kNv2aInvalidateScanout:            return "scanout";
    case kNv2aInvalidateImmediateQuad:      return "immediate_quad";
    case kNv2aInvalidateRetarget:           return "retarget";
    case kNv2aInvalidateOther:              return "other";
    case kNv2aInvalidateCold:               return "cold";
    case kNv2aInvalidateEvicted:            return "evicted";
    case kNv2aInvalidateGenericAccelerated: return "generic_accelerated";
    case kNv2aInvalidateRegionFallback:     return "region_fallback";
    case kNv2aInvalidateRegionOverlap:      return "region_overlap";
    default: break;
    }
    return "unknown";
}
// The token an invalidating call site passes, and the two fields it packs.
inline constexpr uint32_t nv2a_invalidate_token(uint32_t cause,
                                                uint32_t profile = 0) {
    return (cause & 0xFFu) | ((profile & 0xFFu) << 8);
}
inline constexpr uint32_t nv2a_invalidate_token_cause(uint32_t token) {
    return token & 0xFFu;
}
inline constexpr uint32_t nv2a_invalidate_token_profile(uint32_t token) {
    return (token >> 8) & 0xFFu;
}

struct Nv2aWitnessedUploadCensus {
    // [cause][profile] -- profile is 0 for every cause but witnessed_cpu_draw.
    std::array<std::array<uint64_t, kNv2aInvalidateProfileSlots>,
               kNv2aInvalidateCauseSlots> color_uploads{};
    std::array<std::array<uint64_t, kNv2aInvalidateProfileSlots>,
               kNv2aInvalidateCauseSlots> color_bytes{};
    std::array<std::array<uint64_t, kNv2aInvalidateProfileSlots>,
               kNv2aInvalidateCauseSlots> depth_uploads{};
    std::array<std::array<uint64_t, kNv2aInvalidateProfileSlots>,
               kNv2aInvalidateCauseSlots> depth_bytes{};
    // Rows actually pushed by the row-scoped witnessed upload path, against the
    // rows a full-surface upload would have pushed. The ratio is the whole
    // value of step 4 and it is reported, not estimated.
    uint64_t row_uploads = 0;
    uint64_t row_upload_rows = 0;
    uint64_t row_upload_bytes = 0;
    uint64_t row_upload_surface_rows = 0;
    // How the invalidating range sat on the plane it dropped, per cause:
    //   [0] EXACT  -- same base address and same length as the resident plane;
    //                 a whole-plane fill, so a glClear could serve it instead.
    //   [1] COVER  -- strictly contains the plane (same fill, wider request).
    //   [2] PARTIAL-- merely overlaps. Nothing whole-plane can replace this one;
    //                 it needs a row-scoped refresh or the profile moved.
    // Recorded at invalidation time, so it is available even for planes that
    // are never re-uploaded afterwards.
    std::array<std::array<uint64_t, 3>, kNv2aInvalidateCauseSlots>
        color_invalidate_extent{};
    std::array<std::array<uint64_t, 3>, kNv2aInvalidateCauseSlots>
        depth_invalidate_extent{};
    // clear_resident_surface acceptance, split so a decline says which plane
    // the backend could not find.
    uint64_t clear_accept_color = 0;
    uint64_t clear_accept_depth = 0;
    uint64_t clear_decline_color_missing = 0;
    uint64_t clear_decline_depth_missing = 0;
    // An upload that overwrote a plane the GPU had written and no resolve had
    // moved back. This MUST stay zero: it is silent loss of guest-visible
    // output, and it is the failure mode a GPU-side CLEAR_SURFACE introduces if
    // the residency key can change under a dirty plane.
    uint64_t upload_over_dirty_color = 0;
    uint64_t upload_over_dirty_depth = 0;
    // A slot whose ADDRESS matched but whose pitch/height/format key did not.
    // The plane is re-uploaded from guest memory; it is not an invalidation, so
    // it shows up as `cold` in the cause table unless it is counted here.
    uint64_t color_key_mismatch = 0;
    uint64_t depth_key_mismatch = 0;
    // Invalidation stamps dropped because the attribution table was full. Any
    // nonzero value means the `cold` bucket is overstated by up to this much.
    uint64_t stamp_overflows = 0;
    // Clears served on the GPU whose plane was CLEAN, so the caller also filled
    // guest memory and the plane stayed clean. These cost a guest fill and save
    // a re-upload; the others save the fill too and leave the plane dirty.
    uint64_t clear_kept_clean = 0;
    // Uploads that took over a slot still holding a DIFFERENT surface. That
    // ends the other surface's residency without an invalidation, so its next
    // upload is reported `cold`; these two counters bound how much of the cold
    // bucket is slot pressure rather than a genuine first touch.
    uint64_t color_slot_takeover = 0;
    uint64_t depth_slot_takeover = 0;
    // beads-xbox.1.15.10 phase 4 (lever C). How often a witnessed packet's
    // colour and zeta planes were served from DIFFERENT resident slots -- the
    // configuration the pair-scoped selector could not represent and resolved
    // by evicting one of the two colour surfaces instead (color_slot_takeover,
    // 342 per canonical 12 B run before this change). `hits` is the fully
    // resident case, which costs nothing at all; `uploads` is a packet that
    // still had to upload a plane while split. Both are the visible signature
    // of the fix: takeover falls, split rises, uploads fall.
    uint64_t plane_split_hits = 0;
    uint64_t plane_split_uploads = 0;
    // Phase 7. A resident zeta plane whose R32UI WORD form had to be rebuilt
    // by reading the depth attachment back, because the path that made the
    // plane resident had no word form to upload. It is a readback, so it must
    // stay small; a large number means the dashboard depth-EQUAL fixture and
    // the executors are fighting over one plane.
    uint64_t depth_word_rebuilds = 0;
};

// ===================== stage (f): witnessed-profile GPU execution ===========
// beads-xbox.1.15.7. The two dominant witnessed profiles (kSwizzledRadialColor
// and kPitchTexturedColor, 83 percent of all fragments) execute on the GPU with
// the destination surfaces RESIDENT across consecutive GPU jobs. The packet is
// deliberately POST-SETUP: Nv2a has already matched the profile, validated the
// raster subset, resolved the surfaces, snapshotted the texture, assembled the
// primitives, snapped the screen positions to NV2A 1/16-pixel fixed point and
// applied the same culling and degeneracy tests the software rasterizer applies.
// What crosses this boundary is therefore the SAME triangle set the software
// path would have rasterized, in the same order, with the same numbers -- the
// renderer re-derives coverage and interpolation from the identical integer
// edge functions, so the seam carries no rounding of its own.
struct Nv2aWitnessedTriangle {
    // Screen positions in NV2A 1/16-pixel fixed point, in the software
    // rasterizer's post-cull a/b/c order, as the EXACT int64 values
    // `llround(pos*16)` produced. Phase 2 (beads-xbox.1.15.10) changed these
    // from double to int64_t and moved the shader's edge functions to
    // GL_ARB_gpu_shader_int64, because the double form was only exact below
    // 2^24 and the resulting `vertex_range` rejection was measured to be the
    // DOMINANT cause of residency breaks: 1 126 rejected jobs produced 806 of
    // the 1 110 witnessed-CPU-draw round trips per 12 G instructions, against
    // 304 from every other profile combined. In int64 the shader runs the
    // software rasterizer's arithmetic verbatim -- the same products, the same
    // two's-complement wrap on the degenerate geometry that overflows it -- so
    // there is no coordinate bound left to reject on.
    int64_t ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
    // The software path's per-triangle destination bounding box: its scanline
    // loop bounds, already intersected with the clip rect. Used only by the
    // WIDE path below; see `wide`.
    int32_t bbox_xmin = 0, bbox_xmax = -1, bbox_ymin = 0, bbox_ymax = -1;
    // True when a vertex coordinate is too large for the fixed-function
    // rasterizer to place reliably in float clip space. The in-shader edge test
    // is authoritative in both cases -- it can only REMOVE a candidate fragment
    // the driver generated, never add one -- so the only thing at risk for a
    // wide triangle is UNDER-coverage by the driver. A wide triangle is
    // therefore drawn as its own bounding-box quad instead of as itself, which
    // is exactly the region the software path's scanline loop visits, and the
    // edge test then carves the identical triangle out of it.
    bool wide = false;
    float za = 0.0f, zb = 0.0f, zc = 0.0f;       // window depth per vertex
    float inverse_area = 0.0f;                   // 1.0f / (float)area
    float wa = 1.0f, wb = 1.0f, wc = 1.0f;       // position[3] per vertex
    // The software path's inverse_area_double and inverse_position_w0..2. They
    // are carried rather than recomputed on the GPU so the reciprocals are the
    // exact same doubles the software rasterizer divides with -- GLSL's
    // required precision for double division is not the place to introduce a
    // difference the design is trying to avoid.
    double inverse_area_double = 0.0;
    double inverse_w[3] = {1.0, 1.0, 1.0};
    // Per-vertex diffuse after the software path's NaN fixup and [0,1] clamp.
    std::array<std::array<float, 3>, 3> diffuse{};
    // Radial: texture[0].xyz. Pitch-textured and pitch-projected: texture[1].xyz.
    std::array<std::array<float, 3>, 3> tex_a{};
    // Pitch-textured and pitch-projected: texture[2].xyz.
    std::array<std::array<float, 3>, 3> tex_b{};
    // kPitchProjectedColor only: the PROJECTIVE stage-0 texcoord, carried as
    // the software path reads it -- components 0, 1 and 3 of texture[0], in
    // that order (component 3 is the projective w the fragment divides by).
    // It rides in the unused .w lanes of the existing d/t/u vec4 triples on the
    // way to the shader, so the SSBO record and its 288-byte std430 stride are
    // unchanged; see nv2a_witnessed_gl_shaders.hpp.
    std::array<std::array<float, 3>, 3> tex_p{};
};

struct Nv2aWitnessedProfilePacket {
    enum class Profile : uint8_t {
        kSwizzledRadialColor,
        kPitchTexturedColor,
        // beads-xbox.1.15.10 phase 4 (lever A). Same destination geometry and
        // the same triangle record as kPitchTexturedColor; it adds one
        // REPEAT-addressed 2D image sampled through a projective texcoord.
        kPitchProjectedColor,
    };
    Profile profile = Profile::kSwizzledRadialColor;

    // ---- destination -------------------------------------------------------
    uint32_t color_address = 0;
    uint32_t depth_address = 0;
    size_t   surface_size = 0;      // colour bytes; the zeta plane is the same
    uint32_t surface_pitch = 0;     // raw SET_SURFACE_FORMAT pitch word (the key)
    uint32_t surface_format = 0;    // raw SET_SURFACE_FORMAT word (the key)
    uint32_t surface_width = 0;
    uint32_t surface_height = 0;    // the key's height (clip_y + clip_height)
    bool     swizzled_destination = false;

    // ---- clip / depth range ------------------------------------------------
    uint32_t xmin = 0, xmax = 0, ymin = 0, ymax = 0;   // inclusive pixel bounds
    float clip_min = 0.0f, clip_max = 0.0f;

    // ---- pitch-textured / pitch-projected combiner inputs ------------------
    // The only two combiner words either fingerprint leaves free: STAGE 6 for
    // kPitchTexturedColor, STAGE 5 for kPitchProjectedColor. The name records
    // where they came from for the profile that introduced them; both land in
    // the shader's weight0 / weight1 uniforms.
    uint32_t factor0_stage6 = 0;
    uint32_t factor1_stage6 = 0;
    uint32_t texture_width = 0;
    uint32_t texture_height = 0;
    // Already UNSWIZZLED by Nv2a into linear rows of A8R8G8B8 guest words, so
    // the renderer does not need the swizzle table for the texture.
    std::vector<uint8_t> texture_linear;
    // FNV-64 of texture_linear, so the backend can skip an unchanged re-upload
    // without hashing the bytes a second time.
    uint64_t texture_hash = 0;
    // kPitchProjectedColor only: the stage-0 projective image, a SECOND
    // sampler. Same conventions as texture_linear above -- already unswizzled
    // by Nv2a into linear rows of A8R8G8B8 guest words, with its own FNV-64 so
    // an unchanged re-upload is skipped.
    uint32_t projected_texture_width = 0;
    uint32_t projected_texture_height = 0;
    std::vector<uint8_t> projected_texture_linear;
    uint64_t projected_texture_hash = 0;

    // ---- initial surface bytes, supplied only when not already resident ----
    // Both are LINEAR row images the renderer can upload directly: Nv2a
    // unswizzles a swizzled destination on the way in and re-swizzles it on the
    // way out, because the swizzle table lives on the device side.
    bool needs_color_upload = false;
    bool needs_depth_upload = false;
    std::vector<uint8_t> color_input;   // width*height*4, guest A8R8G8B8 words
    std::vector<uint8_t> depth_input;   // width*height*4, guest Z24S8 words

    std::vector<Nv2aWitnessedTriangle> triangles;
};

// ============ stage (f3): the GENERATED generic-combiner executor ===========
// beads-xbox.1.15.10 phase 6. Same contract as Nv2aWitnessedProfilePacket -- a
// POST-SETUP packet whose triangles are exactly the ones the software path
// would have rasterized, in the same order, with the same numbers -- but the
// fragment program is GENERATED from the combiner state instead of hand
// written. See src/host/nv2a_combiner_glsl.hpp for the generator and the
// census that ordered it.
struct Nv2aGeneratedTriangle {
    // The software rasterizer's post-cull a/b/c order in NV2A 1/16-pixel fixed
    // point, exactly the int64 values llround(pos*16) produced.
    int64_t ax = 0, ay = 0, bx = 0, by = 0, cx = 0, cy = 0;
    // Its scanline loop bounds, already intersected with the clip rect. Used
    // only when `wide` is set; see Nv2aWitnessedTriangle for the full argument.
    int32_t bbox_xmin = 0, bbox_xmax = -1, bbox_ymin = 0, bbox_ymax = -1;
    bool wide = false;
    float za = 0.0f, zb = 0.0f, zc = 0.0f;
    float inverse_area = 0.0f;        // 1.0f / (float)area, the software value
    float inv_area_recip = 0.0f;      // (float)(1.0 / (double)area)
    float inv_w[3] = {1.0f, 1.0f, 1.0f};
    // The software path's own doubles, carried rather than recomputed on the
    // GPU. A generated program interpolating in double reads these; the FP32
    // variant reads the float copies above, and the record layout is the same
    // either way so the choice is a runtime switch on one binary.
    double inv_area_double = 0.0;
    double inv_w_double[3] = {1.0, 1.0, 1.0};
    // polygon_bias + polygon_scale * max_z_slope for this triangle.
    float depth_bias = 0.0f;
    // The software path's constant-depth shortcut: when all three window depths
    // are equal it computes ONE value and either skips the whole triangle or
    // uses that value for every fragment. A triangle whose constant depth FAILS
    // the range test is never emitted at all, so the shader only ever sees the
    // passing case.
    bool constant_depth = false;
    float constant_depth_value = 0.0f;
    // Per vertex (a, b, c), per slot: 0 diffuse, 1 specular, 2..5 texcoord 0..3.
    // Already carrying the software path's NaN fixup and [0,1] clamp for
    // diffuse/specular, its NaN->0 fixup for texcoords, and a hard zero in every
    // texcoord lane the software path's component mask leaves uninterpolated.
    std::array<std::array<std::array<float, 4>, 6>, 3> varying{};
};

struct Nv2aGeneratedTexture {
    bool present = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t border_argb = 0;
    // Linear rows of guest A8R8G8B8 words, top row first: the device has
    // already applied the Morton table for a swizzled source and expanded an A8
    // source to (0xFF, 0xFF, 0xFF, a).
    std::vector<uint8_t> linear;
    // FNV-64 in the legacy content_id==0 namespace. A nonzero content_id is
    // unique across device instances; reuse is justified by exact source-byte
    // equality in the device conversion cache. Each packet still owns its bytes.
    // Cache eviction never reuses an ID. Hash may be zero in the ID namespace.
    uint64_t hash = 0;
    uint64_t content_id = 0;
};

struct Nv2aGeneratedShaderPacket {
    Nv2aCombinerShaderState state;
    uint64_t fingerprint = 0;

    uint32_t color_address = 0;
    uint32_t depth_address = 0;
    size_t surface_size = 0;
    uint32_t surface_pitch = 0;      // raw SET_SURFACE_PITCH word (the key)
    uint32_t surface_format = 0;     // raw SET_SURFACE_FORMAT word (the key)
    uint32_t surface_width = 0;      // (pitch & 0xffff) / 4
    uint32_t surface_height = 0;
    // Effective clip rectangle, inclusive pixel bounds.
    uint32_t xmin = 0, xmax = 0, ymin = 0, ymax = 0;
    float clip_min = 0.0f, clip_max = 0.0f;

    bool needs_color_upload = false;
    std::vector<uint8_t> color_input;   // width*height*4 guest A8R8G8B8 words
    bool needs_depth_upload = false;
    std::vector<uint8_t> depth_input;   // width*height*4 guest Z24S8 words

    // Uniform values: everything that parameterises the generated program
    // without changing its source.
    std::array<uint32_t, 8> stage_const0{};
    std::array<uint32_t, 8> stage_const1{};
    uint32_t final_factor0 = 0;
    uint32_t final_factor1 = 0;
    uint32_t alpha_ref = 0;
    uint32_t blend_src = 0, blend_dst = 0;
    uint32_t depth_func = 0x0207;
    // Whether NV_PGRAPH's depth-test-enable bit is set. The software path
    // treats "test disabled, write enabled" as pass-everything-and-store, so
    // the two are independent and both have to travel.
    bool depth_test = false;
    bool depth_write = false;
    bool color_mask[4] = {true, true, true, true};
    std::array<Nv2aGeneratedTexture, 4> textures{};
    std::vector<Nv2aGeneratedTriangle> triangles;
};

struct Nv2aGeneratedShaderResult {
    uint64_t triangles = 0;
    uint64_t draw_calls = 0;
    uint64_t color_upload_bytes = 0;
    uint64_t depth_upload_bytes = 0;
    uint64_t texture_upload_bytes = 0;
    uint64_t programs_compiled = 0;
    uint64_t program_cache_hits = 0;
};

struct Nv2aWitnessedProfileResult {
    uint64_t triangles = 0;
    uint64_t draw_calls = 0;
    uint64_t color_upload_bytes = 0;
    uint64_t depth_upload_bytes = 0;
    uint64_t texture_upload_bytes = 0;
};

class Nv2aRenderer {
public:
    virtual ~Nv2aRenderer() = default;
    virtual const char* name() const = 0;
    // This query must have no side effects. Nv2a snapshots the potentially large
    // guest surfaces only after a renderer accepts the validated descriptor.
    virtual bool supports_generic_combiner(
        const Nv2aGenericCombinerPacket& descriptor) const = 0;
    // Once supports_generic_combiner returned true, failure is fatal for this draw:
    // silently replaying it in software could double-apply partially submitted work.
    virtual bool submit_generic_combiner(
        const Nv2aGenericCombinerPacket& packet,
        Nv2aGenericCombinerResult& result, std::string& error) = 0;
    // Optional resident-surface protocol. The default implementation is inert and
    // preserves the synchronous owned-snapshot contract above. A backend that keeps
    // color data on the host GPU must make overlap queries resolve into complete
    // guest-memory bytes before CPU/software/capture paths read or overwrite them.
    virtual bool resolve_resident_color_surface(
        uint32_t address, size_t size, Nv2aResidentColorResolve& resolve,
        std::string& error) {
        (void)address;
        (void)size;
        resolve = {};
        error.clear();
        return true;
    }
    // beads-xbox.1.15.10 phase 3: `cause` is an ALWAYS-ON attribution token,
    // not a control input. Low 8 bits are an Nv2aResidentInvalidateCause; bits
    // 8..15 carry the witnessed profile index when the caller is a witnessed
    // CPU draw, 0 otherwise. The backend stores it on the slot it invalidates
    // and reports it back through witnessed_upload_census(), so every resident
    // re-upload is attributable to the consumer that dropped the residency
    // instead of being inferred from a simulation. Behaviour must not depend on
    // it.
    virtual void invalidate_resident_color_surface(uint32_t address, size_t size,
                                                   uint32_t cause) {
        (void)address;
        (void)size;
        (void)cause;
    }
    virtual bool resolve_resident_depth_surface(
        uint32_t address, size_t size, Nv2aResidentDepthResolve& resolve,
        std::string& error) {
        (void)address;
        (void)size;
        resolve = {};
        error.clear();
        return true;
    }
    virtual void invalidate_resident_depth_surface(uint32_t address, size_t size,
                                                   uint32_t cause) {
        (void)address;
        (void)size;
        (void)cause;
    }
    virtual bool has_resident_depth_surface(
        uint32_t address, size_t size, uint32_t pitch,
        uint32_t height, uint32_t format) const {
        (void)address;
        (void)size;
        (void)pitch;
        (void)height;
        (void)format;
        return false;
    }
    virtual bool has_resident_color_surface(
        uint32_t address, size_t size, uint32_t pitch,
        uint32_t height, uint32_t format) const {
        (void)address;
        (void)size;
        (void)pitch;
        (void)height;
        (void)format;
        return false;
    }
    // ---- stage (f) phase 2 step 1: CPU/GPU overlap (beads-xbox.1.15.10) ----
    // Issue any GPU work the backend is still holding and submit it to the
    // driver, so the GPU executes it WHILE the emulation thread runs on. Before
    // this existed the pending witnessed batch was issued only when a resolve
    // needed its result, which serialized the whole GPU execution behind the
    // emulation thread: the matched instrumented pair measured gpu_draw_ns
    // 7.740 s of witnessed GPU work and drain_ns 5.878 s of emulation-thread
    // time spent blocking on exactly that work. It must be safe to call at any
    // point where the emulation thread is between guest-visible operations; it
    // changes only WHEN the pending draws are issued, never their order, and
    // GL executes primitives across draw calls in submission order.
    virtual void kick_pending_gpu_work() {}
    // Always-on witness that the early flush actually happened, so the census
    // reports it rather than leaving it to be inferred from the draw-call count
    // (which also moves for unrelated reasons).
    struct WitnessedFlushStats {
        uint64_t size_flushes = 0;  // batch issued because the bound was hit
        uint64_t kick_flushes = 0;  // batch issued by kick_pending_gpu_work
        uint64_t gl_flushes = 0;    // glFlush calls that submitted a draw
        uint64_t bound = 0;         // the configured triangle bound
        uint64_t ssbo_ring = 1;     // SSBO names round-robined per flush
        bool kick_flush = false;    // glFlush issued after each batch
        bool fp32_shader = false;   // single-precision fragment variant
        // beads-xbox.1.15.10 phase 4 (lever C): colour and zeta planes are
        // selected into resident slots independently. False restores the
        // pre-phase-4 pair-scoped rule for the A/B arm.
        bool plane_split = true;
    };
    virtual WitnessedFlushStats witnessed_flush_stats() const { return {}; }
    // Upper bound on triangles allowed to accumulate in the pending batch
    // before it is issued. 0 restores the pre-step-1 behavior (issue only when
    // some other entry point demands it), which is what the bisection arm uses.
    virtual void set_witnessed_flush_triangles(uint64_t triangles) {
        (void)triangles;
    }
    // Whether an issued batch is submitted to the driver immediately (glFlush).
    // Off leaves submission to the driver's own schedule, which is what the
    // pre-step-1 path did; this exists so "chunking" and "submitting early" can
    // be measured apart from each other.
    virtual void set_witnessed_kick_flush(bool on) { (void)on; }
    // Number of SSBO names the witnessed batch round-robins through. 1 is the
    // pre-step-1 behavior; >1 stops a refill from landing on a buffer an
    // in-flight draw is still reading.
    virtual void set_witnessed_ssbo_ring(uint32_t slots) { (void)slots; }
    // beads-xbox.1.15.10 phase 4 (lever C). Independent per-plane resident-slot
    // selection for witnessed packets. Off restores the pre-phase-4 rule where
    // a colour miss fell back to the ZETA key and overwrote whatever colour
    // surface that slot still held (342 takeovers / 686 MB per canonical 12 B
    // run). Runtime switch on one binary so the change is a same-binary A/B.
    virtual void set_witnessed_plane_split(bool on) { (void)on; }
    // Single-precision witnessed fragment shaders. The record layout is the
    // same either way, so this is a runtime switch on one binary and an A/B
    // needs no rebuild. Returns false only if the recompile itself failed.
    virtual bool set_witnessed_fp32_shader(bool on, std::string& error) {
        (void)on;
        error.clear();
        return true;
    }
    // Whether upload_resident_color_rows is available on this backend.
    virtual bool supports_resident_color_region_upload() const { return false; }
    // Push guest bytes for rows [first_row, last_row] of an already-valid
    // resident color surface so the GL copy stays coherent after a software
    // path wrote guest memory directly, without a full-surface re-upload.
    // key_pitch is the exact value used when the surface became resident
    // (raw SET_SURFACE_FORMAT pitch); row_bytes are derived by the backend
    // from its stored surface width. guest_bytes holds
    // (last_row-first_row+1)*row_bytes in guest row order (top row first),
    // exactly as they read from guest memory. The backend applies the same
    // vertical flip and byte-order transform its full uploads use. On success
    // the backend must also invalidate any OTHER overlapping resident surface,
    // preserving the overlap contract of invalidate_resident_color_surface.
    virtual bool upload_resident_color_rows(
        uint32_t address, size_t size, uint32_t key_pitch, uint32_t height,
        uint32_t format, uint32_t first_row, uint32_t last_row,
        const std::vector<uint8_t>& guest_bytes, std::string& error) {
        (void)address;
        (void)size;
        (void)key_pitch;
        (void)height;
        (void)format;
        (void)first_row;
        (void)last_row;
        (void)guest_bytes;
        error = "resident color region upload is not supported";
        return false;
    }
    virtual bool requires_resident_target_switch_resolve() const {
        return true;
    }
    virtual bool supports_resident_generic_combiner(
        const Nv2aGenericCombinerPacket& descriptor) const {
        (void)descriptor;
        return false;
    }
    virtual bool submit_resident_generic_combiner(
        const Nv2aGenericCombinerPacket& packet,
        Nv2aGenericCombinerResult& result, std::string& error) {
        (void)packet;
        (void)result;
        error = "resident generic-combiner submission is not supported";
        return false;
    }
    // ---- stage (f) witnessed-profile GPU execution -------------------------
    // Whether this backend implements the packet's profile AND every state
    // variation it carries. Must have no side effects: Nv2a asks before it
    // builds the (potentially large) triangle array or reads a surface.
    virtual bool supports_witnessed_profile(
        const Nv2aWitnessedProfilePacket& descriptor) const {
        (void)descriptor;
        return false;
    }
    // Whether the destination pair is already resident AND GPU-authoritative,
    // so the packet does not have to carry the initial surface bytes.
    virtual bool witnessed_target_resident(
        uint32_t color_address, size_t size, uint32_t pitch, uint32_t height,
        uint32_t format, uint32_t depth_address) const {
        (void)color_address; (void)size; (void)pitch; (void)height;
        (void)format; (void)depth_address;
        return false;
    }
    // beads-xbox.1.15.10 phase 3. The two planes go non-resident INDEPENDENTLY
    // -- a generic software draw that writes zeta invalidates only the zeta --
    // and the backend already uploads only the plane it is missing. The single
    // bool above made the device read BOTH planes out of guest memory whenever
    // either was missing, so a zeta-only break still cost a full colour
    // mem_read_ and a full colour vector. This reports them apart. It must
    // agree with what ensure_witnessed_surface will decide, hence the shared
    // slot-selection helper in the backend rather than a second predicate.
    virtual void witnessed_target_residency(
        uint32_t color_address, size_t size, uint32_t pitch, uint32_t height,
        uint32_t format, uint32_t depth_address, bool& color_resident,
        bool& depth_resident) const {
        const bool both = witnessed_target_resident(color_address, size, pitch,
                                                    height, format,
                                                    depth_address);
        color_resident = both;
        depth_resident = both;
    }
    // Always-on residency-break attribution; see Nv2aWitnessedUploadCensus.
    virtual Nv2aWitnessedUploadCensus witnessed_upload_census() const {
        return {};
    }
    // Push guest bytes for rows [first_row,last_row] of an already-resident
    // witnessed ZETA plane, the depth counterpart of upload_resident_color_rows.
    // guest_bytes holds (last_row-first_row+1)*width*4 bytes in guest row order.
    virtual bool upload_resident_depth_rows(
        uint32_t depth_address, size_t size, uint32_t first_row,
        uint32_t last_row, const std::vector<uint8_t>& guest_bytes,
        std::string& error) {
        (void)depth_address; (void)size; (void)first_row; (void)last_row;
        (void)guest_bytes;
        error = "resident depth region upload is not supported";
        return false;
    }
    // Whether the witnessed colour plane at this exact key is resident AND
    // clean enough to accept a row-scoped refresh (i.e. the GL copy is not
    // holding writes that guest memory does not have). A dirty plane must be
    // resolved, not patched.
    virtual bool witnessed_rows_refreshable(uint32_t color_address, size_t size,
                                            uint32_t pitch, uint32_t height,
                                            uint32_t format,
                                            uint32_t depth_address,
                                            bool& color_ok,
                                            bool& depth_ok) const {
        (void)color_address; (void)size; (void)pitch; (void)height;
        (void)format; (void)depth_address;
        color_ok = false;
        depth_ok = false;
        return false;
    }
    // Execute the packet against the resident destination. Failure is fatal for
    // this draw in exactly the sense submit_generic_combiner is: the caller has
    // already committed to the GPU path and cannot replay it in software.
    virtual bool submit_witnessed_profile(
        const Nv2aWitnessedProfilePacket& packet,
        Nv2aWitnessedProfileResult& result, std::string& error) {
        (void)packet; (void)result;
        error = "witnessed-profile GPU submission is not supported";
        return false;
    }
    // A full-surface CLEAR_SURFACE served as a glClear on a GPU-authoritative
    // surface. Returns false when the surface is not resident (the caller then
    // performs its normal software clear).
    // `needs_guest_fill` is an OUT parameter and the whole of the policy split
    // measured in beads-xbox.1.15.10 phase 3:
    //   plane already DIRTY (GPU-authoritative) -> false. The GL image is the
    //     truth, guest memory is stale by the existing residency contract, and
    //     the caller skips its fill. This is the case that pays: no resolve in,
    //     no fill, no invalidation, no re-upload.
    //   plane resident but CLEAN -> true. glClear alone would turn a clean
    //     plane dirty and MANUFACTURE a resolve for the next consumer, which is
    //     exactly what regressed the generic-combiner tail (+0.89 percent over
    //     the last four windows, accelerated-generic-color-snapshot resolves
    //     1 -> 101 / 473.4 MB). The caller performs its ordinary guest fill and
    //     the plane stays valid and clean, so there is no resolve AND no
    //     re-upload -- strictly better than the pre-phase-3 invalidate.
    virtual bool clear_resident_surface(
        uint32_t color_address, size_t color_size, bool clear_color,
        uint32_t color_value, uint32_t depth_address, size_t depth_size,
        bool clear_depth, uint32_t depth_value, bool& needs_guest_fill,
        std::string& error) {
        (void)color_address; (void)color_size; (void)clear_color;
        (void)color_value; (void)depth_address; (void)depth_size;
        (void)clear_depth; (void)depth_value;
        needs_guest_fill = true;
        error.clear();
        return false;
    }
    // ---- stage (f3): the GENERATED generic-combiner executor ---------------
    // Whether this backend can execute the packet's generated program AND has
    // somewhere to put its destination surface. The device asks before it
    // collects triangles, and after this returns true failure is fatal for the
    // draw, exactly as it is for submit_generic_combiner and
    // submit_witnessed_profile. It is deliberately NOT const and it MAY compile
    // and cache the generated program: a shader compile is the one step of the
    // submission that can legitimately fail, so it has to happen on the side of
    // the contract where failure is still allowed. Nothing it does is
    // guest-visible.
    virtual bool supports_generated_shader(
        const Nv2aGeneratedShaderPacket& descriptor) {
        (void)descriptor;
        return false;
    }
    // Whether the destination colour plane is already resident, so the packet
    // does not have to carry (and the device does not have to read) the whole
    // surface.
    virtual bool generated_shader_target_resident(
        uint32_t color_address, size_t size, uint32_t pitch, uint32_t height,
        uint32_t format) const {
        (void)color_address; (void)size; (void)pitch; (void)height; (void)format;
        return false;
    }
    // The same question for the ZETA plane. Keyed on (address, size) ONLY,
    // deliberately: that is the key the witnessed executor's
    // find_witnessed_depth_slot uses, and plane 0x033d0000 is made resident by
    // the witnessed executor under the COLOUR packet's pitch/height/format
    // words. A stricter key here would report "not resident" for a plane that
    // is, and the device would then read 2.4 MB of zeta out of guest memory
    // per batch for nothing.
    virtual bool generated_shader_depth_resident(uint32_t depth_address,
                                                 size_t size) const {
        (void)depth_address; (void)size;
        return false;
    }
    virtual bool submit_generated_shader(
        const Nv2aGeneratedShaderPacket& packet,
        Nv2aGeneratedShaderResult& result, std::string& error) {
        (void)packet; (void)result;
        error = "generated-shader GPU submission is not supported";
        return false;
    }
    // Always-on census of the generated-program cache, so "one program per
    // distinct combiner state" is a reported number rather than a claim.
    struct GeneratedShaderStats {
        uint64_t programs = 0;         // distinct fingerprints compiled
        uint64_t compiles = 0;         // compile calls (must equal programs)
        uint64_t cache_hits = 0;
        uint64_t draws = 0;
        uint64_t triangles = 0;
        uint64_t texture_uploads = 0;
        uint64_t texture_upload_skips = 0;  // content identity matched
        uint64_t surface_uploads = 0;
        uint64_t depth_uploads = 0;
        uint64_t depth_draws = 0;
        uint64_t compile_failures = 0;
        uint64_t stage_uniform_scalar_calls = 0;
        uint64_t stage_uniform_bulk_calls = 0;
        // Number of vec4 values requested from the driver. Bulk uploads always
        // request the full declared arrays; the GL spec ignores values beyond
        // a linked uniform array's active length.
        uint64_t stage_uniform_vectors = 0;
    };
    virtual GeneratedShaderStats generated_shader_stats() const { return {}; }
    virtual void set_generated_uniform_batching(bool on) { (void)on; }
    virtual void set_gpu_timing_enabled(bool on) { (void)on; }
    struct GeneratedSubmitCpuStats {
        bool available = false;
        bool enabled = false;
        static constexpr size_t kPhaseCount = 12;
        std::array<uint64_t, kPhaseCount> raw_ticks{};
        std::array<uint64_t, kPhaseCount> compensated_ticks{};
        std::array<uint64_t, kPhaseCount> calls{};
        uint64_t discarded_deltas = 0;
        uint64_t record_bytes = 0;
        uint64_t submits = 0;
    };
    virtual bool set_generated_submit_census(bool on) { return !on; }
    virtual GeneratedSubmitCpuStats generated_submit_cpu_stats() const {
        return {};
    }
    // Diagnostic only, and KNOWN INCORRECT when on: perform every step of a
    // generated submission except the draw call. Default off, no negative
    // spelling, never a shipping path.
    virtual void set_generated_shader_nodraw(bool on) { (void)on; }
    // Phase 7 (upload staging). ON is the shipping path: the generated
    // executor uploads a guest A8R8G8B8 word run straight to GL as GL_BGRA,
    // because the word's bytes in memory ARE B,G,R,A -- so a texture needs no
    // staging buffer at all and a colour surface needs only its vertical flip
    // as a row copy. OFF restores the per-pixel byte shuffle through
    // guest_words_to_rgba on the SAME binary, which is the arm the change is
    // measured against; it is exactness-neutral by construction (the same
    // texels reach the driver either way) and the 28-checkpoint comparison is
    // its evidence.
    virtual void set_generated_shader_bgra_upload(bool on) { (void)on; }

    virtual bool debug_probe_rgba8_image_store(std::string& error) {
        error = "RGBA8 image-store probe is not supported";
        return false;
    }
    virtual bool debug_probe_rgba8_image_load_blend(std::string& error) {
        error = "RGBA8 image-load/blend probe is not supported";
        return false;
    }
};

using GeneratedSubmitCpuStats = Nv2aRenderer::GeneratedSubmitCpuStats;

}  // namespace xlle
