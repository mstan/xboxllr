#include "nv2a_combiner_glsl.hpp"

#include <cstdio>
#include <string>

namespace xlle {
namespace {

// ---------------------------------------------------------------------------
// The ICW byte layout, identical to the software decoder in nv2a.cpp (which is
// itself cross-checked against xemu's parse_combiner_inputs / psh_regs.h):
// A = bits 31:24, B = 23:16, C = 15:8, D = 7:0; within a byte, bits 3:0 are the
// PS_REGISTER id, bit 4 selects the ALPHA channel (colour path) or ALPHA rather
// than BLUE (alpha path), and bits 7:5 are the PS_INPUTMAPPING.
struct DecodedInput {
    uint32_t reg = 0;
    bool alt = false;
    uint32_t mapping = 0;
};
DecodedInput decode_input(uint32_t byte) {
    DecodedInput in;
    in.reg = byte & 0xFu;
    in.alt = ((byte >> 4) & 1u) != 0;
    in.mapping = (byte >> 5) & 0x7u;
    return in;
}

// PS_INPUTMAPPING, textually the software rasterizer's apply_mapping lambda.
// Only UNSIGNED_INVERT clamps before inverting; every other unsigned/half-bias
// mapping only floors at zero, so an intermediate register legitimately holding
// a scaled value above 1 (through an output SHIFTLEFTBY) is not silently
// clipped by the next stage that reads it.
const char* kMappingBody[8] = {
    "max(x, ZERO)",                       // UNSIGNED_IDENTITY
    "(ONE - clamp(x, ZERO, ONE))",        // UNSIGNED_INVERT
    "(TWO * max(x, ZERO) - ONE)",         // EXPAND_NORMAL
    "(-TWO * max(x, ZERO) + ONE)",        // EXPAND_NEGATE
    "(max(x, ZERO) - HALF)",              // HALF_BIAS_NORMAL
    "(-max(x, ZERO) + HALF)",             // HALF_BIAS_NEGATE
    "x",                                  // SIGNED_IDENTITY
    "(-x)",                               // SIGNED_NEGATE
};

std::string mapping_functions() {
    std::string out;
    for (int m = 0; m < 8; ++m) {
        std::string body = kMappingBody[m];
        std::string f = body, v = body;
        auto subst = [](std::string s, const char* from, const char* to) {
            std::string r;
            const size_t n = std::string(from).size();
            for (size_t i = 0; i < s.size();) {
                if (s.compare(i, n, from) == 0) {
                    r += to;
                    i += n;
                } else {
                    r += s[i++];
                }
            }
            return r;
        };
        f = subst(subst(subst(subst(f, "ZERO", "0.0"), "ONE", "1.0"),
                        "TWO", "2.0"),
                  "HALF", "0.5");
        v = subst(subst(subst(subst(v, "ZERO", "vec3(0.0)"), "ONE", "vec3(1.0)"),
                        "TWO", "vec3(2.0)"),
                  "HALF", "vec3(0.5)");
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "float gc_m%d(float x) { return %s; }\n"
                      "vec3 gc_m%d(vec3 x) { return %s; }\n",
                      m, f.c_str(), m, v.c_str());
        out += buf;
    }
    return out;
}

// The register file expression for one input. Registers 14 (V1R0_SUM) and 15
// (EF_PROD) are not stored values -- they are computed on the fly from other
// registers under the FINAL combiner's own flags, matching the software path's
// get_register_value() and xemu's get_var().
std::string reg_vec4(uint32_t reg) {
    if (reg == 14) return "gc_r14()";
    if (reg == 15) return "gc_r15()";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "R[%u]", reg);
    return buf;
}

std::string color_input_expr(const DecodedInput& in) {
    // fetch_color: the base is r.aaa when bit4 is set, r.rgb otherwise.
    const std::string base = reg_vec4(in.reg) + (in.alt ? ".aaa" : ".rgb");
    char buf[128];
    std::snprintf(buf, sizeof(buf), "gc_m%u(%s)", in.mapping, base.c_str());
    return buf;
}

std::string alpha_input_expr(const DecodedInput& in) {
    // fetch_alpha: bit4 selects ALPHA (1) or BLUE (0). This polarity matches
    // xemu's get_input_var() PS_CHANNEL handling for the alpha portion.
    const std::string base = reg_vec4(in.reg) + (in.alt ? ".a" : ".b");
    char buf[128];
    std::snprintf(buf, sizeof(buf), "gc_m%u(%s)", in.mapping, base.c_str());
    return buf;
}

// get_output(): the scale/bias op is applied to the AB/CD/SUM result BEFORE the
// final clamp to [-1,1], and the SAME field applies to all three outputs.
std::string scale_bias_expr(uint32_t sb, const std::string& value) {
    switch (sb) {
        case 0x00: return value;
        case 0x08: return "(" + value + " - 0.5)";
        case 0x10: return "(" + value + " * 2.0)";
        case 0x18: return "((" + value + " - 0.5) * 2.0)";
        case 0x20: return "(" + value + " * 4.0)";
        default:   return "(" + value + " * 0.5)";  // 0x30 SHIFTRIGHTBY1
    }
}
std::string scale_bias_expr3(uint32_t sb, const std::string& value) {
    switch (sb) {
        case 0x00: return value;
        case 0x08: return "(" + value + " - vec3(0.5))";
        case 0x10: return "(" + value + " * 2.0)";
        case 0x18: return "((" + value + " - vec3(0.5)) * 2.0)";
        case 0x20: return "(" + value + " * 4.0)";
        default:   return "(" + value + " * 0.5)";
    }
}

