// Stage (f) witnessed-profile GPU shaders (beads-xbox.1.15.7).
//
// These two programs execute the two dominant witnessed profiles on the GPU.
// They are NOT a re-derivation of the NV2A register combiner from first
// principles: both profiles are matched by an exact-equality fingerprint over
// the whole combiner register block (nv2a.cpp `radial_combiner` and
// `textured_combiner`), so the combiner configuration each program has to
// implement is a single FIXED program, and what is expressed below is that one
// program, read out of the software rasterizer's fragment body expression for
// expression (nv2a.cpp, the shared colour body).
//
// kSwizzledRadialColor
//   RS0 = 0x00000004, CombinerControl = 0x00011103, texture enable mask 0,
//   ColorIcw 48480000 / 2D2D0000 / C4CD0000, AlphaIcw 0 / 3D3D1010 / D4DD1010,
//   ColorOcw 000820D0 / 000000D0 / 000000C0, AlphaOcw 0 / 000000D0 / 000000C0,
//   every stage factor 0, SpecularFog 0000000C / 00002080.
//   Net effect: expand texcoord0.xyz through 2*max(t,0)-1, sum the squares,
//   r1 = 1 - clamp(dot,-1,1), and modulate the perspective-correct diffuse by
//   r1*r1. NO texture is sampled at all -- the "radial" is a combiner-computed
//   falloff, which is why this profile needs no sampler.
//
// kPitchTexturedColor
//   RS0 = 0x00010C60, CombinerControl = 0x00011108, texture enable mask 0xF,
//   the eight-stage ColorIcw/ColorOcw table in nv2a.cpp `textured_color_icw` /
//   `textured_color_ocw`, AlphaIcw D159D15A at stage 0 and 0 elsewhere,
//   AlphaOcw 0, SpecularFog 0C0B000F / 0C011180, FinalFactor0 FF3F3F3F.
//   Net effect: two cube-remapped samples of the SAME swizzled A8R8G8B8 image
//   (stage 1 and stage 2 are pinned to the same offset and format) through
//   texcoord1 and texcoord2, r0 = clamp(2*t1.b-1,-1,1),
//   r1 = max(clamp(2*t2.b-1,-1,1),0) raised to the 32nd power by five
//   squarings, then diffuse * max(clamp(f0*max(r0,0) + f1*r1, -1, 1), 0)
//   * 63/255. f0/f1 are the stage-6 constant registers -- the ONLY combiner
//   words the fingerprint leaves free, which is why they are uniforms and
//   everything else is literal.
//
// Coverage, depth and the perspective-correct interpolation are re-derived from
// the SAME integer edge functions the software rasterizer uses, carried in
// doubles (every product is an exact integer below 2^47, which the device-side
// eligibility predicate guarantees by capping the fixed-point coordinates), so
// the result does not depend on the driver's fill rule or subpixel precision
// for anything except which candidate fragments get generated -- and the
// in-shader test can only REMOVE a fragment the driver generated, never add
// one, so an over-covering driver cannot change the output.
#pragma once

#include <cstdint>

