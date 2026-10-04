#include "host/nv2a_opengl_renderer.hpp"

#include "host/nv2a_witnessed_gl_shaders.hpp"
#include "host/nv2a_combiner_glsl.hpp"
#include "host/nv2a_xemu_psh.hpp"
#if XLLE_NATIVE_DRAW
#include "host/nv2a_native_draw.hpp"
#endif
#include "machine/perf_accounting.hpp"
#include "machine/perf_excl.hpp"
#if XLLE_VSH_GPU_RESIDENT
#include "host/nv2a_vsh_specialize.hpp"
#endif
#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
#include "machine/nv2a_generated_submit_perf.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <cstdio>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#if defined(XLLE_WITH_SDL)
#include <SDL3/SDL.h>
#endif

namespace xlle {

#if defined(XLLE_WITH_SDL)
namespace {
using GlGetIntegerv = void (*)(unsigned int, int*);
using GlCreateShader = unsigned int (*)(unsigned int);
using GlShaderSource = void (*)(unsigned int, int, const char* const*, const int*);
using GlCompileShader = void (*)(unsigned int);
using GlGetShaderiv = void (*)(unsigned int, unsigned int, int*);
using GlGetShaderInfoLog = void (*)(unsigned int, int, int*, char*);
using GlDeleteShader = void (*)(unsigned int);
using GlCreateProgram = unsigned int (*)();
using GlAttachShader = void (*)(unsigned int, unsigned int);
using GlLinkProgram = void (*)(unsigned int);
using GlGetProgramiv = void (*)(unsigned int, unsigned int, int*);
using GlGetProgramInfoLog = void (*)(unsigned int, int, int*, char*);
using GlDeleteProgram = void (*)(unsigned int);
using GlUseProgram = void (*)(unsigned int);
using GlGetUniformLocation = int (*)(unsigned int, const char*);
using GlUniform1i = void (*)(int, int);
using GlUniform1ui = void (*)(int, unsigned int);
using GlUniform1f = void (*)(int, float);
using GlUniform2i = void (*)(int, int, int);
using GlUniform2f = void (*)(int, float, float);
using GlUniform4f = void (*)(int, float, float, float, float);
using GlUniform4i = void (*)(int, int, int, int, int);
using GlUniform4fv = void (*)(int, int, const float*);
using GlGenBuffers = void (*)(int, unsigned int*);
using GlBindBuffer = void (*)(unsigned int, unsigned int);
using GlBufferData = void (*)(unsigned int, std::ptrdiff_t, const void*, unsigned int);
using GlBindBufferBase = void (*)(unsigned int, unsigned int, unsigned int);
using GlDispatchCompute = void (*)(unsigned int, unsigned int, unsigned int);
using GlMemoryBarrier = void (*)(unsigned int);
using GlGetBufferSubData = void (*)(unsigned int, std::ptrdiff_t, std::ptrdiff_t, void*);
using GlDeleteBuffers = void (*)(int, const unsigned int*);
using GlGenTextures = void (*)(int, unsigned int*);
using GlBindTexture = void (*)(unsigned int, unsigned int);
using GlTexImage2D = void (*)(unsigned int, int, int, int, int, int, unsigned int,
                              unsigned int, const void*);
using GlTexSubImage2D = void (*)(unsigned int, int, int, int, int, int,
                                 unsigned int, unsigned int, const void*);
using GlTexParameteri = void (*)(unsigned int, unsigned int, int);
using GlDeleteTextures = void (*)(int, const unsigned int*);
using GlActiveTexture = void (*)(unsigned int);
using GlBindImageTexture = void (*)(unsigned int, unsigned int, int, unsigned char,
                                    int, unsigned int, unsigned int);
using GlGenFramebuffers = void (*)(int, unsigned int*);
using GlBindFramebuffer = void (*)(unsigned int, unsigned int);
using GlFramebufferTexture2D = void (*)(unsigned int, unsigned int, unsigned int,
                                        unsigned int, int);
using GlCheckFramebufferStatus = unsigned int (*)(unsigned int);
using GlDeleteFramebuffers = void (*)(int, const unsigned int*);
using GlViewport = void (*)(int, int, int, int);
using GlScissor = void (*)(int, int, int, int);
using GlEnable = void (*)(unsigned int);
using GlDisable = void (*)(unsigned int);
using GlDepthFunc = void (*)(unsigned int);
using GlDepthMask = void (*)(unsigned char);
using GlBlendFunc = void (*)(unsigned int, unsigned int);
using GlBlendFuncSeparate = void (*)(unsigned int, unsigned int,
                                     unsigned int, unsigned int);
using GlBlendEquation = void (*)(unsigned int);
using GlColorMask = void (*)(unsigned char, unsigned char, unsigned char, unsigned char);
using GlVertexAttribPointer = void (*)(unsigned int, int, unsigned int, unsigned char,
                                       int, const void*);
using GlEnableVertexAttribArray = void (*)(unsigned int);
using GlDisableVertexAttribArray = void (*)(unsigned int);
using GlGenVertexArrays = void (*)(int, unsigned int*);
using GlBindVertexArray = void (*)(unsigned int);
using GlDeleteVertexArrays = void (*)(int, const unsigned int*);
using GlDrawArrays = void (*)(unsigned int, int, int);
using GlReadPixels = void (*)(int, int, int, int, unsigned int, unsigned int, void*);
using GlPixelStorei = void (*)(unsigned int, int);
using GlGetError = unsigned int (*)();
// ---- GPU-side timing entry points (beads-xbox.1.14.6) ---------------------
// GL 3.3 / ARB_timer_query + ARB_sync, loaded through the same
// SDL_GL_GetProcAddress path as everything above but OPTIONALLY: a driver
// without them still renders, and the report says gpu_timing_available=0.
// GLsync is an opaque `struct __GLsync*`; void* is layout-identical and keeps
// this file free of a GL header, matching the hand-rolled prototypes above.
using GlSyncHandle = void*;
using GlGenQueries = void (*)(int, unsigned int*);
using GlDeleteQueries = void (*)(int, const unsigned int*);
using GlBeginQuery = void (*)(unsigned int, unsigned int);
using GlEndQuery = void (*)(unsigned int);
using GlGetQueryObjectuiv = void (*)(unsigned int, unsigned int, unsigned int*);
using GlGetQueryObjectui64v = void (*)(unsigned int, unsigned int, uint64_t*);
using GlFenceSync = GlSyncHandle (*)(unsigned int, unsigned int);
using GlClientWaitSync = unsigned int (*)(GlSyncHandle, unsigned int, uint64_t);
using GlDeleteSync = void (*)(GlSyncHandle);
// Core GL 1.0, but this file resolves everything through SDL_GL_GetProcAddress
// rather than linking libGL, so it needs a pointer like the rest. Loaded OUTSIDE
// the gpu_timing_available_ conjunction: failing to find glFinish must not
// disable GPU timing, it only costs the shutdown drain its safety net.
using GlFinish = void (*)();
// Also core GL 1.0 and also loaded outside the conjunction. glFlush is what
// closes a GL_TIME_ELAPSED bracket around a SYNCHRONOUS readback: glEndQuery
// only ENQUEUES the end marker, so without a flush the marker waits in the
// command buffer until the next submission and the query measures the host's
// idle time between frames (beads-xbox.1.14.6, 2026-09-04).
using GlFlush = void (*)();
// ---- stage (f) witnessed-profile GPU execution (beads-xbox.1.15.7) --------
using GlClear = void (*)(unsigned int);
using GlClearColor = void (*)(float, float, float, float);
using GlClearDepth = void (*)(double);
using GlClearStencil = void (*)(int);
using GlStencilMask = void (*)(unsigned int);
using GlUniform3f = void (*)(int, float, float, float);
using GlBufferSubData = void (*)(unsigned int, std::ptrdiff_t, std::ptrdiff_t,
                                 const void*);
constexpr unsigned int kGlMajorVersion = 0x821B;
constexpr unsigned int kGlMinorVersion = 0x821C;
constexpr unsigned int kGlComputeShader = 0x91B9;
constexpr unsigned int kGlVertexShader = 0x8B31;
constexpr unsigned int kGlFragmentShader = 0x8B30;
constexpr unsigned int kGlCompileStatus = 0x8B81;
constexpr unsigned int kGlLinkStatus = 0x8B82;
constexpr unsigned int kGlShaderStorageBuffer = 0x90D2;
constexpr unsigned int kGlArrayBuffer = 0x8892;
constexpr unsigned int kGlDynamicCopy = 0x88EA;
constexpr unsigned int kGlDynamicDraw = 0x88E8;
constexpr unsigned int kGlShaderStorageBarrierBit = 0x2000;
constexpr unsigned int kGlBufferUpdateBarrierBit = 0x0200;
constexpr unsigned int kGlTextureFetchBarrierBit = 0x0008;
constexpr unsigned int kGlShaderImageAccessBarrierBit = 0x0020;
constexpr unsigned int kGlFramebufferBarrierBit = 0x0400;
constexpr unsigned int kGlFramebuffer = 0x8D40;
constexpr unsigned int kGlColorAttachment0 = 0x8CE0;
constexpr unsigned int kGlDepthStencilAttachment = 0x821A;
constexpr unsigned int kGlFramebufferComplete = 0x8CD5;
constexpr unsigned int kGlTexture2D = 0x0DE1;
constexpr unsigned int kGlRgba8 = 0x8058;
constexpr unsigned int kGlDepth24Stencil8 = 0x88F0;
constexpr unsigned int kGlR32ui = 0x8236;
constexpr unsigned int kGlRgba = 0x1908;
constexpr unsigned int kGlBgra = 0x80E1;
constexpr unsigned int kGlRedInteger = 0x8D94;
constexpr unsigned int kGlDepthStencil = 0x84F9;
constexpr unsigned int kGlUnsignedByte = 0x1401;
constexpr unsigned int kGlUnsignedInt = 0x1405;
constexpr unsigned int kGlUnsignedInt248 = 0x84FA;
constexpr unsigned int kGlFloat = 0x1406;
constexpr unsigned int kGlWriteOnly = 0x88B9;
constexpr unsigned int kGlReadWrite = 0x88BA;
constexpr unsigned int kGlTriangles = 0x0004;
constexpr unsigned int kGlBlend = 0x0BE2;
constexpr unsigned int kGlDepthTest = 0x0B71;
constexpr unsigned int kGlNever = 0x0200;
constexpr unsigned int kGlLess = 0x0201;
constexpr unsigned int kGlEqual = 0x0202;
constexpr unsigned int kGlLequal = 0x0203;
constexpr unsigned int kGlGreater = 0x0204;
constexpr unsigned int kGlNotequal = 0x0205;
constexpr unsigned int kGlGequal = 0x0206;
constexpr unsigned int kGlAlways = 0x0207;
constexpr unsigned int kGlScissorTest = 0x0C11;
constexpr unsigned int kGlFuncAdd = 0x8006;
constexpr unsigned int kGlZero = 0;
constexpr unsigned int kGlOne = 1;
constexpr unsigned int kGlSrcAlpha = 0x0302;
constexpr unsigned int kGlOneMinusSrcAlpha = 0x0303;
// Dual-source blending (GL 3.3 core): the second fragment output supplies the
// blend factor while the first supplies the written colour.
constexpr unsigned int kGlSrc1Alpha = 0x8589;
constexpr unsigned int kGlOneMinusSrc1Alpha = 0x88FB;
constexpr unsigned int kGlTextureMinFilter = 0x2801;
constexpr unsigned int kGlTextureMagFilter = 0x2800;
constexpr unsigned int kGlNearest = 0x2600;
constexpr unsigned int kGlLinear = 0x2601;
constexpr unsigned int kGlTextureWrapS = 0x2802;
constexpr unsigned int kGlTextureWrapT = 0x2803;
constexpr unsigned int kGlClampToEdge = 0x812F;
constexpr unsigned int kGlRepeat = 0x2901;
constexpr unsigned int kGlTexture0 = 0x84C0;
constexpr unsigned int kGlTexture1 = 0x84C1;
constexpr unsigned int kGlUnpackAlignment = 0x0CF5;
constexpr unsigned int kGlPackAlignment = 0x0D05;
constexpr unsigned int kGlNoError = 0;
constexpr unsigned int kGlCullFace = 0x0B44;
constexpr unsigned int kGlStencilTest = 0x0B90;
constexpr unsigned int kGlColorBufferBit = 0x00004000;
constexpr unsigned int kGlDepthBufferBit = 0x00000100;
constexpr unsigned int kGlStencilBufferBit = 0x00000400;
constexpr unsigned int kGlStaticDraw = 0x88E4;
constexpr unsigned int kGlStreamDraw = 0x88E0;
// ---- GPU-side timing (beads-xbox.1.14.6) ---------------------------------
constexpr unsigned int kGlTimeElapsed = 0x88BF;
constexpr unsigned int kGlQueryResult = 0x8866;
constexpr unsigned int kGlQueryResultAvailable = 0x8867;
constexpr unsigned int kGlSyncGpuCommandsComplete = 0x9117;
constexpr unsigned int kGlSyncFlushCommandsBit = 0x00000001;
// One second. A drain that has not finished by then is a hung GPU, not a
// measurement; the glReadPixels that follows blocks anyway, so the wait only
// ever splits the same total into drain_ns and readpixels_ns.
constexpr uint64_t kGlFenceTimeoutNs = 1000000000ull;
// Results are collected many submits later, so the ring only has to cover the
// driver's queue depth. A full ring drops the sample instead of stalling.
constexpr size_t kGpuQueryRingSize = 64;

// GPU-side timing accumulators. File scope, not class members, so the field
// list has exactly one definition (tests/test_gl_timing_guards.py compares it
// against the printed line) and so shutdown can format without reaching into
// the renderer.
//
// Timing totals are written only while perf::g_enabled is true. Availability
// and gpu_timing_enabled are control-plane state and may change before a run so
// the shutdown report can distinguish disabled query timing from unavailable
// query support. With accounting off the renderer issues no query, no fence and
// no clock read; the cost is the bool load perf::ScopedAdd already pays.
struct GlGpuTimingStats {
    std::atomic<uint64_t> gpu_timing_available{0};
    std::atomic<uint64_t> gpu_draw_ns{0};
    std::atomic<uint64_t> gpu_draw_n{0};
    std::atomic<uint64_t> gpu_upload_ns{0};
    std::atomic<uint64_t> gpu_upload_n{0};
    std::atomic<uint64_t> gpu_resolve_ns{0};
    std::atomic<uint64_t> gpu_resolve_n{0};
    std::atomic<uint64_t> drain_ns{0};
    std::atomic<uint64_t> readpixels_ns{0};
    std::atomic<uint64_t> cpu_copy_ns{0};
    std::atomic<uint64_t> bytes_read{0};
    std::atomic<uint64_t> dirty_bytes{0};
    std::atomic<uint64_t> nested_skipped{0};
    std::atomic<uint64_t> queries_dropped{0};
    std::atomic<uint64_t> gpu_timing_enabled{1};
};
GlGpuTimingStats g_gl_gpu;

// perf::ScopedAdd commits an (ns, calls) pair and is already gated on
// g_enabled. The [perf-gl-gpu] line has no call-count column for the three
// CPU-side splits, so they share one discard sink. Reusing ScopedAdd instead of
// a local clock is what keeps this translation unit free of an ungated
// steady_clock read (tests/test_perf_excl_guards.py; audit section 6).
std::atomic<uint64_t> g_gl_gpu_split_calls{0};

std::string format_gl_gpu_report(const char* scope) {
    char buf[832];
    // beads-xbox.1.14.6: a self-check on the GL_TIME_ELAPSED bracket around the
    // readback, COMPUTED AT PRINT TIME from two counters this line already
    // carries -- it is not a struct field and costs nothing at run time.
    //
    // gpu_resolve_ns is the GPU-side bracket; readpixels_ns is the CPU-side wall
    // clock around the same glReadPixels calls. A correctly closed bracket makes
    // the first at most a small multiple of the second (the GPU work is a subset
    // of what the blocking call waits for, plus driver overhead). When the end
    // marker is not flushed the ratio blows out -- it was 77x before the flush
    // landed. 1.5x is the acceptance band. When readpixels_ns is 0 there were no
    // timed resolves at all, and the only consistent reading is gpu_resolve_ns
    // also 0; anything else is a bracket that ran without its CPU-side pair.
    const uint64_t resolve_gpu_ns = g_gl_gpu.gpu_resolve_ns.load();
    const uint64_t resolve_cpu_ns = g_gl_gpu.readpixels_ns.load();
    const unsigned resolve_bracket_ok =
        resolve_cpu_ns == 0
            ? (resolve_gpu_ns == 0 ? 1u : 0u)
            : (resolve_gpu_ns <= resolve_cpu_ns + resolve_cpu_ns / 2 ? 1u : 0u);
    std::snprintf(
        buf, sizeof(buf),
        "[perf-gl-gpu] scope=%s gpu_timing_available=%llu gpu_draw_ns=%llu "
        "gpu_draw_n=%llu gpu_upload_ns=%llu gpu_upload_n=%llu "
        "gpu_resolve_ns=%llu gpu_resolve_n=%llu drain_ns=%llu "
        "readpixels_ns=%llu cpu_copy_ns=%llu bytes_read=%llu dirty_bytes=%llu "
        "nested_skipped=%llu queries_dropped=%llu resolve_bracket_ok=%llu "
        "gpu_timing_enabled=%llu",
        scope,
        (unsigned long long)g_gl_gpu.gpu_timing_available.load(),
        (unsigned long long)g_gl_gpu.gpu_draw_ns.load(),
        (unsigned long long)g_gl_gpu.gpu_draw_n.load(),
        (unsigned long long)g_gl_gpu.gpu_upload_ns.load(),
        (unsigned long long)g_gl_gpu.gpu_upload_n.load(),
        (unsigned long long)g_gl_gpu.gpu_resolve_ns.load(),
        (unsigned long long)g_gl_gpu.gpu_resolve_n.load(),
        (unsigned long long)g_gl_gpu.drain_ns.load(),
        (unsigned long long)g_gl_gpu.readpixels_ns.load(),
        (unsigned long long)g_gl_gpu.cpu_copy_ns.load(),
        (unsigned long long)g_gl_gpu.bytes_read.load(),
        (unsigned long long)g_gl_gpu.dirty_bytes.load(),
        (unsigned long long)g_gl_gpu.nested_skipped.load(),
        (unsigned long long)g_gl_gpu.queries_dropped.load(),
        (unsigned long long)resolve_bracket_ok,
        (unsigned long long)g_gl_gpu.gpu_timing_enabled.load());
    return std::string(buf);
}

void restore_previous_gl_context(SDL_Window* previous_window,
                                 SDL_GLContext previous_context,
                                 SDL_Window* fallback_window,
                                 size_t perf_site) {
    // Gated: this ran two steady_clock reads on every context restore in every build
    // for a number the report discards unless --perf-accounting is on
    // (beads-xbox.1.14.3 item 5).
    perf::ScopedGlContextTimer gl_timer(perf_site);
    if (previous_context) {
        SDL_GL_MakeCurrent(previous_window, previous_context);
    } else {
        SDL_GL_MakeCurrent(fallback_window, nullptr);
    }
}

// `color` begins as the owned guest surface snapshot.  The integer edge equations
// and top-left rule deliberately mirror the software fixture, while the output
// buffer/coverage dispatch shape are reusable when generic-combiner uniforms grow.
constexpr const char* kFixtureComputeSource = R"GLSL(#version 430 core
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 0) buffer ColorSurface { uint color[]; };
uniform int surface_width;
uniform int surface_height;
uniform int surface_pitch_words;
uniform ivec2 vertex_a;
uniform ivec2 vertex_b;
uniform ivec2 vertex_c;
uniform ivec2 clip_origin;
uniform ivec2 clip_extent;
uniform uint source_argb;
int edge_value(ivec2 p0, ivec2 p1, ivec2 p) {
    return (p1.x - p0.x) * (p.y - p0.y) - (p1.y - p0.y) * (p.x - p0.x);
}
bool top_left(ivec2 p0, ivec2 p1) {
    int dx = p1.x - p0.x; int dy = p1.y - p0.y;
    return dy < 0 || (dy == 0 && dx > 0);
}
void main() {
    ivec2 xy = ivec2(gl_GlobalInvocationID.xy);
    if (xy.x >= surface_width || xy.y >= surface_height ||
        xy.x < clip_origin.x || xy.y < clip_origin.y ||
        xy.x >= clip_origin.x + clip_extent.x || xy.y >= clip_origin.y + clip_extent.y) return;
    ivec2 p = xy * 16 + ivec2(8);
    int w0 = edge_value(vertex_b, vertex_c, p);
    int w1 = edge_value(vertex_c, vertex_a, p);
    int w2 = edge_value(vertex_a, vertex_b, p);
    if (w0 < 0 || (w0 == 0 && !top_left(vertex_b, vertex_c)) ||
        w1 < 0 || (w1 == 0 && !top_left(vertex_c, vertex_a)) ||
        w2 < 0 || (w2 == 0 && !top_left(vertex_a, vertex_b))) return;
    uint offset = uint(xy.y * surface_pitch_words + xy.x);
    // The gated first fixture has blending disabled and all four color channels
    // writable, so the exact NV2A result is the decoded combiner word itself.
    color[offset] = source_argb;
}
)GLSL";

constexpr const char* kTextureModulateTriangleComputeSource = R"GLSL(#version 430 core
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 0) buffer ColorSurface { uint color[]; };
layout(std430, binding = 1) buffer VertexData { float vertex_data[]; };
layout(std430, binding = 2) buffer Texture0 { uint tex0_words[]; };
layout(std430, binding = 3) buffer VertexXY { int vertex_xy[]; };
layout(std430, binding = 4) readonly buffer TileOffsets { uint tile_offsets[]; };
layout(std430, binding = 5) readonly buffer TileTriangles { uint tile_triangles[]; };
uniform int surface_width;
uniform int surface_height;
uniform int surface_pitch_words;
uniform int texture_width;
uniform int texture_height;
uniform int texture_pitch_words;
uniform uint texture_swizzled;
uniform uint texture_repeat;
uniform uint texture_constant_color_alpha;
uniform uint constant_argb;
uniform int triangle_count;
uniform ivec2 clip_origin;
uniform ivec2 clip_extent;
uniform uint blend_src;
uniform uint blend_dst;
uniform uint color_format_x8;
uniform uint use_tile_bins;
uniform int tile_size;
uniform int tile_cols;
double edge_value(ivec2 p0, ivec2 p1, ivec2 p) {
    return double(p1.x - p0.x) * double(p.y - p0.y) -
           double(p1.y - p0.y) * double(p.x - p0.x);
}
bool top_left(ivec2 p0, ivec2 p1) {
    int dx = p1.x - p0.x; int dy = p1.y - p0.y;
    return dy < 0 || (dy == 0 && dx > 0);
}
vec4 unpack_argb(uint word) {
    return vec4(float((word >> 16u) & 255u), float((word >> 8u) & 255u),
                float(word & 255u), float((word >> 24u) & 255u)) / 255.0;
}
uint swizzle_index(uint x, uint y, uint width, uint height) {
    uint result = 0u;
    uint output_bit = 1u;
    for (uint input_bit = 1u; input_bit < width || input_bit < height; input_bit <<= 1u) {
        if (input_bit < width) {
            if ((x & input_bit) != 0u) result |= output_bit;
            output_bit <<= 1u;
        }
        if (input_bit < height) {
            if ((y & input_bit) != 0u) result |= output_bit;
            output_bit <<= 1u;
        }
    }
    return result;
}
uint pack_argb(vec4 value) {
    uvec4 v = uvec4(floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5));
    return (v.a << 24u) | (v.r << 16u) | (v.g << 8u) | v.b;
}
float vdata(uint vertex, uint offset) {
    return vertex_data[vertex * 8u + offset];
}
vec4 vdiffuse(uint vertex) {
    return clamp(vec4(vdata(vertex, 0u), vdata(vertex, 1u),
                      vdata(vertex, 2u), vdata(vertex, 3u)), 0.0, 1.0);
}
vec2 vtex(uint vertex) {
    return vec2(vdata(vertex, 4u), vdata(vertex, 5u));
}
float vtexq(uint vertex) {
    return vdata(vertex, 6u);
}
ivec2 vxy(uint vertex) {
    return ivec2(vertex_xy[vertex * 2u], vertex_xy[vertex * 2u + 1u]);
}
vec4 texel_argb(int x, int y) {
    int sx = x;
    int sy = y;
    if (texture_repeat != 0u) {
        if (sx < 0) sx = texture_width - 1;
        if (sx >= texture_width) sx = 0;
        if (sy < 0) sy = texture_height - 1;
        if (sy >= texture_height) sy = 0;
    } else {
        sx = clamp(sx, 0, texture_width - 1);
        sy = clamp(sy, 0, texture_height - 1);
    }
    uint index = texture_swizzled != 0u
        ? swizzle_index(uint(sx), uint(sy), uint(texture_width), uint(texture_height))
        : uint(sy * texture_pitch_words + sx);
    return unpack_argb(tex0_words[index]);
}
vec4 sample_linear_argb(vec2 uv) {
    float raw_s = texture_swizzled != 0u ? uv.x * float(texture_width) : uv.x;
    float raw_t = texture_swizzled != 0u ? uv.y * float(texture_height) : uv.y;
    float s = raw_s;
    float t = raw_t;
    if (texture_repeat != 0u) {
        s = s - floor(s / float(texture_width)) * float(texture_width);
        t = t - floor(t / float(texture_height)) * float(texture_height);
    } else {
        s = clamp(s, 0.0, float(texture_width));
        t = clamp(t, 0.0, float(texture_height));
    }
    float tx = s - 0.5;
    float ty = t - 0.5;
    int x0 = int(floor(tx)); int y0 = int(floor(ty));
    float fx = tx - float(x0); float fy = ty - float(y0);
    vec4 c00 = texel_argb(x0, y0);
    vec4 c10 = texel_argb(x0 + 1, y0);
    vec4 c01 = texel_argb(x0, y0 + 1);
    vec4 c11 = texel_argb(x0 + 1, y0 + 1);
    vec4 top = c00 + (c10 - c00) * fx;
    vec4 bottom = c01 + (c11 - c01) * fx;
    return top + (bottom - top) * fy;
}
float blend_factor(uint factor, vec4 src) {
    if (factor == 0u) return 0.0;
    if (factor == 1u) return 1.0;
    if (factor == 0x0302u) return src.a;
    if (factor == 0x0303u) return 1.0 - src.a;
    return 0.0;
}
void main() {
    ivec2 xy = ivec2(gl_GlobalInvocationID.xy);
    if (xy.x >= surface_width || xy.y >= surface_height ||
        xy.x < clip_origin.x || xy.y < clip_origin.y ||
        xy.x >= clip_origin.x + clip_extent.x || xy.y >= clip_origin.y + clip_extent.y) return;
    ivec2 p = xy * 16 + ivec2(8);
    uint offset = uint(xy.y * surface_pitch_words + xy.x);
    vec4 current = unpack_argb(color[offset]);
    bool covered = false;
    int list_begin = 0;
    int list_end = triangle_count;
    if (use_tile_bins != 0u) {
        uint tile_index = uint((xy.y / tile_size) * tile_cols + (xy.x / tile_size));
        list_begin = int(tile_offsets[tile_index]);
        list_end = int(tile_offsets[tile_index + 1u]);
    }
    for (int list_index = list_begin; list_index < list_end; ++list_index) {
        int tri = use_tile_bins != 0u ? int(tile_triangles[list_index]) : list_index;
        uint ia = uint(tri * 3);
        uint ib = ia + 1u;
        uint ic = ia + 2u;
        ivec2 vertex_a = vxy(ia);
        ivec2 vertex_b = vxy(ib);
        ivec2 vertex_c = vxy(ic);
        double area = edge_value(vertex_a, vertex_b, vertex_c);
        if (area == 0) continue;
        if (area < 0) {
            ivec2 tmp = vertex_b;
            vertex_b = vertex_c;
            vertex_c = tmp;
            uint tmp_index = ib;
            ib = ic;
            ic = tmp_index;
            area = -area;
        }
        double w0 = edge_value(vertex_b, vertex_c, p);
        double w1 = edge_value(vertex_c, vertex_a, p);
        double w2 = edge_value(vertex_a, vertex_b, p);
        if (w0 < 0 || (w0 == 0 && !top_left(vertex_b, vertex_c)) ||
            w1 < 0 || (w1 == 0 && !top_left(vertex_c, vertex_a)) ||
            w2 < 0 || (w2 == 0 && !top_left(vertex_a, vertex_b))) continue;
        double l0 = w0 / area;
        double l1 = w1 / area;
        double l2 = w2 / area;
        double inv_w0 = 1.0 / double(vdata(ia, 7u));
        double inv_w1 = 1.0 / double(vdata(ib, 7u));
        double inv_w2 = 1.0 / double(vdata(ic, 7u));
        double q = l0 * inv_w0 + l1 * inv_w1 + l2 * inv_w2;
        if (q == 0.0 || isnan(q) || isinf(q)) continue;
        vec4 diffuse = vec4(
            float((double(vdiffuse(ia).r) * (l0 * inv_w0) +
                   double(vdiffuse(ib).r) * (l1 * inv_w1) +
                   double(vdiffuse(ic).r) * (l2 * inv_w2)) / q),
            float((double(vdiffuse(ia).g) * (l0 * inv_w0) +
                   double(vdiffuse(ib).g) * (l1 * inv_w1) +
                   double(vdiffuse(ic).g) * (l2 * inv_w2)) / q),
            float((double(vdiffuse(ia).b) * (l0 * inv_w0) +
                   double(vdiffuse(ib).b) * (l1 * inv_w1) +
                   double(vdiffuse(ic).b) * (l2 * inv_w2)) / q),
            float((double(vdiffuse(ia).a) * (l0 * inv_w0) +
                   double(vdiffuse(ib).a) * (l1 * inv_w1) +
                   double(vdiffuse(ic).a) * (l2 * inv_w2)) / q));
        vec2 tex = vec2(
            float((double(vtex(ia).x) * (l0 * inv_w0) +
                   double(vtex(ib).x) * (l1 * inv_w1) +
                   double(vtex(ic).x) * (l2 * inv_w2)) / q),
            float((double(vtex(ia).y) * (l0 * inv_w0) +
                   double(vtex(ib).y) * (l1 * inv_w1) +
                   double(vtex(ic).y) * (l2 * inv_w2)) / q));
        float tex_q = float((double(vtexq(ia)) * (l0 * inv_w0) +
                             double(vtexq(ib)) * (l1 * inv_w1) +
                             double(vtexq(ic)) * (l2 * inv_w2)) / q);
        if (tex_q == 0.0) continue;
        tex /= tex_q;
        vec4 texel = sample_linear_argb(tex);
        vec4 src = clamp(diffuse, 0.0, 1.0) * texel;
        if (texture_constant_color_alpha != 0u) {
            vec4 constant = unpack_argb(constant_argb);
            src = vec4(constant.rgb, texel.a * constant.a);
        }
        float sf = blend_factor(blend_src, src);
        float df = blend_factor(blend_dst, src);
        vec3 rgb = clamp(src.rgb * sf + current.rgb * df, 0.0, 1.0);
        float alpha = color_format_x8 != 0u ? 1.0 : src.a;
        current = unpack_argb(pack_argb(vec4(rgb, alpha)));
        covered = true;
    }
    if (covered) color[offset] = pack_argb(current);
}
)GLSL";

constexpr const char* kPrimaryColorTriangleComputeSource = R"GLSL(#version 430 core
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 0) buffer ColorSurface { uint color[]; };
layout(std430, binding = 1) buffer VertexData { float vertex_data[]; };
layout(std430, binding = 2) buffer VertexXY { int vertex_xy[]; };
uniform int surface_width;
uniform int surface_height;
uniform int surface_pitch_words;
uniform int triangle_count;
uniform ivec2 clip_origin;
uniform ivec2 clip_extent;
uniform uint blend_src;
uniform uint blend_dst;
uniform uint color_format_x8;
double edge_value(ivec2 p0, ivec2 p1, ivec2 p) {
    return double(p1.x - p0.x) * double(p.y - p0.y) -
           double(p1.y - p0.y) * double(p.x - p0.x);
}
bool top_left(ivec2 p0, ivec2 p1) {
    int dx = p1.x - p0.x; int dy = p1.y - p0.y;
    return dy < 0 || (dy == 0 && dx > 0);
}
vec4 unpack_argb(uint word) {
    return vec4(float((word >> 16u) & 255u), float((word >> 8u) & 255u),
                float(word & 255u), float((word >> 24u) & 255u)) / 255.0;
}
uint pack_argb(vec4 value) {
    uvec4 v = uvec4(floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5));
    return (v.a << 24u) | (v.r << 16u) | (v.g << 8u) | v.b;
}
float vdata(uint vertex, uint offset) {
    return vertex_data[vertex * 5u + offset];
}
vec4 vdiffuse(uint vertex) {
    return clamp(vec4(vdata(vertex, 0u), vdata(vertex, 1u),
                      vdata(vertex, 2u), vdata(vertex, 3u)), 0.0, 1.0);
}
float vpos_w(uint vertex) {
    return vdata(vertex, 4u);
}
ivec2 vxy(uint vertex) {
    return ivec2(vertex_xy[vertex * 2u], vertex_xy[vertex * 2u + 1u]);
}
float blend_factor(uint factor, float src_alpha) {
    if (factor == 0u) return 0.0;
    if (factor == 1u) return 1.0;
    if (factor == 0x0302u) return src_alpha;
    if (factor == 0x0303u) return 1.0 - src_alpha;
    return 0.0;
}
void main() {
    ivec2 xy = ivec2(gl_GlobalInvocationID.xy);
    if (xy.x >= surface_width || xy.y >= surface_height ||
        xy.x < clip_origin.x || xy.y < clip_origin.y ||
        xy.x >= clip_origin.x + clip_extent.x ||
        xy.y >= clip_origin.y + clip_extent.y) return;
    ivec2 p = xy * 16 + ivec2(8);
    uint offset = uint(xy.y * surface_pitch_words + xy.x);
    vec4 current = unpack_argb(color[offset]);
    bool covered = false;
    for (int tri = 0; tri < triangle_count; ++tri) {
        uint ia = uint(tri * 3);
        uint ib = ia + 1u;
        uint ic = ia + 2u;
        ivec2 vertex_a = vxy(ia);
        ivec2 vertex_b = vxy(ib);
        ivec2 vertex_c = vxy(ic);
        double area = edge_value(vertex_a, vertex_b, vertex_c);
        if (area == 0.0) continue;
        if (area < 0.0) {
            ivec2 tmp = vertex_b;
            vertex_b = vertex_c;
            vertex_c = tmp;
            uint tmp_index = ib;
            ib = ic;
            ic = tmp_index;
            area = -area;
        }
        double w0 = edge_value(vertex_b, vertex_c, p);
        double w1 = edge_value(vertex_c, vertex_a, p);
        double w2 = edge_value(vertex_a, vertex_b, p);
        if (w0 < 0.0 || (w0 == 0.0 && !top_left(vertex_b, vertex_c)) ||
            w1 < 0.0 || (w1 == 0.0 && !top_left(vertex_c, vertex_a)) ||
            w2 < 0.0 || (w2 == 0.0 && !top_left(vertex_a, vertex_b))) continue;
        double l0 = w0 / area;
        double l1 = w1 / area;
        double l2 = w2 / area;
        double inv_w0 = 1.0 / double(vpos_w(ia));
        double inv_w1 = 1.0 / double(vpos_w(ib));
        double inv_w2 = 1.0 / double(vpos_w(ic));
        double q0 = l0 * inv_w0;
        double q1 = l1 * inv_w1;
        double q2 = l2 * inv_w2;
        double qsum = q0 + q1 + q2;
        if (qsum == 0.0 || isnan(qsum) || isinf(qsum)) continue;
        vec4 diffuse = vec4(
            float((double(vdiffuse(ia).r) * q0 + double(vdiffuse(ib).r) * q1 +
                   double(vdiffuse(ic).r) * q2) / qsum),
            float((double(vdiffuse(ia).g) * q0 + double(vdiffuse(ib).g) * q1 +
                   double(vdiffuse(ic).g) * q2) / qsum),
            float((double(vdiffuse(ia).b) * q0 + double(vdiffuse(ib).b) * q1 +
                   double(vdiffuse(ic).b) * q2) / qsum),
            float((double(vdiffuse(ia).a) * q0 + double(vdiffuse(ib).a) * q1 +
                   double(vdiffuse(ic).a) * q2) / qsum));
        vec4 src = clamp(diffuse, 0.0, 1.0);
        precise float src_alpha = src.a;
        precise float sf = blend_factor(blend_src, src_alpha);
        precise float df = blend_factor(blend_dst, src_alpha);
        precise float src_r = src.r * sf;
        precise float src_g = src.g * sf;
        precise float src_b = src.b * sf;
        precise float dst_r = current.r * df;
        precise float dst_g = current.g * df;
        precise float dst_b = current.b * df;
        vec3 rgb = clamp(vec3(src_r + dst_r, src_g + dst_g, src_b + dst_b),
                         0.0, 1.0);
        float alpha = color_format_x8 != 0u ? 1.0 : src.a;
        current = unpack_argb(pack_argb(vec4(rgb, alpha)));
        covered = true;
    }
    if (covered) color[offset] = pack_argb(current);
}
)GLSL";

constexpr const char* kRgba8ImageStoreProbeSource = R"GLSL(#version 430 core
layout(local_size_x = 16, local_size_y = 1) in;
layout(rgba8, binding = 0) writeonly uniform image2D output_image;
uvec4 pattern(uint x, uint y) {
    if (y == 0u) return uvec4(x, (x * 73u) & 255u, (x * 151u) & 255u, (x * 197u) & 255u);
    if (y == 1u) return uvec4(x, 0u, 0u, 255u);
    if (y == 2u) return uvec4(0u, x, 255u - x, 255u);
    return uvec4(255u, 255u - x, x, x);
}
void main() {
    uvec2 xy = gl_GlobalInvocationID.xy;
    if (xy.x >= 256u || xy.y >= 4u) return;
    uvec4 bytes = pattern(xy.x, xy.y);
    imageStore(output_image, ivec2(xy), vec4(bytes) / 255.0);
}
)GLSL";

constexpr const char* kRgba8ImageLoadBlendProbeSource = R"GLSL(#version 430 core
layout(local_size_x = 16, local_size_y = 1) in;
layout(rgba8, binding = 0) uniform image2D surface_image;
uvec4 source_pattern(uint x, uint y) {
    return uvec4(255u - x, (x * 29u + y * 7u) & 255u,
                 (x * 11u + 17u + y * 13u) & 255u,
                 (x * 53u + y * 31u) & 255u);
}
uint blend_factor_byte(uint factor, uint src_alpha) {
    if (factor == 0u) return 0u;
    if (factor == 1u) return 255u;
    if (factor == 0x0302u) return src_alpha;
    return 255u - src_alpha;
}
uint blend_channel(uint src, uint dst, uint src_factor, uint dst_factor) {
    return (src * src_factor + dst * dst_factor + 127u) / 255u;
}
uint blend_src(uint y) {
    if (y == 1u || y == 2u || y == 5u || y == 6u) return 0x0302u;
    return 1u;
}
uint blend_dst(uint y) {
    if (y == 1u || y == 5u) return 0x0303u;
    if (y == 2u || y == 6u) return 1u;
    return 0u;
}
void main() {
    uvec2 xy = gl_GlobalInvocationID.xy;
    if (xy.x >= 256u || xy.y >= 8u) return;
    uvec4 dst = uvec4(floor(clamp(imageLoad(surface_image, ivec2(xy)), 0.0, 1.0) *
                            255.0 + 0.5));
    uvec4 src = source_pattern(xy.x, xy.y);
    uint sf = blend_factor_byte(blend_src(xy.y), src.a);
    uint df = blend_factor_byte(blend_dst(xy.y), src.a);
    uvec3 rgb = uvec3(blend_channel(src.r, dst.r, sf, df),
                      blend_channel(src.g, dst.g, sf, df),
                      blend_channel(src.b, dst.b, sf, df));
    bool x8 = xy.y >= 4u;
    bool blend_enabled = xy.y == 1u || xy.y == 2u || xy.y == 5u || xy.y == 6u;
    uint alpha = x8 ? 255u : (blend_enabled ? blend_channel(src.a, dst.a, sf, df)
                                            : src.a);
    imageStore(surface_image, ivec2(xy), vec4(uvec4(rgb, alpha)) / 255.0);
}
)GLSL";

constexpr const char* kDashboardProject2dFanComputeSource = R"GLSL(#version 430 core
layout(local_size_x = 8, local_size_y = 8) in;
layout(std430, binding = 0) buffer ColorSurface { uint color[]; };
layout(std430, binding = 1) buffer VertexData { float vertex_data[]; };
layout(std430, binding = 2) buffer Texture0 { uint tex0_words[]; };
layout(std430, binding = 3) buffer Texture1 { uint tex1_words[]; };
layout(std430, binding = 4) buffer Texture2 { uint tex2_words[]; };
layout(std430, binding = 5) buffer Texture3 { uint tex3_words[]; };
uniform int surface_width;
uniform int surface_height;
uniform int surface_pitch_words;
uniform ivec2 vertex_a;
uniform ivec2 vertex_b;
uniform ivec2 vertex_c;
uniform ivec2 vertex_d;
uniform uint factor_stage2;
uniform uint factor_stage4;
int edge_value(ivec2 p0, ivec2 p1, ivec2 p) {
    return (p1.x - p0.x) * (p.y - p0.y) - (p1.y - p0.y) * (p.x - p0.x);
}
bool top_left(ivec2 p0, ivec2 p1) {
    int dx = p1.x - p0.x; int dy = p1.y - p0.y;
    return dy < 0 || (dy == 0 && dx > 0);
}
bool inside_triangle(ivec2 a, ivec2 b, ivec2 c, ivec2 p,
                     out int w0, out int w1, out int w2, out int area) {
    area = edge_value(a, b, c);
    if (area <= 0) return false;
    w0 = edge_value(b, c, p);
    w1 = edge_value(c, a, p);
    w2 = edge_value(a, b, p);
    if (w0 < 0 || (w0 == 0 && !top_left(b, c)) ||
        w1 < 0 || (w1 == 0 && !top_left(c, a)) ||
        w2 < 0 || (w2 == 0 && !top_left(a, b))) return false;
    return true;
}
float vtex(uint vertex, uint stage, uint component) {
    return vertex_data[vertex * 16u + stage * 4u + component];
}
vec2 interpolate_uv(uint ia, uint ib, uint ic, int w0, int w1, int w2, int area,
                    uint stage) {
    float l0 = float(w0) / float(area);
    float l1 = float(w1) / float(area);
    float l2 = float(w2) / float(area);
    return vec2(l0 * vtex(ia, stage, 0u) + l1 * vtex(ib, stage, 0u) +
                    l2 * vtex(ic, stage, 0u),
                l0 * vtex(ia, stage, 1u) + l1 * vtex(ib, stage, 1u) +
                    l2 * vtex(ic, stage, 1u));
}
uint swizzle_index(uint x, uint y, uint width, uint height) {
    uint result = 0u;
    uint output_bit = 1u;
    for (uint input_bit = 1u; input_bit < width || input_bit < height; input_bit <<= 1u) {
        if (input_bit < width) {
            if ((x & input_bit) != 0u) result |= output_bit;
            output_bit <<= 1u;
        }
        if (input_bit < height) {
            if ((y & input_bit) != 0u) result |= output_bit;
            output_bit <<= 1u;
        }
    }
    return result;
}
vec4 unpack_argb(uint word) {
    return vec4(float((word >> 16u) & 255u), float((word >> 8u) & 255u),
                float(word & 255u), float((word >> 24u) & 255u)) / 255.0;
}
uint pack_argb(vec4 value) {
    uvec4 v = uvec4(floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5));
    return (v.a << 24u) | (v.r << 16u) | (v.g << 8u) | v.b;
}
int repeat_coord(int value, int size) {
    int r = value % size;
    return r < 0 ? r + size : r;
}
vec4 sample_t0(vec2 uv) {
    float tx = clamp(uv.x, 0.0, 1.0) * 1024.0 - 0.5;
    float ty = clamp(uv.y, 0.0, 1.0) * 512.0 - 0.5;
    int x0 = int(floor(tx)); int y0 = int(floor(ty));
    float fx = tx - float(x0); float fy = ty - float(y0);
    vec4 accum = vec4(0.0);
    for (int yy = 0; yy < 2; ++yy) {
        for (int xx = 0; xx < 2; ++xx) {
            int sx = clamp(x0 + xx, 0, 1023);
            int sy = clamp(y0 + yy, 0, 511);
            float wx = xx == 0 ? 1.0 - fx : fx;
            float wy = yy == 0 ? 1.0 - fy : fy;
            accum += unpack_argb(tex0_words[swizzle_index(uint(sx), uint(sy), 1024u, 512u)]) *
                     (wx * wy);
        }
    }
    return accum;
}
float read_a8(uint stage, uint index) {
    uint word = stage == 1u ? tex1_words[index >> 2u] :
                (stage == 2u ? tex2_words[index >> 2u] : tex3_words[index >> 2u]);
    return float((word >> ((index & 3u) * 8u)) & 255u) / 255.0;
}
vec4 sample_a8(uint stage, vec2 uv) {
    float tx = (uv.x - floor(uv.x)) * 256.0 - 0.5;
    float ty = (uv.y - floor(uv.y)) * 256.0 - 0.5;
    int x0 = int(floor(tx)); int y0 = int(floor(ty));
    float fx = tx - float(x0); float fy = ty - float(y0);
    float a = 0.0;
    for (int yy = 0; yy < 2; ++yy) {
        for (int xx = 0; xx < 2; ++xx) {
            int sx = repeat_coord(x0 + xx, 256);
            int sy = repeat_coord(y0 + yy, 256);
            float wx = xx == 0 ? 1.0 - fx : fx;
            float wy = yy == 0 ? 1.0 - fy : fy;
            a += read_a8(stage, swizzle_index(uint(sx), uint(sy), 256u, 256u)) *
                 (wx * wy);
        }
    }
    return vec4(1.0, 1.0, 1.0, a);
}
void shade(uint ia, uint ib, uint ic, int w0, int w1, int w2, int area, ivec2 xy) {
    vec4 t0 = sample_t0(interpolate_uv(ia, ib, ic, w0, w1, w2, area, 0u));
    vec4 t1 = sample_a8(1u, interpolate_uv(ia, ib, ic, w0, w1, w2, area, 1u));
    vec4 t2 = sample_a8(2u, interpolate_uv(ia, ib, ic, w0, w1, w2, area, 2u));
    vec4 t3 = sample_a8(3u, interpolate_uv(ia, ib, ic, w0, w1, w2, area, 3u));
    float r1 = clamp(t1.a + t2.a, -1.0, 1.0);
    r1 = clamp(r1 + t3.a, -1.0, 1.0);
    vec4 f2 = unpack_argb(factor_stage2);
    vec4 f4 = unpack_argb(factor_stage4);
    vec3 r1rgb = clamp(vec3(r1) * f2.rgb, -1.0, 1.0);
    vec3 src = clamp(t0.rgb * r1rgb + f4.rgb, 0.0, 1.0);
    uint offset = uint(xy.y * surface_pitch_words + xy.x);
    vec4 dst = unpack_argb(color[offset]);
    color[offset] = pack_argb(vec4(clamp(src + dst.rgb, 0.0, 1.0), 1.0));
}
void main() {
    ivec2 xy = ivec2(gl_GlobalInvocationID.xy);
    if (xy.x >= surface_width || xy.y >= surface_height) return;
    ivec2 p = xy * 16 + ivec2(8);
    int w0; int w1; int w2; int area;
    if (inside_triangle(vertex_a, vertex_b, vertex_c, p, w0, w1, w2, area)) {
        shade(0u, 1u, 2u, w0, w1, w2, area, xy);
    } else if (inside_triangle(vertex_a, vertex_c, vertex_d, p, w0, w1, w2, area)) {
        shade(0u, 2u, 3u, w0, w1, w2, area, xy);
    }
}
)GLSL";

constexpr const char* kResidentPrimaryVertexSource = R"GLSL(#version 430 core
layout(location = 0) in vec4 in_position;
layout(location = 1) in vec4 in_diffuse;
noperspective out vec4 diffuse_over_w;
noperspective out float inv_w_out;
uniform vec2 surface_size;
void main() {
    float inv_w = in_position.w == 0.0 ? 1.0 : 1.0 / in_position.w;
    diffuse_over_w = in_diffuse * inv_w;
    inv_w_out = inv_w;
    vec2 ndc = vec2(in_position.x / surface_size.x * 2.0 - 1.0,
                    1.0 - in_position.y / surface_size.y * 2.0);
    gl_Position = vec4(ndc * in_position.w, 0.0, in_position.w);
}
)GLSL";

constexpr const char* kResidentPrimaryFragmentSource = R"GLSL(#version 430 core
noperspective in vec4 diffuse_over_w;
noperspective in float inv_w_out;
uniform uint primary_mode;
uniform uint constant_argb;
layout(location = 0) out vec4 out_color;
vec4 unpack_argb(uint word) {
    return vec4(float((word >> 16) & 255u) / 255.0,
                float((word >> 8) & 255u) / 255.0,
                float(word & 255u) / 255.0,
                float((word >> 24) & 255u) / 255.0);
}
void main() {
    vec4 diffuse = inv_w_out == 0.0 ? vec4(0.0, 0.0, 0.0, 1.0)
                                    : diffuse_over_w / inv_w_out;
    if (primary_mode == 1u) {
        vec4 constant = unpack_argb(constant_argb);
        out_color = clamp(vec4(constant.rgb, diffuse.a * constant.a), 0.0, 1.0);
    } else {
        out_color = clamp(diffuse, 0.0, 1.0);
    }
}
)GLSL";

constexpr const char* kResidentAlphaVertexSource = R"GLSL(#version 430 core
layout(location = 0) in vec4 in_position;
uniform vec2 surface_size;
void main() {
    vec2 ndc = vec2(in_position.x / surface_size.x * 2.0 - 1.0,
                    1.0 - in_position.y / surface_size.y * 2.0);
    gl_Position = vec4(ndc * in_position.w, 0.0, in_position.w);
}
)GLSL";

constexpr const char* kResidentAlphaFragmentSource = R"GLSL(#version 430 core
layout(location = 0) out vec4 out_color;
void main() {
    out_color = vec4(0.0, 0.0, 0.0, 1.0);
}
)GLSL";

constexpr const char* kResidentTextureModulateVertexSource = R"GLSL(#version 430 core
layout(location = 0) in vec4 in_position;
layout(location = 1) in vec4 in_diffuse;
layout(location = 2) in vec4 in_tex0;
noperspective out vec4 diffuse_over_w;
noperspective out vec4 tex0_over_w;
noperspective out float inv_w_out;
uniform vec2 surface_size;
void main() {
    float inv_w = in_position.w == 0.0 ? 1.0 : 1.0 / in_position.w;
    diffuse_over_w = in_diffuse * inv_w;
    tex0_over_w = in_tex0 * inv_w;
    inv_w_out = inv_w;
    float z = clamp(in_position.z, 0.0, 16777215.0) / 16777215.0 * 2.0 - 1.0;
    vec2 ndc = vec2(in_position.x / surface_size.x * 2.0 - 1.0,
                    1.0 - in_position.y / surface_size.y * 2.0);
    gl_Position = vec4(ndc * in_position.w, z * in_position.w, in_position.w);
}
)GLSL";

constexpr const char* kResidentTextureModulateFragmentSource = R"GLSL(#version 430 core
noperspective in vec4 diffuse_over_w;
noperspective in vec4 tex0_over_w;
noperspective in float inv_w_out;
uniform sampler2D texture0;
uniform vec2 texture_size;
uniform uint texture_passthrough;
uniform uint address_border;
uniform vec4 border_color;
uniform uint alpha_constant_modulate;
uniform float alpha_constant;
uniform uint alpha_kill_zero;
layout(location = 0) out vec4 out_color;
vec4 fetch_repeat(ivec2 p) {
    ivec2 size = ivec2(texture_size);
    ivec2 q = ivec2((p.x % size.x + size.x) % size.x,
                    (p.y % size.y + size.y) % size.y);
    return texelFetch(texture0, q, 0);
}
vec4 sample_repeat_bilinear(vec2 texcoord) {
    vec2 p = texcoord - vec2(0.5, 0.5);
    ivec2 base = ivec2(floor(p));
    vec2 f = p - vec2(base);
    vec4 c00 = fetch_repeat(base);
    vec4 c10 = fetch_repeat(base + ivec2(1, 0));
    vec4 c01 = fetch_repeat(base + ivec2(0, 1));
    vec4 c11 = fetch_repeat(base + ivec2(1, 1));
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}
// Mirrors the software generic-combiner sampler for CLAMP_TO_BORDER on both
// axes: taps outside the texture resolve to the border color, in-range taps
// clamp their coordinate, and the bilinear weights follow the same
// two-stage lerp shape as the software reference.
vec4 sample_border_bilinear(vec2 texcoord) {
    vec2 t = texcoord - vec2(0.5, 0.5);
    ivec2 i0 = ivec2(floor(t));
    vec2 f = t - vec2(i0);
    ivec2 sz = ivec2(texture_size);
    vec4 c00 = border_color, c10 = border_color,
          c01 = border_color, c11 = border_color;
    if (i0.x >= 0 && i0.x < sz.x && i0.y >= 0 && i0.y < sz.y)
        c00 = texelFetch(texture0, i0, 0);
    if (i0.x + 1 >= 0 && i0.x + 1 < sz.x && i0.y >= 0 && i0.y < sz.y)
        c10 = texelFetch(texture0, ivec2(i0.x + 1, i0.y), 0);
    if (i0.x >= 0 && i0.x < sz.x && i0.y + 1 >= 0 && i0.y + 1 < sz.y)
        c01 = texelFetch(texture0, ivec2(i0.x, i0.y + 1), 0);
    if (i0.x + 1 >= 0 && i0.x + 1 < sz.x && i0.y + 1 >= 0 && i0.y + 1 < sz.y)
        c11 = texelFetch(texture0, ivec2(i0.x + 1, i0.y + 1), 0);
    // Same operation sequence as the software sampler (top/bottom rows via
    // fx, then vertical fy), with fma() matching GCC's -march=native FMA
    // contraction of `c + (n - c) * f`.
    vec4 top = fma(c10 - c00, vec4(f.x), c00);
    vec4 bottom = fma(c11 - c01, vec4(f.x), c01);
    return fma(bottom - top, vec4(f.y), top);
}
void main() {
    vec4 diffuse = inv_w_out == 0.0 ? vec4(0.0, 0.0, 0.0, 1.0)
                                    : diffuse_over_w / inv_w_out;
    vec4 texcoord = inv_w_out == 0.0 ? vec4(0.0, 0.0, 0.0, 1.0)
                                     : tex0_over_w / inv_w_out;
    vec4 texel = address_border != 0u
        ? sample_border_bilinear(texcoord.xy)
        : sample_repeat_bilinear(texcoord.xy);
    out_color = texture_passthrough != 0u ? clamp(texel, 0.0, 1.0)
                                          : clamp(diffuse * texel, 0.0, 1.0);
    if (alpha_constant_modulate != 0u) {
        // Quantize to the destination byte grid inside the shader so the
        // framebuffer's own float->unorm8 conversion cannot re-round a
        // neighboring float representation (mirrors software's
        // floor(a*255+0.5) pack exactly).
        const float scaled =
            floor(clamp(texel.a, 0.0, 1.0) * alpha_constant * 255.0 + 0.5);
        out_color.a = scaled / 255.0;
    }
    if (alpha_kill_zero != 0u) {
        // Software packs the combiner output alpha with floor(v*255+0.5) and
        // compares the byte against the alpha reference (GEQUAL ref 1):
        // a zero packed byte means discard.
        float scaled = floor(clamp(out_color.a, 0.0, 1.0) * 255.0 + 0.5);
        if (scaled < 1.0) discard;
    }
}
)GLSL";

constexpr const char* kResidentDashboardProject2dFanVertexSource = R"GLSL(#version 430 core
layout(location = 0) in vec4 in_position;
layout(location = 1) in vec4 in_tex0;
layout(location = 2) in vec4 in_tex1;
layout(location = 3) in vec4 in_tex2;
layout(location = 4) in vec4 in_tex3;
noperspective out vec2 tex0_out;
noperspective out vec2 tex1_out;
noperspective out vec2 tex2_out;
noperspective out vec2 tex3_out;
flat out uint depth24_out;
uniform vec2 surface_size;
void main() {
    tex0_out = in_tex0.xy;
    tex1_out = in_tex1.xy;
    tex2_out = in_tex2.xy;
    tex3_out = in_tex3.xy;
    depth24_out = uint(clamp(in_position.z, 0.0, 16777215.0));
    vec2 ndc = vec2(in_position.x / surface_size.x * 2.0 - 1.0,
                    1.0 - in_position.y / surface_size.y * 2.0);
    gl_Position = vec4(ndc * in_position.w, 0.0, in_position.w);
}
)GLSL";

constexpr const char* kResidentDashboardProject2dFanFragmentSource = R"GLSL(#version 430 core
noperspective in vec2 tex0_out;
noperspective in vec2 tex1_out;
noperspective in vec2 tex2_out;
noperspective in vec2 tex3_out;
flat in uint depth24_out;
layout(std430, binding = 0) readonly buffer Texture0 { uint tex0_words[]; };
layout(std430, binding = 1) readonly buffer Texture1 { uint tex1_words[]; };
layout(std430, binding = 2) readonly buffer Texture2 { uint tex2_words[]; };
layout(std430, binding = 3) readonly buffer Texture3 { uint tex3_words[]; };
uniform usampler2D depth_words;
uniform uint use_depth_equal;
uniform uint factor_stage2;
uniform uint factor_stage4;
layout(location = 0) out vec4 out_color;
uint swizzle_index(uint x, uint y, uint width, uint height) {
    uint result = 0u;
    uint output_bit = 1u;
    for (uint input_bit = 1u; input_bit < width || input_bit < height; input_bit <<= 1u) {
        if (input_bit < width) {
            if ((x & input_bit) != 0u) result |= output_bit;
            output_bit <<= 1u;
        }
        if (input_bit < height) {
            if ((y & input_bit) != 0u) result |= output_bit;
            output_bit <<= 1u;
        }
    }
    return result;
}
vec4 unpack_argb(uint word) {
    return vec4(float((word >> 16u) & 255u), float((word >> 8u) & 255u),
                float(word & 255u), float((word >> 24u) & 255u)) / 255.0;
}
int repeat_coord(int value, int size) {
    int r = value % size;
    return r < 0 ? r + size : r;
}
vec4 sample_t0(vec2 uv) {
    float tx = clamp(uv.x, 0.0, 1.0) * 1024.0 - 0.5;
    float ty = clamp(uv.y, 0.0, 1.0) * 512.0 - 0.5;
    int x0 = int(floor(tx)); int y0 = int(floor(ty));
    float fx = tx - float(x0); float fy = ty - float(y0);
    vec4 accum = vec4(0.0);
    for (int yy = 0; yy < 2; ++yy) {
        for (int xx = 0; xx < 2; ++xx) {
            int sx = clamp(x0 + xx, 0, 1023);
            int sy = clamp(y0 + yy, 0, 511);
            float wx = xx == 0 ? 1.0 - fx : fx;
            float wy = yy == 0 ? 1.0 - fy : fy;
            accum += unpack_argb(tex0_words[swizzle_index(uint(sx), uint(sy), 1024u, 512u)]) *
                     (wx * wy);
        }
    }
    return accum;
}
float read_a8(uint stage, uint index) {
    uint word = stage == 1u ? tex1_words[index >> 2u] :
                (stage == 2u ? tex2_words[index >> 2u] : tex3_words[index >> 2u]);
    return float((word >> ((index & 3u) * 8u)) & 255u) / 255.0;
}
vec4 sample_a8(uint stage, vec2 uv) {
    float tx = (uv.x - floor(uv.x)) * 256.0 - 0.5;
    float ty = (uv.y - floor(uv.y)) * 256.0 - 0.5;
    int x0 = int(floor(tx)); int y0 = int(floor(ty));
    float fx = tx - float(x0); float fy = ty - float(y0);
    float a = 0.0;
    for (int yy = 0; yy < 2; ++yy) {
        for (int xx = 0; xx < 2; ++xx) {
            int sx = repeat_coord(x0 + xx, 256);
            int sy = repeat_coord(y0 + yy, 256);
            float wx = xx == 0 ? 1.0 - fx : fx;
            float wy = yy == 0 ? 1.0 - fy : fy;
            a += read_a8(stage, swizzle_index(uint(sx), uint(sy), 256u, 256u)) *
                 (wx * wy);
        }
    }
    return vec4(1.0, 1.0, 1.0, a);
}
void main() {
    if (use_depth_equal != 0u) {
        uint stored = texelFetch(depth_words, ivec2(gl_FragCoord.xy), 0).r >> 8;
        if (stored != depth24_out) discard;
    }
    vec4 t0 = sample_t0(tex0_out);
    vec4 t1 = sample_a8(1u, tex1_out);
    vec4 t2 = sample_a8(2u, tex2_out);
    vec4 t3 = sample_a8(3u, tex3_out);
    float r1 = clamp(t1.a + t2.a, -1.0, 1.0);
    r1 = clamp(r1 + t3.a, -1.0, 1.0);
    vec4 f2 = unpack_argb(factor_stage2);
    vec4 f4 = unpack_argb(factor_stage4);
    vec3 r1rgb = clamp(vec3(r1) * f2.rgb, -1.0, 1.0);
    vec3 src = clamp(t0.rgb * r1rgb + f4.rgb, 0.0, 1.0);
    out_color = vec4(src, 1.0);
}
)GLSL";

template <typename T>
bool load_gl(T& destination, const char* name) {
    destination = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    return destination != nullptr;
}

class OpenGlNv2aRenderer final : public Nv2aRenderer {
    // Forward declaration only. The definition is far below with the rest of
    // the resident-surface cache; the generated-shader executor sits ABOVE it
    // in the file and needs the name in a parameter type.
    struct ResidentSurface;
public:
    void set_enhanced_xemu(bool enabled) { enhanced_xemu_ = enabled || XLLE_NATIVE_DRAW; }
#if XLLE_NATIVE_DRAW
    bool prepare_native_draw(const Nv2aGeneratedShaderPacket& packet) override {
        ++native_attempts_;
        return !packet.native_vertices.empty() &&
            nv2a_xemu_psh_supported(packet) == XemuPshReject::kNone &&
            supports_generated_shader(packet);
    }
#endif
    bool prepare_generated_shader(const Nv2aGeneratedShaderPacket& packet) override {
        if (!enhanced_xemu_) return true;
        ++enhanced_attempts_;
        const size_t decision = static_cast<size_t>(nv2a_xemu_psh_supported(packet));
        if (++enhanced_decisions_[decision] == 1) {
            std::printf("[nv2a-renderer] xemu-psh first-decision=%zu program=%08x input=%08x "
                        "formats=%08x,%08x,%08x,%08x controls=%08x,%08x,%08x,%08x "
                        "filters=%08x,%08x,%08x,%08x modes=%u,%u,%u,%u "
                        "address-uv=%u/%u,%u/%u,%u/%u,%u/%u\n",
                        decision, packet.guest.shader_program, packet.guest.shader_input,
                        packet.guest.texture_format[0], packet.guest.texture_format[1],
                        packet.guest.texture_format[2], packet.guest.texture_format[3],
                        packet.guest.texture_control[0], packet.guest.texture_control[1],
                        packet.guest.texture_control[2], packet.guest.texture_control[3],
                        packet.guest.texture_filter[0], packet.guest.texture_filter[1],
                        packet.guest.texture_filter[2], packet.guest.texture_filter[3],
                        unsigned(packet.state.textures[0].mode), unsigned(packet.state.textures[1].mode),
                        unsigned(packet.state.textures[2].mode), unsigned(packet.state.textures[3].mode),
                        unsigned(packet.state.textures[0].address_u), unsigned(packet.state.textures[0].address_v),
                        unsigned(packet.state.textures[1].address_u), unsigned(packet.state.textures[1].address_v),
                        unsigned(packet.state.textures[2].address_u), unsigned(packet.state.textures[2].address_v),
                        unsigned(packet.state.textures[3].address_u), unsigned(packet.state.textures[3].address_v));
        }
        return supports_generated_shader(packet);
    }
    OpenGlNv2aRenderer(SDL_Window* window, SDL_GLContext context, bool owns_video,
                       unsigned int fixture_program,
                       unsigned int texture_modulate_program,
                       unsigned int dashboard_program,
                       unsigned int resident_program,
                       unsigned int resident_alpha_program,
                       unsigned int resident_texture_modulate_program,
                       unsigned int resident_dashboard_program)
        : window_(window), context_(context), owns_video_(owns_video),
          fixture_program_(fixture_program),
          texture_modulate_program_(texture_modulate_program),
          dashboard_program_(dashboard_program),
          resident_program_(resident_program),
          resident_alpha_program_(resident_alpha_program),
          resident_texture_modulate_program_(resident_texture_modulate_program),
          resident_dashboard_program_(resident_dashboard_program) {}
    ~OpenGlNv2aRenderer() override {
#if XLLE_NATIVE_DRAW
        std::printf("[nv2a-renderer] native-draw build=HLE attempts=%llu draws=%llu "
                    "vertices=%llu triangles-submitted=%llu\n",
                    (unsigned long long)native_attempts_, (unsigned long long)native_draws_,
                    (unsigned long long)native_vertices_, (unsigned long long)native_triangles_);
#endif
        if (enhanced_xemu_) {
            std::printf("[nv2a-renderer] xemu-psh attempts=%llu draws=%llu triangles=%llu "
                        "eligible=%llu reference-state=%llu reference-combiner=%llu "
                        "reference-texture=%llu reference-geometry=%llu\n",
                        (unsigned long long)enhanced_attempts_,
                        (unsigned long long)enhanced_draws_,
                        (unsigned long long)enhanced_triangles_,
                        (unsigned long long)enhanced_decisions_[0],
                        (unsigned long long)enhanced_decisions_[1],
                        (unsigned long long)enhanced_decisions_[2],
                        (unsigned long long)enhanced_decisions_[3],
                        (unsigned long long)enhanced_decisions_[4]);
        }
        // Destruction can happen after an SDL presentation context was made
        // current; GL object deletion must be issued against this renderer's own
        // context, not whichever context happened to be current last.
        if (window_ && context_) SDL_GL_MakeCurrent(window_, context_);
#if XLLE_VSH_GPU_RESIDENT
        if (delete_program_) {
            if (vsh_batch_program_) delete_program_(vsh_batch_program_);
            if (vsh_pack_program_) delete_program_(vsh_pack_program_);
            if (vsh_parallel_pack_program_) delete_program_(vsh_parallel_pack_program_);
            for (const auto& entry : vsh_specializations_)
                if (entry.program) delete_program_(entry.program);
        }
        if (delete_buffers_) delete_buffers_(static_cast<int>(vsh_buffers_.size()), vsh_buffers_.data());
#endif
        // Stage (f): a pending witnessed batch must be issued before the
        // context and its buffers go away. In practice the final scanout
        // resolve has already flushed it; this is the backstop that makes the
        // "no batch outlives the renderer" property local rather than a fact
        // about the call order.
        flush_witnessed_batch_deferred();
        // beads-xbox.1.14.6: drain the outstanding GL_TIME_ELAPSED queries while
        // this context is still current and the query objects still exist, then
        // emit the one cumulative [perf-gl-gpu] line.
        //
        // WHY SHUTDOWN IS THE ONLY PRINT SITE: main.cpp prints each perf family
        // by calling a free xlle::perf::format_*_report() declared in
        // src/machine/perf_accounting.hpp, and Nv2aRenderer
        // (src/machine/nv2a_renderer.hpp) exposes no report or per-checkpoint
        // hook a backend could feed. Neither file is in scope for this change,
        // so this family has run totals only -- no frame-series samples.
        if (perf::g_enabled) {
            if (gpu_timing_available_ &&
                (gpu_timers_enabled_ || gpu_query_live_ > 0)) {
                // Force the pipeline to completion BEFORE the blocking harvest.
                // harvest_gpu_queries(block=true) spins on GL_QUERY_RESULT for each
                // outstanding query; on a driver that has not been flushed, work the
                // queries sit behind may never be submitted, and the spin never
                // terminates. glFinish makes termination the driver's problem, which
                // it is required to solve. Costs one full sync, once, at shutdown.
                if (finish_) finish_();
                harvest_gpu_queries(/*block=*/true);
            }
            std::printf("%s\n", format_gl_gpu_report("cumulative").c_str());
#if XLLE_VSH_GPU_RESIDENT
            if (vsh_resident_stats_.batches) {
                constexpr const char* phases[] = {"total", "upload", "vsh_submit", "pack_submit",
                    "read_outputs", "read_status", "read_candidates", "read_vertices", "read_control", "verify"};
                for (size_t i = 0; i < vsh_cpu_ns_.size(); ++i)
                    std::printf("[perf-vsh-resident-cpu] phase=%s ns=%llu calls=%llu\n", phases[i],
                        static_cast<unsigned long long>(vsh_cpu_ns_[i].load()),
                        static_cast<unsigned long long>(vsh_cpu_calls_[i].load()));
                std::printf("[perf-vsh-resident-gpu] vsh_ns=%llu vsh_n=%llu pack_ns=%llu pack_n=%llu "
                    "upload_bytes=%llu download_bytes=%llu available=%u\n",
                    static_cast<unsigned long long>(vsh_gpu_ns_[0]), static_cast<unsigned long long>(vsh_gpu_calls_[0]),
                    static_cast<unsigned long long>(vsh_gpu_ns_[1]), static_cast<unsigned long long>(vsh_gpu_calls_[1]),
                    static_cast<unsigned long long>(vsh_upload_bytes_), static_cast<unsigned long long>(vsh_download_bytes_),
                    gpu_timing_available_ && gpu_timers_enabled_ ? 1u : 0u);
            }
#endif
            std::fflush(stdout);
        }
        if (delete_queries_) {
            for (auto& slot : gpu_query_ring_) {
                if (slot.id) delete_queries_(1, &slot.id);
            }
        }
        if (fixture_program_ && delete_program_) delete_program_(fixture_program_);
        if (primary_color_program_ && delete_program_)
            delete_program_(primary_color_program_);
        if (texture_modulate_program_ && delete_program_)
            delete_program_(texture_modulate_program_);
        if (dashboard_program_ && delete_program_)
            delete_program_(dashboard_program_);
        if (resident_program_ && delete_program_) delete_program_(resident_program_);
        if (resident_alpha_program_ && delete_program_)
            delete_program_(resident_alpha_program_);
        if (resident_texture_modulate_program_ && delete_program_)
            delete_program_(resident_texture_modulate_program_);
        if (resident_dashboard_program_ && delete_program_)
            delete_program_(resident_dashboard_program_);
        for (const auto& cache : dashboard_texture_cache_) {
            if (cache.buffer && delete_buffers_) delete_buffers_(1, &cache.buffer);
        }
        if (resident_vbo_ && delete_buffers_) delete_buffers_(1, &resident_vbo_);
        if (delete_buffers_) {
            for (unsigned int& ssbo : witnessed_ssbo_ring_)
                if (ssbo) delete_buffers_(1, &ssbo);
        }
        if (resident_vao_ && delete_vertex_arrays_)
            delete_vertex_arrays_(1, &resident_vao_);
        if (resident_fbo_ && delete_framebuffers_)
            delete_framebuffers_(1, &resident_fbo_);
        if (delete_textures_) {
            for (const auto& surface : resident_surfaces_) {
                if (surface.texture) delete_textures_(1, &surface.texture);
                if (surface.depth_texture)
                    delete_textures_(1, &surface.depth_texture);
                if (surface.depth_word_texture)
                    delete_textures_(1, &surface.depth_word_texture);
            }
        }
        if (resident_source_texture_ && delete_textures_)
            delete_textures_(1, &resident_source_texture_);
        if (context_) SDL_GL_DestroyContext(context_);
        if (window_) SDL_DestroyWindow(window_);
        if (owns_video_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    const char* name() const override { return enhanced_xemu_ ? "xemu-psh-opengl" : "opengl"; }
    bool supports_generic_combiner(const Nv2aGenericCombinerPacket& packet) const override {
        if (packet.compute_fixture ==
            Nv2aGenericCombinerPacket::ComputeFixture::kUntexturedPrimaryTriangle) {
            return fixture_program_ != 0 && packet.primitive_mode == 5 &&
                   packet.vertices.size() == 3 && packet.depth_input.empty() &&
                   packet.surface_pitch != 0 && packet.surface_height != 0 &&
                   packet.fixture_clip_width != 0 &&
                   packet.fixture_clip_height != 0 &&
                   packet.fixture_clip_x + packet.fixture_clip_width <=
                       (packet.surface_pitch & 0xffffu) / 4 &&
                   packet.fixture_clip_y + packet.fixture_clip_height <= packet.surface_height;
        }
        if (packet.compute_fixture ==
            Nv2aGenericCombinerPacket::ComputeFixture::kResidentPrimaryColorTriangles) {
            if (std::getenv("XLLE_PRIMARY_COMPUTE_FIXTURE") == nullptr)
                return false;
            if (primary_color_program_ == 0 || packet.primitive_mode != 5 ||
                packet.vertices.empty() || (packet.vertices.size() % 3u) != 0 ||
                !packet.depth_input.empty() || packet.surface_pitch == 0 ||
                packet.surface_height == 0 || packet.fixture_clip_width == 0 ||
                packet.fixture_clip_height == 0) {
                return false;
            }
            if (packet.fixture_blend_enabled) {
                auto factor_supported = [](uint32_t factor) {
                    return factor == 0 || factor == 1 || factor == 0x0302 ||
                           factor == 0x0303;
                };
                if (!factor_supported(packet.fixture_blend_src) ||
                    !factor_supported(packet.fixture_blend_dst)) {
                    return false;
                }
            }
            const uint32_t width = (packet.surface_pitch & 0xffffu) / 4u;
            const bool large_diagnostic =
                std::getenv("XLLE_LARGE_PRIMARY_COMPUTE_FIXTURE") != nullptr;
            return width > 0 &&
                   (large_diagnostic ||
                    (width <= 64 && packet.surface_height <= 64)) &&
                   packet.fixture_clip_x + packet.fixture_clip_width <= width &&
                   packet.fixture_clip_y + packet.fixture_clip_height <=
                       packet.surface_height;
        }
        if (packet.compute_fixture ==
            Nv2aGenericCombinerPacket::ComputeFixture::kTextureModulateTriangle) {
            if (texture_modulate_program_ == 0 || packet.primitive_mode != 5 ||
                packet.vertices.empty() || (packet.vertices.size() % 3u) != 0 ||
                !packet.depth_input.empty() ||
                packet.surface_pitch == 0 || packet.surface_height == 0 ||
                packet.fixture_clip_width == 0 || packet.fixture_clip_height == 0 ||
                !packet.textures[0].referenced ||
                packet.textures[0].format !=
                    Nv2aGenericCombinerPacket::TextureFormat::kArgb8 ||
                packet.textures[0].width == 0 || packet.textures[0].height == 0) {
                return false;
            }
            const bool linear_texture =
                packet.textures[0].layout ==
                Nv2aGenericCombinerPacket::TextureLayout::kLinear;
            if (!linear_texture &&
                packet.textures[0].layout !=
                    Nv2aGenericCombinerPacket::TextureLayout::kSwizzled) {
                return false;
            }
            if (linear_texture && packet.textures[0].pitch == 0) return false;
            const bool large_diagnostic =
                std::getenv("XLLE_LARGE_TEXTURE_COMPUTE_FIXTURE") != nullptr ||
                packet.fixture_use_tile_bins;
            const uint32_t width = (packet.surface_pitch & 0xffffu) / 4u;
            return width > 0 &&
                   (large_diagnostic ||
                    (width <= 64 && packet.surface_height <= 64)) &&
                   packet.fixture_clip_x + packet.fixture_clip_width <= width &&
                   packet.fixture_clip_y + packet.fixture_clip_height <=
                       packet.surface_height;
        }
        if (packet.compute_fixture !=
            Nv2aGenericCombinerPacket::ComputeFixture::kDashboardProject2dFan) {
            return false;
        }
        if (dashboard_program_ == 0 || packet.primitive_mode != 7 ||
            packet.vertices.size() != 4 || packet.surface_pitch != 0x14001400 ||
            packet.surface_height != 480) {
            return false;
        }
        for (size_t stage = 0; stage < packet.textures.size(); ++stage) {
            const auto& texture = packet.textures[stage];
            if (!texture.referenced ||
                texture.layout != Nv2aGenericCombinerPacket::TextureLayout::kSwizzled ||
                texture.mode != Nv2aGenericCombinerPacket::TextureMode::kProject2d) {
                return false;
            }
            if (stage == 0) {
                if (texture.format != Nv2aGenericCombinerPacket::TextureFormat::kArgb8 ||
                    texture.width != 1024 || texture.height != 512) {
                    return false;
                }
            } else if (texture.format != Nv2aGenericCombinerPacket::TextureFormat::kA8 ||
                       texture.width != 256 || texture.height != 256) {
                return false;
            }
        }
        return true;
    }
    bool debug_probe_rgba8_image_store(std::string& error) override {
        invalidate_gl_state_cache();
        SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
        if (!SDL_GL_MakeCurrent(window_, context_)) {
            error = std::string("OpenGL RGBA8 image-store probe context lost: ") +
                    SDL_GetError();
            return false;
        }
        struct ContextRestore {
            SDL_Window* previous_window;
            SDL_GLContext previous_context;
            SDL_Window* fallback_window;
            size_t perf_site;
            ~ContextRestore() {
                restore_previous_gl_context(previous_window, previous_context,
                                            fallback_window, perf_site);
            }
        } restore{previous_window, previous_context, window_,
                  perf::kGlContextOtherRestore};

        unsigned int program = 0;
        if (!compile_program(kRgba8ImageStoreProbeSource, "rgba8-image-store",
                             program, error)) {
            return false;
        }
        unsigned int texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(kGlTexture2D, texture);
        tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
        tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
        tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
        tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlRgba8, 256, 4, 0, kGlRgba,
                               kGlUnsignedByte, nullptr);
        bind_image_texture_(0, texture, 0, 0, 0, kGlWriteOnly, kGlRgba8);
        use_program_(program);
        gpu_timed_dispatch_compute(16, 4, 1);
        memory_barrier_(kGlShaderImageAccessBarrierBit | kGlFramebufferBarrierBit);

        unsigned int fbo = 0;
        gen_framebuffers_(1, &fbo);
        bind_framebuffer_(kGlFramebuffer, fbo);
        framebuffer_texture_2d_(kGlFramebuffer, kGlColorAttachment0, kGlTexture2D,
                                texture, 0);
        bool ok = true;
        if (check_framebuffer_status_(kGlFramebuffer) != kGlFramebufferComplete) {
            error = "OpenGL RGBA8 image-store probe framebuffer is incomplete";
            ok = false;
        }
        std::vector<uint8_t> actual(256u * 4u * 4u, 0);
        if (ok) {
            pixel_store_i_(kGlPackAlignment, 1);
            gpu_timed_read_pixels(0, 0, 256, 4, kGlRgba, kGlUnsignedByte,
                                  actual.data());
            if (get_error_() != kGlNoError) {
                error = "OpenGL RGBA8 image-store probe readback failed";
                ok = false;
            }
        }
        if (ok) {
            for (uint32_t y = 0; y < 4u; ++y) {
                for (uint32_t x = 0; x < 256u; ++x) {
                    std::array<uint8_t, 4> expected{};
                    if (y == 0u) {
                        expected = {
                            static_cast<uint8_t>(x),
                            static_cast<uint8_t>((x * 73u) & 255u),
                            static_cast<uint8_t>((x * 151u) & 255u),
                            static_cast<uint8_t>((x * 197u) & 255u)};
                    } else if (y == 1u) {
                        expected = {static_cast<uint8_t>(x), 0, 0, 255};
                    } else if (y == 2u) {
                        expected = {0, static_cast<uint8_t>(x),
                                    static_cast<uint8_t>(255u - x), 255};
                    } else {
                        expected = {255, static_cast<uint8_t>(255u - x),
                                    static_cast<uint8_t>(x),
                                    static_cast<uint8_t>(x)};
                    }
                    const size_t offset =
                        (static_cast<size_t>(y) * 256u + x) * 4u;
                    for (size_t channel = 0; channel < 4; ++channel) {
                        if (actual[offset + channel] != expected[channel]) {
                            char why[192];
                            std::snprintf(
                                why, sizeof(why),
                                "OpenGL RGBA8 image-store probe mismatch at %u,%u "
                                "channel %zu: got=%u expected=%u",
                                x, y, channel,
                                static_cast<unsigned>(actual[offset + channel]),
                                static_cast<unsigned>(expected[channel]));
                            error = why;
                            ok = false;
                            break;
                        }
                    }
                    if (!ok) break;
                }
                if (!ok) break;
            }
        }
        if (fbo && delete_framebuffers_) delete_framebuffers_(1, &fbo);
        if (texture && delete_textures_) delete_textures_(1, &texture);
        if (program && delete_program_) delete_program_(program);
        if (ok) error.clear();
        return ok;
    }
    bool debug_probe_rgba8_image_load_blend(std::string& error) override {
        invalidate_gl_state_cache();
        SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
        if (!SDL_GL_MakeCurrent(window_, context_)) {
            error = std::string("OpenGL RGBA8 image-load/blend probe context lost: ") +
                    SDL_GetError();
            return false;
        }
        struct ContextRestore {
            SDL_Window* previous_window;
            SDL_GLContext previous_context;
            SDL_Window* fallback_window;
            size_t perf_site;
            ~ContextRestore() {
                restore_previous_gl_context(previous_window, previous_context,
                                            fallback_window, perf_site);
            }
        } restore{previous_window, previous_context, window_,
                  perf::kGlContextOtherRestore};

        unsigned int program = 0;
        if (!compile_program(kRgba8ImageLoadBlendProbeSource,
                             "rgba8-image-load-blend", program, error)) {
            return false;
        }
        auto initial_pattern = [](uint32_t x, uint32_t y) {
            return std::array<uint8_t, 4>{
                static_cast<uint8_t>(x),
                static_cast<uint8_t>((x * 73u + y * 5u) & 255u),
                static_cast<uint8_t>((x * 151u + y * 3u) & 255u),
                static_cast<uint8_t>((x * 197u + y * 19u) & 255u)};
        };
        auto source_pattern = [](uint32_t x, uint32_t y) {
            return std::array<uint8_t, 4>{
                static_cast<uint8_t>(255u - x),
                static_cast<uint8_t>((x * 29u + y * 7u) & 255u),
                static_cast<uint8_t>((x * 11u + 17u + y * 13u) & 255u),
                static_cast<uint8_t>((x * 53u + y * 31u) & 255u)};
        };
        auto pack_unorm8 = [](float value) {
            return static_cast<uint8_t>(std::floor(
                std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f));
        };
        auto blend_factor = [](uint32_t factor, float src_alpha) {
            switch (factor) {
                case 0: return 0.0f;
                case 1: return 1.0f;
                case 0x0302: return src_alpha;
                default: return 1.0f - src_alpha;
            }
        };
        auto blend_src = [](uint32_t y) {
            return (y == 1u || y == 2u || y == 5u || y == 6u) ? 0x0302u : 1u;
        };
        auto blend_dst = [](uint32_t y) {
            if (y == 1u || y == 5u) return 0x0303u;
            if (y == 2u || y == 6u) return 1u;
            return 0u;
        };
        auto expected_pattern = [&](uint32_t x, uint32_t y) {
            const auto dst_bytes = initial_pattern(x, y);
            const auto src_bytes = source_pattern(x, y);
            std::array<float, 4> dst{};
            std::array<float, 4> src{};
            for (size_t c = 0; c < 4; ++c) {
                dst[c] = static_cast<float>(dst_bytes[c]) / 255.0f;
                src[c] = static_cast<float>(src_bytes[c]) / 255.0f;
            }
            const float src_alpha = std::clamp(src[3], 0.0f, 1.0f);
            const float sf = blend_factor(blend_src(y), src_alpha);
            const float df = blend_factor(blend_dst(y), src_alpha);
            std::array<uint8_t, 4> out{};
            for (size_t c = 0; c < 3; ++c)
                out[c] = pack_unorm8(src[c] * sf + dst[c] * df);
            const bool x8 = y >= 4u;
            const bool blend_enabled =
                y == 1u || y == 2u || y == 5u || y == 6u;
            const float alpha = x8
                ? 1.0f
                : (blend_enabled ? std::clamp(src[3] * sf + dst[3] * df, 0.0f, 1.0f)
                                 : src[3]);
            out[3] = pack_unorm8(alpha);
            return out;
        };

        std::vector<uint8_t> initial(256u * 8u * 4u, 0);
        for (uint32_t y = 0; y < 8u; ++y) {
            for (uint32_t x = 0; x < 256u; ++x) {
                const auto bytes = initial_pattern(x, y);
                const size_t offset = (static_cast<size_t>(y) * 256u + x) * 4u;
                for (size_t channel = 0; channel < 4u; ++channel)
                    initial[offset + channel] = bytes[channel];
            }
        }

        unsigned int texture = 0;
        gen_textures_(1, &texture);
        bind_texture_(kGlTexture2D, texture);
        tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
        tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
        tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
        tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        pixel_store_i_(kGlUnpackAlignment, 1);
        gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlRgba8, 256, 8, 0, kGlRgba,
                               kGlUnsignedByte, initial.data());
        bind_image_texture_(0, texture, 0, 0, 0, kGlReadWrite, kGlRgba8);
        use_program_(program);
        gpu_timed_dispatch_compute(16, 8, 1);
        memory_barrier_(kGlShaderImageAccessBarrierBit | kGlFramebufferBarrierBit);

        unsigned int fbo = 0;
        gen_framebuffers_(1, &fbo);
        bind_framebuffer_(kGlFramebuffer, fbo);
        framebuffer_texture_2d_(kGlFramebuffer, kGlColorAttachment0, kGlTexture2D,
                                texture, 0);
        bool ok = true;
        if (check_framebuffer_status_(kGlFramebuffer) != kGlFramebufferComplete) {
            error = "OpenGL RGBA8 image-load/blend probe framebuffer is incomplete";
            ok = false;
        }
        std::vector<uint8_t> actual(256u * 8u * 4u, 0);
        if (ok) {
            pixel_store_i_(kGlPackAlignment, 1);
            gpu_timed_read_pixels(0, 0, 256, 8, kGlRgba, kGlUnsignedByte,
                                  actual.data());
            if (get_error_() != kGlNoError) {
                error = "OpenGL RGBA8 image-load/blend probe readback failed";
                ok = false;
            }
        }
        if (ok) {
            for (uint32_t y = 0; y < 8u; ++y) {
                for (uint32_t x = 0; x < 256u; ++x) {
                    const auto expected = expected_pattern(x, y);
                    const size_t offset =
                        (static_cast<size_t>(y) * 256u + x) * 4u;
                    for (size_t channel = 0; channel < 4u; ++channel) {
                        if (actual[offset + channel] != expected[channel]) {
                            char why[224];
                            std::snprintf(
                                why, sizeof(why),
                                "OpenGL RGBA8 image-load/blend probe mismatch "
                                "at %u,%u channel %zu: got=%u expected=%u",
                                x, y, channel,
                                static_cast<unsigned>(actual[offset + channel]),
                                static_cast<unsigned>(expected[channel]));
                            error = why;
                            ok = false;
                            break;
                        }
                    }
                    if (!ok) break;
                }
                if (!ok) break;
            }
        }
        if (fbo && delete_framebuffers_) delete_framebuffers_(1, &fbo);
        if (texture && delete_textures_) delete_textures_(1, &texture);
        if (program && delete_program_) delete_program_(program);
        if (ok) error.clear();
        return ok;
    }
    bool has_resident_color_surface(uint32_t address, size_t size, uint32_t pitch,
                                    uint32_t height,
                                    uint32_t format) const override {
        return find_resident_surface(address, size, pitch, height, format) !=
               nullptr;
    }
    bool has_resident_depth_surface(uint32_t address, size_t size, uint32_t pitch,
                                    uint32_t height,
                                    uint32_t format) const override {
        return find_resident_depth_surface(address, size, pitch, height, format) !=
               nullptr;
    }
    bool requires_resident_target_switch_resolve() const override {
        return false;
    }
    bool supports_resident_generic_combiner(
        const Nv2aGenericCombinerPacket& packet) const override {
        const bool primary_fixture =
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kResidentPrimaryColorTriangles;
        const bool constant_fixture =
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kResidentConstantColorTriangles;
        const bool texture_fixture =
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kResidentTextureModulateTriangles;
        const bool dashboard_fixture =
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kDashboardProject2dFan;
        if (!primary_fixture && !constant_fixture && !texture_fixture &&
            !dashboard_fixture) {
            return false;
        }
        size_t vertex_count = packet.vertices.size();
#if XLLE_VSH_GPU_RESIDENT
        if (packet.gpu_vsh && packet.gpu_vsh->mode == 3) {
            if ((!primary_fixture && !constant_fixture) || !packet.vertices.empty() ||
                !valid_vsh_preparation(*packet.gpu_vsh)) return false;
            vertex_count = packet.gpu_vsh->prepared_vertices;
        }
#endif
        if (((primary_fixture || constant_fixture) && resident_program_ == 0) ||
            (texture_fixture && resident_texture_modulate_program_ == 0) ||
            (dashboard_fixture && resident_dashboard_program_ == 0) ||
            (!dashboard_fixture && packet.primitive_mode != 5) ||
            (dashboard_fixture && packet.primitive_mode != 7) ||
            vertex_count == 0 ||
            (!dashboard_fixture && (vertex_count % 3) != 0) ||
            (dashboard_fixture && vertex_count != 4) ||
            packet.surface_pitch == 0 || packet.surface_height == 0 ||
            packet.fixture_clip_width == 0 || packet.fixture_clip_height == 0) {
            return false;
        }
        const uint32_t pitch = packet.surface_pitch & 0xffffu;
        if (pitch == 0 || packet.fixture_clip_x + packet.fixture_clip_width > pitch / 4 ||
            packet.fixture_clip_y + packet.fixture_clip_height > packet.surface_height) {
            return false;
        }
        const size_t size = static_cast<size_t>(pitch) * packet.surface_height;
        if (packet.fixture_blend_enabled) {
            auto factor_supported = [](uint32_t factor) {
                return factor == 0 || factor == 1 || factor == 0x0302 ||
                       factor == 0x0303;
            };
            if (!factor_supported(packet.fixture_blend_src) ||
                !factor_supported(packet.fixture_blend_dst)) {
                return false;
            }
        }
        if (packet.fixture_depth_test_enabled || packet.fixture_depth_write_enabled) {
            const bool texture_depth_ok =
                texture_fixture && packet.fixture_depth_test_enabled &&
                packet.fixture_depth_func == 0x0203;
            const bool dashboard_depth_ok =
                dashboard_fixture && packet.fixture_depth_test_enabled &&
                !packet.fixture_depth_write_enabled &&
                packet.fixture_depth_func == 0x0202;
            if (!texture_depth_ok && !dashboard_depth_ok) {
                return false;
            }
            if (packet.depth_address == 0 || packet.depth_input.size() > size) {
                return false;
            }
        }
        if (texture_fixture) {
            const auto& texture = packet.textures[0];
            const bool supported_texture_extent =
                texture.width == 1280 &&
                (texture.height == 960 ||
                 (texture.height == 480 &&
                  packet.fixture_alpha_constant_modulate));
            if (!texture.referenced ||
                texture.layout != Nv2aGenericCombinerPacket::TextureLayout::kLinear ||
                texture.format != Nv2aGenericCombinerPacket::TextureFormat::kArgb8 ||
                !supported_texture_extent ||
                texture.pitch != 0x1400 || texture.mode !=
                    Nv2aGenericCombinerPacket::TextureMode::kProject2d) {
                return false;
            }
        }
        if (dashboard_fixture) {
            if (packet.surface_height != 480 || pitch != 0x1400 ||
                packet.fixture_clip_x != 0 || packet.fixture_clip_y != 0 ||
                packet.fixture_clip_width != 1280 ||
                packet.fixture_clip_height != 480 ||
                !packet.fixture_blend_enabled || packet.fixture_blend_src != 0x0302 ||
                packet.fixture_blend_dst != 1 ||
                !packet.textures[0].referenced ||
                packet.textures[0].layout !=
                    Nv2aGenericCombinerPacket::TextureLayout::kSwizzled ||
                packet.textures[0].format !=
                    Nv2aGenericCombinerPacket::TextureFormat::kArgb8 ||
                packet.textures[0].mode !=
                    Nv2aGenericCombinerPacket::TextureMode::kProject2d ||
                packet.textures[0].width != 1024 ||
                packet.textures[0].height != 512) {
                return false;
            }
            for (size_t stage = 1; stage < 4; ++stage) {
                const auto& texture = packet.textures[stage];
                if (!texture.referenced ||
                    texture.layout !=
                        Nv2aGenericCombinerPacket::TextureLayout::kSwizzled ||
                    texture.format !=
                        Nv2aGenericCombinerPacket::TextureFormat::kA8 ||
                    texture.mode !=
                        Nv2aGenericCombinerPacket::TextureMode::kProject2d ||
                    texture.width != 256 || texture.height != 256) {
                    return false;
                }
            }
        }
        return true;
    }
#if XLLE_VSH_GPU_RESIDENT
    VshGpuResidentStats vsh_gpu_resident_stats() const override {
        auto stats = vsh_resident_stats_;
        stats.specialized_compile_ns = vsh_compile_ns_.load();
        return stats;
    }
    void set_vsh_gpu_parallel_pack(bool on) override { vsh_parallel_pack_ = on; }
    void set_vsh_gpu_specialize(bool on) override { vsh_specialize_ = on; }
    void set_vsh_gpu_status_summary(bool on) override { vsh_status_summary_ = on; }
    void set_vsh_gpu_native_fp32(bool on) override { vsh_native_fp32_ = on; }
    bool prepare_vsh_gpu_batch(Nv2aGenericCombinerPacket::GpuVshReference& reference,
                               std::string& error) override {
        reference.prepared_token = 0;
        reference.prepared_vertices = 0;
        if (reference.mode != 3) { error = "GPU VSH preparation requires deferred mode"; return false; }
        if (!flush_witnessed_batch(error) || !take_witnessed_flush_error(error)) return false;
        perf_excl::Scope gl_region(perf_excl::kNv2aGl);
        SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
        if (!SDL_GL_MakeCurrent(window_, context_)) {
            error = std::string("GPU VSH batch context: ") + SDL_GetError();
            return false;
        }
        struct Restore {
            SDL_Window* previous_window;
            SDL_GLContext previous_context;
            SDL_Window* window;
            ~Restore() { restore_previous_gl_context(previous_window, previous_context, window,
                                                     perf::kGlContextOtherRestore); }
        } restore{previous_window, previous_context, window_};
        bool accepted = false;
        return prepare_vsh_resident(reference, nullptr, accepted, error);
    }
    bool prepare_vsh_gpu_resident(std::string& error) override {
        SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
        if (!SDL_GL_MakeCurrent(window_, context_)) {
            error = std::string("GPU resident VSH context: ") + SDL_GetError();
            return false;
        }
        const bool ok = ensure_vsh_gpu_programs(error);
        restore_previous_gl_context(previous_window, previous_context, window_,
                                    perf::kGlContextOtherRestore);
        return ok;
    }
#endif
    bool submit_resident_generic_combiner(const Nv2aGenericCombinerPacket& packet,
                                          Nv2aGenericCombinerResult& result,
                                          std::string& error) override {
        if (!flush_witnessed_batch(error)) return false;
        if (!take_witnessed_flush_error(error)) return false;
        // NV2A_GL (beads-xbox.1.14.3): host GL driver wall time for one resident submit --
        // context switch, surface residency, staging, uploads, uniforms and the draw. This
        // is CPU-side API time, not a GPU timer query; the distinction is the audit's
        // (docs/PERF-AUDIT-2026-09-02.md §6, "zero timer queries/fences").
        perf_excl::Scope gl_region(perf_excl::kNv2aGl);
        SDL_Window* previous_window = nullptr;
        SDL_GLContext previous_context = nullptr;
        {
            perf::ScopedAdd timer(perf::g_ns_renderer_resident_context,
                                  perf::g_calls_renderer_resident_context);
            previous_window = SDL_GL_GetCurrentWindow();
            previous_context = SDL_GL_GetCurrentContext();
            bool make_current_ok;
            {
                perf::ScopedGlContextTimer gl_timer(perf::kGlContextResidentSubmitEnter);
                make_current_ok = SDL_GL_MakeCurrent(window_, context_);
            }
            if (!make_current_ok) {
                error = std::string("OpenGL resident context lost: ") +
                        SDL_GetError();
                return false;
            }
        }
        struct ContextRestore {
            SDL_Window* previous_window;
            SDL_GLContext previous_context;
            SDL_Window* fallback_window;
            size_t perf_site;
            ~ContextRestore() {
                perf::ScopedAdd timer(perf::g_ns_renderer_resident_context,
                                      perf::g_calls_renderer_resident_context);
                restore_previous_gl_context(previous_window, previous_context,
                                            fallback_window, perf_site);
            }
        } restore{previous_window, previous_context, window_,
                  perf::kGlContextResidentSubmitRestore};
        const uint32_t pitch = packet.surface_pitch & 0xffffu;
        const uint32_t width = pitch / 4;
        const uint32_t height = packet.surface_height;
        const size_t size = static_cast<size_t>(pitch) * height;
        const bool primary_fixture =
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kResidentPrimaryColorTriangles;
        const bool constant_fixture =
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kResidentConstantColorTriangles;
#if XLLE_VSH_GPU_RESIDENT
        bool use_gpu_vsh_buffer = false;
        if (packet.gpu_vsh && (primary_fixture || constant_fixture)) {
            if (packet.gpu_vsh->mode == 3) {
                if (!packet.vertices.empty() || !valid_vsh_preparation(*packet.gpu_vsh)) {
                    error = "GPU VSH preparation receipt is stale or belongs to another renderer";
                    return false;
                }
                use_gpu_vsh_buffer = true;
                prepared_vsh_reference_ = nullptr; // Single-use, including later fatal draw failures.
            } else if (!prepare_vsh_resident(*packet.gpu_vsh, &packet.vertices,
                                             use_gpu_vsh_buffer, error)) return false;
        }
#endif
        {
            perf::ScopedAdd timer(perf::g_ns_renderer_resident_ensure,
                                  perf::g_calls_renderer_resident_ensure);
            if (!ensure_resident_surface(packet, width, height, size, error)) {
                return false;
            }
            if (!active_resident_ ||
                !ensure_resident_depth_surface(packet, *active_resident_, width,
                                               height, size, error)) {
                return false;
            }
        }
        const bool texture_fixture =
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kResidentTextureModulateTriangles;
        const bool dashboard_fixture =
            packet.compute_fixture == Nv2aGenericCombinerPacket::ComputeFixture::
                                          kDashboardProject2dFan;
        struct GpuVertex {
            float position[4];
            float diffuse[4];
            float tex[4][4];
        };
        std::vector<GpuVertex> vertices;
#if XLLE_VSH_GPU_RESIDENT
        if (!use_gpu_vsh_buffer)
#endif
        {
            perf::ScopedAdd timer(perf::g_ns_renderer_resident_vertex_build,
                                  perf::g_calls_renderer_resident_vertex_build);
            vertices.reserve(dashboard_fixture ? 6u : packet.vertices.size());
            auto append_vertex = [&](const Nv2aGenericCombinerVertex& vertex) {
                GpuVertex out{};
                for (size_t i = 0; i < 4; ++i) {
                    out.position[i] = vertex.position[i];
                    out.diffuse[i] = vertex.diffuse[i];
                    for (size_t stage = 0; stage < 4; ++stage)
                        out.tex[stage][i] = vertex.texture[stage][i];
                }
                vertices.push_back(out);
            };
            if (dashboard_fixture) {
                append_vertex(packet.vertices[0]);
                append_vertex(packet.vertices[1]);
                append_vertex(packet.vertices[2]);
                append_vertex(packet.vertices[0]);
                append_vertex(packet.vertices[2]);
                append_vertex(packet.vertices[3]);
            } else {
                for (const auto& vertex : packet.vertices) append_vertex(vertex);
            }
        }
        size_t resident_vertex_count = vertices.size();
#if XLLE_VSH_GPU_RESIDENT
        if (use_gpu_vsh_buffer) resident_vertex_count = packet.gpu_vsh->mode == 3
            ? packet.gpu_vsh->prepared_vertices : packet.vertices.size();
#endif
        if (dashboard_fixture) {
            perf::ScopedAdd timer(perf::g_ns_renderer_texture_stage,
                                  perf::g_calls_renderer_texture_stage);
            for (size_t stage = 0; stage < 4; ++stage) {
                const auto& texture = packet.textures[stage];
                const size_t expected =
                    texture.format == Nv2aGenericCombinerPacket::TextureFormat::kA8
                        ? static_cast<size_t>(texture.width) * texture.height
                        : static_cast<size_t>(texture.width) * texture.height *
                              sizeof(uint32_t);
                if (texture.input.size() != expected) {
                    error = "OpenGL resident dashboard missing texture snapshot";
                    return false;
                }
                auto& cache = dashboard_texture_cache_[stage];
                if (!cache.buffer) gen_buffers_(1, &cache.buffer);
                const uint64_t hash =
                    fnv1a64(texture.input.data(), texture.input.size());
                bind_buffer_(kGlShaderStorageBuffer, cache.buffer);
                if (!cache.valid || cache.size != texture.input.size() ||
                    cache.hash != hash) {
                    gpu_timed_buffer_data(
                        kGlShaderStorageBuffer,
                        static_cast<std::ptrdiff_t>(texture.input.size()),
                        texture.input.data(), kGlDynamicCopy);
                    cache.valid = true;
                    cache.size = texture.input.size();
                    cache.hash = hash;
                }
                bind_buffer_base_(kGlShaderStorageBuffer,
                                  static_cast<unsigned int>(stage),
                                  cache.buffer);
            }
        } else if (texture_fixture) {
            perf::ScopedAdd timer(perf::g_ns_renderer_texture_stage,
                                  perf::g_calls_renderer_texture_stage);
            const auto& texture = packet.textures[0];
            const size_t expected =
                static_cast<size_t>(texture.pitch) * texture.height;
            if (texture.input.size() != expected) {
                error = "OpenGL resident texture-modulate missing source texture snapshot";
                return false;
            }
            if (!resident_source_texture_) gen_textures_(1, &resident_source_texture_);
            cache_bind_texture_unit(kGlTexture0, resident_source_texture_);
            tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlRepeat);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlRepeat);
            std::vector<uint8_t>& rgba = upload_staging_;
            guest_words_to_rgba_unflipped(texture.input, rgba, texture.width,
                                          texture.height, false);
            pixel_store_i_(kGlUnpackAlignment, 1);
            gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlRgba8,
                                   static_cast<int>(texture.width),
                                   static_cast<int>(texture.height), 0, kGlRgba,
                                   kGlUnsignedByte, rgba.data());
        }
        {
            perf::ScopedAdd timer(perf::g_ns_renderer_resident_state_setup,
                                  perf::g_calls_renderer_resident_state_setup);
            cache_bind_framebuffer(resident_fbo_);
            cache_viewport(0, 0, static_cast<int>(width),
                           static_cast<int>(height));
            const bool native_depth_test = packet.fixture_depth_test_enabled &&
                                           !dashboard_fixture;
            cache_depth_test(native_depth_test,
                             native_depth_test
                                 ? gl_depth_func(packet.fixture_depth_func)
                                 : kGlAlways);
            cache_depth_mask(packet.fixture_depth_write_enabled);
            // Scissor is always enabled for resident draws (the software
            // fixture clips to the same rect).
            cache_scissor(true,
                          static_cast<int>(packet.fixture_clip_x),
                          static_cast<int>(height - packet.fixture_clip_y -
                                           packet.fixture_clip_height),
                          static_cast<int>(packet.fixture_clip_width),
                          static_cast<int>(packet.fixture_clip_height));
            cache_blend(packet.fixture_blend_enabled,
                        gl_blend_factor(packet.fixture_blend_src),
                        gl_blend_factor(packet.fixture_blend_dst));
            cache_color_mask(1, 1, 1, packet.fixture_color_format_x8 ? 0 : 1);
        }
        if (!resident_vao_) gen_vertex_arrays_(1, &resident_vao_);
        if (!resident_vbo_) gen_buffers_(1, &resident_vbo_);
        unsigned int selected_vbo = resident_vbo_;
#if XLLE_VSH_GPU_RESIDENT
        if (use_gpu_vsh_buffer) selected_vbo = vsh_buffers_[6];
#endif
        cache_bind_vao_vbo(resident_vao_, selected_vbo);
        {
            perf::ScopedAdd timer(perf::g_ns_renderer_vertex_upload,
                                  perf::g_calls_renderer_vertex_upload);
#if XLLE_VSH_GPU_RESIDENT
            if (!use_gpu_vsh_buffer)
#endif
            gpu_timed_buffer_data(kGlArrayBuffer,
                                  static_cast<std::ptrdiff_t>(
                                      vertices.size() * sizeof(GpuVertex)),
                                  vertices.data(), kGlDynamicDraw);
        }
        const unsigned int program = dashboard_fixture
            ? resident_dashboard_program_
            : (texture_fixture ? resident_texture_modulate_program_ : resident_program_);
        {
            perf::ScopedAdd timer(perf::g_ns_renderer_resident_uniforms,
                                  perf::g_calls_renderer_resident_uniforms);
            use_program_(program);
            uniform_2f_(cached_uniform_location(program, "surface_size"),
                        static_cast<float>(width), static_cast<float>(height));
            if (primary_fixture || constant_fixture) {
                uniform_1ui_(cached_uniform_location(program, "primary_mode"),
                             constant_fixture ? 1u : 0u);
                uniform_1ui_(cached_uniform_location(program, "constant_argb"),
                             packet.fixture_constant_argb);
            }
            if (texture_fixture) {
                uniform_1i_(cached_uniform_location(program, "texture0"), 0);
                uniform_2f_(cached_uniform_location(program, "texture_size"),
                            static_cast<float>(packet.textures[0].width),
                            static_cast<float>(packet.textures[0].height));
                uniform_1ui_(cached_uniform_location(program, "texture_passthrough"),
                             packet.fixture_texture_passthrough ? 1u : 0u);
                const uint32_t address_word = packet.textures[0].address_mode;
                const bool border_u =
                    (address_word & 0xFFu) == 4u;  // NV097 wrap code 4: BORDER
                const bool border_v = ((address_word >> 8) & 0xFFu) == 4u;
                const uint32_t border_word = packet.textures[0].border_color;
                uniform_1ui_(cached_uniform_location(program, "address_border"),
                             (border_u && border_v) ? 1u : 0u);
                uniform_4f_(
                    cached_uniform_location(program, "border_color"),
                    static_cast<float>((border_word >> 16) & 0xFFu) / 255.0f,
                    static_cast<float>((border_word >> 8) & 0xFFu) / 255.0f,
                    static_cast<float>(border_word & 0xFFu) / 255.0f,
                    static_cast<float>((border_word >> 24) & 0xFFu) / 255.0f);
                uniform_1ui_(
                    cached_uniform_location(program, "alpha_constant_modulate"),
                    packet.fixture_alpha_constant_modulate ? 1u : 0u);
                const uint32_t constant =
                    packet.fixture_alpha_constant_modulate
                        ? packet.fixture_constant_argb
                        : 0u;
                const float alpha =
                    static_cast<float>((constant >> 24) & 0xFFu) / 255.0f;
                uniform_1f_(cached_uniform_location(program, "alpha_constant"),
                            alpha);
                uniform_1ui_(cached_uniform_location(program, "alpha_kill_zero"),
                             packet.fixture_alpha_kill_zero ? 1u : 0u);
            } else if (dashboard_fixture) {
                if (packet.fixture_depth_test_enabled) {
                    if (!active_depth_resident_ ||
                        !active_depth_resident_->depth_word_texture) {
                        error = "OpenGL resident dashboard missing zeta-word texture";
                        return false;
                    }
                    cache_bind_texture_unit(
                        kGlTexture0 + 1,
                        active_depth_resident_->depth_word_texture);
                    uniform_1i_(cached_uniform_location(program, "depth_words"), 1);
                    uniform_1ui_(cached_uniform_location(program, "use_depth_equal"), 1u);
                } else {
                    uniform_1ui_(cached_uniform_location(program, "use_depth_equal"), 0u);
                }
                uniform_1ui_(cached_uniform_location(program, "factor_stage2"),
                             packet.fixture_factor0_stage2_argb);
                uniform_1ui_(cached_uniform_location(program, "factor_stage4"),
                             packet.fixture_factor0_stage4_argb);
            }
        }
        {
            perf::ScopedAdd timer(perf::g_ns_renderer_resident_attribs,
                                  perf::g_calls_renderer_resident_attribs);
            vertex_attrib_pointer_(
                0, 4, kGlFloat, 0, sizeof(GpuVertex),
                reinterpret_cast<const void*>(offsetof(GpuVertex, position)));
            vertex_attrib_pointer_(
                1, 4, kGlFloat, 0, sizeof(GpuVertex),
                reinterpret_cast<const void*>(offsetof(GpuVertex, diffuse)));
            if (texture_fixture || dashboard_fixture) {
                vertex_attrib_pointer_(
                    2, 4, kGlFloat, 0, sizeof(GpuVertex),
                    reinterpret_cast<const void*>(offsetof(GpuVertex, tex[0])));
            }
            if (dashboard_fixture) {
                vertex_attrib_pointer_(
                    3, 4, kGlFloat, 0, sizeof(GpuVertex),
                    reinterpret_cast<const void*>(offsetof(GpuVertex, tex[1])));
                vertex_attrib_pointer_(
                    4, 4, kGlFloat, 0, sizeof(GpuVertex),
                    reinterpret_cast<const void*>(offsetof(GpuVertex, tex[2])));
                vertex_attrib_pointer_(
                    5, 4, kGlFloat, 0, sizeof(GpuVertex),
                    reinterpret_cast<const void*>(offsetof(GpuVertex, tex[3])));
            }
            enable_vertex_attrib_array_(0);
            enable_vertex_attrib_array_(1);
            if (texture_fixture || dashboard_fixture) enable_vertex_attrib_array_(2);
            if (dashboard_fixture) {
                enable_vertex_attrib_array_(3);
                enable_vertex_attrib_array_(4);
                enable_vertex_attrib_array_(5);
            }
        }
        {
            perf::ScopedAdd timer(perf::g_ns_renderer_draw,
                                  perf::g_calls_renderer_draw);
            gpu_timed_draw_arrays(kGlTriangles, 0,
                                  static_cast<int>(resident_vertex_count));
            if (packet.fixture_color_format_x8 &&
                !packet.fixture_depth_test_enabled &&
                !packet.fixture_depth_write_enabled) {
                cache_blend(false, 0, 0);
                cache_color_mask(0, 0, 0, 1);
                use_program_(resident_alpha_program_);
                uniform_2f_(cached_uniform_location(resident_alpha_program_,
                                              "surface_size"),
                            static_cast<float>(width),
                            static_cast<float>(height));
                gpu_timed_draw_arrays(kGlTriangles, 0,
                                      static_cast<int>(resident_vertex_count));
                cache_color_mask(1, 1, 1, 0);
            }
        }
        {
            perf::ScopedAdd timer(perf::g_ns_renderer_resident_cleanup,
                                  perf::g_calls_renderer_resident_cleanup);
            if (dashboard_fixture) {
                disable_vertex_attrib_array_(5);
                disable_vertex_attrib_array_(4);
                disable_vertex_attrib_array_(3);
            }
            if (texture_fixture || dashboard_fixture) disable_vertex_attrib_array_(2);
            disable_vertex_attrib_array_(1);
            disable_vertex_attrib_array_(0);
            cache_color_mask(1, 1, 1, 1);
            cache_depth_mask(true);
            cache_depth_test(false, kGlAlways);
            cache_blend(false, 0, 0);
            cache_scissor(false, 0, 0, 0, 0);
        }
        bool gl_error = false;
        {
            perf::ScopedAdd timer(perf::g_ns_renderer_resident_error_check,
                                  perf::g_calls_renderer_resident_error_check);
            gl_error = get_error_() != kGlNoError;
        }
        if (gl_error) {
            if (dashboard_fixture) {
                error = "OpenGL resident dashboard draw failed";
            } else {
                error = texture_fixture ? "OpenGL resident texture-modulate draw failed"
                                        : "OpenGL resident primary draw failed";
            }
            return false;
        }
#if XLLE_VSH_GPU_RESIDENT
        if (use_gpu_vsh_buffer) ++vsh_resident_stats_.gpu_draws;
#endif
        if (active_resident_) {
            active_resident_->dirty = true;
            perf::record_renderer_dirty_area(
                static_cast<uint64_t>(packet.fixture_clip_width) *
                    packet.fixture_clip_height,
                static_cast<uint64_t>(active_resident_->width) *
                    active_resident_->height);
            if (perf::g_enabled) {
                active_resident_->dirty_rect.add(
                    packet.fixture_clip_x, packet.fixture_clip_y,
                    packet.fixture_clip_width, packet.fixture_clip_height);
            }
            active_resident_ = nullptr;
        }
        if (active_depth_resident_) {
            if (packet.fixture_depth_write_enabled) {
                active_depth_resident_->depth_dirty = true;
                if (perf::g_enabled) {
                    active_depth_resident_->depth_dirty_rect.add(
                        packet.fixture_clip_x, packet.fixture_clip_y,
                        packet.fixture_clip_width, packet.fixture_clip_height);
                }
            }
            active_depth_resident_ = nullptr;
        }
        result.color_output.clear();
        result.depth_output.clear();
        result.triangles = resident_vertex_count / 3;
        result.fragments = 0;
        result.writes = 0;
        return true;
    }
    // ======= stage (f3): the GENERATED generic-combiner executor ==========
    // beads-xbox.1.15.10 phase 6. Sits on the SAME ResidentSurface cache the
    // hand-written resident fixtures and the witnessed executor use, keyed on
    // the same (address, size, raw pitch word, height, raw format word) tuple,
    // so a plane made resident by any of the three is found by the other two
    // and the existing resolve/invalidate seam keeps all of them coherent.
    //
    // The DRAW is phase-1 shaped, not fixture shaped: six vertices per triangle
    // record out of an SSBO, coverage and depth recomputed in the fragment
    // program from int64 edge functions at the software rasterizer's own
    // 1/16-pixel sample points. Blending, the colour mask and the depth test
    // are GL fixed function, exactly as the resident fixtures on this same
    // plane already use them.
    struct GeneratedTriRecord {
        int64_t ab[4];          // 0   ax, ay, bx, by
        int64_t cp[4];          // 32  cx, cy, packed scanline box, wide flag
        float   zi[4];          // 64  za, zb, zc, inverse_area (float)
        float   wv[4];          // 80  1/area, 1/w0, 1/w1, 1/w2 (float)
        float   misc[4];        // 96  depth bias, const-depth flag, its value
        float   v[18][4];       // 112 vertex k slot s at v[k*6+s]
        float   pad[4];         // 400 (keeps `recip` on its 32-byte boundary)
        alignas(32) double recip[4];  // 416 1/area, 1/w0, 1/w1, 1/w2 (double)
    };
    static_assert(sizeof(GeneratedTriRecord) == 448,
                  "generated triangle record must match the std430 layout");

    struct GeneratedProgram {
        unsigned int program = 0;
        bool enhanced = false;
        int loc_xemu_consts = -1, loc_xemu_clip_range = -1;
        int loc_xemu_clip_region = -1, loc_xemu_surface_scale = -1;
        int loc_xemu_tex_scale[4] = {-1, -1, -1, -1};
        int loc_surface_size = -1;
        int loc_clip_min = -1;
        int loc_clip_max = -1;
        int loc_alpha_ref = -1;
        std::array<int, 8> loc_stage_const0 =
            {{-1, -1, -1, -1, -1, -1, -1, -1}};
        std::array<int, 8> loc_stage_const1 =
            {{-1, -1, -1, -1, -1, -1, -1, -1}};
        int loc_final_const0 = -1;
        int loc_final_const1 = -1;
        int loc_texture[4] = {-1, -1, -1, -1};
        int loc_tex_size[4] = {-1, -1, -1, -1};
        int loc_tex_border[4] = {-1, -1, -1, -1};
        bool failed = false;
    };

    // Program cache keyed on the generator's own fingerprint, which is an FNV
    // over exactly the state the SOURCE is a function of. A linear scan is the
    // right shape here: the canonical run compiles a single-digit number of
    // programs and the lookup is once per submitted batch, not per triangle.
    GeneratedProgram* find_generated_program(uint64_t fingerprint) {
        for (auto& entry : generated_programs_)
            if (entry.first == fingerprint) return &entry.second;
        return nullptr;
    }

    GeneratedProgram* ensure_generated_program(
        const Nv2aGeneratedShaderPacket& packet) {
        const bool enhanced = enhanced_xemu_ &&
            nv2a_xemu_psh_supported(packet) == XemuPshReject::kNone;
        const uint64_t key = enhanced ? nv2a_xemu_psh_key(packet) : packet.fingerprint;
        if (GeneratedProgram* existing =
                find_generated_program(key)) {
            ++generated_stats_.cache_hits;
            return existing->failed ? nullptr : existing;
        }
        GeneratedProgram gp;
        gp.enhanced = enhanced;
        const std::string fragment =
            enhanced ? nv2a_xemu_fragment_glsl(packet) :
                       nv2a_generate_combiner_fragment_glsl(packet.state);
        std::string error;
        ++generated_stats_.compiles;
        const char* vertex_source = enhanced ? kNv2aXemuVertexSource : kNv2aGeneratedVertexSource;
#if XLLE_NATIVE_DRAW
        if (!packet.native_vertices.empty()) vertex_source = kNv2aNativeVertexSource;
#endif
        if (!compile_graphics_program(vertex_source,
                                      fragment.c_str(), "generated-combiner",
                                      gp.program, error)) {
            gp.failed = true;
            ++generated_stats_.compile_failures;
            std::fprintf(stderr,
                         "[nv2a-renderer] generated combiner program %016llx "
                         "failed: %s\n",
                         (unsigned long long)packet.fingerprint, error.c_str());
            generated_programs_.push_back({key, gp});
            return nullptr;
        }
        gp.loc_surface_size = cached_uniform_location(gp.program, "surface_size");
        gp.loc_clip_min = cached_uniform_location(gp.program, "clip_min");
        gp.loc_clip_max = cached_uniform_location(gp.program, "clip_max");
        gp.loc_alpha_ref = cached_uniform_location(gp.program, enhanced ? "alphaRef" : "alpha_ref");
        if (enhanced) {
            gp.loc_xemu_consts = cached_uniform_location(gp.program, "consts[0]");
            gp.loc_xemu_clip_range = cached_uniform_location(gp.program, "clipRange");
            gp.loc_xemu_clip_region = cached_uniform_location(gp.program, "clipRegion[0]");
            gp.loc_xemu_surface_scale = cached_uniform_location(gp.program, "surfaceScale");
        }
        for (int i = 0; i < 8; ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), "stage_const0[%d]", i);
            gp.loc_stage_const0[i] = cached_uniform_location(gp.program, name);
            std::snprintf(name, sizeof(name), "stage_const1[%d]", i);
            gp.loc_stage_const1[i] = cached_uniform_location(gp.program, name);
        }
        gp.loc_final_const0 =
            cached_uniform_location(gp.program, "final_const0");
        gp.loc_final_const1 =
            cached_uniform_location(gp.program, "final_const1");
        for (int i = 0; i < 4; ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), enhanced ? "texSamp%d" : "texture%d", i);
            gp.loc_texture[i] = cached_uniform_location(gp.program, name);
            if (enhanced) {
                std::snprintf(name, sizeof(name), "texScale[%d]", i);
                gp.loc_xemu_tex_scale[i] = cached_uniform_location(gp.program, name);
            }
            std::snprintf(name, sizeof(name), "tex_size%d", i);
            gp.loc_tex_size[i] = cached_uniform_location(gp.program, name);
            std::snprintf(name, sizeof(name), "tex_border%d", i);
            gp.loc_tex_border[i] = cached_uniform_location(gp.program, name);
        }
        ++generated_stats_.programs;
        generated_programs_.push_back({key, gp});
        return &generated_programs_.back().second;
    }

    bool supports_generated_shader(
        const Nv2aGeneratedShaderPacket& descriptor) override {
        if (!generated_available_) return false;
        if (descriptor.surface_width == 0 || descriptor.surface_height == 0)
            return false;
        if (descriptor.surface_size !=
            static_cast<size_t>(descriptor.surface_width) *
                descriptor.surface_height * 4u) {
            return false;
        }
        for (const auto& texture : descriptor.textures) {
            if (!texture.present) continue;
            if (texture.width == 0 || texture.height == 0) return false;
            if (texture.linear.size() !=
                static_cast<size_t>(texture.width) * texture.height * 4u) {
                return false;
            }
        }
        // ALIASING GUARD, the same one supports_witnessed_profile carries and
        // for the same reason. The resident cache is keyed on
        // (address, size, pitch, format), so the SAME guest memory rebound
        // with a different surface format or pitch would silently produce a
        // SECOND resident copy. Two GPU-authoritative copies of one range is
        // data loss: whichever resolves last wins and the other's draws
        // vanish. Refusing here sends the batch to the software path, whose
        // resolve/invalidate resolves the stale copy through the existing seam.
        {
            auto overlaps = [](uint64_t a0, uint64_t a1, uint64_t b0,
                               uint64_t b1) { return a0 < b1 && b0 < a1; };
            const uint64_t want0 = descriptor.color_address;
            const uint64_t want1 = want0 + descriptor.surface_size;
            // Phase 7: the ZETA range this batch will write, when it writes
            // one. The guard below is the same one in the other direction --
            // this plane must not alias a DIFFERENT resident colour or zeta
            // image, or two GPU-authoritative copies of one guest range exist
            // and whichever resolves last wins.
            const bool wants_depth = descriptor.state.uses_depth;
            const uint64_t wantd0 = descriptor.depth_address;
            const uint64_t wantd1 = wantd0 + descriptor.surface_size;
            for (const auto& surface : resident_surfaces_) {
                const bool is_ours =
                    surface.valid &&
                    surface.address == descriptor.color_address &&
                    surface.size == descriptor.surface_size &&
                    surface.pitch == descriptor.surface_pitch &&
                    surface.format == descriptor.surface_format;
                if (surface.valid && !is_ours) {
                    const uint64_t c0 = surface.address;
                    const uint64_t c1 = c0 + surface.size;
                    if (overlaps(want0, want1, c0, c1)) return false;
                    if (wants_depth && overlaps(wantd0, wantd1, c0, c1))
                        return false;
                }
                if (surface.depth_valid) {
                    const uint64_t d0 = surface.depth_address;
                    const uint64_t d1 = d0 + surface.depth_size;
                    if (overlaps(want0, want1, d0, d1)) return false;
                    const bool depth_is_ours =
                        surface.depth_address == descriptor.depth_address &&
                        surface.depth_size == descriptor.surface_size;
                    if (wants_depth && !depth_is_ours &&
                        overlaps(wantd0, wantd1, d0, d1)) {
                        return false;
                    }
                }
            }
        }
        // Same rule as the colour plane below: a zeta plane that is not
        // resident needs a snapshot to upload and a slot to land in, and
        // refusing HERE is what keeps "after supports_ returns true, failure
        // is fatal" honest.
        if (descriptor.state.uses_depth) {
            if (descriptor.depth_address == 0) return false;
            if (find_witnessed_depth_slot(descriptor.depth_address,
                                          descriptor.surface_size) == nullptr) {
                if (!descriptor.needs_depth_upload) return false;
                bool slot = false;
                for (const auto& surface : resident_surfaces_) {
                    if (!surface.depth_valid || !surface.depth_dirty) {
                        slot = true;
                        break;
                    }
                }
                if (!slot) return false;
            }
        }
        // A plane that is already resident always has somewhere to land; one
        // that is not needs a slot the cache is allowed to take, and the cache
        // never evicts a dirty target. Refusing HERE is what keeps the "after
        // supports_ returns true, failure is fatal" contract honest.
        if (find_resident_surface(descriptor.color_address,
                                  descriptor.surface_size,
                                  descriptor.surface_pitch,
                                  descriptor.surface_height,
                                  descriptor.surface_format) == nullptr) {
            if (!descriptor.needs_color_upload) return false;
            bool slot = false;
            for (const auto& surface : resident_surfaces_) {
                if ((!surface.valid && !surface.depth_dirty) ||
                    (!surface.dirty && !surface.depth_dirty)) {
                    slot = true;
                    break;
                }
            }
            if (!slot) return false;
        }
        // Compiling here rather than in submit() is deliberate: submit() may
        // not fail once the device has committed to the GPU path, and a shader
        // compile is the one step that legitimately can. It happens once per
        // distinct combiner state.
        std::string error;
        if (!enter_witnessed_context(error)) return false;
        return ensure_generated_program(descriptor) != nullptr;
    }

    bool generated_shader_target_resident(uint32_t color_address, size_t size,
                                          uint32_t pitch, uint32_t height,
                                          uint32_t format) const override {
        return find_resident_surface(color_address, size, pitch, height,
                                     format) != nullptr;
    }

    // (address, size), the witnessed executor's own zeta key. See the
    // declaration in nv2a_renderer.hpp for why it is not the five-field one.
    bool generated_shader_depth_resident(uint32_t depth_address,
                                         size_t size) const override {
        return find_witnessed_depth_slot(depth_address, size) != nullptr;
    }

    GeneratedShaderStats generated_shader_stats() const override {
        return generated_stats_;
    }
    void set_generated_shader_nodraw(bool on) override {
        generated_nodraw_ = on;
    }
    void set_generated_shader_bgra_upload(bool on) override {
        generated_bgra_upload_ = on;
    }
    void set_generated_uniform_batching(bool on) override {
        generated_uniform_batching_ = on;
    }
    void set_gpu_timing_enabled(bool on) override {
        gpu_timers_enabled_ = on;
        g_gl_gpu.gpu_timing_enabled.store(on ? 1u : 0u,
                                         std::memory_order_relaxed);
    }
#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
    bool set_generated_submit_census(bool on) override {
        generated_submit_census_ = on;
        generated_submit_cpu_stats_.available = true;
        generated_submit_cpu_stats_.enabled = on && perf_excl::g_enabled;
        return true;
    }
    GeneratedSubmitCpuStats generated_submit_cpu_stats() const override {
        GeneratedSubmitCpuStats stats = generated_submit_cpu_stats_;
        stats.available = true;
        stats.enabled = generated_submit_census_ && perf_excl::g_enabled;
        return stats;
    }
#endif

    bool submit_generated_shader(const Nv2aGeneratedShaderPacket& packet,
                                 Nv2aGeneratedShaderResult& result,
                                 std::string& error) override {
        perf_excl::Scope gl_region(perf_excl::kNv2aGl);
#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
        generated_submit_cpu_stats_.available = true;
        generated_submit_cpu_stats_.enabled =
            generated_submit_census_ && perf_excl::g_enabled;
        Nv2aGeneratedSubmitPhaseRecorder generated_submit_perf(
            generated_submit_cpu_stats_,
            generated_submit_census_ && perf_excl::g_enabled);
        const auto generated_submit_phase =
            [&](Nv2aGeneratedSubmitPhaseRecorder::Phase phase) {
                generated_submit_perf.enter(phase);
            };
#endif
        result = {};
#if XLLE_NATIVE_DRAW
        const bool native = !packet.native_vertices.empty();
#else
        constexpr bool native = false;
#endif
        if (packet.triangles.empty() && !native) return true;
        // A pending witnessed batch may target this very plane, and GL executes
        // primitives in submission order only within one command stream; the
        // resident fixtures flush for the same reason.
        if (!flush_witnessed_batch(error)) return false;
        if (!take_witnessed_flush_error(error)) return false;
        if (!enter_witnessed_context(error)) return false;

#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
        generated_submit_phase(Nv2aGeneratedSubmitPhaseRecorder::kEnsure);
#endif
        GeneratedProgram* gp = ensure_generated_program(packet);
        if (!gp) {
            error = "OpenGL generated combiner program unavailable";
            return false;
        }
        ResidentSurface* surface = nullptr;
        if (!ensure_generated_surface(packet, surface, result, error))
            return false;
        // Phase 7 (beads-xbox.1.15.10): the zeta plane, on exactly the same
        // resident cache and the same (address, size) key the witnessed
        // executor uses, so a plane one of them made resident is found by the
        // other and the existing resolve/invalidate seam keeps both coherent.
        ResidentSurface* depth_surface = nullptr;
        if (packet.state.uses_depth &&
            !ensure_generated_depth_surface(packet, surface, depth_surface,
                                            result, error)) {
            return false;
        }

#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
        generated_submit_phase(Nv2aGeneratedSubmitPhaseRecorder::kTextures);
#endif
        // ---- textures --------------------------------------------------
        for (uint32_t i = 0; i < 4; ++i) {
            const Nv2aGeneratedTexture& texture = packet.textures[i];
            if (!texture.present) continue;
            GeneratedTextureSlot& slot = generated_textures_[i];
            if (!slot.texture) {
                gen_textures_(1, &slot.texture);
                if (!slot.texture) {
                    error = "OpenGL generated combiner texture allocation failed";
                    return false;
                }
                cache_bind_texture_unit(kGlTexture0 + i, slot.texture);
                tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
                tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
                tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
                tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
            } else {
                cache_bind_texture_unit(kGlTexture0 + i, slot.texture);
            }
            // The same owned texture slot can alternate reference/enhanced
            // draws. Rebind sampler behavior even when the pixel upload is reused.
            if (enhanced_xemu_) {
                const auto& ts = packet.state.textures[i];
                const uint32_t filter = packet.guest.texture_filter[i];
                tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter,
                    gp->enhanced && nv2a_xemu_min_filter(
                        packet.guest.texture_format[i], filter) == 2 ? kGlLinear : kGlNearest);
                tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter,
                    gp->enhanced && ((filter >> 24) & 15) == 2 ? kGlLinear : kGlNearest);
                tex_parameter_i_(kGlTexture2D, kGlTextureWrapS,
                    gp->enhanced && ts.address_u == Nv2aCombinerShaderState::AddressMode::kRepeat ? kGlRepeat : kGlClampToEdge);
                tex_parameter_i_(kGlTexture2D, kGlTextureWrapT,
                    gp->enhanced && ts.address_v == Nv2aCombinerShaderState::AddressMode::kRepeat ? kGlRepeat : kGlClampToEdge);
            }
            const bool same_content = texture.content_id
                ? slot.content_id == texture.content_id
                : slot.content_id == 0 && slot.hash == texture.hash;
            if (slot.valid && same_content &&
                slot.width == texture.width && slot.height == texture.height) {
                ++generated_stats_.texture_upload_skips;
                continue;
            }
            // PHASE 7 (upload staging). GL_BGRA, and NO staging buffer at all.
            // A guest A8R8G8B8 word is little-endian, so its bytes in memory
            // are already B,G,R,A -- which is exactly what
            // GL_BGRA/GL_UNSIGNED_BYTE means. The per-pixel shuffle that was
            // here reordered them to R,G,B,A only for the driver to put them
            // back, over 1 831.6 MB of texture per canonical 12 B run. The
            // device hands over a TIGHTLY PACKED image (it unswizzles and
            // expands A8 on the way in), so the packet's own bytes are the
            // upload. This is the same transform the witnessed executor's
            // upload path already uses.
            if (generated_bgra_upload_) {
                pixel_store_i_(kGlUnpackAlignment, 4);
                if (slot.alloc_w == texture.width &&
                    slot.alloc_h == texture.height) {
                    gpu_timed_tex_sub_image_2d(kGlTexture2D, 0, 0, 0,
                                               static_cast<int>(texture.width),
                                               static_cast<int>(texture.height),
                                               kGlBgra, kGlUnsignedByte,
                                               texture.linear.data());
                } else {
                    gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlRgba8,
                                           static_cast<int>(texture.width),
                                           static_cast<int>(texture.height), 0,
                                           kGlBgra, kGlUnsignedByte,
                                           texture.linear.data());
                    slot.alloc_w = texture.width;
                    slot.alloc_h = texture.height;
                }
            } else {
                // The comparison arm: the pre-phase-7 per-pixel shuffle.
                std::vector<uint8_t>& rgba = upload_staging_;
                guest_words_to_rgba_unflipped(texture.linear, rgba,
                                              texture.width, texture.height,
                                              false);
                pixel_store_i_(kGlUnpackAlignment, 1);
                gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlRgba8,
                                       static_cast<int>(texture.width),
                                       static_cast<int>(texture.height), 0,
                                       kGlRgba, kGlUnsignedByte, rgba.data());
                slot.alloc_w = texture.width;
                slot.alloc_h = texture.height;
            }
            slot.valid = true;
            slot.hash = texture.hash;
            slot.content_id = texture.content_id;
            slot.width = texture.width;
            slot.height = texture.height;
            ++generated_stats_.texture_uploads;
            result.texture_upload_bytes += texture.linear.size();
        }
        // Leave unit 0 active, which every other path in this file assumes.
        cache_bind_texture_unit(kGlTexture0,
                                generated_textures_[0].texture);

#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
        generated_submit_phase(Nv2aGeneratedSubmitPhaseRecorder::kRecords);
#endif
        // ---- triangle records ------------------------------------------
        size_t count = packet.triangles.size();
#if XLLE_NATIVE_DRAW
        if (native) {
            static_assert(sizeof(Nv2aGenericCombinerVertex) == 7 * 4 * sizeof(float));
            count = native_draw_triangle_count(packet.native_primitive, packet.native_vertices.size());
            generated_records_.resize(packet.native_vertices.size() * sizeof(Nv2aGenericCombinerVertex));
            std::memcpy(generated_records_.data(), packet.native_vertices.data(), generated_records_.size());
        } else
#endif
        {
        generated_records_.resize(sizeof(GeneratedTriRecord) * count);
        for (size_t i = 0; i < count; ++i) {
            const Nv2aGeneratedTriangle& t = packet.triangles[i];
            auto* rec = reinterpret_cast<GeneratedTriRecord*>(
                generated_records_.data() + sizeof(GeneratedTriRecord) * i);
            rec->ab[0] = t.ax; rec->ab[1] = t.ay;
            rec->ab[2] = t.bx; rec->ab[3] = t.by;
            rec->cp[0] = t.cx; rec->cp[1] = t.cy;
            rec->cp[2] = (static_cast<int64_t>(t.bbox_xmin) & 0xFFFF) |
                         ((static_cast<int64_t>(t.bbox_xmax) & 0xFFFF) << 16) |
                         ((static_cast<int64_t>(t.bbox_ymin) & 0xFFFF) << 32) |
                         ((static_cast<int64_t>(t.bbox_ymax) & 0xFFFF) << 48);
            rec->cp[3] = t.wide ? 1 : 0;
            rec->zi[0] = t.za; rec->zi[1] = t.zb; rec->zi[2] = t.zc;
            rec->zi[3] = t.inverse_area;
            rec->wv[0] = t.inv_area_recip;
            rec->wv[1] = t.inv_w[0];
            rec->wv[2] = t.inv_w[1];
            rec->wv[3] = t.inv_w[2];
            rec->misc[0] = t.depth_bias;
            rec->misc[1] = t.constant_depth ? 1.0f : 0.0f;
            rec->misc[2] = t.constant_depth_value;
            rec->misc[3] = 0.0f;
            for (int v = 0; v < 3; ++v) {
                for (int slot = 0; slot < 6; ++slot) {
                    for (int c = 0; c < 4; ++c)
                        rec->v[v * 6 + slot][c] = t.varying[v][slot][c];
                }
            }
            rec->pad[0] = rec->pad[1] = rec->pad[2] = rec->pad[3] = 0.0f;
            rec->recip[0] = t.inv_area_double;
            rec->recip[1] = t.inv_w_double[0];
            rec->recip[2] = t.inv_w_double[1];
            rec->recip[3] = t.inv_w_double[2];
        }
        }
#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
        generated_submit_perf.add_record_bytes(generated_records_.size());
        generated_submit_phase(Nv2aGeneratedSubmitPhaseRecorder::kBuffer);
#endif
        if (!generated_ssbo_) gen_buffers_(1, &generated_ssbo_);
        bind_buffer_(kGlShaderStorageBuffer, generated_ssbo_);
        gpu_timed_buffer_data(
            kGlShaderStorageBuffer,
            static_cast<std::ptrdiff_t>(generated_records_.size()),
            generated_records_.data(), kGlStreamDraw);
        bind_buffer_base_(kGlShaderStorageBuffer, 0, generated_ssbo_);

#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
        generated_submit_phase(Nv2aGeneratedSubmitPhaseRecorder::kUniforms);
#endif
        // ---- program and uniforms ---------------------------------------
        use_program_(gp->program);
        witnessed_bound_program_ = gp->program;
        uniform_2f_(gp->loc_surface_size,
                    static_cast<float>(packet.surface_width),
                    static_cast<float>(packet.surface_height));
        uniform_1f_(gp->loc_clip_min, packet.clip_min);
        uniform_1f_(gp->loc_clip_max, packet.clip_max);
        if (gp->enhanced) uniform_1i_(gp->loc_alpha_ref, packet.alpha_ref & 0xFFu);
        else uniform_1f_(gp->loc_alpha_ref, static_cast<float>(packet.alpha_ref & 0xFFu));
        const auto unpack = [](uint32_t word, float* out) {
            out[0] = static_cast<float>((word >> 16) & 0xFFu) / 255.0f;
            out[1] = static_cast<float>((word >> 8) & 0xFFu) / 255.0f;
            out[2] = static_cast<float>(word & 0xFFu) / 255.0f;
            out[3] = static_cast<float>((word >> 24) & 0xFFu) / 255.0f;
        };
        float c[4];
        if (gp->enhanced) {
            float constants[18 * 4];
            for (size_t i = 0; i < 8; ++i) {
                unpack(packet.stage_const0[i], constants + i * 8);
                unpack(packet.stage_const1[i], constants + i * 8 + 4);
            }
            unpack(packet.final_factor0, constants + 64);
            unpack(packet.final_factor1, constants + 68);
            uniform_4fv_(gp->loc_xemu_consts, 18, constants);
            uniform_4f_(gp->loc_xemu_clip_range, 0, 16777215.0f, packet.clip_min, packet.clip_max);
            uniform_4i_(gp->loc_xemu_clip_region, packet.xmin,
                packet.surface_height - 1u - packet.ymax,
                packet.xmax + 1u, packet.surface_height - packet.ymin);
            // Remaining clip rectangles stay empty at their GL initial values.
            uniform_2i_(gp->loc_xemu_surface_scale, 1, 1);
            for (size_t i = 0; i < 4; ++i) uniform_1f_(gp->loc_xemu_tex_scale[i], 1.0f);
        }
        const auto upload_stage_uniforms_scalar = [&](
            const std::array<int, 8>& locations,
            const std::array<uint32_t, 8>& words) {
            for (int i = 0; i < 8; ++i) {
                if (locations[i] < 0) continue;
                unpack(words[i], c);
                uniform_4f_(locations[i], c[0], c[1], c[2], c[3]);
                ++generated_stats_.stage_uniform_scalar_calls;
                ++generated_stats_.stage_uniform_vectors;
            }
        };
        const auto upload_stage_uniforms_bulk = [&](
            const std::array<int, 8>& locations,
            const std::array<uint32_t, 8>& words) -> bool {
            if (!uniform_4fv_ || locations[0] < 0) return false;
            float values[32];
            for (int i = 0; i < 8; ++i) unpack(words[i], values + i * 4);
            uniform_4fv_(locations[0], 8, values);
            ++generated_stats_.stage_uniform_bulk_calls;
            generated_stats_.stage_uniform_vectors += 8;
            return true;
        };
        // Credit to sp00nznet's xboxrecomp for motivating this narrow A/B: the
        // local renderer keeps xlle's generated shader semantics and only
        // changes how already-decoded stage constants enter GL. Scalar uses
        // per-element queried locations so optimized-out array elements cannot
        // alias unrelated uniforms. Bulk starts from element zero; per GL,
        // values beyond a linked array's active length are ignored.
        if (!generated_uniform_batching_ ||
            !upload_stage_uniforms_bulk(gp->loc_stage_const0,
                                        packet.stage_const0)) {
            upload_stage_uniforms_scalar(gp->loc_stage_const0,
                                         packet.stage_const0);
        }
        if (!generated_uniform_batching_ ||
            !upload_stage_uniforms_bulk(gp->loc_stage_const1,
                                        packet.stage_const1)) {
            upload_stage_uniforms_scalar(gp->loc_stage_const1,
                                         packet.stage_const1);
        }
        if (gp->loc_final_const0 >= 0) {
            unpack(packet.final_factor0, c);
            uniform_4f_(gp->loc_final_const0, c[0], c[1], c[2], c[3]);
        }
        if (gp->loc_final_const1 >= 0) {
            unpack(packet.final_factor1, c);
            uniform_4f_(gp->loc_final_const1, c[0], c[1], c[2], c[3]);
        }
        for (int i = 0; i < 4; ++i) {
            if (!packet.textures[i].present) continue;
            if (gp->loc_texture[i] >= 0) uniform_1i_(gp->loc_texture[i], i);
            if (gp->loc_tex_size[i] >= 0) {
                uniform_2f_(gp->loc_tex_size[i],
                            static_cast<float>(packet.textures[i].width),
                            static_cast<float>(packet.textures[i].height));
            }
            if (gp->loc_tex_border[i] >= 0) {
                unpack(packet.textures[i].border_argb, c);
                uniform_4f_(gp->loc_tex_border[i], c[0], c[1], c[2], c[3]);
            }
        }

#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
        generated_submit_phase(Nv2aGeneratedSubmitPhaseRecorder::kState);
#endif
        // ---- fixed-function state ---------------------------------------
        cache_bind_framebuffer(resident_fbo_);
        cache_framebuffer_color_texture(resident_fbo_, surface->texture);
        cache_framebuffer_depth_texture(
            resident_fbo_, depth_surface ? depth_surface->depth_texture : 0u);
        cache_viewport(0, 0, static_cast<int>(packet.surface_width),
                       static_cast<int>(packet.surface_height));
        // Guest scissor rows are top-down; the resident image is stored
        // bottom-up, exactly as guest_words_to_rgba stores it.
        cache_scissor(true, static_cast<int>(packet.xmin),
                      static_cast<int>(packet.surface_height - 1u -
                                       packet.ymax),
                      static_cast<int>(packet.xmax - packet.xmin + 1u),
                      static_cast<int>(packet.ymax - packet.ymin + 1u));
        if (depth_surface) {
            // The shader writes gl_FragDepth from the software path's own
            // expression, so the driver's interpolation never decides the
            // value; the ROP does the COMPARISON and the store. Writing
            // gl_FragDepth forces late-Z, and that is exactly why a
            // depth-using batch does NOT take the image-blend path: an
            // imageStore happens inside the fragment shader, i.e. BEFORE a
            // late depth test, so a fragment that loses the depth test would
            // already have written its colour. With the ROP owning both, the
            // colour write is ordered after the depth test the way the
            // software path's `continue` orders it. The device forces
            // state.image_blend false whenever uses_depth is set for this
            // reason; see nv2a.cpp.
            cache_depth_test(packet.depth_test,
                             packet.depth_test ? gl_depth_func(packet.depth_func)
                                               : kGlAlways);
            // Stencil is never touched: GL_STENCIL_TEST is off for the whole
            // executor, so the packed word's low byte survives, which is what
            // the software path's `(incoming << 8) | (stored & 0xFF)` does.
            cache_depth_mask(packet.depth_write);
            ++generated_stats_.depth_draws;
        } else {
            cache_depth_test(false, kGlAlways);
            cache_depth_mask(false);
        }
        if (packet.state.image_blend && !gp->enhanced) {
            // The blend, the colour mask and the X8 alpha rule are all inside
            // the fragment program now, written against an rgba8 image binding
            // under a fragment-shader interlock. The ROP must therefore write
            // NOTHING: blending off, every channel masked. Measured reason:
            // GL fixed-function blending quantises its inputs to the
            // framebuffer's 8 bits and the software path does not, which the
            // differential verifier reported as exactly 1 LSB over ~90 pct of
            // the plane on every batch, compounding to 38 LSB over the
            // dashboard's ~500 blended layers.
            bind_image_texture_(1, surface->texture, 0, 0, 0, kGlReadWrite,
                                kGlRgba8);
            cache_blend(false, 0, 0);
            cache_color_mask(0, 0, 0, 0);
        } else if (packet.state.blend) {
            if (packet.state.color_format_x8) {
                // X8R8G8B8: the written alpha byte is 0xFF (fragment output
                // index 0) while the BLEND FACTOR is the combiner's own alpha
                // (index 1). SRC_ALPHA would read the written 1.0 and simply
                // replace the destination, so the factors move to their
                // SRC1 forms and the alpha channel takes ONE/ZERO.
                cache_blend(true, kGlOne, kGlZero);
                blend_func_separate_(
                    gl_blend_factor_src1(packet.blend_src),
                    gl_blend_factor_src1(packet.blend_dst), kGlOne, kGlZero);
                // The mirror no longer describes the driver state; force the
                // next cache_blend to re-issue both factors.
                if (gl_state_.valid) gl_state_.blend_src = 0xFFFFFFFFu;
            } else {
                cache_blend(true, gl_blend_factor(packet.blend_src),
                            gl_blend_factor(packet.blend_dst));
            }
            cache_color_mask(packet.color_mask[0] ? 1 : 0,
                             packet.color_mask[1] ? 1 : 0,
                             packet.color_mask[2] ? 1 : 0,
                             packet.color_mask[3] ? 1 : 0);
        } else {
            cache_blend(false, 0, 0);
            cache_color_mask(packet.color_mask[0] ? 1 : 0,
                             packet.color_mask[1] ? 1 : 0,
                             packet.color_mask[2] ? 1 : 0,
                             packet.color_mask[3] ? 1 : 0);
        }
        if (!witnessed_fixed_state_) {
            disable_(kGlCullFace);
            disable_(kGlStencilTest);
            witnessed_fixed_state_ = true;
        }
        if (!generated_vao_) gen_vertex_arrays_(1, &generated_vao_);
        cache_bind_vao_vbo(generated_vao_, 0);
#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
        generated_submit_phase(Nv2aGeneratedSubmitPhaseRecorder::kDraw);
#endif
        // DIAGNOSTIC, KNOWN INCORRECT WHEN ON. Everything but the draw itself:
        // the residency, the uploads and the state all happen, so the ONLY
        // thing missing from the frame is this executor's own output. It is
        // the control that separates "my draws are nearly right" from "my
        // draws contribute almost nothing", which no amount of reading the
        // shader can settle.
#if XLLE_NATIVE_DRAW
        if (native) {
            // Guest front faces have positive screen-space signed area. The
            // Y flip in the vertex shader makes these clockwise in GL.
            if (packet.native_cull) {
                enable_(kGlCullFace);
                native_front_face_(0x0900); // GL_CW
                native_cull_face_(packet.native_cull_front ? 0x0404 : 0x0405);
            }
            const unsigned mode = packet.native_primitive == 5 ? kGlTriangles :
                                  packet.native_primitive == 6 ? 0x0005u : 0x0006u;
            if (!generated_nodraw_) {
                gpu_timed_draw_arrays(mode, 0, static_cast<int>(packet.native_vertices.size()));
                ++native_draws_;
                native_vertices_ += packet.native_vertices.size();
                native_triangles_ += count;
            }
            disable_(kGlCullFace);
            native_front_face_(0x0901); // restore GL_CCW for the other executors
        } else
#endif
        if (!generated_nodraw_)
            gpu_timed_draw_arrays(kGlTriangles, 0, static_cast<int>(count * (gp->enhanced ? 3u : 6u)));
#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
        generated_submit_phase(Nv2aGeneratedSubmitPhaseRecorder::kBarrier);
#endif
        if (packet.state.image_blend && !gp->enhanced) {
            // The image stores must be visible to the NEXT generated draw (a
            // second interlocked read-modify-write of the same pixels), to the
            // resident-surface readback that a resolve performs through the
            // framebuffer, and to any texture fetch of this plane. The
            // interlock orders fragments WITHIN a draw; only a memory barrier
            // orders them against everything else.
            memory_barrier_(kGlShaderImageAccessBarrierBit |
                            kGlFramebufferBarrierBit |
                            kGlTextureFetchBarrierBit);
        }
        if (flush_ && witnessed_kick_flush_) {
            flush_();
            ++witnessed_gl_flushes_;
        }
#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
        generated_submit_phase(Nv2aGeneratedSubmitPhaseRecorder::kCleanup);
#endif
        cache_color_mask(1, 1, 1, 1);
        cache_blend(false, 0, 0);
        cache_scissor(false, 0, 0, 0, 0);
        cache_depth_mask(true);

#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
        generated_submit_phase(Nv2aGeneratedSubmitPhaseRecorder::kError);
#endif
        const unsigned int gl_error = get_error_();
        if (gl_error != kGlNoError) {
            char why[192];
            std::snprintf(why, sizeof(why),
                          "OpenGL generated-combiner draw failed (GL 0x%04x, "
                          "%zu triangles, program %016llx)",
                          gl_error, count,
                          (unsigned long long)packet.fingerprint);
            error = why;
            return false;
        }
#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
        generated_submit_phase(Nv2aGeneratedSubmitPhaseRecorder::kFinish);
#endif
        surface->dirty = true;
        surface->dirty_rect.add(packet.xmin, packet.ymin,
                                packet.xmax - packet.xmin + 1u,
                                packet.ymax - packet.ymin + 1u);
        if (depth_surface && packet.depth_write) {
            depth_surface->depth_dirty = true;
            depth_surface->depth_dirty_rect.add(packet.xmin, packet.ymin,
                                                packet.xmax - packet.xmin + 1u,
                                                packet.ymax - packet.ymin + 1u);
        }
        ++generated_stats_.draws;
        if (gp->enhanced && !generated_nodraw_) {
            ++enhanced_draws_;
            enhanced_triangles_ += count;
        }
        generated_stats_.triangles += count;
        result.triangles = count;
        result.draw_calls = 1;
        return true;
    }

    // The zeta counterpart of ensure_generated_surface. It deliberately mirrors
    // ensure_witnessed_surface's depth half -- the same (address, size) key, the
    // same slot-acquisition rule, the same row-copy upload and the same
    // upload-cause census -- because plane 0x033d0000 is shared with the
    // witnessed executor and the two must agree on what "resident" means.
    bool ensure_generated_depth_surface(const Nv2aGeneratedShaderPacket& packet,
                                        ResidentSurface* color_slot,
                                        ResidentSurface*& depth_slot,
                                        Nv2aGeneratedShaderResult& result,
                                        std::string& error) {
        depth_slot = find_witnessed_depth_slot(packet.depth_address,
                                               packet.surface_size);
        if (depth_slot) {
            if (!resident_fbo_) gen_framebuffers_(1, &resident_fbo_);
            active_depth_resident_ = depth_slot;
            return true;
        }
        if (packet.depth_input.size() != packet.surface_size) {
            error = "OpenGL generated combiner missing initial zeta snapshot";
            return false;
        }
        // The colour slot is the preferred home; it is refused only when that
        // would overwrite a DIRTY zeta plane of a different guest range, which
        // would discard GPU depth writes no resolve has moved back.
        if (color_slot &&
            (!color_slot->depth_valid || !color_slot->depth_dirty ||
             (color_slot->depth_address == packet.depth_address &&
              color_slot->depth_size == packet.surface_size))) {
            depth_slot = note_depth_slot_reuse(*color_slot);
        } else {
            depth_slot = acquire_depth_slot(color_slot);
        }
        if (!depth_slot) {
            error = "OpenGL resident surface cache is full of dirty targets";
            return false;
        }
        const uint32_t width = packet.surface_width;
        const uint32_t height = packet.surface_height;
        if (!depth_slot->depth_texture) {
            gen_textures_(1, &depth_slot->depth_texture);
            if (!depth_slot->depth_texture) {
                error = "OpenGL generated combiner zeta texture allocation failed";
                return false;
            }
            cache_bind_texture_unit(kGlTexture0, depth_slot->depth_texture);
            tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        } else {
            cache_bind_texture_unit(kGlTexture0, depth_slot->depth_texture);
        }
        // The guest word IS GL_UNSIGNED_INT_24_8; the whole transform is the
        // vertical flip. The destination is never swizzled here (the device
        // declines a swizzled destination outright), so this is a row copy.
        const size_t row_words = static_cast<size_t>(width);
        if (packet.depth_input.size() < row_words * height * 4u) {
            error = "OpenGL generated combiner zeta snapshot is smaller than its surface";
            return false;
        }
        resize_or_zero(witnessed_depth_staging_, row_words * height, 1u, 1u);
        uint32_t* dst = witnessed_depth_staging_.data();
        const uint8_t* src = packet.depth_input.data();
        for (uint32_t y = 0; y < height; ++y) {
            std::memcpy(dst + static_cast<size_t>(height - 1u - y) * row_words,
                        src + static_cast<size_t>(y) * row_words * 4u,
                        row_words * 4u);
        }
        // The WORD form of the same plane, R32UI. The resident dashboard
        // fixture's depth-EQUAL test reads it as a usampler2D, and its
        // contract is "a slot whose depth is valid has one" -- a contract
        // ensure_resident_depth_surface establishes and this path has to keep,
        // or the fixture faults with "missing zeta-word texture" the moment
        // this executor is the one that made the plane resident.
        if (!depth_slot->depth_word_texture) {
            gen_textures_(1, &depth_slot->depth_word_texture);
            if (!depth_slot->depth_word_texture) {
                error = "OpenGL generated combiner zeta-word texture allocation failed";
                return false;
            }
            cache_bind_texture_unit(kGlTexture0,
                                    depth_slot->depth_word_texture);
            tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        } else {
            cache_bind_texture_unit(kGlTexture0,
                                    depth_slot->depth_word_texture);
        }
        pixel_store_i_(kGlUnpackAlignment, 4);
        gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlR32ui,
                               static_cast<int>(width),
                               static_cast<int>(height), 0, kGlRedInteger,
                               kGlUnsignedInt, witnessed_depth_staging_.data());
        cache_bind_texture_unit(kGlTexture0, depth_slot->depth_texture);
        pixel_store_i_(kGlUnpackAlignment, 4);
        if (depth_slot->depth_alloc_w == width &&
            depth_slot->depth_alloc_h == height) {
            gpu_timed_tex_sub_image_2d(kGlTexture2D, 0, 0, 0,
                                       static_cast<int>(width),
                                       static_cast<int>(height),
                                       kGlDepthStencil, kGlUnsignedInt248,
                                       witnessed_depth_staging_.data());
        } else {
            gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlDepth24Stencil8,
                                   static_cast<int>(width),
                                   static_cast<int>(height), 0, kGlDepthStencil,
                                   kGlUnsignedInt248,
                                   witnessed_depth_staging_.data());
            depth_slot->depth_alloc_w = width;
            depth_slot->depth_alloc_h = height;
        }
        if (!resident_fbo_) gen_framebuffers_(1, &resident_fbo_);
        cache_bind_framebuffer(resident_fbo_);
        cache_framebuffer_color_texture(
            resident_fbo_, color_slot ? color_slot->texture : 0u);
        cache_framebuffer_depth_texture(resident_fbo_,
                                        depth_slot->depth_texture);
        if (check_framebuffer_status_(kGlFramebuffer) != kGlFramebufferComplete) {
            error = "OpenGL generated combiner colour+zeta framebuffer is incomplete";
            return false;
        }
        if (get_error_() != kGlNoError) {
            error = "OpenGL generated combiner zeta upload failed";
            return false;
        }
        if (depth_slot->depth_valid && depth_slot->depth_dirty)
            ++witnessed_upload_census_.upload_over_dirty_depth;
        if (depth_slot->depth_valid &&
            depth_slot->depth_address == packet.depth_address)
            ++witnessed_upload_census_.depth_key_mismatch;
        if (depth_slot->depth_valid &&
            depth_slot->depth_address != packet.depth_address)
            ++witnessed_upload_census_.depth_slot_takeover;
        note_upload_cause(witnessed_upload_census_.depth_uploads,
                          witnessed_upload_census_.depth_bytes,
                          take_invalidate_stamp(depth_stamps_,
                                                packet.depth_address),
                          packet.surface_size);
        depth_slot->depth_valid = true;
        depth_slot->depth_dirty = false;
        depth_slot->depth_dirty_rect.reset();
        depth_slot->depth_address = packet.depth_address;
        depth_slot->depth_size = packet.surface_size;
        depth_slot->depth_pitch = packet.surface_pitch;
        depth_slot->depth_height = packet.surface_height;
        depth_slot->depth_format = packet.surface_format;
        active_depth_resident_ = depth_slot;
        ++generated_stats_.depth_uploads;
        result.depth_upload_bytes += packet.surface_size;
        return true;
    }

    // The colour-only counterpart of ensure_witnessed_surface. It shares the
    // ResidentSurface cache and the invalidation-stamp census with every other
    // resident path, so a plane this executor uploads is attributable in
    // [host] gpu-upload-cause exactly like one the witnessed executor uploads.
    bool ensure_generated_surface(const Nv2aGeneratedShaderPacket& packet,
                                  ResidentSurface*& surface,
                                  Nv2aGeneratedShaderResult& result,
                                  std::string& error) {
        surface = find_resident_surface(packet.color_address,
                                        packet.surface_size,
                                        packet.surface_pitch,
                                        packet.surface_height,
                                        packet.surface_format);
        if (surface) {
            if (!resident_fbo_) gen_framebuffers_(1, &resident_fbo_);
            return true;
        }
        if (packet.color_input.size() != packet.surface_size) {
            error = "OpenGL generated combiner missing initial colour snapshot";
            return false;
        }
        ResidentSurface* slot = find_reusable_resident_slot();
        if (!slot) {
            error = "OpenGL resident surface cache is full of dirty targets";
            return false;
        }
        note_color_slot_reuse(*slot);
        if (!slot->texture) {
            gen_textures_(1, &slot->texture);
            if (!slot->texture) {
                error = "OpenGL generated combiner colour texture allocation failed";
                return false;
            }
            cache_bind_texture_unit(kGlTexture0, slot->texture);
            tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        } else {
            cache_bind_texture_unit(kGlTexture0, slot->texture);
        }
        // PHASE 7 (upload staging), the same two changes the witnessed path
        // already carries: GL_BGRA (the guest word's own byte order, so the
        // per-pixel shuffle disappears) and a straight ROW COPY, because the
        // vertical flip is the whole of the transform for a pitch-linear
        // destination -- and this executor never takes a swizzled one.
        const uint32_t up_w = packet.surface_width;
        const uint32_t up_h = packet.surface_height;
        const size_t row_bytes = static_cast<size_t>(up_w) * 4u;
        if (packet.color_input.size() < row_bytes * up_h) {
            error = "OpenGL generated combiner colour snapshot is smaller than its surface";
            return false;
        }
        std::vector<uint8_t>& rgba = upload_staging_;
        if (generated_bgra_upload_) {
            rgba.resize(row_bytes * up_h);
            for (uint32_t y = 0; y < up_h; ++y) {
                std::memcpy(
                    rgba.data() + static_cast<size_t>(up_h - 1u - y) * row_bytes,
                    packet.color_input.data() + static_cast<size_t>(y) * row_bytes,
                    row_bytes);
            }
            pixel_store_i_(kGlUnpackAlignment, 4);
            if (slot->tex_alloc_w == up_w && slot->tex_alloc_h == up_h) {
                gpu_timed_tex_sub_image_2d(kGlTexture2D, 0, 0, 0,
                                           static_cast<int>(up_w),
                                           static_cast<int>(up_h), kGlBgra,
                                           kGlUnsignedByte, rgba.data());
            } else {
                gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlRgba8,
                                       static_cast<int>(up_w),
                                       static_cast<int>(up_h), 0, kGlBgra,
                                       kGlUnsignedByte, rgba.data());
                slot->tex_alloc_w = up_w;
                slot->tex_alloc_h = up_h;
            }
        } else {
            // The comparison arm: the pre-phase-7 per-pixel shuffle.
            guest_words_to_rgba(packet.color_input, rgba, up_w, up_h,
                                packet.state.color_format_x8);
            pixel_store_i_(kGlUnpackAlignment, 1);
            gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlRgba8,
                                   static_cast<int>(up_w),
                                   static_cast<int>(up_h), 0, kGlRgba,
                                   kGlUnsignedByte, rgba.data());
            slot->tex_alloc_w = up_w;
            slot->tex_alloc_h = up_h;
        }
        if (!resident_fbo_) gen_framebuffers_(1, &resident_fbo_);
        cache_bind_framebuffer(resident_fbo_);
        cache_framebuffer_color_texture(resident_fbo_, slot->texture);
        cache_framebuffer_depth_texture(resident_fbo_, 0);
        if (check_framebuffer_status_(kGlFramebuffer) != kGlFramebufferComplete) {
            error = "OpenGL generated combiner framebuffer is incomplete";
            return false;
        }
        if (get_error_() != kGlNoError) {
            error = "OpenGL generated combiner colour upload failed";
            return false;
        }
        slot->valid = true;
        slot->dirty = false;
        slot->dirty_rect.reset();
        slot->swizzled = false;
        slot->x8 = packet.state.color_format_x8;
        slot->address = packet.color_address;
        slot->size = packet.surface_size;
        slot->pitch = packet.surface_pitch;
        slot->width = packet.surface_width;
        slot->height = packet.surface_height;
        slot->format = packet.surface_format;
        ++generated_stats_.surface_uploads;
        result.color_upload_bytes += rgba.size();
        surface = slot;
        return true;
    }

    // ============ stage (f) witnessed-profile GPU execution ================
    // beads-xbox.1.15.7. These four entry points sit on top of the SAME
    // ResidentSurface cache the resident generic-combiner path uses, keyed on
    // the same (address, size, raw pitch word, height, raw format word) tuple,
    // so a surface made resident by one path is found by the other and the
    // existing resolve / invalidate seam in nv2a.cpp keeps both coherent
    // without a second protocol.
    bool supports_witnessed_profile(
        const Nv2aWitnessedProfilePacket& descriptor) const override {
        if (!witnessed_available_) return false;
        if (descriptor.surface_width == 0 || descriptor.surface_height == 0)
            return false;
        if (descriptor.surface_size !=
            static_cast<size_t>(descriptor.surface_width) *
                descriptor.surface_height * 4u) {
            return false;
        }
        if (descriptor.profile !=
            Nv2aWitnessedProfilePacket::Profile::kSwizzledRadialColor) {
            if (descriptor.texture_width == 0 || descriptor.texture_height == 0)
                return false;
        }
        if (descriptor.profile ==
            Nv2aWitnessedProfilePacket::Profile::kPitchProjectedColor) {
            if (descriptor.projected_texture_width == 0 ||
                descriptor.projected_texture_height == 0) {
                return false;
            }
        }
        // ALIASING GUARD. The resident cache is keyed on
        // (address, size, pitch, format), so a surface RETARGET -- the same
        // guest memory rebound with a different format or pitch, which is
        // exactly what happens when the swizzled radial target and the pitch
        // textured target share an address -- would silently produce a SECOND
        // resident copy of the same bytes. Two GPU-authoritative copies of one
        // range is data loss: whichever resolves last wins and the other's
        // draws vanish. Refusing here sends the draw to the CPU executor, whose
        // resolve_renderer_*_overlap call resolves and invalidates the stale
        // copy through the existing seam, after which the next draw can go
        // resident cleanly.
        auto overlaps = [](uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) {
            return a0 < b1 && b0 < a1;
        };
        const uint64_t want_colour0 = descriptor.color_address;
        const uint64_t want_colour1 = want_colour0 + descriptor.surface_size;
        const uint64_t want_depth0 = descriptor.depth_address;
        const uint64_t want_depth1 = want_depth0 + descriptor.surface_size;
        for (const auto& surface : resident_surfaces_) {
            const bool is_our_colour =
                surface.valid && surface.address == descriptor.color_address &&
                surface.size == descriptor.surface_size &&
                surface.pitch == descriptor.surface_pitch &&
                surface.format == descriptor.surface_format;
            const bool is_our_depth =
                surface.depth_valid &&
                surface.depth_address == descriptor.depth_address &&
                surface.depth_size == descriptor.surface_size;
            if (surface.valid && !is_our_colour) {
                const uint64_t c0 = surface.address;
                const uint64_t c1 = c0 + surface.size;
                if (overlaps(want_colour0, want_colour1, c0, c1) ||
                    overlaps(want_depth0, want_depth1, c0, c1)) {
                    return false;
                }
            }
            if (surface.depth_valid && !is_our_depth) {
                const uint64_t d0 = surface.depth_address;
                const uint64_t d1 = d0 + surface.depth_size;
                if (overlaps(want_colour0, want_colour1, d0, d1) ||
                    overlaps(want_depth0, want_depth1, d0, d1)) {
                    return false;
                }
            }
        }
        // The cache never evicts a DIRTY target, so a packet that is not
        // already resident and has nowhere to land must be refused here, before
        // the device builds it -- after supports_ returns true, failure is
        // fatal for the draw (the same contract submit_generic_combiner has).
        if (find_resident_surface(descriptor.color_address,
                                  descriptor.surface_size,
                                  descriptor.surface_pitch,
                                  descriptor.surface_height,
                                  descriptor.surface_format) != nullptr) {
            return true;
        }
        for (const auto& surface : resident_surfaces_) {
            if (!surface.valid && !surface.depth_dirty) return true;
            if (!surface.dirty && !surface.depth_dirty) return true;
        }
        return false;
    }

    bool witnessed_target_resident(uint32_t color_address, size_t size,
                                   uint32_t pitch, uint32_t height,
                                   uint32_t format,
                                   uint32_t depth_address) const override {
        const ResidentSurface* color_slot = nullptr;
        const ResidentSurface* depth_slot = nullptr;
        bool colour_resident = false;
        bool depth_resident = false;
        select_witnessed_slots(color_address, size, pitch, height, format,
                               depth_address, color_slot, depth_slot,
                               colour_resident, depth_resident);
        return colour_resident && depth_resident;
    }
    void witnessed_target_residency(uint32_t color_address, size_t size,
                                    uint32_t pitch, uint32_t height,
                                    uint32_t format, uint32_t depth_address,
                                    bool& color_resident,
                                    bool& depth_resident) const override {
        const ResidentSurface* color_slot = nullptr;
        const ResidentSurface* depth_slot = nullptr;
        select_witnessed_slots(color_address, size, pitch, height, format,
                               depth_address, color_slot, depth_slot,
                               color_resident, depth_resident);
    }
    Nv2aWitnessedUploadCensus witnessed_upload_census() const override {
        Nv2aWitnessedUploadCensus out = witnessed_upload_census_;
        out.stamp_overflows = stamp_overflows_;
        return out;
    }

    bool submit_witnessed_profile(const Nv2aWitnessedProfilePacket& packet,
                                  Nv2aWitnessedProfileResult& result,
                                  std::string& error) override {
        perf_excl::Scope gl_region(perf_excl::kNv2aGl);
        result = {};
        if (!take_witnessed_flush_error(error)) return false;
        if (packet.triangles.empty()) return true;
        if (!enter_witnessed_context(error)) return false;
        const uint64_t draw_calls_before = witnessed_draw_calls_;

        // A pending batch for different state has to be issued before this
        // packet can join one; the key is every input the shader reads that is
        // not in the triangle record.
        WitnessedBatchKey key;
        key.profile = packet.profile;
        key.color_address = packet.color_address;
        key.depth_address = packet.depth_address;
        key.surface_size = packet.surface_size;
        key.surface_pitch = packet.surface_pitch;
        key.surface_format = packet.surface_format;
        key.surface_width = packet.surface_width;
        key.surface_height = packet.surface_height;
        key.xmin = packet.xmin; key.xmax = packet.xmax;
        key.ymin = packet.ymin; key.ymax = packet.ymax;
        key.clip_min = packet.clip_min;
        key.clip_max = packet.clip_max;
        key.texture_hash = packet.texture_hash;
        key.texture_width = packet.texture_width;
        key.texture_height = packet.texture_height;
        key.projected_texture_hash = packet.projected_texture_hash;
        key.projected_texture_width = packet.projected_texture_width;
        key.projected_texture_height = packet.projected_texture_height;
        key.factor0_stage6 = packet.factor0_stage6;
        key.factor1_stage6 = packet.factor1_stage6;
        if (witnessed_batch_count_ != 0 && !(key == witnessed_batch_key_)) {
            if (!flush_witnessed_batch(error)) return false;
        }

        ResidentSurface* surface = nullptr;
        ResidentSurface* depth_surface = nullptr;
        {
            perf::ScopedAdd timer(perf::g_ns_renderer_resident_ensure,
                                  perf::g_calls_renderer_resident_ensure);
            if (!ensure_witnessed_surface(packet, surface, depth_surface,
                                          result, error))
                return false;
        }
        const bool radial = packet.profile ==
            Nv2aWitnessedProfilePacket::Profile::kSwizzledRadialColor;

        // ---- triangle records ------------------------------------------------
        // Appended to the pending batch. Consecutive GPU jobs on the same
        // surface with the same uniforms are ONE glDrawArrays: GL processes
        // primitives within a draw in submission order for depth and colour, so
        // merging draws that were already adjacent in guest order cannot change
        // the result, and it takes ~80 k driver submissions per 2 G
        // instructions down to the number of state changes.
        const size_t count = packet.triangles.size();
        // kPitchProjectedColor uses the TEXTURED record, byte for byte: its
        // projective texcoord0 rides in the .w lanes that profile writes as
        // zero. No new stride, no new SSBO layout.
        const bool projected =
            packet.profile ==
            Nv2aWitnessedProfilePacket::Profile::kPitchProjectedColor;
        const size_t stride = radial ? sizeof(witnessed_gl::RadialTriRecord)
                                     : sizeof(witnessed_gl::TexturedTriRecord);
        const size_t append_at = witnessed_records_.size();
        witnessed_records_.resize(append_at + stride * count);
        for (size_t i = 0; i < count; ++i) {
            const Nv2aWitnessedTriangle& t = packet.triangles[i];
            uint8_t* base = witnessed_records_.data() + append_at + stride * i;
            // The two records share their leading 160 bytes by construction, so
            // the common half is written once through the radial view.
            auto* common =
                reinterpret_cast<witnessed_gl::RadialTriRecord*>(base);
            common->ab[0] = t.ax; common->ab[1] = t.ay;
            common->ab[2] = t.bx; common->ab[3] = t.by;
            common->cp[0] = t.cx; common->cp[1] = t.cy;
            // cp[2] packs the software path's scanline box as four 16-bit
            // fields; every bound is clip-rect-clamped and therefore in
            // [0, 4095], and a record with an empty box is never emitted.
            // cp[3] is the wide flag the vertex shader switches on.
            common->cp[2] =
                (static_cast<int64_t>(t.bbox_xmin) & 0xFFFF) |
                ((static_cast<int64_t>(t.bbox_xmax) & 0xFFFF) << 16) |
                ((static_cast<int64_t>(t.bbox_ymin) & 0xFFFF) << 32) |
                ((static_cast<int64_t>(t.bbox_ymax) & 0xFFFF) << 48);
            common->cp[3] = t.wide ? 1 : 0;
            common->zi[0] = t.za; common->zi[1] = t.zb;
            common->zi[2] = t.zc; common->zi[3] = t.inverse_area;
            // beads-xbox.1.15.10 step 2. `wv` was padding that kept `recip` on
            // its 32-byte std430 boundary and carried w0/w1/w2, which no shader
            // ever read. It now carries the SINGLE-PRECISION copies of exactly
            // the four reciprocals `recip` holds in double, so the FP32 shader
            // variant reads its inputs out of the SAME record the FP64 variant
            // uses -- identical layout, identical stride, one runtime switch.
            common->wv[0] = static_cast<float>(t.inverse_area_double);
            common->wv[1] = static_cast<float>(t.inverse_w[0]);
            common->wv[2] = static_cast<float>(t.inverse_w[1]);
            common->wv[3] = static_cast<float>(t.inverse_w[2]);
            for (int v = 0; v < 3; ++v) {
                float* d = v == 0 ? common->d0 : (v == 1 ? common->d1 : common->d2);
                float* a = v == 0 ? common->t0 : (v == 1 ? common->t1 : common->t2);
                for (int c = 0; c < 3; ++c) {
                    d[c] = t.diffuse[v][c];
                    a[c] = t.tex_a[v][c];
                }
                // The projective texcoord0's x and y for this vertex; 0 for
                // every other profile, which is what those lanes have always
                // held and what no shader but the projected one reads.
                d[3] = projected ? t.tex_p[v][0] : 0.0f;
                a[3] = projected ? t.tex_p[v][1] : 0.0f;
            }
            if (radial) {
                auto* rec =
                    reinterpret_cast<witnessed_gl::RadialTriRecord*>(base);
                rec->recip[0] = t.inverse_area_double;
                rec->recip[1] = t.inverse_w[0];
                rec->recip[2] = t.inverse_w[1];
                rec->recip[3] = t.inverse_w[2];
            } else {
                auto* rec =
                    reinterpret_cast<witnessed_gl::TexturedTriRecord*>(base);
                for (int v = 0; v < 3; ++v) {
                    float* u = v == 0 ? rec->u0 : (v == 1 ? rec->u1 : rec->u2);
                    for (int c = 0; c < 3; ++c) u[c] = t.tex_b[v][c];
                    // The projective w (the software path's texture[0][3]).
                    u[3] = projected ? t.tex_p[v][2] : 0.0f;
                }
                rec->recip[0] = t.inverse_area_double;
                rec->recip[1] = t.inverse_w[0];
                rec->recip[2] = t.inverse_w[1];
                rec->recip[3] = t.inverse_w[2];
            }
        }
        if (!radial && !ensure_witnessed_texture(packet, result, error))
            return false;
        if (packet.profile ==
                Nv2aWitnessedProfilePacket::Profile::kPitchProjectedColor &&
            !ensure_witnessed_projected_texture(packet, result, error)) {
            return false;
        }
        witnessed_batch_key_ = key;
        witnessed_batch_surface_ = surface;
        // beads-xbox.1.15.10 phase 4 (lever C): the colour and zeta planes of a
        // witnessed packet can now live in DIFFERENT resident slots, so the
        // batch carries both. It is one pointer per plane and not one per
        // packet because the batch key already pins both guest addresses.
        witnessed_batch_depth_surface_ = depth_surface;
        witnessed_batch_count_ += count;
        result.triangles = count;
        // beads-xbox.1.15.10 step 1 (overlap). The batch used to be issued only
        // when some other entry point demanded its result, and the measured
        // consequence was 965 draw calls of ~8.0 ms of GPU work each, every one
        // of them issued immediately before the readback that waited on it. A
        // triangle bound splits that into chunks the GPU can chew through while
        // the emulation thread runs on. Batching itself is still what keeps the
        // driver-submission count sane (it took this path from ~80 k submits to
        // 965), so the bound is a tunable, not a removal: 0 restores the old
        // behavior for the bisection arm.
        if (witnessed_flush_triangles_ != 0 &&
            witnessed_batch_count_ >= witnessed_flush_triangles_) {
            ++witnessed_flush_size_;
            if (!flush_witnessed_batch(error)) return false;
        }
        // Bound the pending batch so a very long run cannot grow the staging
        // buffer without limit; 64 MiB is ~230 k radial triangles.
        else if (witnessed_records_.size() >= (size_t{64} << 20)) {
            if (!flush_witnessed_batch(error)) return false;
        }
        // CUMULATIVE, not a per-submit delta: most flushes are triggered by
        // some other entry point (a resolve, an invalidate, the next state
        // change), so a delta measured inside submit reads zero and the census
        // silently claims the executor issued no draws at all.
        (void)draw_calls_before;
        result.draw_calls = witnessed_draw_calls_;
        return true;
    }

    // Records the FIRST stage of a flush at which GL already had an error
    // pending, so a failure is attributed to the call that raised it rather
    // than to the next call that happened to look.
    int witnessed_bad_stage_ = -1;
    void witnessed_stage_error(int stage) {
        if (witnessed_bad_stage_ >= 0) return;
        if (get_error_() != kGlNoError) witnessed_bad_stage_ = stage;
    }
    bool flush_witnessed_batch(std::string& error) {
        if (witnessed_batch_count_ == 0) {
            witnessed_records_.clear();
            return true;
        }
        const WitnessedBatchKey& packet = witnessed_batch_key_;
        ResidentSurface* surface = witnessed_batch_surface_;
        ResidentSurface* depth_surface = witnessed_batch_depth_surface_;
        const size_t count = witnessed_batch_count_;
        witnessed_batch_count_ = 0;
        witnessed_batch_surface_ = nullptr;
        witnessed_batch_depth_surface_ = nullptr;
        if (!surface || !depth_surface) {
            witnessed_records_.clear();
            error = "OpenGL witnessed batch lost its target";
            return false;
        }
        perf_excl::Scope gl_region(perf_excl::kNv2aGl);
        if (!enter_witnessed_context(error)) {
            witnessed_records_.clear();
            return false;
        }
        const bool radial = packet.profile ==
            Nv2aWitnessedProfilePacket::Profile::kSwizzledRadialColor;
        witnessed_stage_error(0);
        if (witnessed_ssbo_ring_size_ > kWitnessedSsboRingMax)
            witnessed_ssbo_ring_size_ = kWitnessedSsboRingMax;
        if (witnessed_ssbo_ring_size_ < 1) witnessed_ssbo_ring_size_ = 1;
        unsigned int& slot = witnessed_ssbo_ring_[witnessed_ssbo_ring_head_];
        if (!slot) gen_buffers_(1, &slot);
        witnessed_ssbo_ = slot;
        witnessed_ssbo_ring_head_ =
            (witnessed_ssbo_ring_head_ + 1) % witnessed_ssbo_ring_size_;
        bind_buffer_(kGlShaderStorageBuffer, witnessed_ssbo_);
        // Orphan-and-refill: a fresh store per flush is what lets the driver
        // pipeline the previous draw instead of stalling on a buffer it is
        // still reading.
        gpu_timed_buffer_data(kGlShaderStorageBuffer,
                              static_cast<std::ptrdiff_t>(
                                  witnessed_records_.size()),
                              witnessed_records_.data(), kGlStreamDraw);
        witnessed_ssbo_bytes_ = witnessed_records_.size();
        witnessed_records_.clear();
        bind_buffer_base_(kGlShaderStorageBuffer, 0, witnessed_ssbo_);
        witnessed_stage_error(1);
        Nv2aWitnessedProfileResult result;
        std::string texture_error;

        const bool projected =
            packet.profile ==
            Nv2aWitnessedProfilePacket::Profile::kPitchProjectedColor;
        WitnessedProgram& wp =
            radial ? witnessed_radial_
                   : (projected ? witnessed_projected_ : witnessed_textured_);
        // glGetUniformLocation is a driver-side string lookup; at a few hundred
        // flushes per run it is not a per-flush operation either, so the
        // locations are resolved once at link time and every uniform is written
        // only when its value actually changes.
        //
        // use_program IS issued unconditionally, and the mirror that used to
        // suppress it is gone. The resident generic-combiner path calls
        // use_program_ on its own programs without telling this mirror, so a
        // cached "already bound" could be false, and glUniform* against a
        // location belonging to a different program is GL_INVALID_OPERATION.
        // That is not theoretical: with --nv2a-gpu-profile-mask 2 it stopped
        // the machine at 647 782 750 instructions with GL 0x0502. Uniform
        // VALUES are per-program state and survive a rebind, so uniforms_valid
        // is still correct across one; only the program binding was.
        use_program_(wp.program);
        witnessed_bound_program_ = wp.program;
        const float surface_w = static_cast<float>(packet.surface_width);
        const float surface_h = static_cast<float>(packet.surface_height);
        if (!wp.uniforms_valid || wp.surface_w != surface_w ||
            wp.surface_h != surface_h) {
            uniform_2f_(wp.loc_surface_size, surface_w, surface_h);
            wp.surface_w = surface_w;
            wp.surface_h = surface_h;
        }
        if (!wp.uniforms_valid || wp.clip_min != packet.clip_min) {
            uniform_1f_(wp.loc_clip_min, packet.clip_min);
            wp.clip_min = packet.clip_min;
        }
        if (!wp.uniforms_valid || wp.clip_max != packet.clip_max) {
            uniform_1f_(wp.loc_clip_max, packet.clip_max);
            wp.clip_max = packet.clip_max;
        }
        if (!radial) {
            if (projected) {
                // Unit 1 FIRST, so unit 0 is the active unit when the block
                // ends -- every other path in this file assumes that.
                cache_bind_texture_unit(kGlTexture1,
                                        witnessed_projected_texture_);
            }
            cache_bind_texture_unit(kGlTexture0, witnessed_texture_);
            if (!wp.uniforms_valid) uniform_1i_(wp.loc_texture0, 0);
            const float tex_w = static_cast<float>(packet.texture_width);
            const float tex_h = static_cast<float>(packet.texture_height);
            if (!wp.uniforms_valid || wp.texture_w != tex_w ||
                wp.texture_h != tex_h) {
                uniform_2f_(wp.loc_texture_size, tex_w, tex_h);
                wp.texture_w = tex_w;
                wp.texture_h = tex_h;
            }
            if (projected) {
                if (!wp.uniforms_valid) uniform_1i_(wp.loc_texture1, 1);
                const float pw =
                    static_cast<float>(packet.projected_texture_width);
                const float ph =
                    static_cast<float>(packet.projected_texture_height);
                if (!wp.uniforms_valid || wp.proj_texture_w != pw ||
                    wp.proj_texture_h != ph) {
                    uniform_2f_(wp.loc_projected_texture_size, pw, ph);
                    wp.proj_texture_w = pw;
                    wp.proj_texture_h = ph;
                }
            }
            const uint32_t f0 = packet.factor0_stage6;
            const uint32_t f1 = packet.factor1_stage6;
            if (!wp.uniforms_valid || wp.factor0 != f0) {
                uniform_3f_(wp.loc_weight0,
                            static_cast<float>((f0 >> 16) & 0xFFu) / 255.0f,
                            static_cast<float>((f0 >> 8) & 0xFFu) / 255.0f,
                            static_cast<float>(f0 & 0xFFu) / 255.0f);
                wp.factor0 = f0;
            }
            if (!wp.uniforms_valid || wp.factor1 != f1) {
                uniform_3f_(wp.loc_weight1,
                            static_cast<float>((f1 >> 16) & 0xFFu) / 255.0f,
                            static_cast<float>((f1 >> 8) & 0xFFu) / 255.0f,
                            static_cast<float>(f1 & 0xFFu) / 255.0f);
                wp.factor1 = f1;
            }
        }
        wp.uniforms_valid = true;
        witnessed_stage_error(2);

        cache_bind_framebuffer(resident_fbo_);
        cache_framebuffer_color_texture(resident_fbo_, surface->texture);
        cache_framebuffer_depth_texture(resident_fbo_,
                                        depth_surface->depth_texture);
        cache_viewport(0, 0, static_cast<int>(packet.surface_width),
                       static_cast<int>(packet.surface_height));
        // Guest scissor rows are top-down; the resident image is stored
        // bottom-up, exactly as guest_words_to_rgba stores it.
        const int scissor_w =
            static_cast<int>(packet.xmax - packet.xmin + 1u);
        const int scissor_h =
            static_cast<int>(packet.ymax - packet.ymin + 1u);
        cache_scissor(true, static_cast<int>(packet.xmin),
                      static_cast<int>(packet.surface_height - 1u -
                                       packet.ymax),
                      scissor_w, scissor_h);
        cache_depth_test(true, kGlLequal);
        cache_depth_mask(true);
        cache_blend(false, 0, 0);
        cache_color_mask(1, 1, 1, 1);
        if (!witnessed_fixed_state_) {
            // Both are off in the primed state and nothing in this file turns
            // them on, so this is a one-time assertion, not a per-draw call.
            disable_(kGlCullFace);
            disable_(kGlStencilTest);
            witnessed_fixed_state_ = true;
        }
        witnessed_stage_error(3);
        cache_bind_vao_vbo(witnessed_vao_, 0);
        witnessed_stage_error(4);
        // Phase 2 (beads-xbox.1.15.10): SIX vertices per triangle record, not
        // three. k = 3,4,5 is a degenerate copy of vertex 0 for an ordinary
        // record -- zero area, no fragments, one extra vertex-shader invocation
        // per triangle -- and the two triangles of the scanline bounding box
        // for a wide one. The alternative, two draw calls with two vertex
        // counts, would have split every batch the moment a wide triangle
        // appeared in it, and batching is what took this path from 80 k driver
        // submissions to 1 737.
        gpu_timed_draw_arrays(kGlTriangles, 0,
                              static_cast<int>(count * 6u));
        // beads-xbox.1.15.10 step 1 (overlap). glDrawArrays only APPENDS to the
        // driver's command buffer; nothing submits it to the GPU until some
        // call needs a result. Before this, the only such call was the resolve's
        // glReadPixels (or its glClientWaitSync under --perf-accounting), so the
        // GPU started the batch at the moment the emulation thread began waiting
        // for it and none of the 7.740 s of witnessed GPU work overlapped the
        // emulation thread -- 5.878 s of it was paid a second time as drain_ns.
        // glFlush submits the batch and returns without waiting, which is the
        // whole of the fix: the GPU runs the draw while the emulation thread
        // goes back to TCG, vertex transform and the CPU-side profiles. It is
        // NOT gated on perf accounting: this is the shipping path, not an
        // instrument.
        if (flush_ && witnessed_kick_flush_) {
            flush_();
            ++witnessed_gl_flushes_;
        }
        // glGetError forces a driver round trip; at 80 k submits that is a
        // measurable serialization. The state is checked once per surface
        // residency change (in ensure_witnessed_surface) and at every resolve,
        // which is where a real GL error would surface anyway.

        surface->dirty = true;
        depth_surface->depth_dirty = true;
        surface->dirty_rect.add(packet.xmin, packet.ymin,
                                packet.xmax - packet.xmin + 1u,
                                packet.ymax - packet.ymin + 1u);
        depth_surface->depth_dirty_rect.add(packet.xmin, packet.ymin,
                                            packet.xmax - packet.xmin + 1u,
                                            packet.ymax - packet.ymin + 1u);
        // One glGetError per FLUSH, not per draw: at a few hundred flushes per
        // run this costs nothing, and without it a GL error raised by a draw
        // would surface at the next resolve and be misattributed to it -- which
        // is exactly what happened while this was missing.
        const unsigned int gl_error = get_error_();
        if (gl_error != kGlNoError) {
            char why[192];
            std::snprintf(why, sizeof(why),
                          "OpenGL witnessed-profile draw failed (GL 0x%04x, "
                          "%zu triangles, profile %u, first bad stage %d, "
                          "fbo=%u colour=%u depth=%u unit0=%u tex=%u)",
                          gl_error, count,
                          static_cast<unsigned>(packet.profile),
                          witnessed_bad_stage_, gl_state_.bound_fbo,
                          gl_state_.fbo_color_texture,
                          gl_state_.fbo_depth_texture,
                          gl_state_.unit2d_binding[0], witnessed_texture_);
            error = why;
            return false;
        }
        ++witnessed_draw_calls_;
        (void)texture_error;
        return true;
    }
    // Flush ONLY when the requested guest range can actually observe the
    // pending batch, i.e. when it overlaps the batch's own colour or zeta
    // plane. The unconditional version was correct and far too blunt: every
    // witnessed CPU draw calls resolve_renderer_color_overlap for ITS OWN
    // surface, 160 568 times per 12 G instructions, and flushing there cut the
    // batching from a few hundred draw calls to 93 842.
    bool flush_witnessed_batch_for(uint32_t address, size_t size,
                                   std::string& error) {
        if (witnessed_batch_count_ == 0) return true;
        const uint64_t lo = address;
        const uint64_t hi = lo + size;
        const WitnessedBatchKey& k = witnessed_batch_key_;
        const uint64_t c0 = k.color_address;
        const uint64_t c1 = c0 + k.surface_size;
        const uint64_t d0 = k.depth_address;
        const uint64_t d1 = d0 + k.surface_size;
        if ((lo < c1 && c0 < hi) || (lo < d1 && d0 < hi))
            return flush_witnessed_batch(error);
        return true;
    }
    void flush_witnessed_batch_for_deferred(uint32_t address, size_t size) {
        std::string error;
        if (!flush_witnessed_batch_for(address, size, error) &&
            witnessed_flush_error_.empty()) {
            witnessed_flush_error_ = error;
        }
    }
    // Void entry points cannot report a flush failure, so it is latched and
    // surfaced by the next fallible call. A GL error here is fatal for the run
    // either way; what matters is that it is never silently dropped.
    void flush_witnessed_batch_deferred() {
        std::string error;
        if (!flush_witnessed_batch(error) && witnessed_flush_error_.empty())
            witnessed_flush_error_ = error;
    }
    bool take_witnessed_flush_error(std::string& error) {
        if (witnessed_flush_error_.empty()) return true;
        error = witnessed_flush_error_;
        witnessed_flush_error_.clear();
        return false;
    }

    bool clear_resident_surface(uint32_t color_address, size_t color_size,
                                bool clear_color, uint32_t color_value,
                                uint32_t depth_address, size_t depth_size,
                                bool clear_depth, uint32_t depth_value,
                                bool& needs_guest_fill,
                                std::string& error) override {
        error.clear();
        needs_guest_fill = true;
        if (!witnessed_available_) return false;
        if (!flush_witnessed_batch(error)) return false;
        // beads-xbox.1.15.10 phase 3, step 3. The two planes are looked up
        // SEPARATELY and cleared separately. The pair-only form this had was
        // measured to decline every clear on the canonical workload: at 2 G
        // instructions 457 of 768 CLEAR_SURFACE calls request exactly one plane
        // (the guest clears colour and zeta in separate methods) and the rest
        // found no slot whose colour AND zeta keys both matched. Each plane is
        // its own resident image with its own dirty flag, so clearing one
        // without the other is the same operation the software path performed.
        ResidentSurface* color_surface = nullptr;
        if (clear_color) {
            for (auto& candidate : resident_surfaces_) {
                if (candidate.valid && candidate.address == color_address &&
                    candidate.size == color_size) {
                    color_surface = &candidate;
                    break;
                }
            }
            if (!color_surface) {
                ++witnessed_upload_census_.clear_decline_color_missing;
                return false;
            }
        }
        ResidentSurface* depth_surface = nullptr;
        if (clear_depth) {
            for (auto& candidate : resident_surfaces_) {
                if (candidate.depth_valid &&
                    candidate.depth_address == depth_address &&
                    candidate.depth_size == depth_size) {
                    depth_surface = &candidate;
                    break;
                }
            }
            if (!depth_surface) {
                ++witnessed_upload_census_.clear_decline_depth_missing;
                return false;
            }
        }
        ResidentSurface* surface = color_surface ? color_surface : depth_surface;
        if (!surface) return false;
        perf_excl::Scope gl_region(perf_excl::kNv2aGl);
        if (!enter_witnessed_context(error)) return false;
        cache_bind_framebuffer(resident_fbo_);
        // Attach ONLY the planes being cleared. The resident FBO is shared and
        // two resident surfaces need not have the same dimensions; with both
        // attached its effective size is their intersection and the clear would
        // silently miss part of a plane. That is the same defect the colour
        // readback hit in phase 2 and the fix is the same.
        cache_framebuffer_color_texture(
            resident_fbo_, color_surface ? color_surface->texture : 0u);
        cache_framebuffer_depth_texture(
            resident_fbo_, depth_surface ? depth_surface->depth_texture : 0u);
        cache_scissor(false, 0, 0, 0, 0);
        unsigned int bits = 0;
        if (clear_color) {
            cache_color_mask(1, 1, 1, 1);
            // Exact for every guest byte: the GL float -> UNORM8 conversion is
            // round(clamp(f) * 255), and float(v / 255.0f) * 255 differs from v
            // by at most 255 * 2^-24, so it rounds back to v for all 256
            // values. The channel order is A8R8G8B8 -> (R,G,B,A), matching the
            // GL_BGRA the upload and the resolve both use.
            clear_color_(static_cast<float>((color_value >> 16) & 0xFFu) / 255.0f,
                         static_cast<float>((color_value >> 8) & 0xFFu) / 255.0f,
                         static_cast<float>(color_value & 0xFFu) / 255.0f,
                         static_cast<float>((color_value >> 24) & 0xFFu) / 255.0f);
            bits |= kGlColorBufferBit;
        }
        if (clear_depth) {
            cache_depth_mask(true);
            // Same argument in double: (z / 16777215.0) * 16777215 is within
            // 2e-9 of z for every z < 2^24, so the D24 store is exactly z.
            clear_depth_(static_cast<double>(depth_value >> 8) / 16777215.0);
            clear_stencil_(static_cast<int>(depth_value & 0xFFu));
            stencil_mask_(0xFFFFFFFFu);
            bits |= kGlDepthBufferBit | kGlStencilBufferBit;
        }
        if (bits == 0) return false;
        clear_(bits);
        if (get_error_() != kGlNoError) {
            error = "OpenGL resident clear failed";
            return false;
        }
        if (clear_color) ++witnessed_upload_census_.clear_accept_color;
        if (clear_depth) ++witnessed_upload_census_.clear_accept_depth;
        // See the contract on the base declaration: a plane that was already
        // GPU-authoritative stays so and the caller skips its guest fill; a
        // clean plane is kept clean and the caller fills guest memory to the
        // same values, so the two copies still agree and no resolve is created.
        needs_guest_fill = false;
        if (clear_color) {
            if (color_surface->dirty) {
                color_surface->dirty_rect.add(0, 0, color_surface->width,
                                              color_surface->height);
            } else {
                needs_guest_fill = true;
            }
        }
        if (clear_depth) {
            if (depth_surface->depth_dirty) {
                depth_surface->depth_dirty_rect.add(0, 0, depth_surface->width,
                                                    depth_surface->height);
            } else {
                needs_guest_fill = true;
            }
        }
        if (needs_guest_fill)
            ++witnessed_upload_census_.clear_kept_clean;
        // The overlap contract the software clear used to provide through
        // invalidate_resident_color_surface / _depth_surface: any OTHER slot
        // covering these ranges is now stale, because this call changed the
        // authoritative image for them and did not touch their copies. Skipping
        // it would leave a second resident view of the same guest range holding
        // pre-clear pixels, which is the exact defect class phase 1 recorded
        // for the colour/zeta key mismatch.
        for (auto& other : resident_surfaces_) {
            if (&other == color_surface || &other == depth_surface) continue;
            if (clear_color && resident_overlaps(other, color_address,
                                                 color_size)) {
                other.valid = false;
                other.dirty = false;
                other.dirty_rect.reset();
                stamp_invalidate(color_stamps_, other.address,
                                 nv2a_invalidate_token(
                                     kNv2aInvalidateClearSurface));
                if (active_resident_ == &other) active_resident_ = nullptr;
            }
            if (clear_depth && resident_depth_overlaps(other, depth_address,
                                                       depth_size)) {
                other.depth_valid = false;
                other.depth_dirty = false;
                other.depth_dirty_rect.reset();
                stamp_invalidate(depth_stamps_, other.depth_address,
                                 nv2a_invalidate_token(
                                     kNv2aInvalidateClearSurface));
                if (active_depth_resident_ == &other)
                    active_depth_resident_ = nullptr;
            }
        }
        return true;
    }

    bool resolve_resident_color_surface(uint32_t address, size_t size,
                                        Nv2aResidentColorResolve& resolve,
                                        std::string& error) override {
        // Every pending GPU draw has to have happened before the range it wrote
        // is read back. This, and the matching calls in the depth resolve, the
        // two invalidations, the region upload, the clear and both generic
        // submits, are the complete set of places a deferred batch could
        // otherwise be observed out of order
        // (tests/test_gpu_profiles_guards.py pins it).
        if (!flush_witnessed_batch_for(address, size, error)) return false;
        if (!take_witnessed_flush_error(error)) return false;
        // The caller moves a recycled buffer into resolve.data; keep its
        // capacity and only overwrite the fields this call owns. Paths that
        // resolve nothing MUST clear data so the caller's empty-check stays
        // truthful with a recycled buffer.
        resolve.address = 0;
        resolve.data.clear();
        if (size == 0) return true;
        ResidentSurface* surface = nullptr;
        for (auto& candidate : resident_surfaces_) {
            if (candidate.dirty && resident_overlaps(candidate, address, size)) {
                surface = &candidate;
                break;
            }
        }
        if (!surface) return true;
        SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
        {
            bool make_current_ok;
            {
                perf::ScopedGlContextTimer gl_timer(perf::kGlContextColorResolveEnter);
                make_current_ok = SDL_GL_MakeCurrent(window_, context_);
            }
            if (!make_current_ok) {
                error = std::string("OpenGL resident resolve context lost: ") +
                        SDL_GetError();
                return false;
            }
        }
        struct ContextRestore {
            SDL_Window* previous_window;
            SDL_GLContext previous_context;
            SDL_Window* fallback_window;
            size_t perf_site;
            ~ContextRestore() {
                restore_previous_gl_context(previous_window, previous_context,
                                            fallback_window, perf_site);
            }
        } restore{previous_window, previous_context, window_,
                  perf::kGlContextColorResolveRestore};
        {
            perf::ScopedAdd timer(perf::g_ns_renderer_resolve,
                                  perf::g_calls_renderer_resolve);
            const size_t pixel_bytes = static_cast<size_t>(surface->width) *
                                       surface->height * 4u;
            resolve_readback_scratch_.resize(pixel_bytes);
            cache_bind_framebuffer(resident_fbo_);
            cache_framebuffer_color_texture(resident_fbo_, surface->texture);
            // DETACH the depth attachment before the readback. The resident FBO
            // is shared by every surface, and two resident surfaces need not
            // have the same dimensions -- 1024x512 for the swizzled radial
            // target against 1280x480 for the pitch textured one. With both
            // attached the framebuffer's effective size is their INTERSECTION
            // and glReadPixels outside it is undefined, so a colour readback
            // taken while some other surface's zeta was still attached returned
            // undefined pixels for part of the surface. That is invisible with
            // one resident surface and appears the moment there are two, which
            // is exactly the --nv2a-gpu-profile-mask 3 signature.
            cache_framebuffer_depth_texture(resident_fbo_, 0);
            pixel_store_i_(kGlPackAlignment, 1);
            gpu_timed_resolve_read_pixels(
                0, 0, static_cast<int>(surface->width),
                static_cast<int>(surface->height), kGlBgra, kGlUnsignedByte,
                resolve_readback_scratch_.data(),
                static_cast<uint64_t>(pixel_bytes),
                surface->dirty_rect.bytes(4u));
            perf::record_renderer_resolve_pixels(
                static_cast<uint64_t>(surface->width) * surface->height);
            const unsigned int readback_error = get_error_();
            if (readback_error != kGlNoError) {
                char why[160];
                std::snprintf(why, sizeof(why),
                              "OpenGL resident color readback failed "
                              "(GL 0x%04x, %ux%u, swizzled=%u)",
                              readback_error, surface->width, surface->height,
                              surface->swizzled ? 1u : 0u);
                error = why;
                return false;
            }
            resolve.address = surface->address;
            resolve.data.resize(surface->size);
            const uint8_t* bgra = resolve_readback_scratch_.data();
            const size_t row_bytes = static_cast<size_t>(surface->width) * 4u;
            // The flip into guest byte order is the third component of
            // renderer_resolve_ns and is pure CPU work (beads-xbox.1.14.6).
            perf::ScopedAdd copy_timer(g_gl_gpu.cpu_copy_ns,
                                       g_gl_gpu_split_calls);
            if (surface->swizzled) {
                // Stage (f): a Morton-ordered destination scatters per pixel.
                // The readback is GL_BGRA/GL_UNSIGNED_BYTE, whose byte order is
                // already the guest A8R8G8B8 word's little-endian byte order,
                // so this is a placement, not a conversion.
                swizzle_tables_.ensure(surface->width, surface->height);
                const uint32_t* sx = swizzle_tables_.x.data();
                for (uint32_t y = 0; y < surface->height; ++y) {
                    const uint32_t gl_y = surface->height - 1u - y;
                    const uint8_t* row = bgra + static_cast<size_t>(gl_y) * row_bytes;
                    const uint32_t yb = swizzle_tables_.y[y];
                    for (uint32_t x = 0; x < surface->width; ++x) {
                        std::memcpy(resolve.data.data() +
                                        static_cast<size_t>(sx[x] | yb) * 4u,
                                    row + static_cast<size_t>(x) * 4u, 4u);
                    }
                }
            } else {
                for (uint32_t y = 0; y < surface->height; ++y) {
                    const uint32_t gl_y = surface->height - 1u - y;
                    std::memcpy(resolve.data.data() + static_cast<size_t>(y) * row_bytes,
                                bgra + static_cast<size_t>(gl_y) * row_bytes,
                                row_bytes);
                }
            }
        }
        surface->dirty = false;
        surface->dirty_rect.reset();
        return true;
    }
    bool resolve_resident_depth_surface(uint32_t address, size_t size,
                                        Nv2aResidentDepthResolve& resolve,
                                        std::string& error) override {
        if (!flush_witnessed_batch_for(address, size, error)) return false;
        if (!take_witnessed_flush_error(error)) return false;
        resolve = {};
        if (size == 0) return true;
        ResidentSurface* surface = nullptr;
        for (auto& candidate : resident_surfaces_) {
            if (candidate.depth_dirty &&
                resident_depth_overlaps(candidate, address, size)) {
                surface = &candidate;
                break;
            }
        }
        if (!surface) return true;
        SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
        {
            bool make_current_ok;
            {
                perf::ScopedGlContextTimer gl_timer(perf::kGlContextDepthResolveEnter);
                make_current_ok = SDL_GL_MakeCurrent(window_, context_);
            }
            if (!make_current_ok) {
                error = std::string("OpenGL resident depth resolve context lost: ") +
                        SDL_GetError();
                return false;
            }
        }
        struct ContextRestore {
            SDL_Window* previous_window;
            SDL_GLContext previous_context;
            SDL_Window* fallback_window;
            size_t perf_site;
            ~ContextRestore() {
                restore_previous_gl_context(previous_window, previous_context,
                                            fallback_window, perf_site);
            }
        } restore{previous_window, previous_context, window_,
                  perf::kGlContextDepthResolveRestore};
        // Recycled across resolves for the same reason color_resolve_scratch_ is:
        // a fresh multi-megabyte zero-filled vector per resolve is pure waste,
        // and every element is overwritten by the readback below.
        std::vector<uint32_t>& gl_depth = depth_readback_scratch_;
        gl_depth.resize(static_cast<size_t>(surface->width) * surface->height);
        cache_bind_framebuffer(resident_fbo_);
        // Same rule, the other way round: pair the zeta plane with ITS OWN
        // colour texture so the effective framebuffer size is the surface's.
        cache_framebuffer_color_texture(resident_fbo_, surface->texture);
        cache_framebuffer_depth_texture(resident_fbo_, surface->depth_texture);
        pixel_store_i_(kGlPackAlignment, 4);
        gpu_timed_resolve_read_pixels(
            0, 0, static_cast<int>(surface->width),
            static_cast<int>(surface->height), kGlDepthStencil,
            kGlUnsignedInt248, gl_depth.data(),
            static_cast<uint64_t>(surface->width) * surface->height * 4ull,
            surface->depth_dirty_rect.bytes(4u));
        if (get_error_() != kGlNoError) {
            error = "OpenGL resident zeta readback failed";
            return false;
        }
        resolve.address = surface->depth_address;
        {
            perf::ScopedAdd copy_timer(g_gl_gpu.cpu_copy_ns,
                                       g_gl_gpu_split_calls);
            if (surface->swizzled) {
                // The Morton map is a bijection onto [0, w*h) exactly when
                // both extents are powers of two, which is when every guest
                // word below is written. Where it is not, the zero fill is what
                // keeps an unwritten hole from carrying bytes of a previous
                // resolve, so it stays -- guarded rather than removed.
                resize_or_zero(resolve.data,
                               static_cast<size_t>(surface->width) *
                                   surface->height * sizeof(uint32_t),
                               surface->width, surface->height);
                swizzle_tables_.ensure(surface->width, surface->height);
                const uint32_t* sx = swizzle_tables_.x.data();
                for (uint32_t y = 0; y < surface->height; ++y) {
                    const uint32_t gl_y = surface->height - 1u - y;
                    const uint32_t* src_row =
                        gl_depth.data() + static_cast<size_t>(gl_y) * surface->width;
                    const uint32_t yb = swizzle_tables_.y[y];
                    for (uint32_t x = 0; x < surface->width; ++x) {
                        const uint32_t word = src_row[x];
                        std::memcpy(resolve.data.data() +
                                        static_cast<size_t>(sx[x] | yb) * 4u,
                                    &word, sizeof(word));
                    }
                }
            } else {
                gl_d24s8_to_guest(gl_depth, resolve.data, surface->width,
                                  surface->height);
            }
        }
        surface->depth_dirty = false;
        surface->depth_dirty_rect.reset();
        return true;
    }
    void invalidate_resident_color_surface(uint32_t address, size_t size,
                                           uint32_t cause) override {
        if (size == 0) return;
        flush_witnessed_batch_for_deferred(address, size);
        for (auto& surface : resident_surfaces_) {
            if (resident_overlaps(surface, address, size)) {
                surface.valid = false;
                surface.dirty = false;
                surface.dirty_rect.reset();
                note_invalidate_extent(
                    witnessed_upload_census_.color_invalidate_extent, cause,
                    surface.address, surface.size, address, size);
                stamp_invalidate(color_stamps_, surface.address, cause);
                if (active_resident_ == &surface) active_resident_ = nullptr;
            }
        }
    }
    // beads-xbox.1.15.10 step 1 (overlap). Issue whatever is pending and let
    // the driver submit it. flush_witnessed_batch already ends in a glFlush, so
    // there is never GL work left unsubmitted after this returns; when the
    // batch is empty the call is two compares. Called from Nv2a at the frame
    // boundary so a partial batch at the end of a residency run starts on the
    // GPU immediately rather than waiting for the next consumer.
    void kick_pending_gpu_work() override {
        if (witnessed_batch_count_ == 0) return;
        ++witnessed_flush_kick_;
        flush_witnessed_batch_deferred();
    }
    void set_witnessed_flush_triangles(uint64_t triangles) override {
        witnessed_flush_triangles_ = triangles;
    }
    void set_witnessed_kick_flush(bool on) override {
        witnessed_kick_flush_ = on;
    }
    // The programs are compiled during renderer startup, which happens before
    // the host has parsed its command line, so switching variants means
    // recompiling. That is two shader compiles and a link, once.
    bool set_witnessed_fp32_shader(bool on, std::string& error) override {
        if (on == witnessed_fp32_shader_) return true;
        witnessed_fp32_shader_ = on;
        if (!witnessed_available_) return true;
        if (!enter_witnessed_context(error)) return false;
        if (witnessed_radial_program_ && delete_program_) {
            uniform_location_cache_.erase(witnessed_radial_program_);
            delete_program_(witnessed_radial_program_);
            witnessed_radial_program_ = 0;
        }
        if (witnessed_textured_program_ && delete_program_) {
            uniform_location_cache_.erase(witnessed_textured_program_);
            delete_program_(witnessed_textured_program_);
            witnessed_textured_program_ = 0;
        }
        if (witnessed_projected_program_ && delete_program_) {
            uniform_location_cache_.erase(witnessed_projected_program_);
            delete_program_(witnessed_projected_program_);
            witnessed_projected_program_ = 0;
        }
        // A rebuilt program invalidates the cached "which program is bound"
        // mirror and every cached uniform value on it.
        witnessed_bound_program_ = 0;
        witnessed_radial_.uniforms_valid = false;
        witnessed_textured_.uniforms_valid = false;
        witnessed_projected_.uniforms_valid = false;
        if (!compile_witnessed_programs(error)) {
            witnessed_available_ = false;
            return false;
        }
        return true;
    }
    void set_witnessed_plane_split(bool on) override {
        witnessed_plane_split_ = on;
    }
    void set_witnessed_ssbo_ring(uint32_t slots) override {
        if (slots < 1) slots = 1;
        if (slots > kWitnessedSsboRingMax)
            slots = static_cast<uint32_t>(kWitnessedSsboRingMax);
        witnessed_ssbo_ring_size_ = slots;
    }
    WitnessedFlushStats witnessed_flush_stats() const override {
        WitnessedFlushStats s;
        s.size_flushes = witnessed_flush_size_;
        s.kick_flushes = witnessed_flush_kick_;
        s.gl_flushes = witnessed_gl_flushes_;
        s.bound = witnessed_flush_triangles_;
        s.ssbo_ring = witnessed_ssbo_ring_size_;
        s.kick_flush = witnessed_kick_flush_;
        s.fp32_shader = witnessed_fp32_shader_;
        s.plane_split = witnessed_plane_split_;
        return s;
    }
    bool supports_resident_color_region_upload() const override { return true; }
    bool upload_resident_color_rows(
        uint32_t address, size_t size, uint32_t key_pitch, uint32_t height,
        uint32_t format, uint32_t first_row, uint32_t last_row,
        const std::vector<uint8_t>& guest_bytes, std::string& error) override {
        perf_excl::Scope gl_region(perf_excl::kNv2aGl);
        if (!flush_witnessed_batch_for(address, size, error)) return false;
        ResidentSurface* surface =
            find_resident_surface(address, size, key_pitch, height, format);
        if (!surface || !surface->valid) {
            error = "OpenGL resident color region upload has no matching surface";
            return false;
        }
        if (first_row > last_row || last_row >= surface->height ||
            surface->width == 0) {
            error = "OpenGL resident color region upload has invalid rows";
            return false;
        }
        const uint32_t width = surface->width;
        const uint32_t surface_rows = surface->height;
        const size_t row_bytes = static_cast<size_t>(width) * 4u;
        const uint32_t rows = last_row - first_row + 1u;
        if (guest_bytes.size() != row_bytes * rows) {
            error = "OpenGL resident color region upload has invalid byte count";
            return false;
        }
        // Guest rows are top-down; GL texel rows are bottom-up. Stage the
        // covered rows in GL order. Byte order stays guest ARGB (B,G,R,A in
        // memory): uploading with kGlBgra produces exactly the texels the
        // full-surface kGlRgba swizzled path stores.
        region_staging_.resize(row_bytes * rows);
        for (uint32_t index = 0; index < rows; ++index) {
            const uint32_t guest_row = last_row - index;  // bottom-up target
            const size_t source = static_cast<size_t>(guest_row - first_row) *
                                  row_bytes;
            std::memcpy(region_staging_.data() + static_cast<size_t>(index) *
                                                  row_bytes,
                        guest_bytes.data() + source, row_bytes);
        }
        SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
        {
            bool make_current_ok;
            {
                perf::ScopedGlContextTimer gl_timer(perf::kGlContextRegionUploadEnter);
                make_current_ok = SDL_GL_MakeCurrent(window_, context_);
            }
            if (!make_current_ok) {
                error = std::string("OpenGL resident region context lost: ") +
                        SDL_GetError();
                return false;
            }
        }
        struct ContextRestore {
            SDL_Window* previous_window;
            SDL_GLContext previous_context;
            SDL_Window* fallback_window;
            size_t perf_site;
            ~ContextRestore() {
                restore_previous_gl_context(previous_window, previous_context,
                                            fallback_window, perf_site);
            }
        } restore{previous_window, previous_context, window_,
                  perf::kGlContextRegionUploadRestore};
        active_texture_(kGlTexture0);
        bind_texture_(kGlTexture2D, surface->texture);
        if (gl_state_.valid) {
            gl_state_.active_unit = kGlTexture0;
            gl_state_.unit2d_binding[0] = surface->texture;
        }
        pixel_store_i_(kGlUnpackAlignment, 1);
        gpu_timed_tex_sub_image_2d(
            kGlTexture2D, 0, 0,
            static_cast<int>(surface_rows - 1 - last_row),
            static_cast<int>(width), static_cast<int>(rows), kGlBgra,
            kGlUnsignedByte, region_staging_.data());
        if (get_error_() != kGlNoError) {
            error = "OpenGL resident color region upload failed";
            return false;
        }
        // Preserve the overlap contract: any other resident surface covering
        // part of this address range is now stale relative to guest memory.
        for (auto& other : resident_surfaces_) {
            if (&other == surface) continue;
            if (resident_overlaps(other, address, size)) {
                other.valid = false;
                other.dirty = false;
                other.dirty_rect.reset();
                stamp_invalidate(
                    color_stamps_, other.address,
                    nv2a_invalidate_token(kNv2aInvalidateRegionOverlap));
                if (active_resident_ == &other) active_resident_ = nullptr;
            }
        }
        return true;
    }
    void invalidate_resident_depth_surface(uint32_t address, size_t size,
                                           uint32_t cause) override {
        flush_witnessed_batch_for_deferred(address, size);
        if (size == 0) return;
        for (auto& surface : resident_surfaces_) {
            if (resident_depth_overlaps(surface, address, size)) {
                surface.depth_valid = false;
                surface.depth_dirty = false;
                surface.depth_dirty_rect.reset();
                note_invalidate_extent(
                    witnessed_upload_census_.depth_invalidate_extent, cause,
                    surface.depth_address, surface.depth_size, address, size);
                stamp_invalidate(depth_stamps_, surface.depth_address, cause);
                if (active_depth_resident_ == &surface) active_depth_resident_ = nullptr;
            }
        }
    }
    bool submit_generic_combiner(const Nv2aGenericCombinerPacket& packet,
                                 Nv2aGenericCombinerResult& result,
                                 std::string& error) override {
        if (!flush_witnessed_batch(error)) return false;
        if (!take_witnessed_flush_error(error)) return false;
        perf_excl::Scope gl_region(perf_excl::kNv2aGl);
        SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
        {
            bool make_current_ok;
            {
                perf::ScopedGlContextTimer gl_timer(perf::kGlContextComputeSubmitEnter);
                make_current_ok = SDL_GL_MakeCurrent(window_, context_);
            }
            if (!make_current_ok) {
                error = std::string("OpenGL compute context lost: ") + SDL_GetError();
                return false;
            }
        }
        struct ContextRestore {
            SDL_Window* previous_window;
            SDL_GLContext previous_context;
            SDL_Window* fallback_window;
            size_t perf_site;
            ~ContextRestore() {
                restore_previous_gl_context(previous_window, previous_context,
                                            fallback_window, perf_site);
            }
        } restore{previous_window, previous_context, window_,
                  perf::kGlContextComputeSubmitRestore};
        if (packet.compute_fixture ==
            Nv2aGenericCombinerPacket::ComputeFixture::kResidentPrimaryColorTriangles) {
            if (packet.color_input.size() % sizeof(uint32_t) != 0 ||
                packet.color_input.size() !=
                    static_cast<size_t>(packet.surface_pitch & 0xffffu) *
                        packet.surface_height) {
                error = "OpenGL primary-color fixture received invalid color surface";
                return false;
            }
            unsigned int* buffers = [&] {
                for (auto& ssbo : compute_staging_ssbo_) {
                    if (!ssbo) gen_buffers_(1, &ssbo);
                }
                return compute_staging_ssbo_.data();
            }();
            auto upload_buffer = [&](unsigned int binding, unsigned int buffer,
                                     const void* data, size_t size) {
                bind_buffer_(kGlShaderStorageBuffer, buffer);
                gpu_timed_buffer_data(kGlShaderStorageBuffer,
                                      static_cast<std::ptrdiff_t>(size), data,
                                      kGlDynamicCopy);
                bind_buffer_base_(kGlShaderStorageBuffer, binding, buffer);
            };
            std::vector<float> vertex_data(packet.vertices.size() * 5u, 0.0f);
            std::vector<int32_t> xy_data(packet.vertices.size() * 2u, 0);
            for (size_t vertex = 0; vertex < packet.vertices.size(); ++vertex) {
                for (size_t component = 0; component < 4; ++component) {
                    vertex_data[vertex * 5u + component] =
                        packet.vertices[vertex].diffuse[component];
                }
                vertex_data[vertex * 5u + 4u] =
                    packet.vertices[vertex].position[3];
                xy_data[vertex * 2u + 0u] = static_cast<int32_t>(
                    std::llround(packet.vertices[vertex].position[0] * 16.0f));
                xy_data[vertex * 2u + 1u] = static_cast<int32_t>(
                    std::llround(packet.vertices[vertex].position[1] * 16.0f));
            }
            upload_buffer(0, buffers[0], packet.color_input.data(),
                          packet.color_input.size());
            upload_buffer(1, buffers[1], vertex_data.data(),
                          vertex_data.size() * sizeof(float));
            upload_buffer(2, buffers[2], xy_data.data(),
                          xy_data.size() * sizeof(int32_t));
            use_program_(primary_color_program_);
            uniform_1i_(cached_uniform_location(primary_color_program_, "surface_width"),
                        static_cast<int>((packet.surface_pitch & 0xffffu) / 4u));
            uniform_1i_(cached_uniform_location(primary_color_program_, "surface_height"),
                        static_cast<int>(packet.surface_height));
            uniform_1i_(cached_uniform_location(primary_color_program_,
                                          "surface_pitch_words"),
                        static_cast<int>((packet.surface_pitch & 0xffffu) / 4u));
            uniform_1i_(cached_uniform_location(primary_color_program_, "triangle_count"),
                        static_cast<int>(packet.vertices.size() / 3u));
            uniform_2i_(cached_uniform_location(primary_color_program_, "clip_origin"),
                        static_cast<int>(packet.fixture_clip_x),
                        static_cast<int>(packet.fixture_clip_y));
            uniform_2i_(cached_uniform_location(primary_color_program_, "clip_extent"),
                        static_cast<int>(packet.fixture_clip_width),
                        static_cast<int>(packet.fixture_clip_height));
            uniform_1ui_(cached_uniform_location(primary_color_program_, "blend_src"),
                         packet.fixture_blend_enabled ? packet.fixture_blend_src : 1u);
            uniform_1ui_(cached_uniform_location(primary_color_program_, "blend_dst"),
                         packet.fixture_blend_enabled ? packet.fixture_blend_dst : 0u);
            uniform_1ui_(cached_uniform_location(primary_color_program_,
                                           "color_format_x8"),
                         packet.fixture_color_format_x8 ? 1u : 0u);
            gpu_timed_dispatch_compute(
                ((packet.surface_pitch & 0xffffu) / 4u + 7u) / 8u,
                (packet.surface_height + 7u) / 8u, 1);
            memory_barrier_(kGlShaderStorageBarrierBit | kGlBufferUpdateBarrierBit);
            if (get_error_() != kGlNoError) {
                error = "OpenGL primary-color fixture dispatch failed";
                return false;
            }
            result.color_output.resize(packet.color_input.size());
            bind_buffer_(kGlShaderStorageBuffer, buffers[0]);
            get_buffer_sub_data_(kGlShaderStorageBuffer, 0,
                                 static_cast<std::ptrdiff_t>(
                                     result.color_output.size()),
                                 result.color_output.data());
            if (get_error_() != kGlNoError) {
                error = "OpenGL primary-color fixture readback failed";
                return false;
            }
            result.triangles = packet.vertices.size() / 3u;
            result.fragments = 0;
            result.writes = 0;
            return true;
        }
        if (packet.compute_fixture ==
            Nv2aGenericCombinerPacket::ComputeFixture::kTextureModulateTriangle) {
            if (packet.color_input.size() % sizeof(uint32_t) != 0 ||
                packet.color_input.size() !=
                    static_cast<size_t>(packet.surface_pitch & 0xffffu) *
                        packet.surface_height ||
                packet.textures[0].input.size() % sizeof(uint32_t) != 0) {
                error = "OpenGL texture-modulate fixture received invalid surfaces";
                return false;
            }
            const bool texture_swizzled =
                packet.textures[0].layout ==
                Nv2aGenericCombinerPacket::TextureLayout::kSwizzled;
            const size_t expected_texture_size = texture_swizzled
                ? static_cast<size_t>(packet.textures[0].width) *
                      packet.textures[0].height * sizeof(uint32_t)
                : static_cast<size_t>(packet.textures[0].pitch) *
                      packet.textures[0].height;
            if (packet.textures[0].input.size() < expected_texture_size) {
                error = "OpenGL texture-modulate fixture received invalid texture";
                return false;
            }
            unsigned int* buffers = [&] {
                for (auto& ssbo : compute_staging_ssbo_) {
                    if (!ssbo) gen_buffers_(1, &ssbo);
                }
                return compute_staging_ssbo_.data();
            }();
            auto upload_buffer = [&](unsigned int binding, unsigned int buffer,
                                     const void* data, size_t size) {
                bind_buffer_(kGlShaderStorageBuffer, buffer);
                gpu_timed_buffer_data(kGlShaderStorageBuffer,
                                      static_cast<std::ptrdiff_t>(size), data,
                                      kGlDynamicCopy);
                bind_buffer_base_(kGlShaderStorageBuffer, binding, buffer);
            };
            std::vector<float> vertex_data(packet.vertices.size() * 8u, 0.0f);
            std::vector<int32_t> xy_data(packet.vertices.size() * 2u, 0);
            for (size_t vertex = 0; vertex < packet.vertices.size(); ++vertex) {
                for (size_t component = 0; component < 4; ++component) {
                    vertex_data[vertex * 8u + component] =
                        packet.vertices[vertex].diffuse[component];
                }
                vertex_data[vertex * 8u + 4u] =
                    packet.vertices[vertex].texture[0][0];
                vertex_data[vertex * 8u + 5u] =
                    packet.vertices[vertex].texture[0][1];
                vertex_data[vertex * 8u + 6u] =
                    packet.vertices[vertex].texture[0][3];
                vertex_data[vertex * 8u + 7u] =
                    packet.vertices[vertex].position[3];
                xy_data[vertex * 2u + 0u] = static_cast<int32_t>(
                    std::llround(packet.vertices[vertex].position[0] * 16.0f));
                xy_data[vertex * 2u + 1u] = static_cast<int32_t>(
                    std::llround(packet.vertices[vertex].position[1] * 16.0f));
            }
            static constexpr uint32_t kTextureComputeTileSize = 16;
            const uint32_t surface_width_words =
                (packet.surface_pitch & 0xffffu) / 4u;
            const uint32_t tile_cols =
                (surface_width_words + kTextureComputeTileSize - 1u) /
                kTextureComputeTileSize;
            const uint32_t tile_rows =
                (packet.surface_height + kTextureComputeTileSize - 1u) /
                kTextureComputeTileSize;
            const bool use_tile_bins =
                packet.fixture_use_tile_bins && tile_cols != 0 && tile_rows != 0;
            std::vector<uint32_t> tile_offsets;
            std::vector<uint32_t> tile_triangles;
            if (use_tile_bins) {
                struct TileBounds {
                    uint32_t min_x = 0;
                    uint32_t max_x = 0;
                    uint32_t min_y = 0;
                    uint32_t max_y = 0;
                    bool visible = false;
                };
                const uint32_t tile_count = tile_cols * tile_rows;
                std::vector<TileBounds> triangle_bounds(packet.vertices.size() / 3u);
                std::vector<uint32_t> tile_counts(tile_count, 0);
                const int32_t clip_min_x =
                    static_cast<int32_t>(packet.fixture_clip_x);
                const int32_t clip_min_y =
                    static_cast<int32_t>(packet.fixture_clip_y);
                const int32_t clip_max_x = static_cast<int32_t>(
                    packet.fixture_clip_x + packet.fixture_clip_width - 1u);
                const int32_t clip_max_y = static_cast<int32_t>(
                    packet.fixture_clip_y + packet.fixture_clip_height - 1u);
                for (uint32_t tri = 0; tri < packet.vertices.size() / 3u; ++tri) {
                    const int64_t ax = xy_data[tri * 6u + 0u];
                    const int64_t ay = xy_data[tri * 6u + 1u];
                    const int64_t bx = xy_data[tri * 6u + 2u];
                    const int64_t by = xy_data[tri * 6u + 3u];
                    const int64_t cx = xy_data[tri * 6u + 4u];
                    const int64_t cy = xy_data[tri * 6u + 5u];
                    const int32_t min_x = std::max<int32_t>(
                        clip_min_x,
                        static_cast<int32_t>(std::ceil(
                            (static_cast<double>(std::min({ax, bx, cx})) - 8.0) /
                            16.0)));
                    const int32_t max_x = std::min<int32_t>(
                        clip_max_x,
                        static_cast<int32_t>(std::floor(
                            (static_cast<double>(std::max({ax, bx, cx})) - 8.0) /
                            16.0)));
                    const int32_t min_y = std::max<int32_t>(
                        clip_min_y,
                        static_cast<int32_t>(std::ceil(
                            (static_cast<double>(std::min({ay, by, cy})) - 8.0) /
                            16.0)));
                    const int32_t max_y = std::min<int32_t>(
                        clip_max_y,
                        static_cast<int32_t>(std::floor(
                            (static_cast<double>(std::max({ay, by, cy})) - 8.0) /
                            16.0)));
                    if (max_x < min_x || max_y < min_y) continue;
                    const uint32_t tile_min_x =
                        static_cast<uint32_t>(min_x) / kTextureComputeTileSize;
                    const uint32_t tile_max_x =
                        static_cast<uint32_t>(max_x) / kTextureComputeTileSize;
                    const uint32_t tile_min_y =
                        static_cast<uint32_t>(min_y) / kTextureComputeTileSize;
                    const uint32_t tile_max_y =
                        static_cast<uint32_t>(max_y) / kTextureComputeTileSize;
                    triangle_bounds[tri] =
                        TileBounds{tile_min_x, tile_max_x, tile_min_y, tile_max_y, true};
                    for (uint32_t ty = tile_min_y; ty <= tile_max_y; ++ty) {
                        for (uint32_t tx = tile_min_x; tx <= tile_max_x; ++tx) {
                            ++tile_counts[ty * tile_cols + tx];
                        }
                    }
                }
                tile_offsets.reserve(static_cast<size_t>(tile_count) + 1u);
                tile_offsets.push_back(0);
                for (uint32_t count : tile_counts) {
                    tile_offsets.push_back(tile_offsets.back() + count);
                }
                tile_triangles.resize(tile_offsets.back());
                std::vector<uint32_t> tile_cursor = tile_offsets;
                for (uint32_t tri = 0; tri < triangle_bounds.size(); ++tri) {
                    const TileBounds bounds = triangle_bounds[tri];
                    if (!bounds.visible) continue;
                    for (uint32_t ty = bounds.min_y; ty <= bounds.max_y; ++ty) {
                        for (uint32_t tx = bounds.min_x; tx <= bounds.max_x; ++tx) {
                            const uint32_t tile = ty * tile_cols + tx;
                            tile_triangles[tile_cursor[tile]++] = tri;
                        }
                    }
                }
            } else {
                tile_offsets = {0u, static_cast<uint32_t>(packet.vertices.size() / 3u)};
            }
            if (tile_triangles.empty()) tile_triangles.push_back(0);
            upload_buffer(0, buffers[0], packet.color_input.data(),
                          packet.color_input.size());
            upload_buffer(1, buffers[1], vertex_data.data(),
                          vertex_data.size() * sizeof(float));
            upload_buffer(2, buffers[2], packet.textures[0].input.data(),
                          packet.textures[0].input.size());
            upload_buffer(3, buffers[3], xy_data.data(),
                          xy_data.size() * sizeof(int32_t));
            upload_buffer(4, buffers[4], tile_offsets.data(),
                          tile_offsets.size() * sizeof(uint32_t));
            upload_buffer(5, buffers[5], tile_triangles.data(),
                          tile_triangles.size() * sizeof(uint32_t));
            use_program_(texture_modulate_program_);
            uniform_1i_(cached_uniform_location(texture_modulate_program_, "surface_width"),
                        static_cast<int>((packet.surface_pitch & 0xffffu) / 4u));
            uniform_1i_(cached_uniform_location(texture_modulate_program_, "surface_height"),
                        static_cast<int>(packet.surface_height));
            uniform_1i_(cached_uniform_location(texture_modulate_program_,
                                          "surface_pitch_words"),
                        static_cast<int>((packet.surface_pitch & 0xffffu) / 4u));
            uniform_1i_(cached_uniform_location(texture_modulate_program_, "texture_width"),
                        static_cast<int>(packet.textures[0].width));
            uniform_1i_(cached_uniform_location(texture_modulate_program_, "texture_height"),
                        static_cast<int>(packet.textures[0].height));
            uniform_1i_(cached_uniform_location(texture_modulate_program_,
                                          "texture_pitch_words"),
                        static_cast<int>(texture_swizzled
                                             ? packet.textures[0].width
                                             : packet.textures[0].pitch / 4u));
            uniform_1ui_(cached_uniform_location(texture_modulate_program_,
                                           "texture_swizzled"),
                         texture_swizzled ? 1u : 0u);
            const uint32_t texture_address = packet.textures[0].address_mode;
            const bool texture_repeat =
                (texture_address & 0xffu) == 1u &&
                ((texture_address >> 8) & 0xffu) == 1u;
            uniform_1ui_(cached_uniform_location(texture_modulate_program_,
                                           "texture_repeat"),
                         texture_repeat ? 1u : 0u);
            uniform_1ui_(cached_uniform_location(texture_modulate_program_,
                                           "texture_constant_color_alpha"),
                         packet.fixture_texture_constant_color_alpha ? 1u : 0u);
            uniform_1ui_(cached_uniform_location(texture_modulate_program_, "constant_argb"),
                         packet.fixture_constant_argb);
            uniform_1i_(cached_uniform_location(texture_modulate_program_, "triangle_count"),
                        static_cast<int>(packet.vertices.size() / 3u));
            uniform_2i_(cached_uniform_location(texture_modulate_program_, "clip_origin"),
                        static_cast<int>(packet.fixture_clip_x),
                        static_cast<int>(packet.fixture_clip_y));
            uniform_2i_(cached_uniform_location(texture_modulate_program_, "clip_extent"),
                        static_cast<int>(packet.fixture_clip_width),
                        static_cast<int>(packet.fixture_clip_height));
            uniform_1ui_(cached_uniform_location(texture_modulate_program_, "blend_src"),
                         packet.fixture_blend_enabled ? packet.fixture_blend_src : 1u);
            uniform_1ui_(cached_uniform_location(texture_modulate_program_, "blend_dst"),
                         packet.fixture_blend_enabled ? packet.fixture_blend_dst : 0u);
            uniform_1ui_(cached_uniform_location(texture_modulate_program_,
                                           "color_format_x8"),
                         packet.fixture_color_format_x8 ? 1u : 0u);
            uniform_1ui_(cached_uniform_location(texture_modulate_program_,
                                           "use_tile_bins"),
                         use_tile_bins ? 1u : 0u);
            uniform_1i_(cached_uniform_location(texture_modulate_program_, "tile_size"),
                        static_cast<int>(kTextureComputeTileSize));
            uniform_1i_(cached_uniform_location(texture_modulate_program_, "tile_cols"),
                        static_cast<int>(tile_cols));
            gpu_timed_dispatch_compute(
                ((packet.surface_pitch & 0xffffu) / 4u + 7u) / 8u,
                (packet.surface_height + 7u) / 8u, 1);
            memory_barrier_(kGlShaderStorageBarrierBit | kGlBufferUpdateBarrierBit);
            if (get_error_() != kGlNoError) {
                error = "OpenGL texture-modulate fixture dispatch failed";
                return false;
            }
            result.color_output.resize(packet.color_input.size());
            bind_buffer_(kGlShaderStorageBuffer, buffers[0]);
            get_buffer_sub_data_(kGlShaderStorageBuffer, 0,
                                 static_cast<std::ptrdiff_t>(
                                     result.color_output.size()),
                                 result.color_output.data());
            if (get_error_() != kGlNoError) {
                error = "OpenGL texture-modulate fixture readback failed";
                return false;
            }
            result.triangles = 1;
            result.fragments = 0;
            result.writes = 0;
            return true;
        }
        if (packet.compute_fixture ==
            Nv2aGenericCombinerPacket::ComputeFixture::kDashboardProject2dFan) {
            if (packet.color_input.size() != 0x1400u * 480u ||
                packet.depth_input.size() != 0x1400u * 480u) {
                error = "OpenGL dashboard fixture received invalid surface sizes";
                return false;
            }
            unsigned int* buffers = [&] {
                for (auto& ssbo : compute_staging_ssbo_) {
                    if (!ssbo) gen_buffers_(1, &ssbo);
                }
                return compute_staging_ssbo_.data();
            }();
            auto upload_buffer = [&](unsigned int binding, unsigned int buffer,
                                     const void* data, size_t size) {
                bind_buffer_(kGlShaderStorageBuffer, buffer);
                gpu_timed_buffer_data(kGlShaderStorageBuffer,
                                      static_cast<std::ptrdiff_t>(size), data,
                                      kGlDynamicCopy);
                bind_buffer_base_(kGlShaderStorageBuffer, binding, buffer);
            };
            std::vector<float> vertex_data(4u * 4u * 4u, 0.0f);
            for (size_t vertex = 0; vertex < 4; ++vertex) {
                for (size_t stage = 0; stage < 4; ++stage) {
                    for (size_t component = 0; component < 4; ++component) {
                        vertex_data[vertex * 16u + stage * 4u + component] =
                            packet.vertices[vertex].texture[stage][component];
                    }
                }
            }
            upload_buffer(0, buffers[0], packet.color_input.data(),
                          packet.color_input.size());
            upload_buffer(1, buffers[1], vertex_data.data(),
                          vertex_data.size() * sizeof(float));
            for (size_t stage = 0; stage < 4; ++stage) {
                upload_buffer(static_cast<unsigned int>(2 + stage),
                              buffers[2 + stage],
                              packet.textures[stage].input.data(),
                              packet.textures[stage].input.size());
            }
            auto fixed_xy = [&](size_t vertex, size_t component) {
                return static_cast<int>(std::lround(
                    static_cast<double>(packet.vertices[vertex].position[component]) *
                    16.0));
            };
            use_program_(dashboard_program_);
            uniform_1i_(cached_uniform_location(dashboard_program_, "surface_width"), 1280);
            uniform_1i_(cached_uniform_location(dashboard_program_, "surface_height"), 480);
            uniform_1i_(cached_uniform_location(dashboard_program_, "surface_pitch_words"),
                        0x1400 / 4);
            uniform_2i_(cached_uniform_location(dashboard_program_, "vertex_a"),
                        fixed_xy(0, 0), fixed_xy(0, 1));
            uniform_2i_(cached_uniform_location(dashboard_program_, "vertex_b"),
                        fixed_xy(1, 0), fixed_xy(1, 1));
            uniform_2i_(cached_uniform_location(dashboard_program_, "vertex_c"),
                        fixed_xy(2, 0), fixed_xy(2, 1));
            uniform_2i_(cached_uniform_location(dashboard_program_, "vertex_d"),
                        fixed_xy(3, 0), fixed_xy(3, 1));
            uniform_1ui_(cached_uniform_location(dashboard_program_, "factor_stage2"),
                         packet.fixture_factor0_stage2_argb);
            uniform_1ui_(cached_uniform_location(dashboard_program_, "factor_stage4"),
                         packet.fixture_factor0_stage4_argb);
            gpu_timed_dispatch_compute((1280 + 7) / 8, (480 + 7) / 8, 1);
            memory_barrier_(kGlShaderStorageBarrierBit | kGlBufferUpdateBarrierBit);
            if (get_error_() != kGlNoError) {
                error = "OpenGL dashboard fixture dispatch failed";
                return false;
            }
            result.color_output.resize(packet.color_input.size());
            bind_buffer_(kGlShaderStorageBuffer, buffers[0]);
            get_buffer_sub_data_(kGlShaderStorageBuffer, 0,
                                 static_cast<std::ptrdiff_t>(
                                     result.color_output.size()),
                                 result.color_output.data());
            if (get_error_() != kGlNoError) {
                error = "OpenGL dashboard fixture readback failed";
                return false;
            }
            result.depth_output = packet.depth_input;
            result.triangles = 2;
            result.fragments = 1280ull * 480ull;
            result.writes = 1280ull * 480ull;
            return true;
        }
        if (packet.color_input.size() % sizeof(uint32_t) != 0 ||
            packet.color_input.size() !=
                static_cast<size_t>(packet.surface_pitch & 0xffffu) * packet.surface_height) {
            error = "OpenGL compute fixture received invalid color surface";
            return false;
        }
        unsigned int buffer = 0;
        gen_buffers_(1, &buffer);
        bind_buffer_(kGlShaderStorageBuffer, buffer);
        gpu_timed_buffer_data(
            kGlShaderStorageBuffer,
            static_cast<std::ptrdiff_t>(packet.color_input.size()),
            packet.color_input.data(), kGlDynamicCopy);
        bind_buffer_base_(kGlShaderStorageBuffer, 0, buffer);
        use_program_(fixture_program_);
        uniform_1i_(cached_uniform_location(fixture_program_, "surface_width"),
                    static_cast<int>((packet.surface_pitch & 0xffffu) / 4));
        uniform_1i_(cached_uniform_location(fixture_program_, "surface_height"),
                    static_cast<int>(packet.surface_height));
        uniform_1i_(cached_uniform_location(fixture_program_, "surface_pitch_words"),
                    static_cast<int>((packet.surface_pitch & 0xffffu) / 4));
        uniform_2i_(cached_uniform_location(fixture_program_, "vertex_a"),
                    packet.fixture_xy_16[0], packet.fixture_xy_16[1]);
        uniform_2i_(cached_uniform_location(fixture_program_, "vertex_b"),
                    packet.fixture_xy_16[2], packet.fixture_xy_16[3]);
        uniform_2i_(cached_uniform_location(fixture_program_, "vertex_c"),
                    packet.fixture_xy_16[4], packet.fixture_xy_16[5]);
        uniform_2i_(cached_uniform_location(fixture_program_, "clip_origin"),
                    static_cast<int>(packet.fixture_clip_x),
                    static_cast<int>(packet.fixture_clip_y));
        uniform_2i_(cached_uniform_location(fixture_program_, "clip_extent"),
                    static_cast<int>(packet.fixture_clip_width),
                    static_cast<int>(packet.fixture_clip_height));
        uniform_1ui_(cached_uniform_location(fixture_program_, "source_argb"),
                     packet.fixture_source_argb);
        gpu_timed_dispatch_compute(((packet.surface_pitch & 0xffffu) / 4 + 7) / 8,
                                   (packet.surface_height + 7) / 8, 1);
        memory_barrier_(kGlShaderStorageBarrierBit | kGlBufferUpdateBarrierBit);
        if (get_error_() != kGlNoError) {
            delete_buffers_(1, &buffer);
            error = "OpenGL compute fixture dispatch failed";
            return false;
        }
        result.color_output.resize(packet.color_input.size());
        get_buffer_sub_data_(kGlShaderStorageBuffer, 0,
                             static_cast<std::ptrdiff_t>(result.color_output.size()),
                             result.color_output.data());
        delete_buffers_(1, &buffer);
        if (get_error_() != kGlNoError) {
            error = "OpenGL compute fixture readback failed";
            return false;
        }
        result.triangles = 1;
        // These counts are observability only; the software-equivalence test gates
        // pixels byte-for-byte, and later generic uniforms will supply exact counts.
        result.fragments = 0;
        result.writes = 0;
        return true;
    }
    bool initialize(std::string& error) {
        if (!(load_gl(create_shader_, "glCreateShader") &&
               load_gl(shader_source_, "glShaderSource") &&
               load_gl(compile_shader_, "glCompileShader") &&
               load_gl(get_shader_iv_, "glGetShaderiv") &&
               load_gl(get_shader_log_, "glGetShaderInfoLog") &&
               load_gl(delete_shader_, "glDeleteShader") &&
               load_gl(create_program_, "glCreateProgram") &&
               load_gl(attach_shader_, "glAttachShader") &&
               load_gl(link_program_, "glLinkProgram") &&
               load_gl(get_program_iv_, "glGetProgramiv") &&
               load_gl(get_program_log_, "glGetProgramInfoLog") &&
               load_gl(delete_program_, "glDeleteProgram") &&
               load_gl(use_program_, "glUseProgram") &&
               load_gl(uniform_location_, "glGetUniformLocation") &&
               load_gl(uniform_1i_, "glUniform1i") &&
               load_gl(uniform_1f_, "glUniform1f") &&
               load_gl(uniform_1ui_, "glUniform1ui") &&
               load_gl(uniform_2i_, "glUniform2i") &&
               load_gl(uniform_4i_, "glUniform4i") &&
               load_gl(uniform_2f_, "glUniform2f") &&
               load_gl(uniform_4f_, "glUniform4f") &&
               load_gl(gen_buffers_, "glGenBuffers") &&
               load_gl(bind_buffer_, "glBindBuffer") &&
               load_gl(buffer_data_, "glBufferData") &&
               load_gl(bind_buffer_base_, "glBindBufferBase") &&
               load_gl(dispatch_compute_, "glDispatchCompute") &&
               load_gl(memory_barrier_, "glMemoryBarrier") &&
               load_gl(get_buffer_sub_data_, "glGetBufferSubData") &&
               load_gl(delete_buffers_, "glDeleteBuffers") &&
               load_gl(gen_textures_, "glGenTextures") &&
               load_gl(bind_texture_, "glBindTexture") &&
               load_gl(tex_image_2d_, "glTexImage2D") &&
               load_gl(tex_sub_image_2d_, "glTexSubImage2D") &&
               load_gl(tex_parameter_i_, "glTexParameteri") &&
               load_gl(delete_textures_, "glDeleteTextures") &&
               load_gl(active_texture_, "glActiveTexture") &&
               load_gl(bind_image_texture_, "glBindImageTexture") &&
               load_gl(gen_framebuffers_, "glGenFramebuffers") &&
               load_gl(bind_framebuffer_, "glBindFramebuffer") &&
               load_gl(framebuffer_texture_2d_, "glFramebufferTexture2D") &&
               load_gl(check_framebuffer_status_, "glCheckFramebufferStatus") &&
               load_gl(delete_framebuffers_, "glDeleteFramebuffers") &&
               load_gl(viewport_, "glViewport") &&
               load_gl(scissor_, "glScissor") &&
               load_gl(enable_, "glEnable") &&
               load_gl(disable_, "glDisable") &&
#if XLLE_NATIVE_DRAW
               load_gl(native_cull_face_, "glCullFace") &&
               load_gl(native_front_face_, "glFrontFace") &&
#endif
               load_gl(depth_func_, "glDepthFunc") &&
               load_gl(depth_mask_, "glDepthMask") &&
               load_gl(blend_func_, "glBlendFunc") &&
               load_gl(blend_func_separate_, "glBlendFuncSeparate") &&
               load_gl(blend_equation_, "glBlendEquation") &&
               load_gl(color_mask_, "glColorMask") &&
               load_gl(gen_vertex_arrays_, "glGenVertexArrays") &&
               load_gl(bind_vertex_array_, "glBindVertexArray") &&
               load_gl(delete_vertex_arrays_, "glDeleteVertexArrays") &&
               load_gl(vertex_attrib_pointer_, "glVertexAttribPointer") &&
               load_gl(enable_vertex_attrib_array_, "glEnableVertexAttribArray") &&
               load_gl(disable_vertex_attrib_array_, "glDisableVertexAttribArray") &&
               load_gl(draw_arrays_, "glDrawArrays") &&
               load_gl(read_pixels_, "glReadPixels") &&
               load_gl(pixel_store_i_, "glPixelStorei") &&
               load_gl(get_error_, "glGetError"))) {
            error = "OpenGL 4.3 compute fixture is missing a required entry point";
            return false;
        }
        load_gl(uniform_4fv_, "glUniform4fv");
        // beads-xbox.1.14.6: GPU timing is OPTIONAL. A driver without timer
        // queries or sync objects must still render; the report then carries
        // gpu_timing_available=0 so an empty GPU column is never misread as
        // "the GPU did no work".
        gpu_timing_available_ =
            load_gl(gen_queries_, "glGenQueries") &&
            load_gl(delete_queries_, "glDeleteQueries") &&
            load_gl(begin_query_, "glBeginQuery") &&
            load_gl(end_query_, "glEndQuery") &&
            load_gl(get_query_object_uiv_, "glGetQueryObjectuiv") &&
            load_gl(get_query_object_ui64v_, "glGetQueryObjectui64v") &&
            load_gl(fence_sync_, "glFenceSync") &&
            load_gl(client_wait_sync_, "glClientWaitSync") &&
            load_gl(delete_sync_, "glDeleteSync");
        // Separate from the conjunction above on purpose: glFinish is core GL 1.0
        // and its absence must not turn GPU timing off.
        load_gl(finish_, "glFinish");
        load_gl(flush_, "glFlush");
        // Stage (f): the witnessed-profile GPU executor is OPTIONAL in exactly
        // the way GPU timing is. A driver missing any of these still renders
        // everything through the software path; the device-side predicate asks
        // supports_witnessed_profile() and gets false.
        witnessed_available_ =
            load_gl(clear_, "glClear") &&
            load_gl(clear_color_, "glClearColor") &&
            load_gl(clear_depth_, "glClearDepth") &&
            load_gl(clear_stencil_, "glClearStencil") &&
            load_gl(stencil_mask_, "glStencilMask") &&
            load_gl(uniform_3f_, "glUniform3f") &&
            load_gl(buffer_sub_data_, "glBufferSubData");
        // The generated-combiner executor needs everything the witnessed one
        // needs (int64 in GLSL, SSBOs, glClear) plus glBlendFuncSeparate for
        // the X8R8G8B8 alpha rule. It is optional in exactly the same way.
        generated_available_ = witnessed_available_ &&
                               blend_func_separate_ != nullptr;
        g_gl_gpu.gpu_timing_available.store(gpu_timing_available_ ? 1u : 0u,
                                           std::memory_order_relaxed);
        g_gl_gpu.gpu_timing_enabled.store(gpu_timers_enabled_ ? 1u : 0u,
                                         std::memory_order_relaxed);
        if (!(compile_fixture(error) && compile_primary_color(error) &&
              compile_texture_modulate(error) && compile_dashboard(error) &&
              compile_resident_primary(error) &&
              compile_resident_alpha(error) &&
              compile_resident_texture_modulate(error) &&
              compile_resident_dashboard(error))) {
            return false;
        }
        if (witnessed_available_) {
            std::string witnessed_error;
            if (!compile_witnessed_programs(witnessed_error)) {
                // Not fatal: the witnessed GPU executor turns itself off and
                // says why once, and every draw keeps taking the software path.
                witnessed_available_ = false;
                std::fprintf(stderr,
                             "[nv2a-renderer] witnessed-profile GPU executor "
                             "unavailable: %s\n",
                             witnessed_error.c_str());
            }
        }
        prime_gl_state_cache();
        return true;
    }

    // Establish a fully known GL state and mark the mirror valid. Issued once
    // right after context setup; nothing else uses this context beforehand.
    void prime_gl_state_cache() {
        bind_framebuffer_(kGlFramebuffer, 0);
        gl_state_.bound_fbo = 0;
        gl_state_.fbo_color_texture = 0;
        gl_state_.fbo_depth_texture = 0;
        viewport_(0, 0, 1, 1);
        gl_state_.viewport_x = 0; gl_state_.viewport_y = 0;
        gl_state_.viewport_w = 1; gl_state_.viewport_h = 1;
        disable_(kGlDepthTest);
        gl_state_.depth_test = false;
        depth_func_(kGlAlways);
        gl_state_.depth_func = kGlAlways;
        depth_mask_(1);
        gl_state_.depth_writemask = true;
        disable_(kGlScissorTest);
        gl_state_.scissor_test = false;
        gl_state_.scissor_x = 0; gl_state_.scissor_y = 0;
        gl_state_.scissor_w = 0; gl_state_.scissor_h = 0;
        disable_(kGlBlend);
        gl_state_.blend = false;
        gl_state_.blend_src = 0; gl_state_.blend_dst = 0;
        color_mask_(1, 1, 1, 1);
        gl_state_.color_mask = {{1, 1, 1, 1}};
        bind_vertex_array_(0);
        gl_state_.bound_vao = 0;
        bind_buffer_(kGlArrayBuffer, 0);
        gl_state_.bound_array_buffer = 0;
        for (unsigned unit = 0; unit < 4; ++unit) {
            active_texture_(kGlTexture0 + unit);
            bind_texture_(kGlTexture2D, 0);
            gl_state_.unit2d_binding[unit] = 0;
        }
        active_texture_(kGlTexture0);
        gl_state_.active_unit = kGlTexture0;
        gl_state_.valid = true;
    }
private:
    // Union of the scissor rects written to a resident surface since it last
    // resolved (beads-xbox.1.14.6). dirty_surface_pixels already reports how
    // big the surface is; this reports how small the readback COULD be, which
    // is the number a scissored readback has to beat. Maintained only under
    // --perf-accounting.
    struct DirtyRect {
        uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool any = false;
        void reset() { *this = DirtyRect{}; }
        void add(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
            if (w == 0 || h == 0) return;
            const uint32_t nx1 = x + w;
            const uint32_t ny1 = y + h;
            if (!any) {
                x0 = x; y0 = y; x1 = nx1; y1 = ny1; any = true;
                return;
            }
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (nx1 > x1) x1 = nx1;
            if (ny1 > y1) y1 = ny1;
        }
        uint64_t bytes(uint32_t bytes_per_pixel) const {
            if (!any) return 0;
            return static_cast<uint64_t>(x1 - x0) * (y1 - y0) * bytes_per_pixel;
        }
    };
    struct ResidentSurface {
        bool valid = false;
        bool dirty = false;
        bool depth_valid = false;
        bool depth_dirty = false;
        DirtyRect dirty_rect;
        DirtyRect depth_dirty_rect;
        bool x8 = false;
        // Stage (f): a witnessed swizzled colour surface (SET_SURFACE_FORMAT
        // type 2) stores its guest pixels in Morton order, not row-major. The
        // GL image is always linear and bottom-up; this flag is what the upload
        // and the resolve use to map between them, so one resident cache and
        // one resolve seam serve both destination layouts.
        bool swizzled = false;
        unsigned int texture = 0;
        unsigned int depth_texture = 0;
        unsigned int depth_word_texture = 0;
        uint32_t address = 0;
        size_t size = 0;
        uint32_t pitch = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t format = 0;
        uint32_t depth_address = 0;
        size_t depth_size = 0;
        uint32_t depth_pitch = 0;
        uint32_t depth_height = 0;
        uint32_t depth_format = 0;
        // beads-xbox.1.15.10 step 3 (upload staging). The dimensions the GL
        // texture storage was actually allocated with, which is NOT the same as
        // width/height above: those describe the guest surface currently mapped
        // to this slot and are updated on every upload, while these change only
        // when glTexImage2D re-specifies storage. A re-upload of the same
        // geometry can then be a glTexSubImage2D, which reuses the allocation
        // instead of dropping and re-creating it.
        uint32_t tex_alloc_w = 0;
        uint32_t tex_alloc_h = 0;
        uint32_t depth_alloc_w = 0;
        uint32_t depth_alloc_h = 0;
    };
    // beads-xbox.1.15.10 phase 3. Who dropped a plane's residency last, keyed
    // by the GUEST ADDRESS of that plane rather than by the slot, because a
    // slot is reused for different surfaces and a slot-keyed stamp would then
    // attribute one surface's break to another's consumer. A miss is reported
    // as `cold`, so an undersized table cannot silently misattribute -- it can
    // only show up as cold uploads, which is a visible, checkable number.
    // 256, not 32. The table is shared with every GENERIC resident surface,
    // and the canonical run touches far more than 32 distinct addresses: at 32
    // it overflowed, table[0] was recycled, and 343 of 647 colour uploads came
    // back as `cold` when the invalidation that caused them had in fact been
    // recorded. The overflow counter below makes that failure visible instead
    // of silently mislabelling the census.
    static constexpr size_t kInvalidateStampSlots = 256;
    struct InvalidateStamp {
        bool used = false;
        uint32_t address = 0;
        uint32_t token = 0;
    };
    struct CachedBuffer {
        unsigned int buffer = 0;
        bool valid = false;
        size_t size = 0;
        uint64_t hash = 0;
    };
    static uint64_t fnv1a64(const uint8_t* data, size_t size) {
        uint64_t hash = 1469598103934665603ull;
        for (size_t i = 0; i < size; ++i) {
            hash ^= data[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }
    ResidentSurface* find_resident_surface(uint32_t address, size_t size,
                                           uint32_t pitch, uint32_t height,
                                           uint32_t format) {
        for (auto& surface : resident_surfaces_) {
            if (surface.valid && surface.address == address &&
                surface.size == size && surface.pitch == pitch &&
                surface.height == height && surface.format == format) {
                return &surface;
            }
        }
        return nullptr;
    }
    const ResidentSurface* find_resident_surface(uint32_t address, size_t size,
                                                 uint32_t pitch,
                                                 uint32_t height,
                                                 uint32_t format) const {
        for (const auto& surface : resident_surfaces_) {
            if (surface.valid && surface.address == address &&
                surface.size == size && surface.pitch == pitch &&
                surface.height == height && surface.format == format) {
                return &surface;
            }
        }
        return nullptr;
    }
    ResidentSurface* find_resident_depth_surface(uint32_t address, size_t size,
                                                 uint32_t pitch,
                                                 uint32_t height,
                                                 uint32_t format) {
        for (auto& surface : resident_surfaces_) {
            if (surface.depth_valid && surface.depth_address == address &&
                surface.depth_size == size && surface.depth_pitch == pitch &&
                surface.depth_height == height && surface.depth_format == format) {
                return &surface;
            }
        }
        return nullptr;
    }
    const ResidentSurface* find_resident_depth_surface(uint32_t address,
                                                       size_t size,
                                                       uint32_t pitch,
                                                       uint32_t height,
                                                       uint32_t format) const {
        for (const auto& surface : resident_surfaces_) {
            if (surface.depth_valid && surface.depth_address == address &&
                surface.depth_size == size && surface.depth_pitch == pitch &&
                surface.depth_height == height && surface.depth_format == format) {
                return &surface;
            }
        }
        return nullptr;
    }
    void stamp_invalidate(
        std::array<InvalidateStamp, kInvalidateStampSlots>& table,
        uint32_t address, uint32_t token) {
        for (auto& entry : table) {
            if (entry.used && entry.address == address) {
                entry.token = token;
                return;
            }
        }
        for (auto& entry : table) {
            if (!entry.used) {
                entry.used = true;
                entry.address = address;
                entry.token = token;
                return;
            }
        }
        // Table full: overwrite slot 0, and SAY SO. A census that quietly
        // mislabels its biggest bucket is the failure class this audit exists
        // to catch.
        ++stamp_overflows_;
        table[0].address = address;
        table[0].token = token;
    }
    static uint32_t take_invalidate_stamp(
        std::array<InvalidateStamp, kInvalidateStampSlots>& table,
        uint32_t address) {
        for (auto& entry : table) {
            if (entry.used && entry.address == address) {
                entry.used = false;
                return entry.token;
            }
        }
        return nv2a_invalidate_token(kNv2aInvalidateCold);
    }
    static void note_invalidate_extent(
        std::array<std::array<uint64_t, 3>, kNv2aInvalidateCauseSlots>& table,
        uint32_t token, uint32_t plane_address, size_t plane_size,
        uint32_t address, size_t size) {
        size_t cause = nv2a_invalidate_token_cause(token);
        if (cause >= kNv2aInvalidateCauseSlots) cause = kNv2aInvalidateOther;
        const uint64_t p0 = plane_address;
        const uint64_t p1 = p0 + plane_size;
        const uint64_t r0 = address;
        const uint64_t r1 = r0 + size;
        size_t bucket = 2;
        if (r0 == p0 && r1 == p1) bucket = 0;
        else if (r0 <= p0 && r1 >= p1) bucket = 1;
        ++table[cause][bucket];
    }
    static void note_upload_cause(
        std::array<std::array<uint64_t, kNv2aInvalidateProfileSlots>,
                   kNv2aInvalidateCauseSlots>& counts,
        std::array<std::array<uint64_t, kNv2aInvalidateProfileSlots>,
                   kNv2aInvalidateCauseSlots>& bytes,
        uint32_t token, uint64_t amount) {
        size_t cause = nv2a_invalidate_token_cause(token);
        size_t profile = nv2a_invalidate_token_profile(token);
        if (cause >= kNv2aInvalidateCauseSlots) cause = kNv2aInvalidateOther;
        if (profile >= kNv2aInvalidateProfileSlots) profile = 0;
        ++counts[cause][profile];
        bytes[cause][profile] += amount;
    }
    // The witnessed depth key. Deliberately (address, size) and NOT the full
    // five-field key find_resident_depth_surface uses: the witnessed path
    // uploads a zeta plane with the packet's COLOUR pitch/height/format words,
    // so a plane made resident by the generic path under its own words would
    // miss the full key and be re-uploaded over a possibly dirty image. This is
    // the same key the pre-phase-4 zeta fallback used, kept verbatim so the set
    // of planes that count as resident does not move with this change.
    ResidentSurface* find_witnessed_depth_slot(uint32_t depth_address,
                                               size_t size) {
        for (auto& candidate : resident_surfaces_) {
            if (candidate.depth_valid &&
                candidate.depth_address == depth_address &&
                candidate.depth_size == size) {
                return &candidate;
            }
        }
        return nullptr;
    }
    const ResidentSurface* find_witnessed_depth_slot(uint32_t depth_address,
                                                     size_t size) const {
        for (const auto& candidate : resident_surfaces_) {
            if (candidate.depth_valid &&
                candidate.depth_address == depth_address &&
                candidate.depth_size == size) {
                return &candidate;
            }
        }
        return nullptr;
    }
    // The ONE slot-selection rule, shared by ensure_witnessed_surface (which
    // acts on it) and witnessed_target_residency (which reports it). They used
    // to be two separate predicates and the reporting one answered a coarser
    // question -- both planes or neither -- which is what made the device read
    // a resident plane out of guest memory for nothing.
    //
    // beads-xbox.1.15.10 phase 4 (lever C): THE TWO PLANES ARE LOOKED UP
    // INDEPENDENTLY. The previous rule found the colour slot and, on a colour
    // miss, fell back to a slot whose ZETA matched -- and ensure_witnessed_surface
    // then wrote this packet's colour over whatever different, still-valid colour
    // that slot held. Measured at 342 of 647 colour re-uploads (686.0 MB) per
    // canonical 12 B run: two colour surfaces sharing one zeta plane ping-ponged
    // through a single slot. The capacity knob was TRIED AND DISPROVED (8 -> 16
    // changed the census by exactly nothing). The real fix is here: a colour
    // plane and a zeta plane are separate resident images with separate keys and
    // separate dirty flags, so they get separate slots and the FBO is assembled
    // from both -- exactly what the generic path (ensure_resident_surface +
    // ensure_resident_depth_surface) has always done.
    void select_witnessed_slots(uint32_t color_address, size_t size,
                                uint32_t pitch, uint32_t height,
                                uint32_t format, uint32_t depth_address,
                                const ResidentSurface*& color_slot,
                                const ResidentSurface*& depth_slot,
                                bool& colour_resident,
                                bool& depth_resident) const {
        color_slot =
            find_resident_surface(color_address, size, pitch, height, format);
        colour_resident = color_slot != nullptr;
        depth_slot = find_witnessed_depth_slot(depth_address, size);
        depth_resident = depth_slot != nullptr;
        if (witnessed_plane_split_) return;
        // The PRE-PHASE-4 rule, restored verbatim so the change is a
        // same-binary A/B (--no-nv2a-gpu-plane-split). ONE slot serves both
        // planes: on a colour miss the ZETA key picks the slot, and the colour
        // is then written over whatever different colour surface that slot
        // still held. `colour_resident` stays false in that case -- the
        // fallback slot is where the colour will LAND, not where it already is
        // -- which is exactly what the old two-value predicate reported.
        const ResidentSurface* slot = colour_resident ? color_slot : depth_slot;
        color_slot = slot;
        depth_slot = slot;
        depth_resident = slot != nullptr && slot->depth_valid &&
                         slot->depth_address == depth_address &&
                         slot->depth_size == size;
    }
    ResidentSurface* find_reusable_resident_slot() {
        for (auto& surface : resident_surfaces_) {
            if (!surface.valid && !surface.depth_dirty)
                return note_slot_reuse(surface);
        }
        for (auto& surface : resident_surfaces_) {
            if (!surface.dirty && !surface.depth_dirty)
                return note_slot_reuse(surface);
        }
        return nullptr;
    }
    // Per-plane acquisition for the witnessed path. `exclude` is the slot the
    // OTHER plane of this packet already owns and must therefore not be
    // recycled out from under it.
    //
    // These are deliberately weaker than find_reusable_resident_slot, and the
    // difference is the point: taking a slot's COLOUR does not touch its zeta,
    // so a dirty zeta is no reason to refuse the slot, and vice versa. The old
    // pair-scoped predicate required both planes clean and is what made a
    // colour upload evict a clean colour plane rather than land on a free slot.
    ResidentSurface* acquire_color_slot(const ResidentSurface* exclude) {
        for (auto& surface : resident_surfaces_) {
            if (&surface == exclude) continue;
            if (!surface.valid) return note_color_slot_reuse(surface);
        }
        for (auto& surface : resident_surfaces_) {
            if (&surface == exclude) continue;
            if (!surface.dirty) return note_color_slot_reuse(surface);
        }
        return nullptr;
    }
    ResidentSurface* acquire_depth_slot(const ResidentSurface* exclude) {
        for (auto& surface : resident_surfaces_) {
            if (&surface == exclude) continue;
            if (!surface.depth_valid) return note_depth_slot_reuse(surface);
        }
        for (auto& surface : resident_surfaces_) {
            if (&surface == exclude) continue;
            if (!surface.depth_dirty) return note_depth_slot_reuse(surface);
        }
        return nullptr;
    }
    // A slot handed out for a DIFFERENT surface silently ends the residency of
    // whatever clean image it still held. That is a residency break with no
    // invalidation behind it, so without this stamp its re-upload is reported
    // as `cold` and looks like a first touch.
    ResidentSurface* note_slot_reuse(ResidentSurface& surface) {
        note_color_slot_reuse(surface);
        note_depth_slot_reuse(surface);
        return &surface;
    }
    ResidentSurface* note_color_slot_reuse(ResidentSurface& surface) {
        if (surface.valid) {
            stamp_invalidate(color_stamps_, surface.address,
                             nv2a_invalidate_token(kNv2aInvalidateEvicted));
        }
        return &surface;
    }
    ResidentSurface* note_depth_slot_reuse(ResidentSurface& surface) {
        if (surface.depth_valid) {
            stamp_invalidate(depth_stamps_, surface.depth_address,
                             nv2a_invalidate_token(kNv2aInvalidateEvicted));
        }
        return &surface;
    }
    static bool resident_overlaps(const ResidentSurface& surface, uint32_t address,
                                  size_t size) {
        const uint64_t request_end = static_cast<uint64_t>(address) + size;
        const uint64_t resident_end =
            static_cast<uint64_t>(surface.address) + surface.size;
        return surface.valid && address < resident_end &&
               surface.address < request_end;
    }
    static bool resident_depth_overlaps(const ResidentSurface& surface,
                                        uint32_t address, size_t size) {
        const uint64_t request_end = static_cast<uint64_t>(address) + size;
        const uint64_t resident_end =
            static_cast<uint64_t>(surface.depth_address) + surface.depth_size;
        return surface.depth_valid && address < resident_end &&
               surface.depth_address < request_end;
    }
    // The SRC1 forms of the four supported factors, for the dual-source path.
    static unsigned int gl_blend_factor_src1(uint32_t factor) {
        switch (factor) {
            case 0: return kGlZero;
            case 1: return kGlOne;
            case 0x0302: return kGlSrc1Alpha;
            default: return kGlOneMinusSrc1Alpha;
        }
    }
    static unsigned int gl_blend_factor(uint32_t factor) {
        switch (factor) {
            case 0: return kGlZero;
            case 1: return kGlOne;
            case 0x0302: return kGlSrcAlpha;
            default: return kGlOneMinusSrcAlpha;
        }
    }
    static unsigned int gl_depth_func(uint32_t func) {
        switch (func) {
            case 0x0200: return kGlNever;
            case 0x0201: return kGlLess;
            case 0x0202: return kGlEqual;
            case 0x0203: return kGlLequal;
            case 0x0204: return kGlGreater;
            case 0x0205: return kGlNotequal;
            case 0x0206: return kGlGequal;
            default: return kGlAlways;
        }
    }
    static void guest_words_to_rgba(const std::vector<uint8_t>& guest,
                                    std::vector<uint8_t>& rgba, uint32_t width,
                                    uint32_t height, bool x8) {
        rgba.assign(static_cast<size_t>(width) * height * 4u, 0);
        for (uint32_t y = 0; y < height; ++y) {
            const uint32_t gl_y = height - 1u - y;
            for (uint32_t x = 0; x < width; ++x) {
                const size_t guest_offset = (static_cast<size_t>(y) * width + x) * 4u;
                const size_t gl_offset =
                    (static_cast<size_t>(gl_y) * width + x) * 4u;
                rgba[gl_offset + 0] = guest[guest_offset + 2];
                rgba[gl_offset + 1] = guest[guest_offset + 1];
                rgba[gl_offset + 2] = guest[guest_offset + 0];
                rgba[gl_offset + 3] = guest[guest_offset + 3];
            }
        }
    }
    static void guest_words_to_rgba_unflipped(const std::vector<uint8_t>& guest,
                                              std::vector<uint8_t>& rgba,
                                              uint32_t width, uint32_t height,
                                              bool x8) {
        rgba.assign(static_cast<size_t>(width) * height * 4u, 0);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t guest_offset =
                    (static_cast<size_t>(y) * width + x) * 4u;
                const size_t gl_offset =
                    (static_cast<size_t>(y) * width + x) * 4u;
                rgba[gl_offset + 0] = guest[guest_offset + 2];
                rgba[gl_offset + 1] = guest[guest_offset + 1];
                rgba[gl_offset + 2] = guest[guest_offset + 0];
                rgba[gl_offset + 3] = guest[guest_offset + 3];
            }
        }
    }
    static void rgba_to_guest_words(const std::vector<uint8_t>& rgba,
                                    std::vector<uint8_t>& guest, uint32_t width,
                                    uint32_t height, bool x8) {
        guest.assign(static_cast<size_t>(width) * height * 4u, 0);
        for (uint32_t y = 0; y < height; ++y) {
            const uint32_t gl_y = height - 1u - y;
            for (uint32_t x = 0; x < width; ++x) {
                const size_t guest_offset = (static_cast<size_t>(y) * width + x) * 4u;
                const size_t gl_offset =
                    (static_cast<size_t>(gl_y) * width + x) * 4u;
                guest[guest_offset + 0] = rgba[gl_offset + 2];
                guest[guest_offset + 1] = rgba[gl_offset + 1];
                guest[guest_offset + 2] = rgba[gl_offset + 0];
                guest[guest_offset + 3] = rgba[gl_offset + 3];
            }
        }
    }
    static void guest_d24s8_to_gl(const std::vector<uint8_t>& guest,
                                  std::vector<uint32_t>& gl, uint32_t width,
                                  uint32_t height) {
        gl.assign(static_cast<size_t>(width) * height, 0);
        for (uint32_t y = 0; y < height; ++y) {
            const uint32_t gl_y = height - 1u - y;
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t word = 0;
                const size_t guest_offset =
                    (static_cast<size_t>(y) * width + x) * sizeof(uint32_t);
                std::memcpy(&word, guest.data() + guest_offset, sizeof(word));
                gl[static_cast<size_t>(gl_y) * width + x] = word;
            }
        }
    }
    static void gl_d24s8_to_guest(const std::vector<uint32_t>& gl,
                                  std::vector<uint8_t>& guest, uint32_t width,
                                  uint32_t height) {
        guest.assign(static_cast<size_t>(width) * height * sizeof(uint32_t), 0);
        for (uint32_t y = 0; y < height; ++y) {
            const uint32_t gl_y = height - 1u - y;
            for (uint32_t x = 0; x < width; ++x) {
                const uint32_t word = gl[static_cast<size_t>(gl_y) * width + x];
                const size_t guest_offset =
                    (static_cast<size_t>(y) * width + x) * sizeof(uint32_t);
                std::memcpy(guest.data() + guest_offset, &word, sizeof(word));
            }
        }
    }
    bool ensure_resident_surface(const Nv2aGenericCombinerPacket& packet,
                                 uint32_t width, uint32_t height, size_t size,
                                 std::string& error) {
        if (ResidentSurface* existing = find_resident_surface(
                packet.color_address, size, packet.surface_pitch,
                packet.surface_height, packet.surface_format)) {
            active_resident_ = existing;
            cache_bind_texture_unit(kGlTexture0, existing->texture);
            if (!resident_fbo_) gen_framebuffers_(1, &resident_fbo_);
            cache_bind_framebuffer(resident_fbo_);
            cache_framebuffer_color_texture(resident_fbo_, existing->texture);
            return true;
        }
        if (packet.color_input.size() != size) {
            error = "OpenGL resident primary draw missing initial color snapshot";
            return false;
        }
        ResidentSurface* slot = find_reusable_resident_slot();
        if (!slot) {
            error = "OpenGL resident surface cache is full of dirty targets";
            return false;
        }
        if (!slot->texture) {
            gen_textures_(1, &slot->texture);
            if (!slot->texture) {
                error = "OpenGL resident color texture allocation failed";
                return false;
            }
            cache_bind_texture_unit(kGlTexture0, slot->texture);
            tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        } else {
            cache_bind_texture_unit(kGlTexture0, slot->texture);
        }
        std::vector<uint8_t>& rgba = upload_staging_;
        guest_words_to_rgba(packet.color_input, rgba, width, height,
                            packet.fixture_color_format_x8);
        pixel_store_i_(kGlUnpackAlignment, 1);
        gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlRgba8,
                               static_cast<int>(width),
                               static_cast<int>(height), 0, kGlRgba,
                               kGlUnsignedByte, rgba.data());
        // beads-xbox.1.15.10 step 3. This path RE-SPECIFIES the slot's colour
        // storage, and the witnessed path decides between glTexSubImage2D and
        // glTexImage2D from these two fields. Leaving them stale here would let
        // a later witnessed upload sub-image into storage of a different size.
        slot->tex_alloc_w = width;
        slot->tex_alloc_h = height;
        if (!resident_fbo_) gen_framebuffers_(1, &resident_fbo_);
        cache_bind_framebuffer(resident_fbo_);
        cache_framebuffer_color_texture(resident_fbo_, slot->texture);
        if (check_framebuffer_status_(kGlFramebuffer) != kGlFramebufferComplete) {
            error = "OpenGL resident color framebuffer is incomplete";
            return false;
        }
        if (get_error_() != kGlNoError) {
            error = "OpenGL resident color upload failed";
            return false;
        }
        slot->valid = true;
        slot->dirty = false;
        slot->dirty_rect.reset();
        slot->x8 = packet.fixture_color_format_x8;
        slot->address = packet.color_address;
        slot->size = size;
        slot->pitch = packet.surface_pitch;
        slot->width = width;
        slot->height = height;
        slot->format = packet.surface_format;
        active_resident_ = slot;
        return true;
    }
    // Build the R32UI word form of a resident zeta plane from the plane
    // itself. The depth attachment is the authority for that range whoever
    // wrote it, so reading it back is the one construction that cannot
    // disagree with what the next draw will test against. Used only when a
    // slot became depth-resident through a path that had no word form to
    // upload; it costs one readback of the plane, counted by the same GL
    // timing wrapper every other readback goes through.
    bool ensure_depth_word_texture(ResidentSurface& slot, std::string& error) {
        if (slot.depth_word_texture) return true;
        if (!slot.depth_valid || !slot.depth_texture || !slot.width ||
            !slot.height) {
            error = "OpenGL zeta-word texture requested for a non-resident plane";
            return false;
        }
        std::vector<uint32_t>& words = depth_readback_scratch_;
        words.resize(static_cast<size_t>(slot.width) * slot.height);
        if (!resident_fbo_) gen_framebuffers_(1, &resident_fbo_);
        cache_bind_framebuffer(resident_fbo_);
        cache_framebuffer_color_texture(resident_fbo_, slot.texture);
        cache_framebuffer_depth_texture(resident_fbo_, slot.depth_texture);
        pixel_store_i_(kGlPackAlignment, 4);
        gpu_timed_resolve_read_pixels(
            0, 0, static_cast<int>(slot.width), static_cast<int>(slot.height),
            kGlDepthStencil, kGlUnsignedInt248, words.data(),
            static_cast<uint64_t>(slot.width) * slot.height * 4ull,
            static_cast<uint64_t>(slot.width) * slot.height * 4ull);
        if (get_error_() != kGlNoError) {
            error = "OpenGL zeta-word texture readback failed";
            return false;
        }
        gen_textures_(1, &slot.depth_word_texture);
        if (!slot.depth_word_texture) {
            error = "OpenGL zeta-word texture allocation failed";
            return false;
        }
        cache_bind_texture_unit(kGlTexture0, slot.depth_word_texture);
        tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
        tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
        tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
        tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        pixel_store_i_(kGlUnpackAlignment, 4);
        gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlR32ui,
                               static_cast<int>(slot.width),
                               static_cast<int>(slot.height), 0, kGlRedInteger,
                               kGlUnsignedInt, words.data());
        if (get_error_() != kGlNoError) {
            error = "OpenGL zeta-word texture upload failed";
            return false;
        }
        ++witnessed_upload_census_.depth_word_rebuilds;
        return true;
    }
    bool ensure_resident_depth_surface(const Nv2aGenericCombinerPacket& packet,
                                       ResidentSurface& slot, uint32_t width,
                                       uint32_t height, size_t size,
                                       std::string& error) {
        if (!packet.fixture_depth_test_enabled &&
            !packet.fixture_depth_write_enabled) {
            cache_framebuffer_depth_texture(resident_fbo_, 0);
            active_depth_resident_ = nullptr;
            return true;
        }
        if (ResidentSurface* existing = find_resident_depth_surface(
                packet.depth_address, size, packet.surface_pitch,
                packet.surface_height, packet.surface_format)) {
            // Phase 7: the plane can now have been made resident by a path
            // that has no reason to build the WORD form of it (the witnessed
            // executor never did; the generated executor does, but only when
            // IT performed the upload). The fixture's depth-EQUAL test reads
            // that word texture, so build it here from the authoritative
            // image -- the GPU depth attachment -- rather than faulting.
            if (!existing->depth_word_texture &&
                !ensure_depth_word_texture(*existing, error)) {
                return false;
            }
            cache_framebuffer_depth_texture(resident_fbo_,
                                            existing->depth_texture);
            if (check_framebuffer_status_(kGlFramebuffer) !=
                kGlFramebufferComplete) {
                error = "OpenGL resident color+zeta framebuffer is incomplete";
                return false;
            }
            active_depth_resident_ = existing;
            return true;
        }
        if (packet.depth_address == 0 || packet.depth_input.size() != size) {
            error = "OpenGL resident draw missing initial zeta snapshot";
            return false;
        }
        if (!slot.depth_texture) {
            gen_textures_(1, &slot.depth_texture);
            if (!slot.depth_texture) {
                error = "OpenGL resident zeta texture allocation failed";
                return false;
            }
            cache_bind_texture_unit(kGlTexture0, slot.depth_texture);
            tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        } else {
            cache_bind_texture_unit(kGlTexture0, slot.depth_texture);
        }
        if (!slot.depth_word_texture) {
            gen_textures_(1, &slot.depth_word_texture);
            if (!slot.depth_word_texture) {
                error = "OpenGL resident zeta-word texture allocation failed";
                return false;
            }
            cache_bind_texture_unit(kGlTexture0, slot.depth_word_texture);
            tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        }
        std::vector<uint32_t> gl_depth;
        guest_d24s8_to_gl(packet.depth_input, gl_depth, width, height);
        cache_bind_texture_unit(kGlTexture0, slot.depth_word_texture);
        pixel_store_i_(kGlUnpackAlignment, 4);
        gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlR32ui,
                               static_cast<int>(width),
                               static_cast<int>(height), 0, kGlRedInteger,
                               kGlUnsignedInt, gl_depth.data());
        cache_bind_texture_unit(kGlTexture0, slot.depth_texture);
        pixel_store_i_(kGlUnpackAlignment, 4);
        gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlDepth24Stencil8,
                               static_cast<int>(width),
                               static_cast<int>(height), 0, kGlDepthStencil,
                               kGlUnsignedInt248, gl_depth.data());
        // Same reason as the colour plane above: this re-specifies the zeta
        // storage the witnessed path's sub-image decision keys on.
        slot.depth_alloc_w = width;
        slot.depth_alloc_h = height;
        slot.depth_valid = true;
        slot.depth_dirty = false;
        slot.depth_dirty_rect.reset();
        slot.depth_address = packet.depth_address;
        slot.depth_size = size;
        slot.depth_pitch = packet.surface_pitch;
        slot.depth_height = packet.surface_height;
        slot.depth_format = packet.surface_format;
        cache_framebuffer_depth_texture(resident_fbo_, slot.depth_texture);
        if (check_framebuffer_status_(kGlFramebuffer) != kGlFramebufferComplete) {
            error = "OpenGL resident color+zeta framebuffer is incomplete";
            return false;
        }
        if (get_error_() != kGlNoError) {
            error = "OpenGL resident zeta upload failed";
            return false;
        }
        active_depth_resident_ = &slot;
        return true;
    }
#if XLLE_VSH_GPU_RESIDENT
    unsigned int specialized_vsh_program(const std::vector<vsh_gpu_batch::Op>& ops) {
        if (!vsh_specialize_) return 0;
        for (const auto& entry : vsh_specializations_) {
            if (!vsh_gpu_batch::same_program(entry.ops, ops)) continue;
            ++vsh_resident_stats_.specialized_hits;
            return entry.program; // A failed compile is also cached; never retry each draw.
        }
        if (vsh_specializations_.size() == vsh_gpu_batch::kSpecializeCacheSize) {
            ++vsh_resident_stats_.specialized_declines;
            return 0;
        }
        const auto source = vsh_gpu_batch::specialized_vsh_source(ops);
        if (source.empty()) {
            ++vsh_resident_stats_.specialized_declines;
            return 0;
        }
        VshSpecialization entry;
        entry.ops = ops;
        ++vsh_resident_stats_.specialized_compiles;
        std::string error;
        std::printf("[vsh-specialize] compiling ops=%zu\n", ops.size());
        std::fflush(stdout);
        {
            perf::ScopedAdd timer(vsh_compile_ns_, vsh_compile_calls_);
            if (!compile_program(source.c_str(), "specialized VSH", entry.program, error)) {
                ++vsh_resident_stats_.specialized_failures;
                std::fprintf(stderr, "[vsh-specialize] using generic GPU program: %s\n", error.c_str());
            }
        }
        std::printf("[vsh-specialize] compiled ops=%zu success=%u\n", ops.size(), entry.program ? 1u : 0u);
        std::fflush(stdout);
        const auto program = entry.program;
        vsh_specializations_.push_back(std::move(entry));
        return program;
    }
    unsigned int& active_vsh_pack_program() {
        return vsh_parallel_pack_ ? vsh_parallel_pack_program_ : vsh_pack_program_;
    }
    bool ensure_vsh_gpu_programs(std::string& error) {
        return (vsh_batch_program_ || compile_program(vsh_gpu_batch::kVshBatchComputeSource.c_str(),
                    "shared VSH", vsh_batch_program_, error)) &&
               (active_vsh_pack_program() || compile_program(
                    (vsh_parallel_pack_ ? vsh_gpu_batch::kResidentPrimaryParallelPackComputeSource :
                        vsh_gpu_batch::kResidentPrimaryPackComputeSource).c_str(),
                    "resident VSH pack", active_vsh_pack_program(), error));
    }
    bool valid_vsh_preparation(const Nv2aGenericCombinerPacket::GpuVshReference& reference) const {
        return prepared_vsh_reference_ == &reference && reference.prepared_token != 0 &&
               reference.prepared_token == vsh_preparation_token_ &&
               reference.prepared_vertices == vsh_prepared_vertices_;
    }
    bool prepare_vsh_resident(Nv2aGenericCombinerPacket::GpuVshReference& reference,
                               const std::vector<Nv2aGenericCombinerVertex>* cpu_vertices,
                               bool& use_gpu_buffer, std::string& error) {
        namespace gpu = vsh_gpu_batch;
        perf::ScopedAdd total_timer(vsh_cpu_ns_[0], vsh_cpu_calls_[0]);
        const bool verify = reference.mode != 3;
        const bool read_statuses = verify || !vsh_status_summary_;
        use_gpu_buffer = false;
        reference.prepared_token = 0;
        reference.prepared_vertices = 0;
        prepared_vsh_reference_ = nullptr; // Any new dispatch invalidates the previous scratch receipt.
        const auto& batch = reference.batch;
        auto& stats = vsh_resident_stats_;
        ++stats.batches;
        stats.executions += batch.executions.size();
        stats.references += batch.references.size();
        const auto mismatch = [&](const char* reason) {
            ++stats.mismatches;
            error = std::string("GPU resident VSH verification: ") + reason;
            return false;
        };
        if (!gpu::validate_batch_vectors(batch) || batch.executions.empty() ||
            (verify && (!cpu_vertices || reference.expected.size() != batch.executions.size())) ||
            (!verify && (cpu_vertices || !reference.expected.empty())))
            return mismatch("invalid owned batch shape");
        for (uint32_t id : batch.references)
            if (id >= batch.executions.size()) return mismatch("invalid execution reference");
        if (!gpu::supported_program_layout(batch.ops)) { ++stats.declines; ++stats.layout_declines; return true; }
        if (!ensure_vsh_gpu_programs(error)) return false;
        const unsigned int specialized = specialized_vsh_program(batch.ops);
        const unsigned int program = specialized ? specialized : vsh_batch_program_;
        for (auto& buffer : vsh_buffers_) if (!buffer) gen_buffers_(1, &buffer);
        const uint32_t groups = (batch.shape.execution_count + 63u) / 64u;
        const uint32_t candidates = gpu::triangle_count(batch.shape.primitive_mode, batch.shape.reference_count);
        const size_t max_vertices = static_cast<size_t>(candidates) * 3;
        const size_t output_count = verify ? (groups + 1u) * 64u : batch.executions.size();
        std::vector<gpu::ProgramOutput128B> outputs(verify ? output_count : 0);
        for (auto& out : outputs) for (auto& vec : out.values) vec.fill(0xdeadbeefu);
        std::vector<uint32_t> statuses(read_statuses ? output_count : 0, 0xdeadbeefu);
        std::vector<std::array<uint32_t, 4>> triangle_data(verify ? candidates + 64 : 0);
        for (auto& vec : triangle_data) vec.fill(0xdeadbeefu);
        std::vector<gpu::ResidentVertex96B> packed(verify ? max_vertices + 64 : 0);
        for (auto& vertex : packed) {
            vertex.position.fill(0xdeadbeefu); vertex.diffuse.fill(0xdeadbeefu);
            for (auto& tex : vertex.texture) tex.fill(0xdeadbeefu);
        }
        std::array<uint32_t, 4> control{};
        control.fill(0xdeadbeefu);
        const auto upload = [&](size_t index, unsigned binding, const void* data, size_t bytes) {
            perf::ScopedAdd timer(vsh_cpu_ns_[1], vsh_cpu_calls_[1]);
            if (perf::g_enabled && data) vsh_upload_bytes_ += bytes;
            bind_buffer_(kGlShaderStorageBuffer, vsh_buffers_[index]);
            gpu_timed_buffer_data(kGlShaderStorageBuffer, static_cast<std::ptrdiff_t>(bytes), data, kGlDynamicCopy);
            bind_buffer_base_(kGlShaderStorageBuffer, binding, vsh_buffers_[index]);
        };
        upload(0, 0, batch.executions.data(), batch.executions.size() * sizeof(batch.executions[0]));
        if (!specialized) upload(1, 1, batch.ops.data(), batch.ops.size() * sizeof(batch.ops[0]));
        upload(8, 4, &batch.constants, sizeof(batch.constants));
        upload(2, 2, verify ? outputs.data() : nullptr, output_count * sizeof(gpu::ProgramOutput128B));
        upload(3, 3, verify ? statuses.data() : nullptr, output_count * sizeof(uint32_t));
        const auto uniform = [&](unsigned program, const char* name, uint32_t value) {
            const int location = cached_uniform_location(program, name);
            if (location < 0) { error = std::string("Missing GPU VSH uniform: ") + name; return false; }
            uniform_1ui_(location, value);
            return true;
        };
        {
        perf::ScopedAdd timer(vsh_cpu_ns_[2], vsh_cpu_calls_[2]);
        use_program_(program);
        if (specialized) {
            // A constant-result program may have all arithmetic folded away.
            const int location = cached_uniform_location(program, "u_native_fp32");
            if (location >= 0) uniform_1ui_(location, vsh_native_fp32_ ? 1u : 0u);
        } else if (!uniform(program, "u_native_fp32", vsh_native_fp32_ ? 1u : 0u)) return false;
        if (!uniform(program, "u_execution_count", batch.shape.execution_count) ||
            (!specialized && !uniform(program, "u_op_count", batch.shape.op_count))) return false;
        gpu_timed_dispatch_compute(groups, 1, 1, GpuTimerBucket::kVsh);
        stats.specialized_dispatches += specialized != 0;
        stats.native_fp32_dispatches += vsh_native_fp32_;
        memory_barrier_(kGlShaderStorageBarrierBit | kGlBufferUpdateBarrierBit);
        }
        upload(4, 4, batch.references.data(), batch.references.size() * sizeof(uint32_t));
        upload(5, 5, verify ? triangle_data.data() : nullptr,
               (verify ? triangle_data.size() : candidates) * sizeof(std::array<uint32_t, 4>));
        upload(6, 6, verify ? packed.data() : nullptr,
               (verify ? packed.size() : max_vertices) * sizeof(gpu::ResidentVertex96B));
        upload(7, 7, control.data(), sizeof(control));
        {
        perf::ScopedAdd timer(vsh_cpu_ns_[3], vsh_cpu_calls_[3]);
        const auto pack_program = active_vsh_pack_program();
        use_program_(pack_program);
        if (!uniform(pack_program, "u_status_summary", vsh_status_summary_ ? 1u : 0u)) return false;
        if (!uniform(pack_program, "u_execution_count", batch.shape.execution_count) ||
            !uniform(pack_program, "u_reference_count", batch.shape.reference_count) ||
            !uniform(pack_program, "u_primitive", batch.shape.primitive_mode) ||
            !uniform(pack_program, "u_large_target", batch.shape.large_target) ||
            !uniform(pack_program, "u_cull_enabled", batch.shape.cull_enabled) ||
            !uniform(pack_program, "u_cull_front", batch.shape.cull_front) ||
            !uniform(pack_program, "u_max_resident_vertices", static_cast<uint32_t>(max_vertices))) return false;
        gpu_timed_dispatch_compute(1, 1, 1, GpuTimerBucket::kVshPack);
        constexpr unsigned kVertexAttribBarrier = 0x00000001;
        memory_barrier_(kGlShaderStorageBarrierBit | kGlBufferUpdateBarrierBit | kVertexAttribBarrier);
        }
        const auto download = [&](size_t index, void* data, size_t bytes, size_t phase) {
            perf::ScopedAdd timer(vsh_cpu_ns_[phase], vsh_cpu_calls_[phase]);
            if (perf::g_enabled) vsh_download_bytes_ += bytes;
            bind_buffer_(kGlShaderStorageBuffer, vsh_buffers_[index]);
            get_buffer_sub_data_(kGlShaderStorageBuffer, 0, static_cast<std::ptrdiff_t>(bytes), data);
        };
        if (verify) download(2, outputs.data(), outputs.size() * sizeof(outputs[0]), 4);
        if (read_statuses) download(3, statuses.data(), statuses.size() * sizeof(statuses[0]), 5);
        else ++stats.status_readbacks_skipped;
        if (verify) download(5, triangle_data.data(), triangle_data.size() * sizeof(triangle_data[0]), 6);
        if (verify) download(6, packed.data(), packed.size() * sizeof(packed[0]), 7);
        download(7, control.data(), sizeof(control), 8);
        perf::ScopedAdd verify_timer(vsh_cpu_ns_[9], vsh_cpu_calls_[9]);
        if (get_error_() != kGlNoError) { error = "GPU VSH resident compute/readback failed"; return false; }
        bool program_decline = false;
        bool numeric_decline = false, opcode_decline = false;
        for (size_t i = 0; i < statuses.size(); ++i) {
            if (i >= batch.executions.size()) {
                if (statuses[i] != 0xdeadbeefu) return mismatch("program status tail guard");
            } else {
                if (statuses[i] > 2) return mismatch("invalid program status");
                program_decline = program_decline || statuses[i] != 0;
                numeric_decline = numeric_decline || statuses[i] == gpu::ProgramStatus::kNumericDecline;
                opcode_decline = opcode_decline || statuses[i] == gpu::ProgramStatus::kUnsupportedProgram;
                if (verify && !statuses[i] && std::memcmp(&outputs[i], &reference.expected[i], sizeof(outputs[i])))
                    return mismatch("accepted program output differs from CPU");
            }
            if (verify && (i >= batch.executions.size() || statuses[i]))
                for (const auto& vec : outputs[i].values) for (uint32_t word : vec)
                    if (word != 0xdeadbeefu) return mismatch("declined output or program tail guard");
        }
        if (control[0] > max_vertices || control[0] % 3 || (control[1] & ~15u) ||
            control[2] != candidates || (control[3] & ~3u) ||
            (!vsh_status_summary_ && control[3])) return mismatch("invalid packed control");
        if (vsh_status_summary_) {
            const uint32_t cpu_summary = (numeric_decline ? 1u : 0u) | (opcode_decline ? 2u : 0u);
            if (read_statuses && control[3] != cpu_summary)
                return mismatch("GPU execution status summary differs from CPU status scan");
            numeric_decline = (control[3] & 1u) != 0;
            opcode_decline = (control[3] & 2u) != 0;
            program_decline = numeric_decline || opcode_decline;
        }
        for (size_t i = 0; i < triangle_data.size(); ++i) {
            if (i >= candidates) {
                for (uint32_t word : triangle_data[i])
                    if (word != 0xdeadbeefu) return mismatch("triangle tail guard");
            } else if (triangle_data[i][3] > 4) return mismatch("invalid triangle status");
        }
        for (size_t i = control[0]; verify && i < packed.size(); ++i) {
            for (uint32_t word : packed[i].position) if (word != 0xdeadbeefu) return mismatch("packed position tail guard");
            for (uint32_t word : packed[i].diffuse) if (word != 0xdeadbeefu) return mismatch("packed diffuse tail guard");
            for (const auto& tex : packed[i].texture) for (uint32_t word : tex)
                if (word != 0xdeadbeefu) return mismatch("packed texture tail guard");
        }
        if (program_decline || control[1]) {
            ++stats.declines;
            stats.numeric_declines += numeric_decline;
            stats.opcode_declines += opcode_decline;
            stats.geometry_declines += (control[1] & gpu::BatchStatusBits::kGeometryDecline) != 0;
            return true;
        }
        if (!verify) {
            ++stats.accepted;
            stats.vertices += control[0];
            if (++vsh_preparation_token_ == 0) ++vsh_preparation_token_;
            reference.prepared_token = vsh_preparation_token_;
            reference.prepared_vertices = control[0];
            vsh_prepared_vertices_ = control[0];
            prepared_vsh_reference_ = &reference;
            use_gpu_buffer = true;
            return true;
        }
        if (control[0] != cpu_vertices->size()) return mismatch("expanded vertex count differs from CPU");
        for (size_t i = 0; i < cpu_vertices->size(); ++i) {
            const auto& cpu = (*cpu_vertices)[i];
            if (std::memcmp(packed[i].position.data(), cpu.position.data(), 16) ||
                std::memcmp(packed[i].diffuse.data(), cpu.diffuse.data(), 16) ||
                std::memcmp(packed[i].texture.data(), cpu.texture.data(), 64))
                return mismatch("packed resident vertex differs from CPU");
        }
        ++stats.verified;
        stats.vertices += control[0];
        use_gpu_buffer = reference.mode == 2;
        return true;
    }
#endif
    bool compile_program(const char* source, const char* label,
                         unsigned int& program, std::string& error) {
        const unsigned int shader = create_shader_(kGlComputeShader);
        shader_source_(shader, 1, &source, nullptr);
        compile_shader_(shader);
        int success = 0;
        get_shader_iv_(shader, kGlCompileStatus, &success);
        if (!success) {
            std::array<char, 1024> log{};
            get_shader_log_(shader, static_cast<int>(log.size()), nullptr, log.data());
            error = std::string("OpenGL ") + label + " compute compile failed: " +
                    log.data();
            delete_shader_(shader);
            return false;
        }
        program = create_program_();
        attach_shader_(program, shader);
        link_program_(program);
        delete_shader_(shader);
        get_program_iv_(program, kGlLinkStatus, &success);
        if (!success) {
            std::array<char, 1024> log{};
            get_program_log_(program, static_cast<int>(log.size()), nullptr, log.data());
            error = std::string("OpenGL ") + label + " compute link failed: " +
                    log.data();
            delete_program_(program);
            program = 0;
            return false;
        }
        // Program names can be reused by the driver after deletion; drop any
        // cached uniform locations bound to this handle.
        uniform_location_cache_.erase(program);
        return true;
    }
    bool compile_graphics_program(const char* vertex_source,
                                  const char* fragment_source,
                                  const char* label, unsigned int& program,
                                  std::string& error) {
        const unsigned int vertex_shader = create_shader_(kGlVertexShader);
        const unsigned int fragment_shader = create_shader_(kGlFragmentShader);
        auto compile_one = [&](unsigned int shader, const char* source,
                               const char* stage) {
            shader_source_(shader, 1, &source, nullptr);
            compile_shader_(shader);
            int success = 0;
            get_shader_iv_(shader, kGlCompileStatus, &success);
            if (success) return true;
            std::array<char, 1024> log{};
            get_shader_log_(shader, static_cast<int>(log.size()), nullptr,
                            log.data());
            error = std::string("OpenGL ") + label + " " + stage +
                    " compile failed: " + log.data();
            return false;
        };
        if (!compile_one(vertex_shader, vertex_source, "vertex") ||
            !compile_one(fragment_shader, fragment_source, "fragment")) {
            delete_shader_(vertex_shader);
            delete_shader_(fragment_shader);
            return false;
        }
        program = create_program_();
        attach_shader_(program, vertex_shader);
        attach_shader_(program, fragment_shader);
        link_program_(program);
        delete_shader_(vertex_shader);
        delete_shader_(fragment_shader);
        int success = 0;
        get_program_iv_(program, kGlLinkStatus, &success);
        if (!success) {
            std::array<char, 1024> log{};
            get_program_log_(program, static_cast<int>(log.size()), nullptr,
                             log.data());
            error = std::string("OpenGL ") + label + " link failed: " +
                    log.data();
            delete_program_(program);
            program = 0;
            return false;
        }
        return true;
    }
    bool compile_fixture(std::string& error) {
        return compile_program(kFixtureComputeSource, "fixture", fixture_program_, error);
    }
    bool compile_primary_color(std::string& error) {
        return compile_program(kPrimaryColorTriangleComputeSource, "primary-color",
                               primary_color_program_, error);
    }
    bool compile_dashboard(std::string& error) {
        return compile_program(kDashboardProject2dFanComputeSource, "dashboard",
                               dashboard_program_, error);
    }
    bool compile_texture_modulate(std::string& error) {
        return compile_program(kTextureModulateTriangleComputeSource,
                               "texture-modulate", texture_modulate_program_,
                               error);
    }
    bool compile_resident_primary(std::string& error) {
        return compile_graphics_program(kResidentPrimaryVertexSource,
                                        kResidentPrimaryFragmentSource,
                                        "resident-primary", resident_program_,
                                        error);
    }
    bool compile_resident_alpha(std::string& error) {
        return compile_graphics_program(kResidentAlphaVertexSource,
                                        kResidentAlphaFragmentSource,
                                        "resident-alpha", resident_alpha_program_,
                                        error);
    }
    bool compile_resident_texture_modulate(std::string& error) {
        return compile_graphics_program(kResidentTextureModulateVertexSource,
                                        kResidentTextureModulateFragmentSource,
                                        "resident-texture-modulate",
                                        resident_texture_modulate_program_,
                                        error);
    }
    // ============ stage (f) witnessed-profile GPU helpers ==================
    // Injects `#define WP_FP32 0|1` immediately after the shader's `#version`
    // line -- the only place a #define may go and still precede every use. The
    // define is ALWAYS emitted, so a source that spells `#if WP_FP32` cannot
    // silently take the wrong branch on an undefined name.
    std::string witnessed_fp32_source(const char* source) const {
        std::string out(source);
        const size_t newline = out.find('\n');
        const char* define =
            witnessed_fp32_shader_ ? "#define WP_FP32 1\n" : "#define WP_FP32 0\n";
        out.insert(newline == std::string::npos ? out.size() : newline + 1,
                   define);
        return out;
    }
    bool compile_witnessed_programs(std::string& error) {
        static_assert(sizeof(witnessed_gl::RadialTriRecord) == 224,
                      "RadialTriRecord must match the std430 array stride");
        static_assert(sizeof(witnessed_gl::TexturedTriRecord) == 288,
                      "TexturedTriRecord must match the std430 array stride");
        static_assert(offsetof(witnessed_gl::RadialTriRecord, recip) == 192,
                      "RadialTriRecord.recip must sit on the dvec4 boundary");
        static_assert(offsetof(witnessed_gl::TexturedTriRecord, recip) == 256,
                      "TexturedTriRecord.recip must sit on the dvec4 boundary");
        // The leading 160 bytes are written once through the radial view in
        // submit_witnessed_profile, so the two records must agree on them.
        static_assert(offsetof(witnessed_gl::RadialTriRecord, t2) ==
                          offsetof(witnessed_gl::TexturedTriRecord, t2),
                      "witnessed triangle records must share their common half");
        // beads-xbox.1.15.10 step 2 (FP64 removal). ONE source per stage with a
        // WP_FP32 switch injected after the #version line, so the two variants
        // cannot drift apart the way two copies would; the SSBO record is
        // byte-identical either way (the FP32 body reads the float reciprocals
        // out of `wv`, the FP64 body the doubles out of `recip`).
        const std::string radial_vs =
            witnessed_fp32_source(witnessed_gl::kRadialVertexSource);
        const std::string radial_fs =
            witnessed_fp32_source(witnessed_gl::kRadialFragmentSource);
        const std::string textured_vs =
            witnessed_fp32_source(witnessed_gl::kTexturedVertexSource);
        const std::string textured_fs =
            witnessed_fp32_source(witnessed_gl::kTexturedFragmentSource);
        // beads-xbox.1.15.10 phase 4 (lever A). The projected program shares
        // the TEXTURED VERTEX SHADER verbatim -- same record, same 288-byte
        // std430 stride, same six-vertex wide-triangle expansion -- so only the
        // fragment stage differs.
        const std::string projected_fs =
            witnessed_fp32_source(witnessed_gl::kProjectedFragmentSource);
        if (!compile_graphics_program(radial_vs.c_str(), radial_fs.c_str(),
                                      "witnessed-radial",
                                      witnessed_radial_program_, error)) {
            return false;
        }
        if (!compile_graphics_program(textured_vs.c_str(), textured_fs.c_str(),
                                      "witnessed-textured",
                                      witnessed_textured_program_, error)) {
            return false;
        }
        if (!compile_graphics_program(textured_vs.c_str(), projected_fs.c_str(),
                                      "witnessed-projected",
                                      witnessed_projected_program_, error)) {
            return false;
        }
        gen_vertex_arrays_(1, &witnessed_vao_);
        if (!witnessed_vao_) {
            error = "OpenGL witnessed vertex array allocation failed";
            return false;
        }
        auto bind_uniforms = [this](WitnessedProgram& wp, unsigned int program,
                                    bool textured, bool projected) {
            wp.program = program;
            wp.loc_surface_size = uniform_location_(program, "surface_size");
            wp.loc_clip_min = uniform_location_(program, "clip_min");
            wp.loc_clip_max = uniform_location_(program, "clip_max");
            if (textured) {
                wp.loc_texture0 = uniform_location_(program, "texture0");
                wp.loc_texture_size = uniform_location_(program, "texture_size");
                wp.loc_weight0 = uniform_location_(program, "weight0");
                wp.loc_weight1 = uniform_location_(program, "weight1");
            }
            if (projected) {
                wp.loc_texture1 = uniform_location_(program, "texture1");
                wp.loc_projected_texture_size =
                    uniform_location_(program, "projected_texture_size");
            }
            wp.uniforms_valid = false;
        };
        bind_uniforms(witnessed_radial_, witnessed_radial_program_, false,
                      false);
        bind_uniforms(witnessed_textured_, witnessed_textured_program_, true,
                      false);
        bind_uniforms(witnessed_projected_, witnessed_projected_program_, true,
                      true);
        return true;
    }

    // The GL context is made current ONCE and left current. The previous
    // pattern -- make current, draw, restore -- unbinds the context on every
    // submit, and at ~80 k submits per 2 G instructions the bind/unbind pair
    // dominated the whole executor. Leaving a context current is not observable
    // by any other consumer: everything in this process that uses GL makes its
    // own context current first, which is what the restore was protecting.
    bool enter_witnessed_context(std::string& error) {
        if (SDL_GL_GetCurrentContext() == context_) return true;
        perf::ScopedAdd timer(perf::g_ns_renderer_resident_context,
                              perf::g_calls_renderer_resident_context);
        if (!SDL_GL_MakeCurrent(window_, context_)) {
            error = std::string("OpenGL witnessed context lost: ") +
                    SDL_GetError();
            witnessed_context_owned_ = false;
            return false;
        }
        // Something else held the context between two submits, so neither the
        // mirrored GL state nor the bound program and its uniforms can be
        // trusted. Dropping every mirror here is what makes leaving the context
        // current safe rather than merely fast.
        if (witnessed_context_owned_) invalidate_gl_state_cache();
        witnessed_bound_program_ = 0;
        witnessed_radial_.uniforms_valid = false;
        witnessed_textured_.uniforms_valid = false;
        witnessed_projected_.uniforms_valid = false;
        witnessed_fixed_state_ = false;
        witnessed_context_owned_ = true;
        return true;
    }

    static uint32_t witnessed_swizzle_index(uint32_t x, uint32_t y,
                                            uint32_t width, uint32_t height) {
        // Identical to SwizzleLookup (nv2a.hpp) and to the swizzle_index()
        // already present in this file's compute shaders: interleave the x and
        // y bits, low bit first, skipping the axis that has run out of bits.
        uint32_t result = 0;
        uint32_t output_bit = 1;
        for (uint32_t input_bit = 1;
             input_bit < width || input_bit < height; input_bit <<= 1) {
            if (input_bit < width) {
                if (x & input_bit) result |= output_bit;
                output_bit <<= 1;
            }
            if (input_bit < height) {
                if (y & input_bit) result |= output_bit;
                output_bit <<= 1;
            }
        }
        return result;
    }
    // beads-xbox.1.15.10 step 3 (upload/resolve staging). witnessed_swizzle_index
    // ORs bits derived from x into output positions that are DISJOINT from the
    // ones it ORs bits of y into -- that is what interleaving means -- so
    //     index(x, y) == index(x, 0) | index(0, y)
    // identically, for every (x, y, width, height), including the ragged case
    // where one axis runs out of bits first. Two tables therefore replace an
    // eleven-iteration bit loop per pixel with a load and an OR. The tables are
    // rebuilt only when the geometry changes, which on this workload is never
    // after the first upload of each surface.
    static bool is_power_of_two(uint32_t v) { return v != 0 && (v & (v - 1)) == 0; }
    // Sizes a staging/destination buffer for a full-surface rewrite. When the
    // Morton map covers every element (both extents powers of two) or the
    // layout is linear, resize is enough and the zero fill of a multi-megabyte
    // vector is pure cost; otherwise the fill is required, because the holes
    // would otherwise carry bytes from a previous surface.
    template <typename T>
    static void resize_or_zero(std::vector<T>& buffer, size_t elements,
                               uint32_t width, uint32_t height) {
        if (is_power_of_two(width) && is_power_of_two(height)) {
            buffer.resize(elements);
        } else {
            buffer.assign(elements, T{});
        }
    }
    std::vector<uint32_t> depth_readback_scratch_;
    struct SwizzleTables {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint32_t> x;
        std::vector<uint32_t> y;
        void ensure(uint32_t w, uint32_t h) {
            if (width == w && height == h && x.size() == w && y.size() == h)
                return;
            width = w;
            height = h;
            x.resize(w);
            y.resize(h);
            for (uint32_t i = 0; i < w; ++i)
                x[i] = witnessed_swizzle_index(i, 0, w, h);
            for (uint32_t j = 0; j < h; ++j)
                y[j] = witnessed_swizzle_index(0, j, w, h);
        }
    };
    SwizzleTables swizzle_tables_;
    // (beads-xbox.1.15.10 step 3: witnessed_guest_pixel, the per-pixel
    // "which guest pixel is (x, y)" helper, is deleted. Every one of its five
    // call sites was the inner statement of a full-surface loop, and each is
    // now either a row memcpy -- the pitch layout only ever needed the vertical
    // flip -- or a lookup in the separable Morton tables above.)

    bool ensure_witnessed_texture(const Nv2aWitnessedProfilePacket& packet,
                                  Nv2aWitnessedProfileResult& result,
                                  std::string& error) {
        if (witnessed_texture_ && witnessed_texture_hash_ == packet.texture_hash &&
            witnessed_texture_width_ == packet.texture_width &&
            witnessed_texture_height_ == packet.texture_height) {
            return true;   // same bytes as last upload; nothing to send
        }
        if (!witnessed_texture_) {
            gen_textures_(1, &witnessed_texture_);
            if (!witnessed_texture_) {
                error = "OpenGL witnessed texture allocation failed";
                return false;
            }
            cache_bind_texture_unit(kGlTexture0, witnessed_texture_);
            tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        } else {
            cache_bind_texture_unit(kGlTexture0, witnessed_texture_);
        }
        // packet.texture_linear is already unswizzled by the device (it owns the
        // SwizzleLookup) and holds guest A8R8G8B8 words, so the byte shuffle is
        // the same one guest_words_to_rgba does.
        const size_t pixels = static_cast<size_t>(packet.texture_width) *
                              packet.texture_height;
        if (packet.texture_linear.size() != pixels * 4u) {
            error = "OpenGL witnessed texture snapshot has the wrong size";
            return false;
        }
        // beads-xbox.1.15.10 step 3. The guest A8R8G8B8 words are already in
        // GL_BGRA byte order, so the shuffle-into-a-staging-buffer step is
        // deleted outright and the snapshot is uploaded in place; and an
        // unchanged geometry re-uploads through glTexSubImage2D.
        pixel_store_i_(kGlUnpackAlignment, 4);
        if (witnessed_texture_alloc_w_ == packet.texture_width &&
            witnessed_texture_alloc_h_ == packet.texture_height) {
            gpu_timed_tex_sub_image_2d(kGlTexture2D, 0, 0, 0,
                                       static_cast<int>(packet.texture_width),
                                       static_cast<int>(packet.texture_height),
                                       kGlBgra, kGlUnsignedByte,
                                       packet.texture_linear.data());
        } else {
            gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlRgba8,
                                   static_cast<int>(packet.texture_width),
                                   static_cast<int>(packet.texture_height), 0,
                                   kGlBgra, kGlUnsignedByte,
                                   packet.texture_linear.data());
            witnessed_texture_alloc_w_ = packet.texture_width;
            witnessed_texture_alloc_h_ = packet.texture_height;
        }
        if (get_error_() != kGlNoError) {
            error = "OpenGL witnessed texture upload failed";
            return false;
        }
        witnessed_texture_hash_ = packet.texture_hash;
        witnessed_texture_width_ = packet.texture_width;
        witnessed_texture_height_ = packet.texture_height;
        result.texture_upload_bytes = pixels * 4u;
        return true;
    }

    // beads-xbox.1.15.10 phase 4 (lever A). kPitchProjectedColor's stage-0
    // image, on texture unit 1. A separate object and a separate upload cache
    // from the cube image above: the two are different guest ranges with
    // different addressing modes, and sharing one slot would make every
    // alternation between the projected and textured programs re-upload both.
    // The upload itself is the same transform -- guest A8R8G8B8 words are
    // already GL_BGRA bytes, so the snapshot goes in place -- and an unchanged
    // geometry re-uploads through glTexSubImage2D.
    bool ensure_witnessed_projected_texture(
        const Nv2aWitnessedProfilePacket& packet,
        Nv2aWitnessedProfileResult& result, std::string& error) {
        if (witnessed_projected_texture_ &&
            witnessed_projected_texture_hash_ ==
                packet.projected_texture_hash &&
            witnessed_projected_texture_width_ ==
                packet.projected_texture_width &&
            witnessed_projected_texture_height_ ==
                packet.projected_texture_height) {
            return true;
        }
        if (!witnessed_projected_texture_) {
            gen_textures_(1, &witnessed_projected_texture_);
            if (!witnessed_projected_texture_) {
                error = "OpenGL witnessed projected texture allocation failed";
                return false;
            }
            cache_bind_texture_unit(kGlTexture1, witnessed_projected_texture_);
            // texelFetch ignores every one of these; they are set so the object
            // is never left in a state a future sampler read would surprise on.
            tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlRepeat);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlRepeat);
        } else {
            cache_bind_texture_unit(kGlTexture1, witnessed_projected_texture_);
        }
        const size_t pixels =
            static_cast<size_t>(packet.projected_texture_width) *
            packet.projected_texture_height;
        if (packet.projected_texture_linear.size() != pixels * 4u) {
            error =
                "OpenGL witnessed projected texture snapshot has the wrong size";
            cache_bind_texture_unit(kGlTexture0, witnessed_texture_);
            return false;
        }
        pixel_store_i_(kGlUnpackAlignment, 4);
        if (witnessed_projected_texture_alloc_w_ ==
                packet.projected_texture_width &&
            witnessed_projected_texture_alloc_h_ ==
                packet.projected_texture_height) {
            gpu_timed_tex_sub_image_2d(
                kGlTexture2D, 0, 0, 0,
                static_cast<int>(packet.projected_texture_width),
                static_cast<int>(packet.projected_texture_height), kGlBgra,
                kGlUnsignedByte, packet.projected_texture_linear.data());
        } else {
            gpu_timed_tex_image_2d(
                kGlTexture2D, 0, kGlRgba8,
                static_cast<int>(packet.projected_texture_width),
                static_cast<int>(packet.projected_texture_height), 0, kGlBgra,
                kGlUnsignedByte, packet.projected_texture_linear.data());
            witnessed_projected_texture_alloc_w_ =
                packet.projected_texture_width;
            witnessed_projected_texture_alloc_h_ =
                packet.projected_texture_height;
        }
        // Leave unit 0 active: every other site in this file binds without
        // selecting a unit first and would otherwise land on unit 1.
        cache_bind_texture_unit(kGlTexture0, witnessed_texture_);
        if (get_error_() != kGlNoError) {
            error = "OpenGL witnessed projected texture upload failed";
            return false;
        }
        witnessed_projected_texture_hash_ = packet.projected_texture_hash;
        witnessed_projected_texture_width_ = packet.projected_texture_width;
        witnessed_projected_texture_height_ = packet.projected_texture_height;
        result.texture_upload_bytes += pixels * 4u;
        return true;
    }

    bool ensure_witnessed_surface(const Nv2aWitnessedProfilePacket& packet,
                                  ResidentSurface*& out,
                                  ResidentSurface*& out_depth,
                                  Nv2aWitnessedProfileResult& result,
                                  std::string& error) {
        const uint32_t width = packet.surface_width;
        const uint32_t height = packet.surface_height;
        // A slot is OURS if EITHER plane still matches. The colour and the zeta
        // plane are invalidated independently -- a CPU draw of a profile that
        // reads depth without writing it invalidates only the colour -- so a
        // slot can be colour-invalid while still holding the authoritative,
        // DIRTY zeta image. Matching on the colour key alone then sent the next
        // upload to a different slot and left two resident copies of the same
        // zeta range, and resolve_resident_depth_surface takes the first dirty
        // match: the stale one won and the depth the guest read back was one
        // epoch old. That is a silent depth corruption whose symptom is large
        // smooth colour differences inside a single frame, which is exactly what
        // the 4.0 G checkpoint showed.
        //
        // beads-xbox.1.15.10 phase 3: the selection is now ONE function, shared
        // with witnessed_target_residency, so the device's per-plane snapshot
        // decision cannot drift from what this does with it.
        //
        // beads-xbox.1.15.10 phase 4 (lever C): and the two planes are selected
        // INDEPENDENTLY. `slot` (colour) and `depth_slot` may be different
        // entries; the FBO is then assembled from both, which is what the
        // generic resident path has always done. See select_witnessed_slots.
        const ResidentSurface* found_color = nullptr;
        const ResidentSurface* found_depth = nullptr;
        bool colour_resident = false;
        bool depth_resident = false;
        select_witnessed_slots(packet.color_address, packet.surface_size,
                               packet.surface_pitch, packet.surface_height,
                               packet.surface_format, packet.depth_address,
                               found_color, found_depth, colour_resident,
                               depth_resident);
        ResidentSurface* slot =
            colour_resident ? const_cast<ResidentSurface*>(found_color)
                            : nullptr;
        ResidentSurface* depth_slot =
            depth_resident ? const_cast<ResidentSurface*>(found_depth)
                           : nullptr;
        // Legacy arm only: the zeta-key fallback slot is where the colour will
        // LAND, so it is carried past the residency test as the landing slot.
        ResidentSurface* legacy_slot =
            witnessed_plane_split_
                ? nullptr
                : const_cast<ResidentSurface*>(found_color);
        if (colour_resident && depth_resident) {
            out = slot;
            out_depth = depth_slot;
            active_resident_ = slot;
            active_depth_resident_ = depth_slot;
            if (slot != depth_slot) ++witnessed_upload_census_.plane_split_hits;
            return true;
        }
        // Only the planes that are NOT already resident are uploaded. Uploading
        // a dirty resident zeta from guest memory would discard GPU-side depth
        // writes that no resolve has moved back yet.
        const bool upload_colour = !colour_resident;
        const bool upload_depth = !depth_resident;
        if ((upload_colour && packet.color_input.size() != packet.surface_size) ||
            (upload_depth && packet.depth_input.size() != packet.surface_size)) {
            error = "OpenGL witnessed draw missing its initial surface snapshot";
            return false;
        }
        if (!slot) {
            if (!witnessed_plane_split_) {
                // Pre-phase-4: the zeta-key fallback slot, else any reusable
                // slot, and the colour is written over whatever it held.
                slot = legacy_slot ? legacy_slot : find_reusable_resident_slot();
            } else if (depth_slot && !depth_slot->valid) {
                // Prefer the slot the zeta already lives in, but ONLY when its
                // colour half is free. Taking it while it holds a different
                // valid colour is precisely the 342-takeover defect this
                // replaces.
                slot = note_color_slot_reuse(*depth_slot);
            } else {
                slot = acquire_color_slot(depth_slot);
            }
            if (!slot) {
                error = "OpenGL resident surface cache is full of dirty targets";
                return false;
            }
        }
        if (!depth_slot && !witnessed_plane_split_) {
            depth_slot = slot;
        }
        if (!depth_slot) {
            // The colour slot is the preferred home for the zeta -- one slot,
            // one FBO pair, and the common case stays exactly as it was. It is
            // refused only when that would overwrite a DIRTY zeta plane of a
            // different guest range, which would discard GPU depth writes no
            // resolve has moved back.
            if (!slot->depth_valid || !slot->depth_dirty ||
                (slot->depth_address == packet.depth_address &&
                 slot->depth_size == packet.surface_size)) {
                depth_slot = note_depth_slot_reuse(*slot);
            } else {
                depth_slot = acquire_depth_slot(slot);
            }
            if (!depth_slot) {
                error = "OpenGL resident surface cache is full of dirty targets";
                return false;
            }
        }
        if (slot != depth_slot) ++witnessed_upload_census_.plane_split_uploads;
        if (!slot->texture) {
            gen_textures_(1, &slot->texture);
            if (!slot->texture) {
                error = "OpenGL witnessed colour texture allocation failed";
                return false;
            }
            cache_bind_texture_unit(kGlTexture0, slot->texture);
            tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        } else {
            cache_bind_texture_unit(kGlTexture0, slot->texture);
        }
        if (upload_colour) {
        // Colour: guest words (linear or Morton) -> bottom-up image, the same
        // storage convention guest_words_to_rgba establishes for the generic
        // resident path, so one resolve path serves both.
        //
        // beads-xbox.1.15.10 step 3 (upload staging). Two changes, both exact:
        //  * GL_BGRA, not GL_RGBA. A guest A8R8G8B8 word is little-endian, so
        //    its bytes in memory are already B,G,R,A -- which is precisely what
        //    GL_BGRA/GL_UNSIGNED_BYTE means. The per-pixel byte shuffle that
        //    was here reordered them to R,G,B,A only to have the driver put
        //    them back. The RESOLVE has always read back with GL_BGRA and
        //    memcpy'd rows straight into guest words, so this makes the two
        //    directions the same transform; the texels are unchanged.
        //  * the pitch layout is a straight row copy. It only ever needed the
        //    vertical flip, and calling witnessed_guest_pixel per pixel to
        //    compute y*width+x cost 614 400 calls per 1280x480 upload.
        // The Morton path keeps its per-pixel scatter -- there is no row
        // structure to exploit -- but copies a 4-byte word instead of shuffling
        // four bytes.
        const size_t row_bytes = static_cast<size_t>(width) * 4u;
        // The row copy reads [0, row_bytes*height) out of the snapshot, and the
        // Morton scatter reads the same range through a permutation, so one
        // check covers both. It was implicit before and is now stated, because
        // the row copy would read past the end instead of indexing past it.
        if (packet.color_input.size() < row_bytes * height) {
            error = "OpenGL witnessed colour snapshot is smaller than its surface";
            return false;
        }
        resize_or_zero(witnessed_surface_staging_, row_bytes * height,
                       packet.swizzled_destination ? width : 1u,
                       packet.swizzled_destination ? height : 1u);
        uint8_t* dst_base = witnessed_surface_staging_.data();
        const uint8_t* src_base = packet.color_input.data();
        if (!packet.swizzled_destination) {
            for (uint32_t y = 0; y < height; ++y) {
                std::memcpy(dst_base + static_cast<size_t>(height - 1u - y) * row_bytes,
                            src_base + static_cast<size_t>(y) * row_bytes,
                            row_bytes);
            }
        } else {
            swizzle_tables_.ensure(width, height);
            const uint32_t* sx = swizzle_tables_.x.data();
            for (uint32_t y = 0; y < height; ++y) {
                uint8_t* row = dst_base +
                               static_cast<size_t>(height - 1u - y) * row_bytes;
                const uint32_t yb = swizzle_tables_.y[y];
                for (uint32_t x = 0; x < width; ++x) {
                    std::memcpy(row + static_cast<size_t>(x) * 4u,
                                src_base + static_cast<size_t>(sx[x] | yb) * 4u,
                                4u);
                }
            }
        }
        pixel_store_i_(kGlUnpackAlignment, 4);
        // glTexImage2D RE-SPECIFIES storage: the driver drops the allocation
        // and makes a new one on every upload. glTexSubImage2D reuses it, and
        // the geometry is unchanged on all but the first upload into a slot.
        if (slot->tex_alloc_w == width && slot->tex_alloc_h == height) {
            gpu_timed_tex_sub_image_2d(kGlTexture2D, 0, 0, 0,
                                       static_cast<int>(width),
                                       static_cast<int>(height), kGlBgra,
                                       kGlUnsignedByte,
                                       witnessed_surface_staging_.data());
        } else {
            gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlRgba8,
                                   static_cast<int>(width),
                                   static_cast<int>(height), 0, kGlBgra,
                                   kGlUnsignedByte,
                                   witnessed_surface_staging_.data());
            slot->tex_alloc_w = width;
            slot->tex_alloc_h = height;
        }
        }
        if (!depth_slot->depth_texture) {
            gen_textures_(1, &depth_slot->depth_texture);
            if (!depth_slot->depth_texture) {
                error = "OpenGL witnessed zeta texture allocation failed";
                return false;
            }
            cache_bind_texture_unit(kGlTexture0, depth_slot->depth_texture);
            tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        } else {
            cache_bind_texture_unit(kGlTexture0, depth_slot->depth_texture);
        }
        if (upload_depth) {
        // Zeta: the guest word IS GL_UNSIGNED_INT_24_8, depth in the high 24
        // bits and stencil in the low 8, so the plane needs only the layout and
        // vertical flip -- no numeric conversion at all, in either direction.
        // Same two changes as the colour plane: the pitch layout is a row copy
        // (the flip is the whole of the transform), and an unchanged geometry
        // re-uploads through glTexSubImage2D instead of re-specifying storage.
        const size_t depth_row_words = static_cast<size_t>(width);
        if (packet.depth_input.size() < depth_row_words * height * 4u) {
            error = "OpenGL witnessed zeta snapshot is smaller than its surface";
            return false;
        }
        resize_or_zero(witnessed_depth_staging_, depth_row_words * height,
                       packet.swizzled_destination ? width : 1u,
                       packet.swizzled_destination ? height : 1u);
        uint32_t* depth_dst = witnessed_depth_staging_.data();
        const uint8_t* depth_src = packet.depth_input.data();
        if (!packet.swizzled_destination) {
            for (uint32_t y = 0; y < height; ++y) {
                std::memcpy(depth_dst +
                                static_cast<size_t>(height - 1u - y) * depth_row_words,
                            depth_src + static_cast<size_t>(y) * depth_row_words * 4u,
                            depth_row_words * 4u);
            }
        } else {
            swizzle_tables_.ensure(width, height);
            const uint32_t* sx = swizzle_tables_.x.data();
            for (uint32_t y = 0; y < height; ++y) {
                uint32_t* row = depth_dst +
                                static_cast<size_t>(height - 1u - y) * depth_row_words;
                const uint32_t yb = swizzle_tables_.y[y];
                for (uint32_t x = 0; x < width; ++x)
                    std::memcpy(row + x,
                                depth_src + static_cast<size_t>(sx[x] | yb) * 4u,
                                4u);
            }
        }
        pixel_store_i_(kGlUnpackAlignment, 4);
        if (depth_slot->depth_alloc_w == width && depth_slot->depth_alloc_h == height) {
            gpu_timed_tex_sub_image_2d(kGlTexture2D, 0, 0, 0,
                                       static_cast<int>(width),
                                       static_cast<int>(height),
                                       kGlDepthStencil, kGlUnsignedInt248,
                                       witnessed_depth_staging_.data());
        } else {
            gpu_timed_tex_image_2d(kGlTexture2D, 0, kGlDepth24Stencil8,
                                   static_cast<int>(width),
                                   static_cast<int>(height), 0, kGlDepthStencil,
                                   kGlUnsignedInt248,
                                   witnessed_depth_staging_.data());
            depth_slot->depth_alloc_w = width;
            depth_slot->depth_alloc_h = height;
        }
        }
        if (!resident_fbo_) gen_framebuffers_(1, &resident_fbo_);
        cache_bind_framebuffer(resident_fbo_);
        cache_framebuffer_color_texture(resident_fbo_, slot->texture);
        cache_framebuffer_depth_texture(resident_fbo_, depth_slot->depth_texture);
        if (check_framebuffer_status_(kGlFramebuffer) != kGlFramebufferComplete) {
            error = "OpenGL witnessed colour+zeta framebuffer is incomplete";
            return false;
        }
        if (get_error_() != kGlNoError) {
            error = "OpenGL witnessed surface upload failed";
            return false;
        }
        if (upload_colour) {
            // Overwriting a plane the GPU wrote and no resolve has moved back
            // destroys guest-visible output. Counted, always on, so the CLEAR
            // path above cannot lose a clear without saying so.
            if (slot->valid && slot->dirty)
                ++witnessed_upload_census_.upload_over_dirty_color;
            if (slot->valid && slot->address == packet.color_address)
                ++witnessed_upload_census_.color_key_mismatch;
            // The slot still held a DIFFERENT colour surface. Writing this
            // packet's colour into it ends that surface's residency with no
            // invalidation behind it, which is why its next upload reports
            // `cold`. Counted so the size of that path is a number.
            if (slot->valid && slot->address != packet.color_address)
                ++witnessed_upload_census_.color_slot_takeover;
            note_upload_cause(witnessed_upload_census_.color_uploads,
                              witnessed_upload_census_.color_bytes,
                              take_invalidate_stamp(color_stamps_,
                                                    packet.color_address),
                              packet.surface_size);
            slot->valid = true;
            slot->dirty = false;
            slot->dirty_rect.reset();
            slot->x8 = false;
            slot->swizzled = packet.swizzled_destination;
            slot->address = packet.color_address;
            slot->size = packet.surface_size;
            slot->pitch = packet.surface_pitch;
            slot->width = width;
            slot->height = height;
            slot->format = packet.surface_format;
            result.color_upload_bytes = packet.surface_size;
        }
        if (upload_depth) {
            if (depth_slot->depth_valid && depth_slot->depth_dirty)
                ++witnessed_upload_census_.upload_over_dirty_depth;
            if (depth_slot->depth_valid &&
                depth_slot->depth_address == packet.depth_address)
                ++witnessed_upload_census_.depth_key_mismatch;
            if (depth_slot->depth_valid &&
                depth_slot->depth_address != packet.depth_address)
                ++witnessed_upload_census_.depth_slot_takeover;
            note_upload_cause(witnessed_upload_census_.depth_uploads,
                              witnessed_upload_census_.depth_bytes,
                              take_invalidate_stamp(depth_stamps_,
                                                    packet.depth_address),
                              packet.surface_size);
            depth_slot->depth_valid = true;
            depth_slot->depth_dirty = false;
            depth_slot->depth_dirty_rect.reset();
            depth_slot->depth_address = packet.depth_address;
            depth_slot->depth_size = packet.surface_size;
            depth_slot->depth_pitch = packet.surface_pitch;
            depth_slot->depth_height = packet.surface_height;
            depth_slot->depth_format = packet.surface_format;
            result.depth_upload_bytes = packet.surface_size;
        }
        active_resident_ = slot;
        active_depth_resident_ = depth_slot;
        out = slot;
        out_depth = depth_slot;
        return true;
    }

    bool compile_resident_dashboard(std::string& error) {
        return compile_graphics_program(kResidentDashboardProject2dFanVertexSource,
                                        kResidentDashboardProject2dFanFragmentSource,
                                        "resident-dashboard",
                                        resident_dashboard_program_, error);
    }
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool owns_video_ = false;
    unsigned int fixture_program_ = 0;
    unsigned int primary_color_program_ = 0;
    unsigned int texture_modulate_program_ = 0;
    unsigned int dashboard_program_ = 0;
    unsigned int resident_program_ = 0;
    unsigned int resident_alpha_program_ = 0;
    unsigned int resident_texture_modulate_program_ = 0;
    unsigned int resident_dashboard_program_ = 0;
    unsigned int resident_source_texture_ = 0;
    unsigned int resident_fbo_ = 0;
    unsigned int resident_vbo_ = 0;
#if XLLE_VSH_GPU_RESIDENT
    unsigned int vsh_batch_program_ = 0, vsh_pack_program_ = 0;
    unsigned int vsh_parallel_pack_program_ = 0;
    bool vsh_parallel_pack_ = false;
    bool vsh_specialize_ = false;
    bool vsh_status_summary_ = false;
    bool vsh_native_fp32_ = false;
    struct VshSpecialization {
        std::vector<vsh_gpu_batch::Op> ops;
        unsigned int program = 0;
    };
    std::vector<VshSpecialization> vsh_specializations_;
    std::atomic<uint64_t> vsh_compile_ns_{}, vsh_compile_calls_{};
    std::array<unsigned int, 9> vsh_buffers_{};
    VshGpuResidentStats vsh_resident_stats_{};
    uint64_t vsh_preparation_token_ = 0;
    uint32_t vsh_prepared_vertices_ = 0;
    const Nv2aGenericCombinerPacket::GpuVshReference* prepared_vsh_reference_ = nullptr;
    // CPU phases are raw inclusive wall durations, not the exclusive partition.
    // Total contains every child phase plus setup/destruction. GPU times overlap
    // blocking CPU calls and must never be added to them.
    std::array<std::atomic<uint64_t>, 10> vsh_cpu_ns_{}, vsh_cpu_calls_{};
    std::array<uint64_t, 2> vsh_gpu_ns_{}, vsh_gpu_calls_{};
    uint64_t vsh_upload_bytes_ = 0, vsh_download_bytes_ = 0;
#endif
    unsigned int resident_vao_ = 0;
    // beads-xbox.1.15.10 phase 3: 16 was TRIED and is NULL. 342 of the 647
    // colour re-uploads on the canonical 12 B run (686 MB) are slot TAKEOVERS
    // -- ensure_witnessed_surface repurposing a slot that still holds a
    // different VALID colour surface, reached through the zeta-key fallback --
    // and doubling the cache changed the census by exactly nothing
    // (takeover_color 342 either way, byte-identical upload and resolve
    // tables). It is not capacity pressure: two colour surfaces are contending
    // for ONE slot because the fallback matches on the zeta plane. Do not retry
    // the capacity knob; fix the fallback to move the zeta to a free slot
    // instead of stealing the colour. witnessed_upload_census()
    // .color_slot_takeover is the number to watch.
    static constexpr size_t kMaxResidentSurfaces = 8;
    std::array<ResidentSurface, kMaxResidentSurfaces> resident_surfaces_{};
    uint64_t stamp_overflows_ = 0;
    std::array<InvalidateStamp, kInvalidateStampSlots> color_stamps_{};
    std::array<InvalidateStamp, kInvalidateStampSlots> depth_stamps_{};
    Nv2aWitnessedUploadCensus witnessed_upload_census_{};
    std::vector<uint8_t> region_staging_;
    // Recycled glReadPixels target for resident color resolves.
    std::vector<uint8_t> resolve_readback_scratch_;
    // Persistent shader-storage staging for the non-resident compute fixture
    // paths. Generated once and re-specified with glBufferData per submit;
    // the previous per-submit glGenBuffers/glDeleteBuffers pair was a major
    // driver-entry and host-memory-churn source.
    std::array<unsigned int, 6> compute_staging_ssbo_{};
    // Recycled guest->RGBA staging for resident uploads.
    std::vector<uint8_t> upload_staging_;
    ResidentSurface* active_resident_ = nullptr;
    ResidentSurface* active_depth_resident_ = nullptr;
    std::array<CachedBuffer, 4> dashboard_texture_cache_{};
    GlCreateShader create_shader_ = nullptr; GlShaderSource shader_source_ = nullptr;
    GlCompileShader compile_shader_ = nullptr; GlGetShaderiv get_shader_iv_ = nullptr;
    GlGetShaderInfoLog get_shader_log_ = nullptr; GlDeleteShader delete_shader_ = nullptr;
    GlCreateProgram create_program_ = nullptr; GlAttachShader attach_shader_ = nullptr;
    GlLinkProgram link_program_ = nullptr; GlGetProgramiv get_program_iv_ = nullptr;
    GlGetProgramInfoLog get_program_log_ = nullptr; GlDeleteProgram delete_program_ = nullptr;
    GlUseProgram use_program_ = nullptr; GlGetUniformLocation uniform_location_ = nullptr;
    GlUniform1i uniform_1i_ = nullptr; GlUniform1ui uniform_1ui_ = nullptr;
    GlUniform1f uniform_1f_ = nullptr;
    GlUniform4i uniform_4i_ = nullptr;
#if XLLE_NATIVE_DRAW
    GlEnable native_cull_face_ = nullptr, native_front_face_ = nullptr;
    uint64_t native_attempts_ = 0, native_draws_ = 0, native_vertices_ = 0, native_triangles_ = 0;
#endif
    bool enhanced_xemu_ = false;
    uint64_t enhanced_attempts_ = 0, enhanced_draws_ = 0, enhanced_triangles_ = 0;
    std::array<uint64_t, static_cast<size_t>(XemuPshReject::kCount)> enhanced_decisions_{};
    GlUniform2i uniform_2i_ = nullptr; GlUniform2f uniform_2f_ = nullptr;
    GlUniform4f uniform_4f_ = nullptr; GlUniform4fv uniform_4fv_ = nullptr;
    GlGenBuffers gen_buffers_ = nullptr;
    GlBindBuffer bind_buffer_ = nullptr; GlBufferData buffer_data_ = nullptr;
    GlBindBufferBase bind_buffer_base_ = nullptr; GlDispatchCompute dispatch_compute_ = nullptr;
    GlMemoryBarrier memory_barrier_ = nullptr; GlGetBufferSubData get_buffer_sub_data_ = nullptr;
    GlDeleteBuffers delete_buffers_ = nullptr; GlGenTextures gen_textures_ = nullptr;
    GlBindTexture bind_texture_ = nullptr; GlTexImage2D tex_image_2d_ = nullptr;
    GlTexSubImage2D tex_sub_image_2d_ = nullptr; GlTexParameteri tex_parameter_i_ = nullptr;
    GlDeleteTextures delete_textures_ = nullptr; GlActiveTexture active_texture_ = nullptr;
    GlBindImageTexture bind_image_texture_ = nullptr;
    GlGenFramebuffers gen_framebuffers_ = nullptr;
    GlBindFramebuffer bind_framebuffer_ = nullptr;
    GlFramebufferTexture2D framebuffer_texture_2d_ = nullptr;
    GlCheckFramebufferStatus check_framebuffer_status_ = nullptr;
    GlDeleteFramebuffers delete_framebuffers_ = nullptr; GlViewport viewport_ = nullptr;
    GlScissor scissor_ = nullptr; GlEnable enable_ = nullptr; GlDisable disable_ = nullptr;
    GlDepthFunc depth_func_ = nullptr; GlDepthMask depth_mask_ = nullptr;
    GlBlendFunc blend_func_ = nullptr; GlBlendEquation blend_equation_ = nullptr;
    GlBlendFuncSeparate blend_func_separate_ = nullptr;
    GlColorMask color_mask_ = nullptr; GlGenVertexArrays gen_vertex_arrays_ = nullptr;
    GlBindVertexArray bind_vertex_array_ = nullptr;
    GlDeleteVertexArrays delete_vertex_arrays_ = nullptr;
    GlVertexAttribPointer vertex_attrib_pointer_ = nullptr;
    GlEnableVertexAttribArray enable_vertex_attrib_array_ = nullptr;
    GlDisableVertexAttribArray disable_vertex_attrib_array_ = nullptr;
    GlDrawArrays draw_arrays_ = nullptr; GlReadPixels read_pixels_ = nullptr;
    GlPixelStorei pixel_store_i_ = nullptr; GlGetError get_error_ = nullptr;

    // ---- GPU-side timing (beads-xbox.1.14.6) -----------------------------
    // Optional entry points; initialize() loads them after the required set and
    // clears gpu_timing_available_ instead of failing when any is missing.
    GlGenQueries gen_queries_ = nullptr;
    GlDeleteQueries delete_queries_ = nullptr;
    GlBeginQuery begin_query_ = nullptr;
    GlEndQuery end_query_ = nullptr;
    GlGetQueryObjectuiv get_query_object_uiv_ = nullptr;
    GlGetQueryObjectui64v get_query_object_ui64v_ = nullptr;
    GlFenceSync fence_sync_ = nullptr;
    GlClientWaitSync client_wait_sync_ = nullptr;
    GlDeleteSync delete_sync_ = nullptr;
    GlFinish finish_ = nullptr;
    GlFlush flush_ = nullptr;
    // ---- stage (f) witnessed-profile GPU execution (beads-xbox.1.15.7) ----
    GlClear clear_ = nullptr;
    GlClearColor clear_color_ = nullptr;
    GlClearDepth clear_depth_ = nullptr;
    GlClearStencil clear_stencil_ = nullptr;
    GlStencilMask stencil_mask_ = nullptr;
    GlUniform3f uniform_3f_ = nullptr;
    GlBufferSubData buffer_sub_data_ = nullptr;
    bool witnessed_available_ = false;
    // ---- stage (f3): the generated generic-combiner executor -------------
    bool generated_available_ = false;
    bool generated_nodraw_ = false;
    bool generated_bgra_upload_ = true;
    bool generated_uniform_batching_ = false;
#if XLLE_GENERATED_SUBMIT_DIAGNOSTICS
    bool generated_submit_census_ = false;
    GeneratedSubmitCpuStats generated_submit_cpu_stats_{};
#endif
    struct GeneratedTextureSlot {
        unsigned int texture = 0;
        uint64_t hash = 0;
        uint64_t content_id = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        // Phase 7 (upload staging): the geometry the GL storage was actually
        // allocated with, so an unchanged geometry re-uploads through
        // glTexSubImage2D instead of re-specifying storage. Same distinction
        // ResidentSurface::tex_alloc_w draws, and for the same reason.
        uint32_t alloc_w = 0;
        uint32_t alloc_h = 0;
        bool valid = false;
    };
    std::vector<std::pair<uint64_t, GeneratedProgram>> generated_programs_;
    std::array<GeneratedTextureSlot, 4> generated_textures_{};
    unsigned int generated_ssbo_ = 0;
    unsigned int generated_vao_ = 0;
    std::vector<uint8_t> generated_records_;
    GeneratedShaderStats generated_stats_{};
    bool witnessed_fixed_state_ = false;
    bool witnessed_context_owned_ = false;
    unsigned int witnessed_bound_program_ = 0;
    // One cached uniform block per witnessed program. `uniforms_valid` is
    // cleared whenever the program is rebound, because a foreign use_program
    // between two submits would otherwise let a stale mirror suppress a needed
    // write.
    struct WitnessedProgram {
        unsigned int program = 0;
        int loc_surface_size = -1;
        int loc_clip_min = -1;
        int loc_clip_max = -1;
        int loc_texture0 = -1;
        int loc_texture_size = -1;
        int loc_texture1 = -1;
        int loc_projected_texture_size = -1;
        int loc_weight0 = -1;
        int loc_weight1 = -1;
        bool uniforms_valid = false;
        float surface_w = 0.0f, surface_h = 0.0f;
        float clip_min = 0.0f, clip_max = 0.0f;
        float texture_w = 0.0f, texture_h = 0.0f;
        float proj_texture_w = 0.0f, proj_texture_h = 0.0f;
        uint32_t factor0 = 0xFFFFFFFFu, factor1 = 0xFFFFFFFFu;
    };
    WitnessedProgram witnessed_radial_;
    WitnessedProgram witnessed_textured_;
    WitnessedProgram witnessed_projected_;
    unsigned int witnessed_radial_program_ = 0;
    unsigned int witnessed_textured_program_ = 0;
    unsigned int witnessed_projected_program_ = 0;
    // beads-xbox.1.15.10 step 1. ONE buffer name was enough while the batch was
    // issued only at a resolve, because the resolve had already drained the GPU
    // before the next glBufferData touched the buffer. Once the draws are
    // flushed early the previous draw is still IN FLIGHT reading this buffer
    // when the next batch refills it, and glBufferData then either orphans (a
    // fresh multi-hundred-kilobyte allocation per flush) or waits. A small
    // round-robin of buffer names removes both: by the time a name comes back
    // around, ring_size-1 draws have been issued after it. Size 1 is the
    // pre-step-1 behavior and is the bisection arm.
    static constexpr size_t kWitnessedSsboRingMax = 16;
    std::array<unsigned int, kWitnessedSsboRingMax> witnessed_ssbo_ring_{};
    size_t witnessed_ssbo_ring_size_ = 1;
    size_t witnessed_ssbo_ring_head_ = 0;
    unsigned int witnessed_ssbo_ = 0;
    size_t witnessed_ssbo_bytes_ = 0;
    unsigned int witnessed_vao_ = 0;
    unsigned int witnessed_texture_ = 0;
    uint64_t witnessed_texture_hash_ = 0;
    uint32_t witnessed_texture_width_ = 0;
    uint32_t witnessed_texture_height_ = 0;
    // beads-xbox.1.15.10 phase 4 (lever A): kPitchProjectedColor's second
    // image. Its own object and its own upload cache, on texture unit 1, so a
    // projected batch never re-uploads the cube image and vice versa.
    unsigned int witnessed_projected_texture_ = 0;
    uint64_t witnessed_projected_texture_hash_ = 0;
    uint32_t witnessed_projected_texture_width_ = 0;
    uint32_t witnessed_projected_texture_height_ = 0;
    uint32_t witnessed_projected_texture_alloc_w_ = 0;
    uint32_t witnessed_projected_texture_alloc_h_ = 0;
    // Dimensions the witnessed texture's GL storage was allocated with; see
    // ResidentSurface::tex_alloc_w. The staging vector the shuffle used is
    // gone: the guest snapshot is uploaded in place as GL_BGRA.
    uint32_t witnessed_texture_alloc_w_ = 0;
    uint32_t witnessed_texture_alloc_h_ = 0;
    std::vector<uint8_t> witnessed_surface_staging_;
    std::vector<uint32_t> witnessed_depth_staging_;
    std::vector<uint8_t> witnessed_records_;
    // Everything the shader reads that is NOT in a triangle record. Two
    // consecutive GPU jobs whose keys are equal are merged into one draw.
    struct WitnessedBatchKey {
        Nv2aWitnessedProfilePacket::Profile profile =
            Nv2aWitnessedProfilePacket::Profile::kSwizzledRadialColor;
        uint32_t color_address = 0;
        uint32_t depth_address = 0;
        size_t   surface_size = 0;
        uint32_t surface_pitch = 0;
        uint32_t surface_format = 0;
        uint32_t surface_width = 0;
        uint32_t surface_height = 0;
        uint32_t xmin = 0, xmax = 0, ymin = 0, ymax = 0;
        float clip_min = 0.0f, clip_max = 0.0f;
        uint64_t texture_hash = 0;
        uint32_t texture_width = 0, texture_height = 0;
        // kPitchProjectedColor's second sampler; zero for the other profiles.
        uint64_t projected_texture_hash = 0;
        uint32_t projected_texture_width = 0, projected_texture_height = 0;
        uint32_t factor0_stage6 = 0, factor1_stage6 = 0;
        bool operator==(const WitnessedBatchKey& o) const {
            return profile == o.profile && color_address == o.color_address &&
                   depth_address == o.depth_address &&
                   surface_size == o.surface_size &&
                   surface_pitch == o.surface_pitch &&
                   surface_format == o.surface_format &&
                   surface_width == o.surface_width &&
                   surface_height == o.surface_height && xmin == o.xmin &&
                   xmax == o.xmax && ymin == o.ymin && ymax == o.ymax &&
                   clip_min == o.clip_min && clip_max == o.clip_max &&
                   texture_hash == o.texture_hash &&
                   texture_width == o.texture_width &&
                   texture_height == o.texture_height &&
                   projected_texture_hash == o.projected_texture_hash &&
                   projected_texture_width == o.projected_texture_width &&
                   projected_texture_height == o.projected_texture_height &&
                   factor0_stage6 == o.factor0_stage6 &&
                   factor1_stage6 == o.factor1_stage6;
        }
    };
    WitnessedBatchKey witnessed_batch_key_;
    ResidentSurface* witnessed_batch_surface_ = nullptr;
    ResidentSurface* witnessed_batch_depth_surface_ = nullptr;
    size_t witnessed_batch_count_ = 0;
    uint64_t witnessed_draw_calls_ = 0;
    std::string witnessed_flush_error_;
    // beads-xbox.1.15.10 step 1 (overlap). Triangle bound on the pending batch;
    // 0 = unbounded, which is the pre-step-1 behavior. Set from the CLI through
    // set_witnessed_flush_triangles.
    uint64_t witnessed_flush_triangles_ = 0;
    bool witnessed_kick_flush_ = true;
    // beads-xbox.1.15.10 phase 4 (lever C). See set_witnessed_plane_split.
    bool witnessed_plane_split_ = true;
    bool witnessed_fp32_shader_ = false;
    // Early-flush accounting, always on and free when the path is not taken, so
    // "did the batch actually get issued early" is a measurement rather than an
    // inference from the draw-call count.
    uint64_t witnessed_flush_size_ = 0;    // issued because the bound was hit
    uint64_t witnessed_flush_kick_ = 0;    // issued by kick_pending_gpu_work
    uint64_t witnessed_gl_flushes_ = 0;    // glFlush calls that kicked a draw
    bool gpu_timing_available_ = false;
    bool gpu_timers_enabled_ = true;

    enum class GpuTimerBucket : uint8_t { kDraw, kUpload, kResolve
#if XLLE_VSH_GPU_RESIDENT
        , kVsh, kVshPack
#endif
    };
    struct GpuQuerySlot {
        unsigned int id = 0;
        bool in_flight = false;
        GpuTimerBucket bucket = GpuTimerBucket::kDraw;
    };
    // Ring of GL_TIME_ELAPSED query objects. Results are harvested in issue
    // order and only once GL_QUERY_RESULT_AVAILABLE says so, so collecting them
    // never blocks the emulation thread on the GPU (PRINCIPLES section 7: read
    // the buffer, do not arm-and-wait). A full ring drops the sample.
    std::array<GpuQuerySlot, kGpuQueryRingSize> gpu_query_ring_{};
    size_t gpu_query_head_ = 0;
    size_t gpu_query_live_ = 0;
    bool gpu_query_active_ = false;

    bool gpu_timing_active() const {
        return gpu_timing_available_ && perf::g_enabled && gpu_timers_enabled_;
    }

    // Collects finished queries in issue order. `block` is used once, from the
    // destructor, where GL_QUERY_RESULT waits for the last few submissions
    // rather than throwing them away.
    void harvest_gpu_queries(bool block) {
        while (gpu_query_live_ > 0) {
            const size_t tail =
                (gpu_query_head_ + kGpuQueryRingSize - gpu_query_live_) %
                kGpuQueryRingSize;
            GpuQuerySlot& slot = gpu_query_ring_[tail];
            if (!slot.in_flight || slot.id == 0) {
                slot.in_flight = false;
                --gpu_query_live_;
                continue;
            }
            if (!block) {
                unsigned int available = 0;
                get_query_object_uiv_(slot.id, kGlQueryResultAvailable,
                                      &available);
                if (!available) return;
            }
            uint64_t elapsed = 0;
            get_query_object_ui64v_(slot.id, kGlQueryResult, &elapsed);
            switch (slot.bucket) {
#if XLLE_VSH_GPU_RESIDENT
                case GpuTimerBucket::kVsh:
                case GpuTimerBucket::kVshPack: {
                    const size_t phase = slot.bucket == GpuTimerBucket::kVsh ? 0 : 1;
                    vsh_gpu_ns_[phase] += elapsed;
                    ++vsh_gpu_calls_[phase];
                    [[fallthrough]];
                }
#endif
                case GpuTimerBucket::kDraw:
                    g_gl_gpu.gpu_draw_ns.fetch_add(elapsed,
                                                   std::memory_order_relaxed);
                    g_gl_gpu.gpu_draw_n.fetch_add(1, std::memory_order_relaxed);
                    break;
                case GpuTimerBucket::kUpload:
                    g_gl_gpu.gpu_upload_ns.fetch_add(elapsed,
                                                     std::memory_order_relaxed);
                    g_gl_gpu.gpu_upload_n.fetch_add(1,
                                                    std::memory_order_relaxed);
                    break;
                case GpuTimerBucket::kResolve:
                    g_gl_gpu.gpu_resolve_ns.fetch_add(elapsed,
                                                      std::memory_order_relaxed);
                    g_gl_gpu.gpu_resolve_n.fetch_add(1,
                                                     std::memory_order_relaxed);
                    break;
            }
            slot.in_flight = false;
            --gpu_query_live_;
        }
    }

    // Opens one GL_TIME_ELAPSED region. Returns false -- and the caller then
    // must NOT call gpu_timer_end() -- when timing is off, when the ring is
    // full, or when a query is already active: GL permits only one
    // GL_TIME_ELAPSED query at a time, so a site entered inside another is
    // counted under nested_skipped instead of raising GL_INVALID_OPERATION,
    // which the get_error_() checks in the submit paths would turn into a
    // spurious draw failure.
    bool gpu_timer_begin(GpuTimerBucket bucket) {
        if (!gpu_timing_active()) return false;
        if (gpu_query_active_) {
            g_gl_gpu.nested_skipped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        harvest_gpu_queries(/*block=*/false);
        if (gpu_query_live_ >= kGpuQueryRingSize) {
            g_gl_gpu.queries_dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        GpuQuerySlot& slot = gpu_query_ring_[gpu_query_head_];
        if (slot.id == 0) {
            gen_queries_(1, &slot.id);
            if (slot.id == 0) {
                g_gl_gpu.queries_dropped.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        }
        slot.bucket = bucket;
        begin_query_(kGlTimeElapsed, slot.id);
        gpu_query_active_ = true;
        return true;
    }

    void gpu_timer_end() {
        end_query_(kGlTimeElapsed);
        gpu_query_ring_[gpu_query_head_].in_flight = true;
        gpu_query_head_ = (gpu_query_head_ + 1) % kGpuQueryRingSize;
        ++gpu_query_live_;
        gpu_query_active_ = false;
    }

    class ScopedGpuTimer {
    public:
        // gpu_timer_begin() is called from the constructor BODY, not a
        // mem-initializer: the enclosing class is only guaranteed complete
        // inside function bodies of a nested class.
        ScopedGpuTimer(OpenGlNv2aRenderer* owner, GpuTimerBucket bucket)
            : owner_(owner), started_(false) {
            started_ = owner_->gpu_timer_begin(bucket);
        }
        ~ScopedGpuTimer() {
            if (started_) owner_->gpu_timer_end();
        }
        ScopedGpuTimer(const ScopedGpuTimer&) = delete;
        ScopedGpuTimer& operator=(const ScopedGpuTimer&) = delete;

    private:
        OpenGlNv2aRenderer* owner_;
        bool started_;
    };

    // ---- GPU-timed GL entry wrappers -------------------------------------
    // EVERY submission, upload and readback in this file goes through one of
    // these. The raw draw_arrays_ / dispatch_compute_ / buffer_data_ /
    // tex_image_2d_ / tex_sub_image_2d_ / read_pixels_ pointers are called
    // nowhere else, so a new site cannot silently escape GPU timing
    // (tests/test_gl_timing_guards.py asserts exactly that).
    void gpu_timed_draw_arrays(unsigned int mode, int first, int count) {
        ScopedGpuTimer gpu_timer(this, GpuTimerBucket::kDraw);
        draw_arrays_(mode, first, count);
    }
    void gpu_timed_dispatch_compute(unsigned int groups_x, unsigned int groups_y,
                                    unsigned int groups_z, GpuTimerBucket bucket = GpuTimerBucket::kDraw) {
        ScopedGpuTimer gpu_timer(this, bucket);
        dispatch_compute_(groups_x, groups_y, groups_z);
    }
    void gpu_timed_buffer_data(unsigned int target, std::ptrdiff_t size,
                               const void* data, unsigned int usage) {
        ScopedGpuTimer gpu_timer(this, GpuTimerBucket::kUpload);
        buffer_data_(target, size, data, usage);
    }
    void gpu_timed_tex_image_2d(unsigned int target, int level,
                                int internal_format, int width, int height,
                                int border, unsigned int format,
                                unsigned int type, const void* pixels) {
        ScopedGpuTimer gpu_timer(this, GpuTimerBucket::kUpload);
        tex_image_2d_(target, level, internal_format, width, height, border,
                      format, type, pixels);
    }
    void gpu_timed_tex_sub_image_2d(unsigned int target, int level, int xoffset,
                                    int yoffset, int width, int height,
                                    unsigned int format, unsigned int type,
                                    const void* pixels) {
        ScopedGpuTimer gpu_timer(this, GpuTimerBucket::kUpload);
        tex_sub_image_2d_(target, level, xoffset, yoffset, width, height, format,
                          type, pixels);
    }
    // beads-xbox.1.14.6 (defect found 2026-09-03, fixed 2026-09-04): closes the
    // GL_TIME_ELAPSED bracket around a readback at the point the code means it to
    // close.
    //
    // THE DEFECT. glReadPixels is synchronous: it flushes the command stream and
    // blocks, so the GL_TIME_ELAPSED begin marker and the read itself are executed
    // by the GPU immediately. gpu_timer_end() then calls glEndQuery, which only
    // ENQUEUES the end marker -- nothing flushes it. The GPU executes that marker
    // at the NEXT flush, which is the next frame's submission, milliseconds of
    // host wall time later. GL_TIME_ELAPSED reports the GPU time between the two
    // markers EXECUTING, so the resolve bucket accumulated the host's idle time
    // between resolves: measured 138.480 s over 1 308 resolves (105.9 ms each)
    // against readpixels_ns of 1.799 s (1.38 ms each), a factor of 77. The number
    // was GPU idle time wearing a "GPU resolve" label -- the same class of error
    // the audit found when this family had no GPU timing at all.
    //
    // THE FIX. Flush right after glEndQuery so the end marker is executed while
    // the GPU is still at the readback. Gated on gpu_timing_active(), so a noperf
    // replay -- where no query was opened at all -- pays one predictable bool load
    // and issues no GL call. tests/test_gl_timing_guards.py asserts that every
    // ScopedGpuTimer body containing a read_pixels_ call is followed by this.
    void flush_timed_readback_bracket() {
        if (!gpu_timing_active()) return;
        if (flush_) flush_();
    }

    void gpu_timed_read_pixels(int x, int y, int width, int height,
                               unsigned int format, unsigned int type,
                               void* pixels) {
        {
            ScopedGpuTimer gpu_timer(this, GpuTimerBucket::kResolve);
            read_pixels_(x, y, width, height, format, type, pixels);
        }
        flush_timed_readback_bracket();
    }
    // Fence-split readback. renderer_resolve_ns (3 998 ms over 1 308 resolves,
    // 3.06 ms each, where a 640x480 BGRA8 transfer is about 0.3 ms) conflates
    // three different costs: the GPU still working through queued draws, the
    // pixel transfer itself, and the CPU flip/copy into guest byte order.
    // glFenceSync + glClientWaitSync BEFORE the read charges the first of those
    // to drain_ns, leaving readpixels_ns as transfer plus driver; the caller
    // times its own flip under cpu_copy_ns. `bytes` is the area actually read,
    // `dirty` the tracked union of scissor rects written since the last resolve
    // -- the gain a scissored readback could deliver, quantified before one is
    // written (beads-xbox.1.14.6).
    void gpu_timed_resolve_read_pixels(int x, int y, int width, int height,
                                       unsigned int format, unsigned int type,
                                       void* pixels, uint64_t bytes,
                                       uint64_t dirty) {
        if (gpu_timing_active()) {
            perf::ScopedAdd drain_timer(g_gl_gpu.drain_ns, g_gl_gpu_split_calls);
            const GlSyncHandle sync = fence_sync_(kGlSyncGpuCommandsComplete, 0);
            if (sync) {
                // A GL_TIMEOUT_EXPIRED return is not an error here: the read
                // below blocks on the same work, so the split just degrades.
                client_wait_sync_(sync, kGlSyncFlushCommandsBit,
                                  kGlFenceTimeoutNs);
                delete_sync_(sync);
            }
        }
        {
            ScopedGpuTimer gpu_timer(this, GpuTimerBucket::kResolve);
            // Declared second so it is destroyed first: readpixels_ns covers the
            // call alone, not the glEndQuery that closes the timer query.
            perf::ScopedAdd read_timer(g_gl_gpu.readpixels_ns,
                                       g_gl_gpu_split_calls);
            read_pixels_(x, y, width, height, format, type, pixels);
        }
        flush_timed_readback_bracket();
        if (perf::g_enabled) {
            g_gl_gpu.bytes_read.fetch_add(bytes, std::memory_order_relaxed);
            g_gl_gpu.dirty_bytes.fetch_add(dirty, std::memory_order_relaxed);
        }
    }

    // ---- driver-entry reduction caches -----------------------------------
    // glGetUniformLocation results per program handle. Entries are dropped
    // when a program is (re)compiled, because the driver may reuse names.
    std::unordered_map<unsigned int, std::unordered_map<std::string, int>>
        uniform_location_cache_;
    int cached_uniform_location(unsigned int program, const char* name) {
        auto& per_program = uniform_location_cache_[program];
        const auto it = per_program.find(name);
        if (it != per_program.end()) return it->second;
        const int location = uniform_location_(program, name);
        per_program.emplace(name, location);
        return location;
    }

    // Mirrored GL pipeline state so consecutive submits with identical state
    // skip redundant driver calls. Anything outside this class that touches
    // the context must call invalidate_gl_state_cache().
    struct GlStateCache {
        bool valid = false;
        unsigned int bound_fbo = 0;
        unsigned int fbo_color_texture = 0;
        unsigned int fbo_depth_texture = 0;
        int viewport_x = 0, viewport_y = 0, viewport_w = 0, viewport_h = 0;
        bool depth_test = false;
        unsigned int depth_func = 0;
        bool depth_writemask = true;
        bool scissor_test = false;
        int scissor_x = 0, scissor_y = 0, scissor_w = 0, scissor_h = 0;
        bool blend = false;
        unsigned int blend_src = 0, blend_dst = 0;
        std::array<uint8_t, 4> color_mask = {{1, 1, 1, 1}};
        unsigned int bound_vao = 0;
        unsigned int bound_array_buffer = 0;
        unsigned int active_unit = kGlTexture0;
        unsigned int unit2d_binding[4] = {0, 0, 0, 0};  // units TEXTURE0..3
        void invalidate() { valid = false; }
    } gl_state_;

    // Cached wrappers: issue the driver call only when the mirrored state
    // would actually change. All of them assume gl_state_ is coherent; any
    // foreign context touch invalidates.
    void cache_bind_framebuffer(unsigned int fbo) {
        if (gl_state_.valid && gl_state_.bound_fbo == fbo) return;
        bind_framebuffer_(kGlFramebuffer, fbo);
        gl_state_.bound_fbo = fbo;
        // Attachments belong to the bound FBO object and persist across
        // rebinds, but our mirror only tracks what we set on resident_fbo_;
        // rebinds to other FBOs drop attachment knowledge conservatively.
        if (fbo != resident_fbo_) {
            gl_state_.fbo_color_texture = 0;
            gl_state_.fbo_depth_texture = 0;
        }
    }
    void cache_framebuffer_color_texture(unsigned int fbo, unsigned int texture) {
        if (!gl_state_.valid || gl_state_.bound_fbo != fbo ||
            gl_state_.fbo_color_texture != texture) {
            framebuffer_texture_2d_(kGlFramebuffer, kGlColorAttachment0,
                                    kGlTexture2D, texture, 0);
            if (gl_state_.bound_fbo == fbo) gl_state_.fbo_color_texture = texture;
        }
    }
    void cache_framebuffer_depth_texture(unsigned int fbo, unsigned int texture) {
        if (!gl_state_.valid || gl_state_.bound_fbo != fbo ||
            gl_state_.fbo_depth_texture != texture) {
            framebuffer_texture_2d_(kGlFramebuffer, kGlDepthStencilAttachment,
                                    kGlTexture2D, texture, 0);
            if (gl_state_.bound_fbo == fbo) gl_state_.fbo_depth_texture = texture;
        }
    }
    void cache_bind_texture_unit(unsigned int unit, unsigned int texture) {
        if (gl_state_.valid && gl_state_.active_unit == unit &&
            unit - kGlTexture0 < 4 &&
            gl_state_.unit2d_binding[unit - kGlTexture0] == texture) {
            return;
        }
        active_texture_(unit);
        bind_texture_(kGlTexture2D, texture);
        if (!gl_state_.valid) return;
        gl_state_.active_unit = unit;
        if (unit - kGlTexture0 < 4)
            gl_state_.unit2d_binding[unit - kGlTexture0] = texture;
    }
    void cache_viewport(int x, int y, int w, int h) {
        if (gl_state_.valid && gl_state_.viewport_x == x &&
            gl_state_.viewport_y == y && gl_state_.viewport_w == w &&
            gl_state_.viewport_h == h) return;
        viewport_(x, y, w, h);
        if (gl_state_.valid) {
            gl_state_.viewport_x = x; gl_state_.viewport_y = y;
            gl_state_.viewport_w = w; gl_state_.viewport_h = h;
        }
    }
    void cache_depth_test(bool enable, unsigned int func) {
        if (!gl_state_.valid || gl_state_.depth_test != enable) {
            if (enable) enable_(kGlDepthTest); else disable_(kGlDepthTest);
            if (gl_state_.valid) gl_state_.depth_test = enable;
        }
        if (!gl_state_.valid || gl_state_.depth_func != func) {
            depth_func_(func);
            if (gl_state_.valid) gl_state_.depth_func = func;
        }
    }
    void cache_depth_mask(bool writemask) {
        if (gl_state_.valid && gl_state_.depth_writemask == writemask) return;
        depth_mask_(writemask ? 1 : 0);
        if (gl_state_.valid) gl_state_.depth_writemask = writemask;
    }
    void cache_scissor(bool enable, int x, int y, int w, int h) {
        if (!gl_state_.valid || gl_state_.scissor_test != enable) {
            if (enable) enable_(kGlScissorTest); else disable_(kGlScissorTest);
            if (gl_state_.valid) gl_state_.scissor_test = enable;
        }
        if (!gl_state_.valid || gl_state_.scissor_x != x ||
            gl_state_.scissor_y != y || gl_state_.scissor_w != w ||
            gl_state_.scissor_h != h) {
            scissor_(x, y, w, h);
            if (gl_state_.valid) {
                gl_state_.scissor_x = x; gl_state_.scissor_y = y;
                gl_state_.scissor_w = w; gl_state_.scissor_h = h;
            }
        }
    }
    void cache_blend(bool enable, unsigned int src, unsigned int dst) {
        if (!gl_state_.valid || gl_state_.blend != enable) {
            if (enable) enable_(kGlBlend); else disable_(kGlBlend);
            if (gl_state_.valid) gl_state_.blend = enable;
        }
        if (enable &&
            (!gl_state_.valid || gl_state_.blend_src != src ||
             gl_state_.blend_dst != dst)) {
            blend_func_(src, dst);
            if (gl_state_.valid) { gl_state_.blend_src = src; gl_state_.blend_dst = dst; }
        }
    }
    void cache_color_mask(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (gl_state_.valid && gl_state_.color_mask[0] == r &&
            gl_state_.color_mask[1] == g && gl_state_.color_mask[2] == b &&
            gl_state_.color_mask[3] == a) return;
        color_mask_(r, g, b, a);
        if (gl_state_.valid) {
            gl_state_.color_mask = {{r, g, b, a}};
        }
    }
    void cache_bind_vao_vbo(unsigned int vao, unsigned int vbo) {
        if (!gl_state_.valid || gl_state_.bound_vao != vao) {
            bind_vertex_array_(vao);
            if (gl_state_.valid) {
                gl_state_.bound_vao = vao;
                gl_state_.bound_array_buffer = 0;  // VAO switch resets attrib binds
            }
        }
        if (!gl_state_.valid || gl_state_.bound_array_buffer != vbo) {
            bind_buffer_(kGlArrayBuffer, vbo);
            if (gl_state_.valid) gl_state_.bound_array_buffer = vbo;
        }
    }
    void invalidate_gl_state_cache() { gl_state_.invalidate(); }
};

}  // namespace

std::unique_ptr<Nv2aRenderer> create_nv2a_opengl_renderer(std::string& error, bool enhanced_xemu) {
    error.clear();
    const bool owns_video = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
    if (owns_video && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        error = std::string("SDL video initialization failed: ") + SDL_GetError();
        return nullptr;
    }
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                             SDL_GL_CONTEXT_PROFILE_CORE)) {
        error = std::string("OpenGL context attribute setup failed: ") +
                SDL_GetError();
        if (owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }
    SDL_Window* window = SDL_CreateWindow("xlle NV2A OpenGL backend", 1, 1,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
    SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
    if (!window || !context || !SDL_GL_MakeCurrent(window, context)) {
        error = std::string("OpenGL context creation failed: ") + SDL_GetError();
        if (context) SDL_GL_DestroyContext(context);
        if (window) SDL_DestroyWindow(window);
        if (owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }
    auto get_integerv = reinterpret_cast<GlGetIntegerv>(SDL_GL_GetProcAddress("glGetIntegerv"));
    int major = 0, minor = 0;
    if (!get_integerv) {
        error = "OpenGL context lacks glGetIntegerv";
    } else {
        get_integerv(kGlMajorVersion, &major);
        get_integerv(kGlMinorVersion, &minor);
        if (major < 4 || (major == 4 && minor < 3))
            error = "OpenGL 4.3 compute capability is required (got " +
                    std::to_string(major) + "." + std::to_string(minor) + ")";
    }
    if (!error.empty()) {
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        if (owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return nullptr;
    }
    auto renderer =
        std::make_unique<OpenGlNv2aRenderer>(window, context, owns_video, 0, 0, 0,
                                             0, 0, 0, 0);
    if (!renderer->initialize(error)) return nullptr;
    renderer->set_enhanced_xemu(enhanced_xemu);
    restore_previous_gl_context(previous_window, previous_context, window,
                                perf::kGlContextOtherRestore);
    std::printf("[nv2a-renderer] opengl context ready: GL %d.%d; narrow compute fixtures enabled\n",
                major, minor);
    return renderer;
}
#else
std::unique_ptr<Nv2aRenderer> create_nv2a_opengl_renderer(std::string& error, bool) {
    error = "OpenGL renderer is unavailable: xlle was built without SDL3";
    return nullptr;
}
#endif

}  // namespace xlle