const char* compare_op(uint32_t func) {
    switch (func) {
        case 0x0200: return nullptr;   // NEVER
        case 0x0201: return "<";
        case 0x0202: return "==";
        case 0x0203: return "<=";
        case 0x0204: return ">";
        case 0x0205: return "!=";
        case 0x0206: return ">=";
        default: return "";            // ALWAYS
    }
}

// One texture stage's texel fetch and bilinear filter, written out in the
// software sampler's own operation order. texelFetch is used rather than a
// LINEAR sampler because hardware bilinear weights are fixed point and would
// introduce a difference the software model does not have.
std::string emit_texture_stage(
    uint32_t i, const Nv2aCombinerShaderState::TextureStageState& ts) {
    using TexMode = Nv2aCombinerShaderState::TexMode;
    using AddressMode = Nv2aCombinerShaderState::AddressMode;
    const bool border_u = ts.address_u == AddressMode::kBorder;
    const bool border_v = ts.address_v == AddressMode::kBorder;
    const bool repeat_u = ts.address_u == AddressMode::kRepeat;
    const bool repeat_v = ts.address_v == AddressMode::kRepeat;
    char idx[8];
    std::snprintf(idx, sizeof(idx), "%u", i);
    const std::string I = idx;
    std::string out;

    if (ts.mode == TexMode::kLinear2d) {
        // sample_linear. The coordinates are already in TEXEL space, the rows
        // are `pitch` bytes apart in guest memory -- but the DEVICE delivers a
        // tightly packed image, so the GL texture is width x height and the
        // pitch never appears here.
        out += "vec4 gc_texel" + I + "(int x, int y) {\n";
        out += "    ivec2 sz = ivec2(tex_size" + I + ");\n";
        if (border_u || border_v) {
            out += "    if (";
            if (border_u) out += "(x < 0 || x >= sz.x)";
            if (border_u && border_v) out += " || ";
            if (border_v) out += "(y < 0 || y >= sz.y)";
            out += ") return tex_border" + I + ";\n";
            // The border path uses the full modulo/clamp `coordinate` lambda.
            out += repeat_u ? "    int cx = ((x % sz.x) + sz.x) % sz.x;\n"
                            : "    int cx = clamp(x, 0, sz.x - 1);\n";
            out += repeat_v ? "    int cy = ((y % sz.y) + sz.y) % sz.y;\n"
                            : "    int cy = clamp(y, 0, sz.y - 1);\n";
        } else {
            // coordinate_fast: the reachable range is [-1, size], so one
            // compare each way is the whole of the wrap.
            out += repeat_u
                       ? "    int cx = x < 0 ? sz.x - 1 : (x >= sz.x ? 0 : x);\n"
                       : "    int cx = clamp(x, 0, sz.x - 1);\n";
            out += repeat_v
                       ? "    int cy = y < 0 ? sz.y - 1 : (y >= sz.y ? 0 : y);\n"
                       : "    int cy = clamp(y, 0, sz.y - 1);\n";
        }
        out += "    return texelFetch(texture" + I + ", ivec2(cx, cy), 0);\n";
        out += "}\n";
        out += "bool gc_sample" + I + "(float s, float t, out vec4 color) {\n";
        out += "    color = vec4(0.0);\n";
        out += "    if (isinf(s) || isnan(s) || isinf(t) || isnan(t)) "
               "return false;\n";
        // wrap_texel_space
        if (ts.address_u == AddressMode::kClampToEdge)
            out += "    s = clamp(s, 0.0, tex_size" + I + ".x);\n";
        else if (repeat_u)
            out += "    s = s - floor(s / tex_size" + I + ".x) * tex_size" + I +
                   ".x;\n";
        if (ts.address_v == AddressMode::kClampToEdge)
            out += "    t = clamp(t, 0.0, tex_size" + I + ".y);\n";
        else if (repeat_v)
            out += "    t = t - floor(t / tex_size" + I + ".y) * tex_size" + I +
                   ".y;\n";
        out += "    float tx = s - 0.5;\n    float ty = t - 0.5;\n";
    } else {
        // sample_swizzled: NORMALISED coordinates. The Morton table has already
        // been applied by the device, so the GL image is linear.
        out += "vec4 gc_texel" + I + "(int x, int y) {\n";
        out += "    ivec2 sz = ivec2(tex_size" + I + ");\n";
        if (ts.mode != TexMode::kCubemap && (border_u || border_v)) {
            // The software checks out-of-range against BOTH axes and returns
            // the border if EITHER axis is in border mode.
            out += "    if ((x < 0 || y < 0 || x >= sz.x || y >= sz.y)) "
                   "return tex_border" + I + ";\n";
        }
        const bool cube = ts.mode == TexMode::kCubemap;
        const bool cu = cube ? false : repeat_u;
        const bool cv = cube ? false : repeat_v;
        out += cu ? "    int cx = ((x % sz.x) + sz.x) % sz.x;\n"
                  : "    int cx = clamp(x, 0, sz.x - 1);\n";
        out += cv ? "    int cy = ((y % sz.y) + sz.y) % sz.y;\n"
                  : "    int cy = clamp(y, 0, sz.y - 1);\n";
        out += "    return texelFetch(texture" + I + ", ivec2(cx, cy), 0);\n";
        out += "}\n";
        out += "bool gc_sample" + I + "(float u, float v, out vec4 color) {\n";
        out += "    color = vec4(0.0);\n";
        out += "    if (isinf(u) || isnan(u) || isinf(v) || isnan(v)) "
               "return false;\n";
        if (cube) {
            // A cubemap sample always clamps, regardless of the stage's own
            // address register -- matching every pinned cubemap sample.
            out += "    u = clamp(u, 0.0, 1.0);\n    v = clamp(v, 0.0, 1.0);\n";
        } else {
            out += repeat_u ? "    u = u - floor(u);\n"
                            : "    u = clamp(u, 0.0, 1.0);\n";
            out += repeat_v ? "    v = v - floor(v);\n"
                            : "    v = clamp(v, 0.0, 1.0);\n";
        }
        out += "    float tx = u * tex_size" + I + ".x - 0.5;\n";
        out += "    float ty = v * tex_size" + I + ".y - 0.5;\n";
    }
    out += "    float fx0 = floor(tx);\n    float fy0 = floor(ty);\n";
    out += "    int x0 = int(fx0);\n    int y0 = int(fy0);\n";
    out += "    float fx = tx - fx0;\n    float fy = ty - fy0;\n";
    out += "    vec4 c00 = gc_texel" + I + "(x0,     y0    );\n";
    out += "    vec4 c10 = gc_texel" + I + "(x0 + 1, y0    );\n";
    out += "    vec4 c01 = gc_texel" + I + "(x0,     y0 + 1);\n";
    out += "    vec4 c11 = gc_texel" + I + "(x0 + 1, y0 + 1);\n";
    out += "    precise vec4 top = c00 + (c10 - c00) * fx;\n";
    out += "    precise vec4 bottom = c01 + (c11 - c01) * fx;\n";
    out += "    color = top + (bottom - top) * fy;\n";
    out += "    return true;\n}\n";

    if (ts.mode == TexMode::kCubemap) {
        // The generic path's own major-axis direction-to-UV algorithm, with the
        // strict '>' comparisons and the Z-axis fallback on any tie.
        out += "bool gc_cube" + I + "(vec3 d, out vec4 color) {\n";
        out += "    color = vec4(0.0);\n";
        out += "    if (isinf(d.x) || isnan(d.x) || isinf(d.y) || isnan(d.y) ||\n"
               "        isinf(d.z) || isnan(d.z)) return false;\n";
        out += "    float ax = abs(d.x), ay = abs(d.y), az = abs(d.z);\n";
        out += "    float u, v, divisor;\n";
        out += "    if (ax > ay && ax > az) {\n"
               "        divisor = ax; u = d.x >= 0.0 ? -d.z : d.z; v = d.y;\n"
               "    } else if (ay > ax && ay > az) {\n"
               "        divisor = ay; u = d.x; v = d.y >= 0.0 ? -d.z : d.z;\n"
               "    } else {\n"
               "        divisor = az; u = d.z >= 0.0 ? d.x : -d.x; v = d.y;\n"
               "    }\n";
        out += "    if (divisor == 0.0) return false;\n";
        out += "    return gc_sample" + I + "(u / divisor, v / divisor, color);\n";
        out += "}\n";
    }
    return out;
}

}  // namespace

