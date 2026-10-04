#include "host/nv2a_xemu_psh.hpp"
#include <cmath>
extern "C" {
#include "third_party/xemu/psh.h"
}

namespace xlle {
namespace {
bool inputs_supported(const Nv2aCombinerShaderState& s, uint32_t word,
                      unsigned count = 4, bool final = false) {
    for (unsigned n = 0; n < count; ++n) {
        const unsigned r = (word >> (24 - 8 * n)) & 15;
        // Fog has no carried varying; EF_PRODUCT is only initialized in the
        // final combiner. Reserved registers never become fabricated zeroes.
        if (r == 3 || r == 6 || r == 7 || (!final && r >= 14)) return false;
        if (r == 4 && !s.needs_diffuse) return false;
        if ((r == 5 || r == 14) && !s.needs_specular) return false;
        if (r >= 8 && r <= 11 && !s.textures[r - 8].referenced) return false;
    }
    return true;
}

// xemu initializes R0.a from T0 (or one without T0); the reference currently
// initializes its scratch registers to zero. Admit only shaders which overwrite
// that alpha before observing it, including MUX's implicit dependency. Do not
// conceal the missing reference initialization by manufacturing a donor value.
bool initial_r0_alpha_unused(const Nv2aCombinerShaderState& s) {
    const auto reads_alpha = [](uint32_t word, unsigned count = 4) {
        for (unsigned n = 0; n < count; ++n) {
            const unsigned input = (word >> (24 - 8 * n)) & 31;
            if (input == 0x1C || input == 0x1E) return true;
        }
        return false;
    };
    bool written = false;
    for (unsigned i = 0; i < s.stage_count; ++i) {
        const uint32_t co = s.color_ocw[i], ao = s.alpha_ocw[i];
        if (!written && (reads_alpha(s.color_icw[i]) ||
                         reads_alpha(s.alpha_icw[i]) || ((co | ao) & 0x4000u)))
            return false;
        for (unsigned shift : {0u, 4u, 8u})
            written |= ((ao >> shift) & 15) == 12;
        written |= ((co & 0x80000u) && ((co >> 4) & 15) == 12) ||
                   ((co & 0x40000u) && (co & 15) == 12);
    }
    return written || (!reads_alpha(s.final_cw0) && !reads_alpha(s.final_cw1, 3));
}
}

XemuPshReject nv2a_xemu_psh_supported(const Nv2aGeneratedShaderPacket& p) {
    using R = XemuPshReject;
    const auto& g = p.guest;
    const auto& s = p.state;
    if (!g.valid || g.shade_mode != 0x1D01 ||
        (g.control0 & ((1u << 12) | (1u << 16))) ||
        ((p.surface_format >> 4) & 15) != 2 ||
        !s.final_enabled || !(s.final_cw0 || s.final_cw1) ||
        g.zminmax != 1)
        return R::kState;
    if ((g.combiner_control & 0xFFu) != s.stage_count || s.stage_count > 8 ||
        nv2a_combiner_shader_supported(s) != Nv2aCombinerShaderReject::kNone ||
        !inputs_supported(s, s.final_cw0, 4, true) ||
        !inputs_supported(s, s.final_cw1, 3, true) ||
        !initial_r0_alpha_unused(s)) return R::kCombiner;
    // E/F cannot recursively refer to SUM/EF_PRODUCT in the donor emitter.
    if (((s.final_cw1 >> 24) & 15) >= 14 ||
        ((s.final_cw1 >> 16) & 15) >= 14) return R::kCombiner;
    for (unsigned i = 0; i < s.stage_count; ++i) {
        if (!inputs_supported(s, s.color_icw[i]) || !inputs_supported(s, s.alpha_icw[i]))
            return R::kCombiner;
        // The current reference evaluator reads per-stage constants. Until its
        // shared-constant contract is implemented, only admit shared mode when
        // those values agree; never use enhanced output to conceal a floor gap.
        if (!(g.combiner_control & 0x1000u) && p.stage_const0[i] != p.stage_const0[0]) return R::kCombiner;
        if (!(g.combiner_control & 0x10000u) && p.stage_const1[i] != p.stage_const1[0]) return R::kCombiner;
    }
    // Stage selectors in the upper bits are inert for independent PROJECT2D
    // samples. They are commonly left programmed by earlier guest shaders.
    if (g.shader_input & 0xFFFu) return R::kTexture;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned mode = (g.shader_program >> (5 * i)) & 31;
        if (mode == 0) {
            if (s.textures[i].referenced || p.textures[i].present) return R::kTexture;
            continue;
        }
        const auto& t = s.textures[i];
        const uint32_t fmt = g.texture_format[i], ctl = g.texture_control[i];
        const uint32_t filter = g.texture_filter[i];
        // Pure independent PROJECT2D has no observable effect when no combiner
        // input reads it and alpha-kill/color-key are disabled. The reference
        // skips that fetch as well; strip it from the donor program instead of
        // demanding (or inventing) an unused texture snapshot/mip chain.
        if (mode == 1 && !t.referenced && !p.textures[i].present &&
            !(ctl & 7u) && !(fmt & 4u) && ((fmt >> 4) & 15) == 2)
            continue;
        const unsigned min_filter = nv2a_xemu_min_filter(fmt, filter);
        const uint32_t color = (fmt >> 8) & 255;
        const bool linear = t.mode == Nv2aCombinerShaderState::TexMode::kLinear2d;
        // The device's single-stage PROJECT2D path decodes DXT1/DXT2-3
        // into the same owned, tight ARGB image as a raw linear texture.
        // The shader consumes that decoded image, not compressed guest bytes.
        // Do not admit other compressed layouts or programs: those have not
        // passed through this reference decoder/coordinate contract.
        const bool decoded_dxt = (color == 0x0C || color == 0x0E) &&
                                 linear && i == 0 && g.shader_program == 1;
        if (linear ? (color != 0x12 && color != 0x1E && !decoded_dxt) :
                     (color != 0x06 && color != 0x07 && color != 0x19 && color != 0x0B))
            return R::kTexture;
        if (mode != 1 || !t.referenced || !p.textures[i].present ||
            // The general reference evaluator has no texture alpha-kill or
            // color-key implementation yet, so neither is interchangeable.
            !(ctl & 0x40000000u) || (ctl & 7u) || (fmt & 4u) || !(fmt & 8u) ||
            ((fmt >> 4) & 15) != 2 || (filter & 0xF0000000u) ||
            min_filter < 1 || min_filter > 2 ||
            ((filter >> 24) & 15) < 1 || ((filter >> 24) & 15) > 2 ||
            (t.mode != Nv2aCombinerShaderState::TexMode::kProject2d &&
             t.mode != Nv2aCombinerShaderState::TexMode::kLinear2d) ||
            t.address_u == Nv2aCombinerShaderState::AddressMode::kBorder ||
            t.address_v == Nv2aCombinerShaderState::AddressMode::kBorder)
            return R::kTexture;
    }
#if XLLE_NATIVE_DRAW
    if (!p.native_vertices.empty()) {
        if (p.native_primitive < 5 || p.native_primitive > 7 ||
            p.native_vertices.size() > 0x7fffffffU || !p.triangles.empty()) return R::kGeometry;
        for (const auto& v : p.native_vertices) {
            for (float f : v.position) if (!std::isfinite(f)) return R::kGeometry;
            if (v.position[3] <= 0) return R::kGeometry;
            for (float f : v.diffuse) if (!std::isfinite(f)) return R::kGeometry;
            for (float f : v.specular) if (!std::isfinite(f)) return R::kGeometry;
            for (unsigned stage = 0; stage < 4; ++stage) {
                if (!s.textures[stage].referenced) continue;
                for (float f : v.texture[stage]) if (!std::isfinite(f)) return R::kGeometry;
                // Same-sign projective coordinates cannot cross a pole within
                // a triangle. Other inputs retain the complete existing path.
                if (v.texture[stage][3] <= 0) return R::kGeometry;
            }
        }
    }
#endif
    for (const auto& t : p.triangles) {
        if (t.wide || !std::isfinite(t.depth_bias) || t.depth_bias != 0 ||
            !std::isfinite(t.za) || !std::isfinite(t.zb) || !std::isfinite(t.zc))
            return R::kGeometry;
        for (unsigned v = 0; v < 3; ++v) {
            if (!std::isfinite(t.inv_w[v]) || t.inv_w[v] <= 0 ||
                !std::isfinite(1.0f / t.inv_w[v])) return R::kGeometry;
            for (const auto& varying : t.varying[v])
                for (float value : varying) if (!std::isfinite(value)) return R::kGeometry;
            const int64_t x = v == 0 ? t.ax : (v == 1 ? t.bx : t.cx);
            const int64_t y = v == 0 ? t.ay : (v == 1 ? t.by : t.cy);
            const float w = 1.0f / t.inv_w[v];
            const float nx = float(x) * 0.0625f / float(p.surface_width) * 2.0f - 1.0f;
            const float ny = 1.0f - float(y) * 0.0625f / float(p.surface_height) * 2.0f;
            if (!std::isfinite(nx * w) || !std::isfinite(ny * w)) return R::kGeometry;
            for (unsigned stage = 0; stage < 4; ++stage) {
                if (!s.textures[stage].referenced) continue;
                const float q = t.varying[v][stage + 2][3];
                const float q0 = t.varying[0][stage + 2][3];
                if (q == 0 || std::signbit(q) != std::signbit(q0)) return R::kGeometry;
            }
        }
    }
    return R::kNone;
}