namespace xlle {
namespace witnessed_gl {

constexpr const char* kRadialVertexSource = R"GLSL(#version 430 core
#extension GL_ARB_gpu_shader_int64 : require
struct RadialTri {
    i64vec4 ab;
    i64vec4 cp;
    vec4  zi;
    vec4  wv;
    vec4  d0; vec4 d1; vec4 d2;
    vec4  t0; vec4 t1; vec4 t2;
    dvec4 recip;
};
layout(std430, binding = 0) readonly buffer TriBuffer { RadialTri tris[]; };
uniform vec2 surface_size;
flat out int tri_id;
// Six vertices per triangle record, always. k = 0,1,2 is the triangle itself
// and k = 3,4,5 is degenerate -- UNLESS the record is flagged wide, in which
// case all six form the triangle's own scanline bounding box as two triangles.
// A wide triangle has a 1/16-pixel coordinate at or above 2^24, which a float
// clip-space position cannot place, so the fixed-function rasterizer would
// UNDER-cover it; the in-shader edge test can only remove a candidate fragment,
// never add one, so under-coverage is the one error it cannot repair. Covering
// the software path's exact scanline box instead makes the candidate set a
// superset of the software's fragment set in every case, and the edge test then
// carves out the identical triangle. cp.z packs the four box bounds as four
// 16-bit fields (all are clip-rect-clamped and therefore in [0, 4095], and a
// record whose box is empty is never emitted); cp.w is the wide flag.
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

constexpr const char* kRadialFragmentSource = R"GLSL(#version 430 core
#extension GL_ARB_gpu_shader_int64 : require
struct RadialTri {
    i64vec4 ab;
    i64vec4 cp;
    vec4  zi;
    vec4  wv;
    vec4  d0; vec4 d1; vec4 d2;
    vec4  t0; vec4 t1; vec4 t2;
    dvec4 recip;
};
layout(std430, binding = 0) readonly buffer TriBuffer { RadialTri tris[]; };
flat in int tri_id;
uniform vec2 surface_size;
uniform float clip_min;
uniform float clip_max;
layout(location = 0) out vec4 out_color;
// Textually the software rasterizer's own `edge` and `top_left` lambdas
// (nv2a.cpp), in int64. Two's-complement wrap on the degenerate geometry that
// overflows them is not an approximation here -- it is the same wrap, so the
// two paths agree at every coordinate magnitude instead of only below 2^24.
int64_t wp_edge(int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                int64_t px, int64_t py) {
    return (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
}
bool wp_top_left(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    int64_t dx = x1 - x0;
    int64_t dy = y1 - y0;
    return dy < 0L || (dy == 0L && dx > 0L);
}
float wp_pack_unorm8(float value) {
    return floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}
void main() {
    RadialTri T = tris[tri_id];
    int xg = int(gl_FragCoord.x);
    int yg = int(surface_size.y) - 1 - int(gl_FragCoord.y);
    int64_t px = int64_t(xg) * 16L + 8L;
    int64_t py = int64_t(yg) * 16L + 8L;
    int64_t ax = T.ab.x, ay = T.ab.y;
    int64_t bx = T.ab.z, by = T.ab.w;
    int64_t cx = T.cp.x, cy = T.cp.y;
    int64_t w0 = wp_edge(bx, by, cx, cy, px, py);
    int64_t w1 = wp_edge(cx, cy, ax, ay, px, py);
    int64_t w2 = wp_edge(ax, ay, bx, by, px, py);
    if (w0 < 0L || (w0 == 0L && !wp_top_left(bx, by, cx, cy)) ||
        w1 < 0L || (w1 == 0L && !wp_top_left(cx, cy, ax, ay)) ||
        w2 < 0L || (w2 == 0L && !wp_top_left(ax, ay, bx, by))) {
        discard;
    }
    precise float depth = (float(w0) * T.zi.x + float(w1) * T.zi.y +
                           float(w2) * T.zi.z) * T.zi.w;
    if (isinf(depth) || isnan(depth) || depth < clip_min || depth > clip_max)
        discard;
    vec3 diffuse;
    vec3 texcoord;
#if WP_FP32
    // beads-xbox.1.15.10 step 2 (FP64 removal). Same expressions, same order,
    // single precision. wv carries the float copies of the four reciprocals the
    // dvec4 `recip` holds in double; the record layout is IDENTICAL either way,
    // which is what makes this a same-binary runtime switch rather than a
    // second record format. FP64 is 1/64 rate on GeForce, so every double
    // multiply and divide in this body was costing 64 fragment-shader slots.
    float inv_area = T.wv.x;
    float q0 = (float(w0) * inv_area) * T.wv.y;
    float q1 = (float(w1) * inv_area) * T.wv.z;
    float q2 = (float(w2) * inv_area) * T.wv.w;
    float qsum = q0 + q1 + q2;
    if (isinf(qsum) || isnan(qsum) || qsum == 0.0) discard;
    for (int c = 0; c < 3; ++c) {
        precise float d = (q0 * T.d0[c] + q1 * T.d1[c] + q2 * T.d2[c]) / qsum;
        precise float t = (q0 * T.t0[c] + q1 * T.t1[c] + q2 * T.t2[c]) / qsum;
        if (isinf(d) || isnan(d) || isinf(t) || isnan(t)) discard;
        diffuse[c] = d;
        texcoord[c] = t;
    }
#else
    double inv_area = T.recip.x;
    double q0 = (double(w0) * inv_area) * T.recip.y;
    double q1 = (double(w1) * inv_area) * T.recip.z;
    double q2 = (double(w2) * inv_area) * T.recip.w;
    double qsum = q0 + q1 + q2;
    if (isinf(qsum) || isnan(qsum) || qsum == 0.0LF) discard;
    for (int c = 0; c < 3; ++c) {
        double d = (q0 * double(T.d0[c]) + q1 * double(T.d1[c]) +
                    q2 * double(T.d2[c])) / qsum;
        double t = (q0 * double(T.t0[c]) + q1 * double(T.t1[c]) +
                    q2 * double(T.t2[c])) / qsum;
        if (isinf(d) || isnan(d) || isinf(t) || isnan(t)) discard;
        diffuse[c] = float(d);
        texcoord[c] = float(t);
    }
#endif
    precise float dotsum = 0.0;
    for (int c = 0; c < 3; ++c) {
        precise float expanded = 2.0 * max(texcoord[c], 0.0) - 1.0;
        dotsum += expanded * expanded;
    }
    precise float r1 = 1.0 - clamp(dotsum, -1.0, 1.0);
    precise float radial = r1 * r1;
    out_color = vec4(wp_pack_unorm8(diffuse[0] * radial),
                     wp_pack_unorm8(diffuse[1] * radial),
                     wp_pack_unorm8(diffuse[2] * radial),
                     1.0);
    uint incoming = uint(floor(clamp(depth, 0.0, 16777215.0)));
#if WP_FP32
    // The GL_DEPTH24_STENCIL8 buffer stores round(value * (2^24-1)), so what
    // matters is that this maps each of the 2^24 integers back onto itself.
    // float(incoming) is exact below 2^24 and the single divide rounds once;
    // the double form rounded twice. The 28-checkpoint comparison is the
    // acceptance test for that claim, not this comment.
    gl_FragDepth = float(incoming) / 16777215.0;
#else
    gl_FragDepth = float(double(incoming) / 16777215.0LF);
#endif
}
)GLSL";

constexpr const char* kTexturedVertexSource = R"GLSL(#version 430 core
#extension GL_ARB_gpu_shader_int64 : require
struct TexturedTri {
    i64vec4 ab;
    i64vec4 cp;
    vec4  zi;
    vec4  wv;
    vec4  d0; vec4 d1; vec4 d2;
    vec4  t0; vec4 t1; vec4 t2;
    vec4  u0; vec4 u1; vec4 u2;
    dvec4 recip;
};
layout(std430, binding = 0) readonly buffer TriBuffer { TexturedTri tris[]; };
uniform vec2 surface_size;
flat out int tri_id;
// Six vertices per triangle record, always. k = 0,1,2 is the triangle itself
// and k = 3,4,5 is degenerate -- UNLESS the record is flagged wide, in which
// case all six form the triangle's own scanline bounding box as two triangles.
// A wide triangle has a 1/16-pixel coordinate at or above 2^24, which a float
// clip-space position cannot place, so the fixed-function rasterizer would
// UNDER-cover it; the in-shader edge test can only remove a candidate fragment,
// never add one, so under-coverage is the one error it cannot repair. Covering
// the software path's exact scanline box instead makes the candidate set a
// superset of the software's fragment set in every case, and the edge test then
// carves out the identical triangle. cp.z packs the four box bounds as four
// 16-bit fields (all are clip-rect-clamped and therefore in [0, 4095], and a
// record whose box is empty is never emitted); cp.w is the wide flag.
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

constexpr const char* kTexturedFragmentSource = R"GLSL(#version 430 core
#extension GL_ARB_gpu_shader_int64 : require
struct TexturedTri {
    i64vec4 ab;
    i64vec4 cp;
    vec4  zi;
    vec4  wv;
    vec4  d0; vec4 d1; vec4 d2;
    vec4  t0; vec4 t1; vec4 t2;
    vec4  u0; vec4 u1; vec4 u2;
    dvec4 recip;
};
layout(std430, binding = 0) readonly buffer TriBuffer { TexturedTri tris[]; };
flat in int tri_id;
uniform vec2 surface_size;
uniform float clip_min;
uniform float clip_max;
uniform vec2 texture_size;
uniform vec3 weight0;
uniform vec3 weight1;
uniform sampler2D texture0;
layout(location = 0) out vec4 out_color;
// Textually the software rasterizer's own `edge` and `top_left` lambdas
// (nv2a.cpp), in int64. Two's-complement wrap on the degenerate geometry that
// overflows them is not an approximation here -- it is the same wrap, so the
// two paths agree at every coordinate magnitude instead of only below 2^24.
int64_t wp_edge(int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                int64_t px, int64_t py) {
    return (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
}
bool wp_top_left(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    int64_t dx = x1 - x0;
    int64_t dy = y1 - y0;
    return dy < 0L || (dy == 0L && dx > 0L);
}
float wp_pack_unorm8(float value) {
    return floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}
// Mirrors nv2a.cpp's sample_texture with TextureAddressMode::kClampToEdge:
// u and v are clamped to [0,1] first, the tap grid is texel_x = u*W - 0.5,
// each tap coordinate is clamped into range, and the two-stage lerp is written
// in the same order the software rasterizer writes it. texelFetch is used
// rather than a LINEAR sampler because hardware bilinear weights are fixed
// point and would introduce a difference the software model does not have.
vec3 wp_sample_clamped(float u, float v) {
    u = clamp(u, 0.0, 1.0);
    v = clamp(v, 0.0, 1.0);
    float texel_x = u * texture_size.x - 0.5;
    float texel_y = v * texture_size.y - 0.5;
    float fx0 = floor(texel_x);
    float fy0 = floor(texel_y);
    int x0 = int(fx0);
    int y0 = int(fy0);
    float fx = texel_x - fx0;
    float fy = texel_y - fy0;
    ivec2 hi = ivec2(texture_size) - ivec2(1, 1);
    ivec2 p00 = clamp(ivec2(x0,     y0    ), ivec2(0, 0), hi);
    ivec2 p10 = clamp(ivec2(x0 + 1, y0    ), ivec2(0, 0), hi);
    ivec2 p01 = clamp(ivec2(x0,     y0 + 1), ivec2(0, 0), hi);
    ivec2 p11 = clamp(ivec2(x0 + 1, y0 + 1), ivec2(0, 0), hi);
    vec3 c00 = texelFetch(texture0, p00, 0).rgb;
    vec3 c10 = texelFetch(texture0, p10, 0).rgb;
    vec3 c01 = texelFetch(texture0, p01, 0).rgb;
    vec3 c11 = texelFetch(texture0, p11, 0).rgb;
    precise vec3 top = c00 + (c10 - c00) * fx;
    precise vec3 bottom = c01 + (c11 - c01) * fx;
    return top + (bottom - top) * fy;
}
// nv2a.cpp sample_cube_texture with both tie counters absent, which is exactly
// how the pitch-textured profile calls it: the exact-zero direction and the
// |x| == |y| > |z| tie both fall through to the historical chain. A zero
// direction or zero divisor is a software FAULT; a GPU executor may not fault
// (design F13), so it discards and the device-side census records that the
// packet took the GPU path -- a faulting run must be re-run on the CPU path to
// get the true attribution, which is the same rule stage (e) already carries.
bool wp_sample_cube(vec3 d, out vec3 color) {
    color = vec3(0.0);
    if (isinf(d.x) || isnan(d.x) || isinf(d.y) || isnan(d.y) ||
        isinf(d.z) || isnan(d.z)) {
        return false;
    }
    if (d.x == 0.0 && d.y == 0.0 && d.z == 0.0) return false;
    float ax = abs(d.x), ay = abs(d.y), az = abs(d.z);
    float u, v, divisor;
    if (ax > ay && ax > az) {
        divisor = ax;
        u = d.x >= 0.0 ? -d.z : d.z;
        v = d.y;
    } else if (ay > ax && ay > az) {
        divisor = ay;
        u = d.x;
        v = d.y >= 0.0 ? -d.z : d.z;
    } else {
        divisor = az;
        u = d.z >= 0.0 ? d.x : -d.x;
        v = d.y;
    }
    if (divisor == 0.0) return false;
    color = wp_sample_clamped(u / divisor, v / divisor);
    return true;
}
void main() {
    TexturedTri T = tris[tri_id];
    int xg = int(gl_FragCoord.x);
    int yg = int(surface_size.y) - 1 - int(gl_FragCoord.y);
    int64_t px = int64_t(xg) * 16L + 8L;
    int64_t py = int64_t(yg) * 16L + 8L;
    int64_t ax = T.ab.x, ay = T.ab.y;
    int64_t bx = T.ab.z, by = T.ab.w;
    int64_t cx = T.cp.x, cy = T.cp.y;
    int64_t w0 = wp_edge(bx, by, cx, cy, px, py);
    int64_t w1 = wp_edge(cx, cy, ax, ay, px, py);
    int64_t w2 = wp_edge(ax, ay, bx, by, px, py);
    if (w0 < 0L || (w0 == 0L && !wp_top_left(bx, by, cx, cy)) ||
        w1 < 0L || (w1 == 0L && !wp_top_left(cx, cy, ax, ay)) ||
        w2 < 0L || (w2 == 0L && !wp_top_left(ax, ay, bx, by))) {
        discard;
    }
    precise float depth = (float(w0) * T.zi.x + float(w1) * T.zi.y +
                           float(w2) * T.zi.z) * T.zi.w;
    if (isinf(depth) || isnan(depth) || depth < clip_min || depth > clip_max)
        discard;
    vec3 diffuse, dir1, dir2;
#if WP_FP32
    // See the radial shader: same expressions, same order, single precision,
    // reading the float copies of the reciprocals out of wv so the record
    // layout is byte-identical between the two variants.
    float inv_area = T.wv.x;
    float q0 = (float(w0) * inv_area) * T.wv.y;
    float q1 = (float(w1) * inv_area) * T.wv.z;
    float q2 = (float(w2) * inv_area) * T.wv.w;
    float qsum = q0 + q1 + q2;
    if (isinf(qsum) || isnan(qsum) || qsum == 0.0) discard;
    for (int c = 0; c < 3; ++c) {
        precise float d = (q0 * T.d0[c] + q1 * T.d1[c] + q2 * T.d2[c]) / qsum;
        precise float e1 = (q0 * T.t0[c] + q1 * T.t1[c] + q2 * T.t2[c]) / qsum;
        precise float e2 = (q0 * T.u0[c] + q1 * T.u1[c] + q2 * T.u2[c]) / qsum;
        if (isinf(d) || isnan(d) || isinf(e1) || isnan(e1) ||
            isinf(e2) || isnan(e2)) {
            discard;
        }
        diffuse[c] = d;
        dir1[c] = e1;
        dir2[c] = e2;
    }
#else
    double inv_area = T.recip.x;
    double q0 = (double(w0) * inv_area) * T.recip.y;
    double q1 = (double(w1) * inv_area) * T.recip.z;
    double q2 = (double(w2) * inv_area) * T.recip.w;
    double qsum = q0 + q1 + q2;
    if (isinf(qsum) || isnan(qsum) || qsum == 0.0LF) discard;
    for (int c = 0; c < 3; ++c) {
        double d = (q0 * double(T.d0[c]) + q1 * double(T.d1[c]) +
                    q2 * double(T.d2[c])) / qsum;
        double e1 = (q0 * double(T.t0[c]) + q1 * double(T.t1[c]) +
                     q2 * double(T.t2[c])) / qsum;
        double e2 = (q0 * double(T.u0[c]) + q1 * double(T.u1[c]) +
                     q2 * double(T.u2[c])) / qsum;
        if (isinf(d) || isnan(d) || isinf(e1) || isnan(e1) ||
            isinf(e2) || isnan(e2)) {
            discard;
        }
        diffuse[c] = float(d);
        dir1[c] = float(e1);
        dir2[c] = float(e2);
    }
#endif
    vec3 texture1, texture2;
    if (!wp_sample_cube(dir1, texture1)) discard;
    if (!wp_sample_cube(dir2, texture2)) discard;
    precise float r0 = clamp(2.0 * texture1[2] - 1.0, -1.0, 1.0);
    precise float r1 = max(clamp(2.0 * texture2[2] - 1.0, -1.0, 1.0), 0.0);
    for (int s = 0; s < 5; ++s) r1 = r1 * r1;
    precise float positive_r0 = max(r0, 0.0);
    vec3 shaded;
    for (int c = 0; c < 3; ++c) {
        precise float weighted =
            clamp(weight0[c] * positive_r0 + weight1[c] * r1, -1.0, 1.0);
        shaded[c] = diffuse[c] * max(weighted, 0.0) * (63.0 / 255.0);
    }
    out_color = vec4(wp_pack_unorm8(shaded[0]), wp_pack_unorm8(shaded[1]),
                     wp_pack_unorm8(shaded[2]), 1.0);
    uint incoming = uint(floor(clamp(depth, 0.0, 16777215.0)));
#if WP_FP32
    // The GL_DEPTH24_STENCIL8 buffer stores round(value * (2^24-1)), so what
    // matters is that this maps each of the 2^24 integers back onto itself.
    // float(incoming) is exact below 2^24 and the single divide rounds once;
    // the double form rounded twice. The 28-checkpoint comparison is the
    // acceptance test for that claim, not this comment.
    gl_FragDepth = float(incoming) / 16777215.0;
#else
    gl_FragDepth = float(double(incoming) / 16777215.0LF);
#endif
}
)GLSL";

// kPitchProjectedColor (beads-xbox.1.15.10 phase 4, lever A)
//   RS0 = 0x00010C61, CombinerControl = 0x00011107, texture enable mask 0xF,
//   the SEVEN-stage ColorIcw/ColorOcw table in nv2a.cpp `projected_color_icw` /
//   `projected_color_ocw`, AlphaIcw 5859585A at stage 0 and 0 elsewhere,
//   AlphaOcw 0, SpecularFog 0C0B000F / 0C011180, FinalFactor0 FF3F3F3F.
//   Net effect: the same two cube-remapped taps the pitch-textured profile
//   makes through texcoord1 and texcoord2, PLUS a third tap of a separate,
//   REPEAT-addressed 2D image through the perspective-divided projective
//   texcoord0 (x/w, y/w). The three are then combined by two dot products in
//   expanded [-1,1] space -- r0 = dot(2*t0-1, 2*t1-1), r1 = dot(2*t0-1, 2*t2-1)
//   -- where the textured profile simply reads t1.b and t2.b. r1 is raised to
//   the 16th power by FOUR squarings (the textured profile uses five), and the
//   free combiner constants are STAGE 5, not stage 6.
//
//   The record is the SAME TexturedTriRecord, byte for byte, with no new
//   stride and no new vertex shader: the projective texcoord0 (x, y, w) rides
//   in the three .w lanes of d0/t0/u0 (vertex 0), d1/t1/u1 (vertex 1) and
//   d2/t2/u2 (vertex 2), which the pitch-textured path writes as 0.0f and no
//   shader has ever read. That keeps every offsetof static_assert and the
//   288-byte std430 stride untouched, so this profile adds a fragment program
//   and nothing else to the SSBO plumbing.
constexpr const char* kProjectedFragmentSource = R"GLSL(#version 430 core
#extension GL_ARB_gpu_shader_int64 : require
struct TexturedTri {
    i64vec4 ab;
    i64vec4 cp;
    vec4  zi;
    vec4  wv;
    vec4  d0; vec4 d1; vec4 d2;
    vec4  t0; vec4 t1; vec4 t2;
    vec4  u0; vec4 u1; vec4 u2;
    dvec4 recip;
};
layout(std430, binding = 0) readonly buffer TriBuffer { TexturedTri tris[]; };
flat in int tri_id;
uniform vec2 surface_size;
uniform float clip_min;
uniform float clip_max;
uniform vec2 texture_size;
uniform vec2 projected_texture_size;
uniform vec3 weight0;
uniform vec3 weight1;
uniform sampler2D texture0;
uniform sampler2D texture1;
layout(location = 0) out vec4 out_color;
int64_t wp_edge(int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                int64_t px, int64_t py) {
    return (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
}
bool wp_top_left(int64_t x0, int64_t y0, int64_t x1, int64_t y1) {
    int64_t dx = x1 - x0;
    int64_t dy = y1 - y0;
    return dy < 0L || (dy == 0L && dx > 0L);
}
float wp_pack_unorm8(float value) {
    return floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5) / 255.0;
}
// nv2a.cpp sample_texture with TextureAddressMode::kClampToEdge -- the cube
// faces. Identical to the pitch-textured program's helper.
vec3 wp_sample_clamped(float u, float v) {
    u = clamp(u, 0.0, 1.0);
    v = clamp(v, 0.0, 1.0);
    float texel_x = u * texture_size.x - 0.5;
    float texel_y = v * texture_size.y - 0.5;
    float fx0 = floor(texel_x);
    float fy0 = floor(texel_y);
    int x0 = int(fx0);
    int y0 = int(fy0);
    float fx = texel_x - fx0;
    float fy = texel_y - fy0;
    ivec2 hi = ivec2(texture_size) - ivec2(1, 1);
    ivec2 p00 = clamp(ivec2(x0,     y0    ), ivec2(0, 0), hi);
    ivec2 p10 = clamp(ivec2(x0 + 1, y0    ), ivec2(0, 0), hi);
    ivec2 p01 = clamp(ivec2(x0,     y0 + 1), ivec2(0, 0), hi);
    ivec2 p11 = clamp(ivec2(x0 + 1, y0 + 1), ivec2(0, 0), hi);
    vec3 c00 = texelFetch(texture0, p00, 0).rgb;
    vec3 c10 = texelFetch(texture0, p10, 0).rgb;
    vec3 c01 = texelFetch(texture0, p01, 0).rgb;
    vec3 c11 = texelFetch(texture0, p11, 0).rgb;
    precise vec3 top = c00 + (c10 - c00) * fx;
    precise vec3 bottom = c01 + (c11 - c01) * fx;
    return top + (bottom - top) * fy;
}
// nv2a.cpp sample_texture's `coordinate` lambda for kRepeat:
//   wrapped = value % size; if (wrapped < 0) wrapped += size;
// written without GLSL's `%`, whose sign behaviour on negative operands the
// spec leaves undefined. The domain is proved, not assumed: the caller has
// already applied `u -= floor(u)`, so u is in [0,1] (1.0 is reachable only by
// the float rounding of `u - floor(u)` for a tiny negative u), texel_x is in
// [-0.5, size-0.5], x0 = floor(texel_x) is in [-1, size-1] and x0+1 is in
// [0, size]. One compare each way covers that range exactly.
int wp_wrap(int v, int size) {
    if (v < 0) return v + size;
    if (v >= size) return v - size;
    return v;
}
// nv2a.cpp sample_texture with TextureAddressMode::kRepeat and border 0 (which
// kRepeat never reads) -- the stage-0 projective image, a SECOND texture unit.
bool wp_sample_repeat(float u, float v, out vec3 color) {
    color = vec3(0.0);
    if (isinf(u) || isnan(u) || isinf(v) || isnan(v)) return false;
    u = u - floor(u);
    v = v - floor(v);
    float texel_x = u * projected_texture_size.x - 0.5;
    float texel_y = v * projected_texture_size.y - 0.5;
    float fx0 = floor(texel_x);
    float fy0 = floor(texel_y);
    int x0 = int(fx0);
    int y0 = int(fy0);
    float fx = texel_x - fx0;
    float fy = texel_y - fy0;
    int sw = int(projected_texture_size.x);
    int sh = int(projected_texture_size.y);
    ivec2 p00 = ivec2(wp_wrap(x0,     sw), wp_wrap(y0,     sh));
    ivec2 p10 = ivec2(wp_wrap(x0 + 1, sw), wp_wrap(y0,     sh));
    ivec2 p01 = ivec2(wp_wrap(x0,     sw), wp_wrap(y0 + 1, sh));
    ivec2 p11 = ivec2(wp_wrap(x0 + 1, sw), wp_wrap(y0 + 1, sh));
    vec3 c00 = texelFetch(texture1, p00, 0).rgb;
    vec3 c10 = texelFetch(texture1, p10, 0).rgb;
    vec3 c01 = texelFetch(texture1, p01, 0).rgb;
    vec3 c11 = texelFetch(texture1, p11, 0).rgb;
    precise vec3 top = c00 + (c10 - c00) * fx;
    precise vec3 bottom = c01 + (c11 - c01) * fx;
    color = top + (bottom - top) * fy;
    return true;
}
// nv2a.cpp sample_cube_texture with both tie counters absent, exactly as the
// projected profile calls it. Identical to the pitch-textured program's copy.
bool wp_sample_cube(vec3 d, out vec3 color) {
    color = vec3(0.0);
    if (isinf(d.x) || isnan(d.x) || isinf(d.y) || isnan(d.y) ||
        isinf(d.z) || isnan(d.z)) {
        return false;
    }
    if (d.x == 0.0 && d.y == 0.0 && d.z == 0.0) return false;
    float ax = abs(d.x), ay = abs(d.y), az = abs(d.z);
    float u, v, divisor;
    if (ax > ay && ax > az) {
        divisor = ax;
        u = d.x >= 0.0 ? -d.z : d.z;
        v = d.y;
    } else if (ay > ax && ay > az) {
        divisor = ay;
        u = d.x;
        v = d.y >= 0.0 ? -d.z : d.z;
    } else {
        divisor = az;
        u = d.z >= 0.0 ? d.x : -d.x;
        v = d.y;
    }
    if (divisor == 0.0) return false;
    color = wp_sample_clamped(u / divisor, v / divisor);
    return true;
}
void main() {
    TexturedTri T = tris[tri_id];
    int xg = int(gl_FragCoord.x);
    int yg = int(surface_size.y) - 1 - int(gl_FragCoord.y);
    int64_t px = int64_t(xg) * 16L + 8L;
    int64_t py = int64_t(yg) * 16L + 8L;
    int64_t ax = T.ab.x, ay = T.ab.y;
    int64_t bx = T.ab.z, by = T.ab.w;
    int64_t cx = T.cp.x, cy = T.cp.y;
    int64_t w0 = wp_edge(bx, by, cx, cy, px, py);
    int64_t w1 = wp_edge(cx, cy, ax, ay, px, py);
    int64_t w2 = wp_edge(ax, ay, bx, by, px, py);
    if (w0 < 0L || (w0 == 0L && !wp_top_left(bx, by, cx, cy)) ||
        w1 < 0L || (w1 == 0L && !wp_top_left(cx, cy, ax, ay)) ||
        w2 < 0L || (w2 == 0L && !wp_top_left(ax, ay, bx, by))) {
        discard;
    }
    precise float depth = (float(w0) * T.zi.x + float(w1) * T.zi.y +
                           float(w2) * T.zi.z) * T.zi.w;
    if (isinf(depth) || isnan(depth) || depth < clip_min || depth > clip_max)
        discard;
    vec3 diffuse, dir1, dir2;
    // The projective texcoord0: .x and .y in the software path's component 0
    // and 1, .z here is the software path's component 3 (the projective w).
    vec3 proj;
#if WP_FP32
    float inv_area = T.wv.x;
    float q0 = (float(w0) * inv_area) * T.wv.y;
    float q1 = (float(w1) * inv_area) * T.wv.z;
    float q2 = (float(w2) * inv_area) * T.wv.w;
    float qsum = q0 + q1 + q2;
    if (isinf(qsum) || isnan(qsum) || qsum == 0.0) discard;
    for (int c = 0; c < 3; ++c) {
        precise float d = (q0 * T.d0[c] + q1 * T.d1[c] + q2 * T.d2[c]) / qsum;
        precise float e1 = (q0 * T.t0[c] + q1 * T.t1[c] + q2 * T.t2[c]) / qsum;
        precise float e2 = (q0 * T.u0[c] + q1 * T.u1[c] + q2 * T.u2[c]) / qsum;
        if (isinf(d) || isnan(d) || isinf(e1) || isnan(e1) ||
            isinf(e2) || isnan(e2)) {
            discard;
        }
        diffuse[c] = d;
        dir1[c] = e1;
        dir2[c] = e2;
    }
    {
        precise float p0 = (q0 * T.d0.w + q1 * T.d1.w + q2 * T.d2.w) / qsum;
        precise float p1 = (q0 * T.t0.w + q1 * T.t1.w + q2 * T.t2.w) / qsum;
        precise float p3 = (q0 * T.u0.w + q1 * T.u1.w + q2 * T.u2.w) / qsum;
        if (isinf(p0) || isnan(p0) || isinf(p1) || isnan(p1) ||
            isinf(p3) || isnan(p3)) {
            discard;
        }
        proj = vec3(p0, p1, p3);
    }
#else
    double inv_area = T.recip.x;
    double q0 = (double(w0) * inv_area) * T.recip.y;
    double q1 = (double(w1) * inv_area) * T.recip.z;
    double q2 = (double(w2) * inv_area) * T.recip.w;
    double qsum = q0 + q1 + q2;
    if (isinf(qsum) || isnan(qsum) || qsum == 0.0LF) discard;
    for (int c = 0; c < 3; ++c) {
        double d = (q0 * double(T.d0[c]) + q1 * double(T.d1[c]) +
                    q2 * double(T.d2[c])) / qsum;
        double e1 = (q0 * double(T.t0[c]) + q1 * double(T.t1[c]) +
                     q2 * double(T.t2[c])) / qsum;
        double e2 = (q0 * double(T.u0[c]) + q1 * double(T.u1[c]) +
                     q2 * double(T.u2[c])) / qsum;
        if (isinf(d) || isnan(d) || isinf(e1) || isnan(e1) ||
            isinf(e2) || isnan(e2)) {
            discard;
        }
        diffuse[c] = float(d);
        dir1[c] = float(e1);
        dir2[c] = float(e2);
    }
    {
        double p0 = (q0 * double(T.d0.w) + q1 * double(T.d1.w) +
                     q2 * double(T.d2.w)) / qsum;
        double p1 = (q0 * double(T.t0.w) + q1 * double(T.t1.w) +
                     q2 * double(T.t2.w)) / qsum;
        double p3 = (q0 * double(T.u0.w) + q1 * double(T.u1.w) +
                     q2 * double(T.u2.w)) / qsum;
        if (isinf(p0) || isnan(p0) || isinf(p1) || isnan(p1) ||
            isinf(p3) || isnan(p3)) {
            discard;
        }
        proj = vec3(float(p0), float(p1), float(p3));
    }
#endif
    vec3 texture1c, texture2c;
    if (!wp_sample_cube(dir1, texture1c)) discard;
    if (!wp_sample_cube(dir2, texture2c)) discard;
    // The software path FAULTS on a zero projective w (design F13 forbids the
    // executor from faulting), and on a non-finite projected varying. Both
    // discard here, exactly as the cube helper's failure does, and the device
    // census records that this packet took the GPU path -- a run that would
    // have faulted must be re-run on the CPU path for the true attribution.
    if (proj.z == 0.0) discard;
    vec3 texture0c;
    if (!wp_sample_repeat(proj.x / proj.z, proj.y / proj.z, texture0c)) discard;
    precise float r0 = 0.0;
    precise float r1 = 0.0;
    for (int c = 0; c < 3; ++c) {
        precise float expanded0 = 2.0 * texture0c[c] - 1.0;
        precise float expanded1 = 2.0 * texture1c[c] - 1.0;
        precise float expanded2 = 2.0 * texture2c[c] - 1.0;
        r0 += expanded0 * expanded1;
        r1 += expanded0 * expanded2;
    }
    r0 = clamp(r0, -1.0, 1.0);
    r1 = max(clamp(r1, -1.0, 1.0), 0.0);
    for (int s = 0; s < 4; ++s) r1 = r1 * r1;
    precise float positive_r0 = max(r0, 0.0);
    vec3 shaded;
    for (int c = 0; c < 3; ++c) {
        precise float weighted =
            clamp(weight0[c] * positive_r0 + weight1[c] * r1, -1.0, 1.0);
        shaded[c] = diffuse[c] * max(weighted, 0.0) * (63.0 / 255.0);
    }
    out_color = vec4(wp_pack_unorm8(shaded[0]), wp_pack_unorm8(shaded[1]),
                     wp_pack_unorm8(shaded[2]), 1.0);
    uint incoming = uint(floor(clamp(depth, 0.0, 16777215.0)));
#if WP_FP32
    gl_FragDepth = float(incoming) / 16777215.0;
#else
    gl_FragDepth = float(double(incoming) / 16777215.0LF);
#endif
}
)GLSL";

// The CPU-side mirror of the SSBO records. Layout must match std430 exactly:
// dvec4 has 32-byte alignment there, so the trailing `recip` array is forced to
// the same offset with alignas and the sizes are static_asserted at the use
// site. `wv` is padding that keeps `recip` on its 32-byte boundary in the
// radial record; it is left zero.
struct RadialTriRecord {
    // Phase 2: i64vec4 in std430, same 32-byte alignment and stride the dvec4
    // form had, so every offset below is unchanged.
    int64_t ab[4];
    int64_t cp[4];   // cp[2] = packed scanline box, cp[3] = wide flag
    float   zi[4];   // za, zb, zc, inverse_area (float, as the software path)
    float   wv[4];
    float   d0[4], d1[4], d2[4];
    float   t0[4], t1[4], t2[4];
    alignas(32) double recip[4];  // 1/area (double), 1/w0, 1/w1, 1/w2
};
// std430 offsets, with dvec4 forcing 32-byte alignment throughout:
//   ab 0, cp 32, zi 64, wv 80, d0 96, d1 112, d2 128, t0 144, t1 160, t2 176,
//   recip 192, size 224.

struct TexturedTriRecord {
    int64_t ab[4];
    int64_t cp[4];
    float   zi[4];
    float   wv[4];
    float   d0[4], d1[4], d2[4];
    float   t0[4], t1[4], t2[4];
    float   u0[4], u1[4], u2[4];
    alignas(32) double recip[4];
};

}  // namespace witnessed_gl
}  // namespace xlle
