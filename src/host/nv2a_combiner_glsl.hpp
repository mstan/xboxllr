// Data-driven NV2A register-combiner -> GLSL fragment-program generator.
// beads-xbox.1.15.10 phase 6, stage (f3) of beads-xbox.1.15.7.
//
// WHY THIS EXISTS, with the number it attacks. The phase-6 census
// ([host] generic-shader / -words / -dest, always on) measured that all 704
// transferring `software-generic-color-surface` resolves of the canonical 12 G
// replay land on ONE destination plane, 0x03AA4000, and are caused by exactly
// EIGHT distinct generic-combiner shader states across 1 339 software-fallback
// batches. At the phase-5 per-resolve cost (2.92 ms) that is 2.06 s, 3.6 pct of
// the run, plus 506 Mpx of software scanline work. Hand-writing eight more
// fixtures is what the existing accelerated generic path already does five
// times over (kUntexturedPrimaryTriangle, kDashboardProject2dFan,
// kResidentPrimaryColorTriangles, kResidentConstantColorTriangles,
// kResidentTextureModulateTriangles) and it does not generalize: 946 distinct
// combiner fingerprints have been observed. This generator is the general form.
//
// WHAT IS HARVESTED FROM xemu, AND HOW IT IS RE-EXPRESSED.
// refs/xemu/hw/xbox/nv2a/pgraph/glsl/psh.c is the reference for the SEMANTICS
// of the register combiner -- the input mappings, the AB/CD/MUX/SUM structure,
// the output scale/bias, the final-combiner A*B+(1-A)*C+D shape with its EF
// product and V1R0_SUM, and the per-stage constant sourcing. None of its CODE
// is copied: xemu emits GLSL against ITS OWN pipeline (driver rasterization,
// its own texture units, its own uniform block), and this file emits GLSL
// against the phase-1 executor's contract instead -- exact in-shader coverage
// from int64 edge functions, carried reciprocals, explicit bilinear in the
// software rasterizer's own operation order, and a destination surface that
// stays GPU-resident. The expressions below are therefore the SOFTWARE
// RASTERIZER's own fragment body (src/machine/nv2a.cpp,
// rasterize_generic_combiner_batch) transcribed statement for statement, and
// psh.c is the cross-check that the software body decodes the hardware
// correctly -- which nv2a.cpp's own comments already record it doing, register
// by register.
//
// EXACTNESS RULES CARRIED FORWARD FROM PHASE 1 (beads-xbox.1.15.7):
//   * Coverage is recomputed in the fragment shader from the SAME int64 edge
//     functions and the SAME top-left rule at the SAME 1/16-pixel sample
//     points, so the driver only ever generates CANDIDATE fragments and the
//     shader can remove one but never add one.
//   * Depth is recomputed in the shader from the same expression and written
//     through gl_FragDepth, so the driver's own interpolation never decides it.
//   * The perspective reciprocals are CARRIED from the device (1/area and
//     1/w per vertex) rather than divided for on the GPU.
//   * Bilinear filtering is written out explicitly in the software sampler's
//     operation order against texelFetch, never left to a LINEAR sampler whose
//     weights are fixed point.
//   * Swizzled textures are unswizzled by the DEVICE on the way in (it owns the
//     Morton table) and uploaded as linear RGBA8. Upload reuse uses a unique
//     exact-snapshot content ID, or FNV-64 on the legacy path.
//   * FP32 throughout, matching the shipping witnessed shaders (docs/
//     PERF-METHODOLOGY.md section 3b). The software path carries the
//     barycentric division in double; that difference is the same precision
//     trade section 3a already accepted, and the 28-checkpoint comparison is
//     its acceptance test.
#pragma once

#include <cstdint>
#include <string>