const char* const kNv2aGeneratedVertexSource = R"GLSL(#version 430 core
#extension GL_ARB_gpu_shader_int64 : require
struct GenTri {
    i64vec4 ab;
    i64vec4 cp;
    vec4 zi;
    vec4 wv;
    vec4 misc;
    vec4 v[18];
    vec4 pad;
    dvec4 recip;
};
layout(std430, binding = 0) readonly buffer TriBuffer { GenTri tris[]; };
uniform vec2 surface_size;
flat out int tri_id;
// Six vertices per triangle record, always. k = 0,1,2 is the triangle itself
// and k = 3,4,5 is degenerate -- UNLESS the record is flagged wide, in which
// case all six form the triangle's own scanline bounding box as two triangles.
// Identical in shape and reasoning to the witnessed executor's vertex program:
// the in-shader edge test can only REMOVE a candidate fragment, never add one,
// so UNDER-coverage by the fixed-function rasterizer is the one error it cannot
// repair, and a coordinate too large to place in float clip space is drawn as
// the software path's own scanline box instead.
void main() {
    int t = gl_VertexID / 6;
    int k = gl_VertexID - t * 6;
    tri_id = t;
    float xg, yg;
    if (tris[t].cp.w != 0L) {
        int64_t b = tris[t].cp.z;
        float x0 = float(int(b & 0xFFFFL));
        float x1 = float(int((b >> 16) & 0xFFFFL)) + 1.0;
        float y0 = float(int((b >> 32) & 0xFFFFL));
        float y1 = float(int((b >> 48) & 0xFFFFL)) + 1.0;
        bool right  = (k == 1 || k == 3 || k == 4);
        bool bottom = (k == 2 || k == 4 || k == 5);
        xg = right ? x1 : x0;
        yg = bottom ? y1 : y0;
    } else {
        i64vec2 p = k == 0 ? tris[t].ab.xy
                  : (k == 1 ? tris[t].ab.zw : tris[t].cp.xy);
        if (k >= 3) p = tris[t].ab.xy;
        xg = float(p.x) * 0.0625;
        yg = float(p.y) * 0.0625;
    }
    gl_Position = vec4(xg / surface_size.x * 2.0 - 1.0,
                       1.0 - yg / surface_size.y * 2.0, 0.0, 1.0);
}
)GLSL";