uint64_t nv2a_xemu_psh_key(const Nv2aGeneratedShaderPacket& p) {
    uint64_t h = p.fingerprint;
    using combiner_glsl_detail::fnv_word;
    fnv_word(h, 0x78656d752d707368ull);
#if XLLE_NATIVE_DRAW
    fnv_word(h, !p.native_vertices.empty());
#endif
    fnv_word(h, p.guest.combiner_control);
    fnv_word(h, p.guest.shader_program);
    fnv_word(h, p.guest.shader_input);
    for (size_t i = 0; i < 4; ++i) {
        fnv_word(h, p.guest.texture_format[i]);
        fnv_word(h, p.guest.texture_control[i]);
        fnv_word(h, p.guest.texture_filter[i]);
    }
    return h;
}

std::string nv2a_xemu_fragment_glsl(const Nv2aGeneratedShaderPacket& p) {
    if (nv2a_xemu_psh_supported(p) != XemuPshReject::kNone) return {};
    PshState state{};
    state.combiner_control = p.guest.combiner_control;
    state.shader_stage_program = p.guest.shader_program;
    for (unsigned i = 0; i < 4; ++i)
        if (!p.state.textures[i].referenced)
            state.shader_stage_program &= ~(31u << (5 * i));
    state.other_stage_input = p.guest.shader_input;
    state.final_inputs_0 = p.state.final_cw0;
    state.final_inputs_1 = p.state.final_cw1;
    for (unsigned i = 0; i < 8; ++i) {
        state.rgb_inputs[i] = p.state.color_icw[i];
        state.rgb_outputs[i] = p.state.color_ocw[i];
        state.alpha_inputs[i] = p.state.alpha_icw[i];
        state.alpha_outputs[i] = p.state.alpha_ocw[i];
    }
    for (unsigned i = 0; i < 4; ++i) {
        state.dim_tex[i] = 2;
        state.rect_tex[i] = p.state.textures[i].mode == Nv2aCombinerShaderState::TexMode::kLinear2d;
        state.alphakill[i] = (p.guest.texture_control[i] & 4u) != 0;
    }
    state.smooth_shading = true;
    state.depth_clipping = true;
    state.depth_format = DEPTH_FORMAT_D24;
    state.alpha_test = p.state.alpha_test;
    state.alpha_func = static_cast<PshAlphaFunc>(p.state.alpha_func & 7);
    MString* generated = pgraph_glsl_gen_psh(&state, {false, 0, 0});
    if (!generated) return {};
    std::string result = mstring_get_str(generated);
    mstring_unref(generated);
#if XLLE_NATIVE_DRAW
    if (!p.native_vertices.empty()) {
        // Retain the donor combiner and depth tests, but use the driver's
        // linear depth interpolation instead of per-fragment edge equations.
        const auto begin = result.find("vec2 unscaled_xy = gl_FragCoord.xy / surfaceScale;");
        const std::string end_marker = "zvalue += depthFactor*triMZ;\n";
        const auto end = result.find(end_marker, begin);
        if (begin == std::string::npos || end == std::string::npos) return {};
        result.replace(begin, end + end_marker.size() - begin,
                       "float zvalue = native_depth;\n");
        const auto main = result.find("void main() {");
        if (main == std::string::npos) return {};
        result.insert(main, "noperspective in float native_depth;\n");
    }
#endif
    // X8's stored padding byte must be FF, while blending uses the real
    // combiner alpha. Preserve both through the existing dual-source ROP path.
    if (p.state.color_format_x8) {
        if (p.state.blend) {
            const std::string declaration = "layout(location = 0) out vec4 fragColor;";
            result.replace(result.find(declaration), declaration.size(),
                "layout(location = 0, index = 0) out vec4 fragColor;\n"
                "layout(location = 0, index = 1) out vec4 out_blend;");
        }
        const size_t end = result.rfind('}');
        result.insert(end, p.state.blend ? "out_blend = vec4(fragColor.a);\nfragColor.a = 1.0;\n" :
                                        "fragColor.a = 1.0;\n");
    }
    return result;
}