namespace xlle {

// Everything the GENERATED SOURCE is a function of, and nothing else. Values
// that only parameterise an otherwise identical program -- the eight per-stage
// constant pairs, the two final factors, texture dimensions and pitches, the
// border colours, the alpha reference, the clip range -- are UNIFORMS and must
// not appear here, or one program would be compiled per constant value.
struct Nv2aCombinerShaderState {
    // How the fragment's final colour is produced. The software rasterizer has
    // four exact shortcuts that bypass the general final combiner, and they are
    // NOT always equal to evaluating it: kPrimaryPassthrough emits the
    // interpolated diffuse verbatim with no combiner clamp at all. Reproducing
    // the shortcut rather than the general form is what keeps the generated
    // program equal to the software body it replaces.
    enum class FinalMode : uint8_t {
        kGeneric,                  // D + A*B + (1-A)*C, alpha = G
        kPassthroughR0,            // both SpecularFog words zero: R0 / R0.a
        kPrimaryColor,             // final = diffuse
        kTexture0ModulatePrimary,  // rgb = t0*diffuse, a = diffuse.a
        kTexture0ModulatePrimaryAlpha,  // ... a = t0.a * diffuse.a
        kTexture0Passthrough,      // rgb = t0, a = diffuse.a
        kBlackAlphaOne,            // (0,0,0,1); texture sampling is skipped
    };
    // PS_TEXTUREMODES as this evaluator implements them, plus the destination
    // layout distinction the software sampler makes (a "linear" texture is
    // addressed in TEXEL space through a row pitch; a swizzled one is addressed
    // in NORMALISED space through the Morton table the device already applied).
    enum class TexMode : uint8_t {
        kNone,
        kSwizzled2d,   // mode 1, non-projective: u,v used directly
        kProject2d,    // mode 1, projective: u/q, v/q
        kLinear2d,     // pitch-linear image, texel-space coordinates, /q
        kCubemap,      // mode 3: direction vector, major-axis remap
    };
    enum class AddressMode : uint8_t { kRepeat, kClampToEdge, kBorder };
    struct TextureStageState {
        bool referenced = false;
        TexMode mode = TexMode::kNone;
        AddressMode address_u = AddressMode::kRepeat;
        AddressMode address_v = AddressMode::kRepeat;
        // An A8 texture samples as (1,1,1,a). The device expands it to RGBA8 on
        // upload, so the shader needs no branch -- the flag is kept because it
        // is part of the state's identity and a future non-expanding upload
        // path would need it.
        bool alpha8 = false;
    };

    uint8_t stage_count = 0;
    bool mux_on_msb = false;
    uint32_t color_icw[8] = {};
    uint32_t alpha_icw[8] = {};
    uint32_t color_ocw[8] = {};
    uint32_t alpha_ocw[8] = {};
    uint32_t final_cw0 = 0;
    uint32_t final_cw1 = 0;
    bool final_enabled = false;
    bool needs_general_combiner = true;
    bool needs_diffuse = true;
    bool needs_specular = false;
    FinalMode final_mode = FinalMode::kGeneric;
    TextureStageState textures[4] = {};
    // Alpha test is IN the shader (the software path compares the packed byte
    // against the reference, which GL's own alpha test could not do since it
    // was removed in core profile anyway). The reference is a uniform.
    bool alpha_test = false;
    uint32_t alpha_func = 0x0207;
    // Whether the guest asked for blending at all.
    //
    // MEASURED, not assumed: GL fixed-function blending does NOT reproduce the
    // software path's blend. The differential verifier
    // (--nv2a-gpu-generic-shader-verify, which runs both paths on the same
    // starting image and the same collected geometry) reported, for every one
    // of twenty consecutive batches of the primary-colour program, a maximum
    // difference of exactly 1 LSB spread over about 90 pct of the plane, in
    // BOTH directions. That is the signature of GL quantising the source
    // colour and the blend factor to the framebuffer's 8 bits before
    // evaluating `Cs*Sf + Cd*Df`, where the software path evaluates it on the
    // unquantised floats and only rounds the RESULT. One LSB per layer is
    // invisible; the dashboard composites about five hundred blended layers
    // onto one plane, and the same measurement end to end was a maximum of 38
    // LSB over 79 pct of the frame.
    //
    // So the blend, the colour mask and the X8R8G8B8 alpha rule are all done
    // IN THE SHADER against an image binding, under a fragment-shader
    // interlock, using the software path's own expressions and its own
    // floor(x*255+0.5) pack. `image_blend` selects that; false restores GL
    // fixed-function blending on the same binary, which is how the two were
    // compared.
    bool blend = false;
    bool image_blend = true;
    // Source-affecting once the blend moves into the shader: the factors, the
    // per-channel write mask and the X8 alpha rule are all now code.
    uint32_t blend_src = 0;
    uint32_t blend_dst = 0;
    bool color_mask[4] = {true, true, true, true};
    // Whether gl_FragDepth is written at all.
    bool uses_depth = false;
    // Whether the perspective-correct interpolation runs in DOUBLE, matching
    // the software rasterizer's own arithmetic, or in single precision like the
    // shipping witnessed shaders.
    //
    // This is not the same trade the witnessed profiles took. Their geometry is
    // ordinary screen-space triangles; the generic-combiner batches this
    // executor takes are, by construction, the ones the hand-written resident
    // fixture DECLINED -- and its commonest decline reason is a triangle that
    // crosses the w = 0 plane. The software clipper cuts those at
    // clip_w >= 1e-6, so a clipped vertex carries 1/w = 1e6 while its
    // neighbours carry 1/w near 1, and `(q0*v0 + q1*v1 + q2*v2) / (q0+q1+q2)`
    // then spans six orders of magnitude. FP32 has seven digits; the software
    // path has fifteen. Measured: FP32 put 1-LSB gradient banding across most
    // of the dashboard with a local excursion to 21 LSB, and the same run in
    // double is clean.
    bool fp64_interpolate = true;
    // X8R8G8B8 stores 0xFF in the padding byte; the alpha channel is masked off
    // at the GL level, so the shader's alpha output is irrelevant then -- but it
    // is still part of the identity because the ALPHA TEST reads it.
    bool color_format_x8 = false;
};

// FNV-1a over exactly the fields above. Two states with the same fingerprint
// produce byte-identical source; this is the program cache key. Header-inline
// because the DEVICE computes it too (to key its own decline census and its
// batch identity) and src/machine links no host object.
namespace combiner_glsl_detail {
inline void fnv_word(uint64_t& hash, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        hash ^= (value >> (i * 8)) & 0xFFu;
        hash *= 1099511628211ull;
    }
}
inline bool dst_supported(uint32_t dst) {
    return dst == 0 || dst == 12 || dst == 13;
}
inline bool scale_bias_supported(uint32_t sb) {
    return sb == 0x00 || sb == 0x08 || sb == 0x10 || sb == 0x18 ||
           sb == 0x20 || sb == 0x30;
}
inline bool input_register_supported(uint32_t reg) {
    return reg == 0 || reg == 1 || reg == 2 || reg == 4 || reg == 5 ||
           (reg >= 8 && reg <= 15);
}
}  // namespace combiner_glsl_detail