std::string nv2a_generate_combiner_fragment_glsl(
    const Nv2aCombinerShaderState& state) {
    using FinalMode = Nv2aCombinerShaderState::FinalMode;
    using TexMode = Nv2aCombinerShaderState::TexMode;
    std::string s;
    s += "#version 430 core\n";
    s += "#extension GL_ARB_gpu_shader_int64 : require\n";
    s += "struct GenTri {\n"
         "    i64vec4 ab;\n"
         "    i64vec4 cp;\n"
         "    vec4 zi;\n"
         "    vec4 wv;\n"
         "    vec4 misc;\n"
         "    vec4 v[18];\n"
         "    vec4 pad;\n"
         "    dvec4 recip;\n"
         "};\n";
    s += "layout(std430, binding = 0) readonly buffer TriBuffer "
         "{ GenTri tris[]; };\n";
    s += "flat in int tri_id;\n";
    s += "uniform vec2 surface_size;\n";
    s += "uniform float clip_min;\nuniform float clip_max;\n";
    s += "uniform float alpha_ref;\n";
    s += "uniform vec4 stage_const0[8];\nuniform vec4 stage_const1[8];\n";
    s += "uniform vec4 final_const0;\nuniform vec4 final_const1;\n";
    for (uint32_t i = 0; i < 4; ++i) {
        if (!state.textures[i].referenced) continue;
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "uniform sampler2D texture%u;\n"
                      "uniform vec2 tex_size%u;\n"
                      "uniform vec4 tex_border%u;\n",
                      i, i, i);
        s += buf;
    }
    // X8R8G8B8 has no alpha channel: the software path stores 0xFF in the
    // padding byte, but it BLENDS with the combiner's own alpha. Those two
    // cannot both come out of one fragment output, because GL_SRC_ALPHA reads
    // the value that is also written. DUAL-SOURCE blending separates them: the
    // written alpha is index 0 (1.0) and the blend factor comes from index 1
    // (the real src_alpha), with the backend selecting GL_SRC1_ALPHA /
    // GL_ONE_MINUS_SRC1_ALPHA. Getting this wrong is not subtle -- with the
    // factor forced to 1.0 the destination is simply replaced, which is what
    // the first phase-6 measurement showed as a whole-screen difference from
    // 10.5 G onward.
    const bool dual_source =
        !state.image_blend && state.color_format_x8 && state.blend;
    if (state.image_blend) {
        // PROGRAMMABLE BLENDING. The destination is an IMAGE, read and written
        // inside a fragment-shader interlock so overlapping fragments of the
        // same draw serialise in primitive order exactly as the software
        // rasterizer's scanline loop does. There is no fragment output at all;
        // the backend sets the colour mask to zero so the ROP writes nothing
        // and every guest-visible byte comes from the imageStore below.
        s += "#extension GL_ARB_fragment_shader_interlock : require\n";
        s += "layout(pixel_interlock_ordered) in;\n";
        s += "layout(rgba8, binding = 1) coherent uniform image2D dest_image;\n";
    } else if (dual_source) {
        s += "layout(location = 0, index = 0) out vec4 out_color;\n";
        s += "layout(location = 0, index = 1) out vec4 out_blend;\n";
    } else {
        s += "layout(location = 0) out vec4 out_color;\n";
    }

    // ---- the exactness prologue: coverage, the top-left rule, packing ------
    s += R"GLSL(
// Textually the software rasterizer's own `edge` and `top_left` lambdas
// (src/machine/nv2a.cpp), in int64. Two's-complement wrap on the degenerate
// geometry that overflows them is not an approximation here -- it is the same
// wrap, so the two paths agree at every coordinate magnitude.
int64_t gc_edge(int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                int64_t px, int64_t py) {
    return (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
}
bool gc_top_left(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    int64_t dx = x1 - x0;
    int64_t dy = y1 - y0;
    return dy < 0L || (dy == 0L && dx > 0L);
}
float gc_pack(float v) { return floor(clamp(v, 0.0, 1.0) * 255.0 + 0.5) / 255.0; }
)GLSL";
    s += mapping_functions();

    // ---- registers --------------------------------------------------------
    // R is declared at file scope so the on-the-fly registers 14 and 15 can be
    // written as functions of it, exactly as get_register_value() is.
    s += "vec4 R[16];\n";
    {
        const uint32_t flags = state.final_cw1 & 0xFFu;
        const bool clamp_sum = (flags & 0x80u) != 0;
        const bool inv_v1 = (flags & 0x40u) != 0;
        const bool inv_r0 = (flags & 0x20u) != 0;
        s += "vec4 gc_r14() {\n";
        s += std::string("    vec3 v1 = ") +
             (inv_v1 ? "vec3(1.0) - R[5].rgb" : "R[5].rgb") + ";\n";
        s += std::string("    vec3 r0 = ") +
             (inv_r0 ? "vec3(1.0) - R[12].rgb" : "R[12].rgb") + ";\n";
        s += "    vec3 sum = v1 + r0;\n";
        s += clamp_sum ? "    sum = clamp(sum, vec3(0.0), vec3(1.0));\n" : "";
        s += "    return vec4(sum, 0.0);\n}\n";

        const DecodedInput fe = decode_input((state.final_cw1 >> 24) & 0xFFu);
        const DecodedInput ff = decode_input((state.final_cw1 >> 16) & 0xFFu);
        s += "vec4 gc_r15() {\n";
        // get_register_value(15) reads regs[E]/regs[F] DIRECTLY and treats an
        // index above 13 as zero -- it never recurses into 14 or 15.
        const std::string e_base =
            fe.reg <= 13 ? (std::string("R[") + std::to_string(fe.reg) + "]" +
                            (fe.alt ? ".aaa" : ".rgb"))
                         : std::string("vec3(0.0)");
        const std::string f_base =
            ff.reg <= 13 ? (std::string("R[") + std::to_string(ff.reg) + "]" +
                            (ff.alt ? ".aaa" : ".rgb"))
                         : std::string("vec3(0.0)");
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "    vec3 e = gc_m%u(%s);\n"
                      "    vec3 f = gc_m%u(%s);\n"
                      "    return vec4(e * f, 0.0);\n}\n",
                      fe.mapping, e_base.c_str(), ff.mapping, f_base.c_str());
        s += buf;
    }

    // ---- per-stage texture samplers ---------------------------------------
    const bool samples_textures = state.final_mode != FinalMode::kBlackAlphaOne;
    if (samples_textures) {
        for (uint32_t i = 0; i < 4; ++i) {
            if (!state.textures[i].referenced) continue;
            s += emit_texture_stage(i, state.textures[i]);
        }
    }

    // ---- main -------------------------------------------------------------
    s += R"GLSL(