// Shared owned triangle record; real driver coverage and perspective-correct
// interpolation replace software edge tests and double-precision pixel loops.
const char* const kNv2aXemuVertexSource = R"GLSL(#version 430 core
#extension GL_ARB_gpu_shader_int64 : require
struct GenTri {
    i64vec4 ab; i64vec4 cp; vec4 zi; vec4 wv; vec4 misc;
    vec4 v[18]; vec4 pad; dvec4 recip;
};
layout(std430, binding = 0) readonly buffer TriBuffer { GenTri tris[]; };
uniform vec2 surface_size;
out vec4 vtxD0, vtxD1, vtxB0, vtxB1;
out float vtxFog;
out vec4 vtxT0, vtxT1, vtxT2, vtxT3;
flat out vec4 vtxPos0, vtxPos1, vtxPos2;
flat out float triMZ;
void main() {
    int n = gl_VertexID / 3, k = gl_VertexID % 3;
    vec2 a = vec2(tris[n].ab.xy) * 0.0625;
    vec2 b = vec2(tris[n].ab.zw) * 0.0625;
    vec2 c = vec2(tris[n].cp.xy) * 0.0625;
    a.y = surface_size.y - a.y;
    b.y = surface_size.y - b.y;
    c.y = surface_size.y - c.y;
    vtxPos0 = vec4(a, tris[n].zi.x, 1.0 / tris[n].wv.y);
    vtxPos1 = vec4(b, tris[n].zi.y, 1.0 / tris[n].wv.z);
    vtxPos2 = vec4(c, tris[n].zi.z, 1.0 / tris[n].wv.w);
    vec4 pos = k == 0 ? vtxPos0 : (k == 1 ? vtxPos1 : vtxPos2);
    gl_Position = vec4((pos.xy / surface_size * 2.0 - 1.0) * pos.w, 0.0, pos.w);
    vtxD0 = tris[n].v[k * 6]; vtxD1 = tris[n].v[k * 6 + 1];
    vtxT0 = tris[n].v[k * 6 + 2]; vtxT1 = tris[n].v[k * 6 + 3];
    vtxT2 = tris[n].v[k * 6 + 4]; vtxT3 = tris[n].v[k * 6 + 5];
    // These varyings are unreachable by the supported combiner contract.
    vtxB0 = vec4(0); vtxB1 = vec4(0); vtxFog = 0.0; triMZ = 0.0;
}
)GLSL";
#if XLLE_NATIVE_DRAW
// Whole-draw GPU geometry, following the native submission model of the
// DOAXBV donor. Guest shader outputs are screen coordinates with clip W.
// Depth clipping stays in the donor fragment shader; XY clipping, winding
// and varying interpolation belong to the host graphics pipeline.
const char* const kNv2aNativeVertexSource = R"GLSL(#version 430 core
struct Vertex { vec4 position, diffuse, specular; vec4 texture[4]; };
layout(std430, binding = 0) readonly buffer Vertices { Vertex vertices[]; };
uniform vec2 surface_size;
out vec4 vtxD0, vtxD1, vtxB0, vtxB1;
out float vtxFog;
out vec4 vtxT0, vtxT1, vtxT2, vtxT3;
flat out vec4 vtxPos0, vtxPos1, vtxPos2;
flat out float triMZ;
noperspective out float native_depth;
void main() {
    Vertex v = vertices[gl_VertexID];
    vec2 ndc = vec2(v.position.x / surface_size.x * 2.0 - 1.0,
                    1.0 - v.position.y / surface_size.y * 2.0);
    gl_Position = vec4(ndc * v.position.w, 0.0, v.position.w);
    native_depth = v.position.z;
    vtxD0 = clamp(v.diffuse, 0.0, 1.0); vtxD1 = clamp(v.specular, 0.0, 1.0);
    vtxT0 = v.texture[0]; vtxT1 = v.texture[1];
    vtxT2 = v.texture[2]; vtxT3 = v.texture[3];
    // Not read by this admitted combiner/depth contract.
    vtxB0 = vtxB1 = vtxPos0 = vtxPos1 = vtxPos2 = vec4(0);
    vtxFog = triMZ = 0.0;
}
)GLSL";
#endif

}