// The generated fragment program. Deterministic: the same state always yields
// the same bytes, which is what makes the cache and its guard test meaningful.
std::string nv2a_generate_combiner_fragment_glsl(
    const Nv2aCombinerShaderState& state);

// The single vertex program every generated fragment program pairs with. It is
// fixed because the triangle record is fixed: six vertices per record, the
// ordinary case placing the triangle itself and the `wide` case placing the
// software path's own scanline bounding box, exactly as the witnessed executor
// does (src/host/nv2a_witnessed_gl_shaders.hpp).
extern const char* const kNv2aGeneratedVertexSource;

// True when the state uses only features the generator emits. A state that
// fails this must go to the software path; the caller counts the reason.
enum class Nv2aCombinerShaderReject : uint8_t {
    kNone = 0,
    kStageCount,
    kOutputDestination,
    kOutputScaleBias,
    kInputRegister,
    kTextureMode,
    kAlphaFunc,
    kCount,
};
inline uint64_t nv2a_combiner_shader_fingerprint(
    const Nv2aCombinerShaderState& state) {
    uint64_t h = 1469598103934665603ull;
    combiner_glsl_detail::fnv_word(h, state.stage_count);
    combiner_glsl_detail::fnv_word(h, state.mux_on_msb ? 1u : 0u);
    for (int i = 0; i < 8; ++i) {
        // Only the stages the combiner actually runs can affect the source.
        if (i >= state.stage_count) continue;
        combiner_glsl_detail::fnv_word(h, state.color_icw[i]);
        combiner_glsl_detail::fnv_word(h, state.alpha_icw[i]);
        combiner_glsl_detail::fnv_word(h, state.color_ocw[i]);
        combiner_glsl_detail::fnv_word(h, state.alpha_ocw[i]);
    }
    combiner_glsl_detail::fnv_word(h, state.final_cw0);
    combiner_glsl_detail::fnv_word(h, state.final_cw1);
    combiner_glsl_detail::fnv_word(h, state.final_enabled ? 1u : 0u);
    combiner_glsl_detail::fnv_word(h, state.needs_general_combiner ? 1u : 0u);
    combiner_glsl_detail::fnv_word(h, state.needs_diffuse ? 1u : 0u);
    combiner_glsl_detail::fnv_word(h, state.needs_specular ? 1u : 0u);
    combiner_glsl_detail::fnv_word(h, static_cast<uint32_t>(state.final_mode));
    for (int i = 0; i < 4; ++i) {
        const auto& t = state.textures[i];
        combiner_glsl_detail::fnv_word(h, (t.referenced ? 1u : 0u) |
                   (static_cast<uint32_t>(t.mode) << 1) |
                   (static_cast<uint32_t>(t.address_u) << 8) |
                   (static_cast<uint32_t>(t.address_v) << 12) |
                   (t.alpha8 ? (1u << 16) : 0u));
    }
    combiner_glsl_detail::fnv_word(h, state.alpha_test ? state.alpha_func : 0u);
    combiner_glsl_detail::fnv_word(h, state.blend ? 1u : 0u);
    combiner_glsl_detail::fnv_word(h, state.image_blend ? 1u : 0u);
    if (state.image_blend) {
        combiner_glsl_detail::fnv_word(
            h, state.blend ? (state.blend_src | (state.blend_dst << 16)) : 0u);
        combiner_glsl_detail::fnv_word(
            h, (state.color_mask[0] ? 1u : 0u) |
                   (state.color_mask[1] ? 2u : 0u) |
                   (state.color_mask[2] ? 4u : 0u) |
                   (state.color_mask[3] ? 8u : 0u));
    }
    combiner_glsl_detail::fnv_word(h, state.uses_depth ? 1u : 0u);
    combiner_glsl_detail::fnv_word(
        h, state.fp64_interpolate ? 1u : 0u);
    combiner_glsl_detail::fnv_word(h, state.color_format_x8 ? 1u : 0u);
    return h;
}