void main() {
    GenTri T = tris[tri_id];
    int xg = int(gl_FragCoord.x);
    int yg = int(surface_size.y) - 1 - int(gl_FragCoord.y);
    int64_t px = int64_t(xg) * 16L + 8L;
    int64_t py = int64_t(yg) * 16L + 8L;
    int64_t ax = T.ab.x, ay = T.ab.y;
    int64_t bx = T.ab.z, by = T.ab.w;
    int64_t cx = T.cp.x, cy = T.cp.y;
    int64_t w0 = gc_edge(bx, by, cx, cy, px, py);
    int64_t w1 = gc_edge(cx, cy, ax, ay, px, py);
    int64_t w2 = gc_edge(ax, ay, bx, by, px, py);
    if (w0 < 0L || (w0 == 0L && !gc_top_left(bx, by, cx, cy)) ||
        w1 < 0L || (w1 == 0L && !gc_top_left(cx, cy, ax, ay)) ||
        w2 < 0L || (w2 == 0L && !gc_top_left(ax, ay, bx, by))) {
        discard;
    }
)GLSL";
    if (state.uses_depth) {
        // The constant-depth branch is decided on the DEVICE: a triangle whose
        // three z are equal either fails the range test for every fragment (and
        // is then never emitted at all) or carries its single value here, so
        // the shader must not re-derive it through an interpolation that would
        // round differently.
        s += "    float depth;\n";
        s += "    if (T.misc.y != 0.0) {\n        depth = T.misc.z;\n"
             "    } else {\n";
        s += "        precise float d = (float(w0) * T.zi.x + "
             "float(w1) * T.zi.y + float(w2) * T.zi.z) * T.zi.w;\n";
        // The bias is ALWAYS added, never optimised away when polygon
        // offset is off: it is polygon_bias + polygon_scale *
        // max_z_slope, and 0.0f * inf is NaN for a triangle whose
        // float determinant underflows to zero, which is exactly how
        // the software path discards those fragments.
        s += "        d += T.misc.x;\n";
        s += "        if (isinf(d) || isnan(d) || d < clip_min || "
             "d > clip_max) discard;\n";
        s += "        depth = d;\n    }\n";
    } else {
        // Depth is still RANGE TESTED even when the zeta surface is unusable:
        // the software path computes the interpolated depth and skips the
        // fragment when it leaves [clip_min, clip_max] regardless of whether it
        // then reads or writes zeta.
        s += "    if (T.misc.y == 0.0) {\n";
        s += "        precise float d = (float(w0) * T.zi.x + "
             "float(w1) * T.zi.y + float(w2) * T.zi.z) * T.zi.w;\n";
        // The bias is ALWAYS added, never optimised away when polygon
        // offset is off: it is polygon_bias + polygon_scale *
        // max_z_slope, and 0.0f * inf is NaN for a triangle whose
        // float determinant underflows to zero, which is exactly how
        // the software path discards those fragments.
        s += "        d += T.misc.x;\n";
        s += "        if (isinf(d) || isnan(d) || d < clip_min || "
             "d > clip_max) discard;\n";
        s += "    }\n";
    }
    // ---- perspective-correct interpolation --------------------------------
    // The reciprocals are CARRIED: wv.x is the float copy of the software
    // path's 1/(double)area and wv.yzw are its 1/position[3] per vertex.
    if (state.fp64_interpolate) {
        s += R"GLSL(
    double inv_area = T.recip.x;
    double q0 = (double(w0) * inv_area) * T.recip.y;
    double q1 = (double(w1) * inv_area) * T.recip.z;
    double q2 = (double(w2) * inv_area) * T.recip.w;
    double qsum = q0 + q1 + q2;
    if (isinf(qsum) || isnan(qsum) || qsum == 0.0LF) discard;
)GLSL";
    } else {
        s += R"GLSL(
    float inv_area = T.wv.x;
    float q0 = (float(w0) * inv_area) * T.wv.y;
    float q1 = (float(w1) * inv_area) * T.wv.z;
    float q2 = (float(w2) * inv_area) * T.wv.w;
    float qsum = q0 + q1 + q2;
    if (isinf(qsum) || isnan(qsum) || qsum == 0.0) discard;
)GLSL";
    }
    // v[k*6 + slot]: slot 0 diffuse, 1 specular, 2..5 texcoord 0..3.
    const bool fp64 = state.fp64_interpolate;
    auto interp = [fp64](const char* name, int slot) {
        char buf[640];
        if (fp64) {
            // Textually the software path: the products and the divide are in
            // double and only the RESULT becomes a float.
            std::snprintf(buf, sizeof(buf),
                          "    vec4 %s;\n"
                          "    for (int c = 0; c < 4; ++c) {\n"
                          "        double t = (q0 * double(T.v[%d][c]) + "
                          "q1 * double(T.v[%d][c]) + "
                          "q2 * double(T.v[%d][c])) / qsum;\n"
                          "        if (isinf(t) || isnan(t)) discard;\n"
                          "        %s[c] = float(t);\n    }\n",
                          name, slot, 6 + slot, 12 + slot, name);
        } else {
            std::snprintf(buf, sizeof(buf),
                          "    vec4 %s;\n"
                          "    for (int c = 0; c < 4; ++c) {\n"
                          "        precise float t = (q0 * T.v[%d][c] + "
                          "q1 * T.v[%d][c] + q2 * T.v[%d][c]) / qsum;\n"
                          "        if (isinf(t) || isnan(t)) discard;\n"
                          "        %s[c] = t;\n    }\n",
                          name, slot, 6 + slot, 12 + slot, name);
        }
        return std::string(buf);
    };
    if (state.needs_diffuse) s += interp("diffuse", 0);
    else s += "    vec4 diffuse = vec4(0.0);\n";
    if (state.needs_specular) s += interp("specular", 1);
    else s += "    vec4 specular = vec4(0.0);\n";
    for (uint32_t i = 0; i < 4; ++i) {
        if (!state.textures[i].referenced) continue;
        char name[16];
        std::snprintf(name, sizeof(name), "tc%u", i);
        s += interp(name, static_cast<int>(2 + i));
    }

    // ---- register file initialisation --------------------------------------
    s += "    for (int i = 0; i < 16; ++i) R[i] = vec4(0.0);\n";
    if (state.needs_diffuse) s += "    R[4] = diffuse;\n";
    if (state.needs_specular) s += "    R[5] = specular;\n";
    if (samples_textures) {
        for (uint32_t i = 0; i < 4; ++i) {
            if (!state.textures[i].referenced) continue;
            const auto& ts = state.textures[i];
            char buf[512];
            const char* I = "";
            (void)I;
            if (ts.mode == TexMode::kCubemap) {
                std::snprintf(buf, sizeof(buf),
                              "    { vec4 sampled;\n"
                              "      if (!gc_cube%u(tc%u.xyz, sampled)) discard;\n"
                              "      R[%u] = sampled; }\n",
                              i, i, 8 + i);
            } else if (ts.mode == TexMode::kProject2d ||
                       ts.mode == TexMode::kLinear2d) {
                std::snprintf(buf, sizeof(buf),
                              "    { if (tc%u.w == 0.0) discard;\n"
                              "      vec4 sampled;\n"
                              "      if (!gc_sample%u(tc%u.x / tc%u.w, "
                              "tc%u.y / tc%u.w, sampled)) discard;\n"
                              "      R[%u] = sampled; }\n",
                              i, i, i, i, i, i, 8 + i);
            } else {
                std::snprintf(buf, sizeof(buf),
                              "    { vec4 sampled;\n"
                              "      if (!gc_sample%u(tc%u.x, tc%u.y, sampled)) "
                              "discard;\n"
                              "      R[%u] = sampled; }\n",
                              i, i, i, 8 + i);
            }
            s += buf;
        }
    }

    // ---- the general combiner ---------------------------------------------
    if (state.needs_general_combiner) {
        for (uint32_t stage = 0; stage < state.stage_count; ++stage) {
            char head[192];
            std::snprintf(head, sizeof(head),
                          "    // ---- combiner stage %u ----\n    {\n"
                          "    R[1] = stage_const0[%u];\n"
                          "    R[2] = stage_const1[%u];\n",
                          stage, stage, stage);
            s += head;
            const uint32_t ci = state.color_icw[stage];
            const uint32_t ai = state.alpha_icw[stage];
            const uint32_t co = state.color_ocw[stage];
            const uint32_t ao = state.alpha_ocw[stage];
            const DecodedInput c[4] = {
                decode_input((ci >> 24) & 0xFFu), decode_input((ci >> 16) & 0xFFu),
                decode_input((ci >> 8) & 0xFFu), decode_input(ci & 0xFFu)};
            const DecodedInput a[4] = {
                decode_input((ai >> 24) & 0xFFu), decode_input((ai >> 16) & 0xFFu),
                decode_input((ai >> 8) & 0xFFu), decode_input(ai & 0xFFu)};
            const uint32_t cd_dst = co & 0xFu;
            const uint32_t ab_dst = (co >> 4) & 0xFu;
            const uint32_t sum_dst = (co >> 8) & 0xFu;
            const uint32_t cflags = co >> 12;
            const bool cd_dot = (cflags & 1u) != 0;
            const bool ab_dot = (cflags & 2u) != 0;
            const bool cmux = (cflags & 4u) != 0;
            const uint32_t csb = cflags & 0x38u;
            const bool ab_b2a = (cflags & 0x80u) != 0;
            const bool cd_b2a = (cflags & 0x40u) != 0;
            const uint32_t a_cd_dst = ao & 0xFu;
            const uint32_t a_ab_dst = (ao >> 4) & 0xFu;
            const uint32_t a_sum_dst = (ao >> 8) & 0xFu;
            const uint32_t aflags = ao >> 12;
            const bool amux = (aflags & 4u) != 0;
            const uint32_t asb = aflags & 0x38u;

            s += "    vec3 ca = " + color_input_expr(c[0]) + ";\n";
            s += "    vec3 cb = " + color_input_expr(c[1]) + ";\n";
            s += "    vec3 cc = " + color_input_expr(c[2]) + ";\n";
            s += "    vec3 cd = " + color_input_expr(c[3]) + ";\n";
            s += ab_dot ? "    precise float abdot = ca.x * cb.x + "
                          "ca.y * cb.y + ca.z * cb.z;\n"
                          "    vec3 cab = vec3(abdot);\n"
                        : "    vec3 cab = vec3(ca.x * cb.x, ca.y * cb.y, "
                          "ca.z * cb.z);\n";
            s += cd_dot ? "    precise float cddot = cc.x * cd.x + "
                          "cc.y * cd.y + cc.z * cd.z;\n"
                          "    vec3 ccd = vec3(cddot);\n"
                        : "    vec3 ccd = vec3(cc.x * cd.x, cc.y * cd.y, "
                          "cc.z * cd.z);\n";
            // MUX_SUM. The comparison is a single global flag shared by every
            // stage: MSB of R0.alpha (bit 8 set) vs its packed LSB.
            const char* mux_cond =
                state.mux_on_msb
                    ? "(R[12].a >= 0.5)"
                    : "((int(floor(R[12].a * 255.0 + 0.5)) & 1) != 0)";
            if (cmux) {
                s += std::string("    vec3 csum = ") + mux_cond +
                     " ? ccd : cab;\n";
            } else {
                s += "    vec3 csum = vec3(cab.x + ccd.x, cab.y + ccd.y, "
                     "cab.z + ccd.z);\n";
            }
            auto store_color = [&](uint32_t dst, const char* value, bool b2a) {
                if (!dst) return;
                char buf[512];
                const std::string scaled = scale_bias_expr3(csb, value);
                std::snprintf(buf, sizeof(buf),
                              "    R[%u].rgb = clamp(%s, vec3(-1.0), "
                              "vec3(1.0));\n",
                              dst, scaled.c_str());
                s += buf;
                if (b2a) {
                    std::snprintf(buf, sizeof(buf), "    R[%u].a = R[%u].b;\n",
                                  dst, dst);
                    s += buf;
                }
            };
            store_color(ab_dst, "cab", ab_b2a);
            store_color(cd_dst, "ccd", cd_b2a);
            store_color(sum_dst, "csum", false);

            // The alpha half reads the registers AFTER the colour stores, and
            // its own MUX condition is evaluated after them too -- a
            // BLUE_TO_ALPHA store into R0 changes it. Order is preserved.
            s += "    float aa = " + alpha_input_expr(a[0]) + ";\n";
            s += "    float ab = " + alpha_input_expr(a[1]) + ";\n";
            s += "    float ac = " + alpha_input_expr(a[2]) + ";\n";
            s += "    float ad = " + alpha_input_expr(a[3]) + ";\n";
            s += "    float aab = aa * ab;\n    float acd = ac * ad;\n";
            if (amux) {
                s += std::string("    float asum = ") + mux_cond +
                     " ? acd : aab;\n";
            } else {
                s += "    float asum = aab + acd;\n";
            }
            auto store_alpha = [&](uint32_t dst, const char* value) {
                if (!dst) return;
                char buf[384];
                const std::string scaled = scale_bias_expr(asb, value);
                std::snprintf(buf, sizeof(buf),
                              "    R[%u].a = clamp(%s, -1.0, 1.0);\n", dst,
                              scaled.c_str());
                s += buf;
            };
            store_alpha(a_ab_dst, "aab");
            store_alpha(a_cd_dst, "acd");
            store_alpha(a_sum_dst, "asum");
            // Each stage body is its own scope so `ca`/`aa`/... do not collide
            // between the up-to-eight stages emitted into one function.
            s += "    }\n";
        }
    }

    // ---- final combiner ----------------------------------------------------
    s += "    vec4 final_color;\n";
    switch (state.final_mode) {
        case FinalMode::kPrimaryColor:
            s += "    final_color = diffuse;\n";
            break;
        case FinalMode::kTexture0ModulatePrimary:
            s += "    final_color = vec4(R[8].rgb * diffuse.rgb, diffuse.a);\n";
            break;
        case FinalMode::kTexture0ModulatePrimaryAlpha:
            s += "    final_color = vec4(R[8].rgb * diffuse.rgb, "
                 "R[8].a * diffuse.a);\n";
            break;
        case FinalMode::kTexture0Passthrough:
            s += "    final_color = vec4(R[8].rgb, diffuse.a);\n";
            break;
        case FinalMode::kBlackAlphaOne:
            s += "    final_color = vec4(0.0, 0.0, 0.0, 1.0);\n";
            break;
        case FinalMode::kPassthroughR0:
            s += "    final_color = R[12];\n";
            break;
        case FinalMode::kGeneric:
        default: {
            // The final combiner's CONSTANT_COLOR0/1 come from
            // SPECFOGFACTOR0/1, a different source from the per-stage factors,
            // and they are loaded only now that every ordinary stage is done.
            s += "    R[1] = final_const0;\n    R[2] = final_const1;\n";
            const DecodedInput fa = decode_input((state.final_cw0 >> 24) & 0xFFu);
            const DecodedInput fb = decode_input((state.final_cw0 >> 16) & 0xFFu);
            const DecodedInput fc = decode_input((state.final_cw0 >> 8) & 0xFFu);
            const DecodedInput fd = decode_input(state.final_cw0 & 0xFFu);
            const DecodedInput fg = decode_input((state.final_cw1 >> 8) & 0xFFu);
            s += "    vec3 fa = " + color_input_expr(fa) + ";\n";
            s += "    vec3 fb = " + color_input_expr(fb) + ";\n";
            s += "    vec3 fc = " + color_input_expr(fc) + ";\n";
            s += "    vec3 fd = " + color_input_expr(fd) + ";\n";
            s += "    precise vec3 frgb = fd + fa * fb + (vec3(1.0) - fa) * fc;\n";
            s += "    final_color = vec4(frgb, " + alpha_input_expr(fg) + ");\n";
            break;
        }
    }

    // ---- alpha test --------------------------------------------------------
    s += "    float src_alpha = clamp(final_color.a, 0.0, 1.0);\n";
    if (state.alpha_test) {
        const char* op = compare_op(state.alpha_func);
        if (op == nullptr) {
            s += "    discard;  // alpha func NEVER\n";
        } else if (op[0] != '\0') {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "    if (!(int(floor(src_alpha * 255.0 + 0.5)) %s "
                          "int(alpha_ref))) discard;\n",
                          op);
            s += buf;
        }
    }

    // ---- output ------------------------------------------------------------
    if (state.image_blend) {
        // Textually the software rasterizer's own write block: read the
        // existing pixel, blend in float with the same factor expressions,
        // clamp, pack with floor(x*255+0.5), and honour the per-channel write
        // mask by storing the destination back unchanged where it is off.
        // imageLoad on an rgba8 image returns exactly k/255, which is what
        // unpack_argb8 produces from the guest word, so the two sides agree on
        // the destination bit for bit -- and, unlike GL fixed-function
        // blending, nothing quantises the SOURCE before the multiply.
        s += "    vec3 out_rgb = clamp(final_color.rgb, vec3(0.0), vec3(1.0));\n";
        s += "    float out_alpha = src_alpha;\n";
        s += "    ivec2 store_at = ivec2(int(gl_FragCoord.x), "
             "int(gl_FragCoord.y));\n";
        s += "    beginInvocationInterlockARB();\n";
        s += "    vec4 dst = imageLoad(dest_image, store_at);\n";
        if (state.blend) {
            auto factor = [](uint32_t f) -> const char* {
                switch (f) {
                    case 0: return "0.0";                  // ZERO
                    case 1: return "1.0";                  // ONE
                    case 0x0302: return "src_alpha";       // SRC_ALPHA
                    default: return "(1.0 - src_alpha)";   // ONE_MINUS_SRC_ALPHA
                }
            };
            s += std::string("    float src_weight = ") +
                 factor(state.blend_src) + ";\n";
            s += std::string("    float dst_weight = ") +
                 factor(state.blend_dst) + ";\n";
            s += "    for (int c = 0; c < 3; ++c) {\n"
                 "        out_rgb[c] = clamp(out_rgb[c] * src_weight + "
                 "dst[c] * dst_weight, 0.0, 1.0);\n    }\n";
            s += "    out_alpha = clamp(src_alpha * src_weight + "
                 "dst.a * dst_weight, 0.0, 1.0);\n";
        }
        // X8R8G8B8 has no alpha channel on real hardware: the software path
        // always stores 0xFF in the padding byte when the mask lets it through.
        const char* stored_alpha =
            state.color_format_x8 ? "1.0" : "gc_pack(out_alpha)";
        const char* chan[4] = {"gc_pack(out_rgb.r)", "gc_pack(out_rgb.g)",
                               "gc_pack(out_rgb.b)", stored_alpha};
        const char* keep[4] = {"dst.r", "dst.g", "dst.b", "dst.a"};
        s += "    vec4 stored = vec4(";
        for (int i = 0; i < 4; ++i) {
            s += state.color_mask[i] ? chan[i] : keep[i];
            s += i == 3 ? ");\n" : ", ";
        }
        s += "    imageStore(dest_image, store_at, stored);\n";
        s += "    endInvocationInterlockARB();\n";
    } else {
    const char* out_alpha = state.color_format_x8
                                ? "1.0"
                                : (state.blend ? "src_alpha"
                                               : "gc_pack(src_alpha)");
    if (state.blend) {
        // The software path blends in float and quantises AFTER, so the shader
        // must hand GL the unquantised, clamped source colour and let the fixed
        // function blend produce the byte.
        s += std::string(
                 "    out_color = vec4(clamp(final_color.rgb, vec3(0.0), "
                 "vec3(1.0)), ") + out_alpha + ");\n";
    } else {
        s += std::string("    out_color = vec4(gc_pack(final_color.r), "
                         "gc_pack(final_color.g), gc_pack(final_color.b), ") +
             out_alpha + ");\n";
    }
    if (dual_source) {
        // Every component carries src_alpha so a factor of SRC1_COLOR would
        // behave like SRC1_ALPHA; only the alpha lanes are actually selected
        // by the factors the backend maps.
        s += "    out_blend = vec4(src_alpha);\n";
    }
    }
    if (state.uses_depth) {
        s += "    uint incoming = uint(floor(clamp(depth, 0.0, 16777215.0)));\n";
        s += "    gl_FragDepth = float(incoming) / 16777215.0;\n";
    }
    s += "}\n";
    return s;
}

}  // namespace xlle