inline Nv2aCombinerShaderReject nv2a_combiner_shader_supported(
    const Nv2aCombinerShaderState& state) {
    using R = Nv2aCombinerShaderReject;
    if (state.stage_count > 8) return R::kStageCount;
    if (state.needs_general_combiner && state.stage_count == 0)
        return R::kStageCount;
    for (uint32_t s = 0; s < state.stage_count; ++s) {
        const uint32_t co = state.color_ocw[s];
        const uint32_t ao = state.alpha_ocw[s];
        if (!combiner_glsl_detail::dst_supported(co & 0xFu) || !combiner_glsl_detail::dst_supported((co >> 4) & 0xFu) ||
            !combiner_glsl_detail::dst_supported((co >> 8) & 0xFu) || !combiner_glsl_detail::dst_supported(ao & 0xFu) ||
            !combiner_glsl_detail::dst_supported((ao >> 4) & 0xFu) ||
            !combiner_glsl_detail::dst_supported((ao >> 8) & 0xFu)) {
            return R::kOutputDestination;
        }
        if (!combiner_glsl_detail::scale_bias_supported((co >> 12) & 0x38u) ||
            !combiner_glsl_detail::scale_bias_supported((ao >> 12) & 0x38u)) {
            return R::kOutputScaleBias;
        }
        const uint32_t words[2] = {state.color_icw[s], state.alpha_icw[s]};
        for (uint32_t w : words) {
            for (int b = 0; b < 4; ++b) {
                const uint32_t reg = (w >> (b * 8)) & 0xFu;
                if (!combiner_glsl_detail::input_register_supported(reg)) return R::kInputRegister;
            }
        }
    }
    if (state.final_enabled) {
        const uint32_t bytes[7] = {
            (state.final_cw0 >> 24) & 0xFFu, (state.final_cw0 >> 16) & 0xFFu,
            (state.final_cw0 >> 8) & 0xFFu,  state.final_cw0 & 0xFFu,
            (state.final_cw1 >> 24) & 0xFFu, (state.final_cw1 >> 16) & 0xFFu,
            (state.final_cw1 >> 8) & 0xFFu};
        for (uint32_t b : bytes) {
            if (!combiner_glsl_detail::input_register_supported(b & 0xFu)) return R::kInputRegister;
        }
    }
    for (const auto& t : state.textures) {
        if (!t.referenced) continue;
        if (t.mode == Nv2aCombinerShaderState::TexMode::kNone)
            return R::kTextureMode;
    }
    if (state.alpha_test &&
        (state.alpha_func < 0x0200 || state.alpha_func > 0x0207)) {
        return R::kAlphaFunc;
    }
    return R::kNone;
}

inline const char* nv2a_combiner_shader_reject_name(Nv2aCombinerShaderReject r) {
    switch (r) {
        case Nv2aCombinerShaderReject::kNone: return "none";
        case Nv2aCombinerShaderReject::kStageCount: return "stage-count";
        case Nv2aCombinerShaderReject::kOutputDestination: return "ocw-dst";
        case Nv2aCombinerShaderReject::kOutputScaleBias: return "ocw-scale";
        case Nv2aCombinerShaderReject::kInputRegister: return "icw-register";
        case Nv2aCombinerShaderReject::kTextureMode: return "texture-mode";
        case Nv2aCombinerShaderReject::kAlphaFunc: return "alpha-func";
        default: break;
    }
    return "unknown";
}

}  // namespace xlle
