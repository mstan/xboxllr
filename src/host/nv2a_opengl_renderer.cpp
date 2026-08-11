#include "host/nv2a_opengl_renderer.hpp"

#include <cstdio>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
using GlUniform2i = void (*)(int, int, int);
using GlUniform2f = void (*)(int, float, float);
using GlUniform4f = void (*)(int, float, float, float, float);
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
using GlBlendFunc = void (*)(unsigned int, unsigned int);
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
constexpr unsigned int kGlFramebuffer = 0x8D40;
constexpr unsigned int kGlColorAttachment0 = 0x8CE0;
constexpr unsigned int kGlFramebufferComplete = 0x8CD5;
constexpr unsigned int kGlTexture2D = 0x0DE1;
constexpr unsigned int kGlRgba8 = 0x8058;
constexpr unsigned int kGlRgba = 0x1908;
constexpr unsigned int kGlUnsignedByte = 0x1401;
constexpr unsigned int kGlFloat = 0x1406;
constexpr unsigned int kGlTriangles = 0x0004;
constexpr unsigned int kGlBlend = 0x0BE2;
constexpr unsigned int kGlScissorTest = 0x0C11;
constexpr unsigned int kGlFuncAdd = 0x8006;
constexpr unsigned int kGlZero = 0;
constexpr unsigned int kGlOne = 1;
constexpr unsigned int kGlSrcAlpha = 0x0302;
constexpr unsigned int kGlOneMinusSrcAlpha = 0x0303;
constexpr unsigned int kGlTextureMinFilter = 0x2801;
constexpr unsigned int kGlTextureMagFilter = 0x2800;
constexpr unsigned int kGlNearest = 0x2600;
constexpr unsigned int kGlTextureWrapS = 0x2802;
constexpr unsigned int kGlTextureWrapT = 0x2803;
constexpr unsigned int kGlClampToEdge = 0x812F;
constexpr unsigned int kGlUnpackAlignment = 0x0CF5;
constexpr unsigned int kGlPackAlignment = 0x0D05;
constexpr unsigned int kGlNoError = 0;

void restore_previous_gl_context(SDL_Window* previous_window,
                                 SDL_GLContext previous_context,
                                 SDL_Window* fallback_window) {
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
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)GLSL";

constexpr const char* kResidentPrimaryFragmentSource = R"GLSL(#version 430 core
noperspective in vec4 diffuse_over_w;
noperspective in float inv_w_out;
layout(location = 0) out vec4 out_color;
void main() {
    vec4 diffuse = inv_w_out == 0.0 ? vec4(0.0, 0.0, 0.0, 1.0)
                                    : diffuse_over_w / inv_w_out;
    out_color = clamp(diffuse, 0.0, 1.0);
}
)GLSL";

template <typename T>
bool load_gl(T& destination, const char* name) {
    destination = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    return destination != nullptr;
}

class OpenGlNv2aRenderer final : public Nv2aRenderer {
public:
    OpenGlNv2aRenderer(SDL_Window* window, SDL_GLContext context, bool owns_video,
                       unsigned int fixture_program, unsigned int dashboard_program,
                       unsigned int resident_program)
        : window_(window), context_(context), owns_video_(owns_video),
          fixture_program_(fixture_program), dashboard_program_(dashboard_program),
          resident_program_(resident_program) {}
    ~OpenGlNv2aRenderer() override {
        // Destruction can happen after an SDL presentation context was made
        // current; GL object deletion must be issued against this renderer's own
        // context, not whichever context happened to be current last.
        if (window_ && context_) SDL_GL_MakeCurrent(window_, context_);
        if (fixture_program_ && delete_program_) delete_program_(fixture_program_);
        if (dashboard_program_ && delete_program_)
            delete_program_(dashboard_program_);
        if (resident_program_ && delete_program_) delete_program_(resident_program_);
        if (resident_vbo_ && delete_buffers_) delete_buffers_(1, &resident_vbo_);
        if (resident_vao_ && delete_vertex_arrays_)
            delete_vertex_arrays_(1, &resident_vao_);
        if (resident_fbo_ && delete_framebuffers_)
            delete_framebuffers_(1, &resident_fbo_);
        if (resident_texture_ && delete_textures_)
            delete_textures_(1, &resident_texture_);
        if (context_) SDL_GL_DestroyContext(context_);
        if (window_) SDL_DestroyWindow(window_);
        if (owns_video_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    const char* name() const override { return "opengl"; }
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
    bool has_resident_color_surface(uint32_t address, size_t size, uint32_t pitch,
                                    uint32_t height,
                                    uint32_t format) const override {
        return resident_.valid && resident_.address == address &&
               resident_.size == size && resident_.pitch == pitch &&
               resident_.height == height && resident_.format == format;
    }
    bool supports_resident_generic_combiner(
        const Nv2aGenericCombinerPacket& packet) const override {
        if (packet.compute_fixture != Nv2aGenericCombinerPacket::ComputeFixture::
                                          kResidentPrimaryColorTriangles) {
            return false;
        }
        if (resident_program_ == 0 || packet.primitive_mode != 5 ||
            packet.vertices.empty() || (packet.vertices.size() % 3) != 0 ||
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
        return true;
    }
    bool submit_resident_generic_combiner(const Nv2aGenericCombinerPacket& packet,
                                          Nv2aGenericCombinerResult& result,
                                          std::string& error) override {
        SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
        if (!SDL_GL_MakeCurrent(window_, context_)) {
            error = std::string("OpenGL resident context lost: ") + SDL_GetError();
            return false;
        }
        struct ContextRestore {
            SDL_Window* previous_window;
            SDL_GLContext previous_context;
            SDL_Window* fallback_window;
            ~ContextRestore() {
                restore_previous_gl_context(previous_window, previous_context,
                                            fallback_window);
            }
        } restore{previous_window, previous_context, window_};
        const uint32_t pitch = packet.surface_pitch & 0xffffu;
        const uint32_t width = pitch / 4;
        const uint32_t height = packet.surface_height;
        const size_t size = static_cast<size_t>(pitch) * height;
        if (!ensure_resident_surface(packet, width, height, size, error)) {
            return false;
        }
        struct GpuVertex {
            float position[4];
            float diffuse[4];
        };
        std::vector<GpuVertex> vertices;
        vertices.reserve(packet.vertices.size());
        for (const auto& vertex : packet.vertices) {
            GpuVertex out{};
            for (size_t i = 0; i < 4; ++i) {
                out.position[i] = vertex.position[i];
                out.diffuse[i] = vertex.diffuse[i];
            }
            vertices.push_back(out);
        }
        bind_framebuffer_(kGlFramebuffer, resident_fbo_);
        viewport_(0, 0, static_cast<int>(width), static_cast<int>(height));
        enable_(kGlScissorTest);
        scissor_(static_cast<int>(packet.fixture_clip_x),
                 static_cast<int>(height - packet.fixture_clip_y -
                                  packet.fixture_clip_height),
                 static_cast<int>(packet.fixture_clip_width),
                 static_cast<int>(packet.fixture_clip_height));
        if (packet.fixture_blend_enabled) {
            enable_(kGlBlend);
            blend_equation_(kGlFuncAdd);
            blend_func_(gl_blend_factor(packet.fixture_blend_src),
                        gl_blend_factor(packet.fixture_blend_dst));
        } else {
            disable_(kGlBlend);
        }
        color_mask_(1, 1, 1, packet.fixture_color_format_x8 ? 0 : 1);
        if (!resident_vao_) gen_vertex_arrays_(1, &resident_vao_);
        bind_vertex_array_(resident_vao_);
        if (!resident_vbo_) gen_buffers_(1, &resident_vbo_);
        bind_buffer_(kGlArrayBuffer, resident_vbo_);
        buffer_data_(kGlArrayBuffer,
                     static_cast<std::ptrdiff_t>(vertices.size() * sizeof(GpuVertex)),
                     vertices.data(), kGlDynamicDraw);
        use_program_(resident_program_);
        uniform_2f_(uniform_location_(resident_program_, "surface_size"),
                    static_cast<float>(width), static_cast<float>(height));
        vertex_attrib_pointer_(0, 4, kGlFloat, 0, sizeof(GpuVertex),
                               reinterpret_cast<const void*>(offsetof(GpuVertex, position)));
        vertex_attrib_pointer_(1, 4, kGlFloat, 0, sizeof(GpuVertex),
                               reinterpret_cast<const void*>(offsetof(GpuVertex, diffuse)));
        enable_vertex_attrib_array_(0);
        enable_vertex_attrib_array_(1);
        draw_arrays_(kGlTriangles, 0, static_cast<int>(vertices.size()));
        disable_vertex_attrib_array_(1);
        disable_vertex_attrib_array_(0);
        color_mask_(1, 1, 1, 1);
        disable_(kGlBlend);
        disable_(kGlScissorTest);
        if (get_error_() != kGlNoError) {
            error = "OpenGL resident primary draw failed";
            return false;
        }
        resident_.dirty = true;
        result.color_output.clear();
        result.depth_output.clear();
        result.triangles = vertices.size() / 3;
        result.fragments = 0;
        result.writes = 0;
        return true;
    }
    bool resolve_resident_color_surface(uint32_t address, size_t size,
                                        Nv2aResidentColorResolve& resolve,
                                        std::string& error) override {
        resolve = {};
        if (!resident_.valid || !resident_.dirty || size == 0) return true;
        const uint64_t request_end = static_cast<uint64_t>(address) + size;
        const uint64_t resident_end =
            static_cast<uint64_t>(resident_.address) + resident_.size;
        if (address >= resident_end || resident_.address >= request_end) return true;
        SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
        if (!SDL_GL_MakeCurrent(window_, context_)) {
            error = std::string("OpenGL resident resolve context lost: ") +
                    SDL_GetError();
            return false;
        }
        struct ContextRestore {
            SDL_Window* previous_window;
            SDL_GLContext previous_context;
            SDL_Window* fallback_window;
            ~ContextRestore() {
                restore_previous_gl_context(previous_window, previous_context,
                                            fallback_window);
            }
        } restore{previous_window, previous_context, window_};
        std::vector<uint8_t> rgba(static_cast<size_t>(resident_.width) *
                                  resident_.height * 4u);
        bind_framebuffer_(kGlFramebuffer, resident_fbo_);
        pixel_store_i_(kGlPackAlignment, 1);
        read_pixels_(0, 0, static_cast<int>(resident_.width),
                     static_cast<int>(resident_.height), kGlRgba,
                     kGlUnsignedByte, rgba.data());
        if (get_error_() != kGlNoError) {
            error = "OpenGL resident color readback failed";
            return false;
        }
        resolve.address = resident_.address;
        resolve.data.resize(resident_.size);
        rgba_to_guest_words(rgba, resolve.data, resident_.width,
                            resident_.height, resident_.x8);
        resident_.dirty = false;
        return true;
    }
    void invalidate_resident_color_surface(uint32_t address, size_t size) override {
        if (!resident_.valid || size == 0) return;
        const uint64_t request_end = static_cast<uint64_t>(address) + size;
        const uint64_t resident_end =
            static_cast<uint64_t>(resident_.address) + resident_.size;
        if (address < resident_end && resident_.address < request_end) {
            resident_.valid = false;
            resident_.dirty = false;
        }
    }
    bool submit_generic_combiner(const Nv2aGenericCombinerPacket& packet,
                                 Nv2aGenericCombinerResult& result,
                                 std::string& error) override {
        SDL_Window* previous_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext previous_context = SDL_GL_GetCurrentContext();
        if (!SDL_GL_MakeCurrent(window_, context_)) {
            error = std::string("OpenGL compute context lost: ") + SDL_GetError();
            return false;
        }
        struct ContextRestore {
            SDL_Window* previous_window;
            SDL_GLContext previous_context;
            SDL_Window* fallback_window;
            ~ContextRestore() {
                restore_previous_gl_context(previous_window, previous_context,
                                            fallback_window);
            }
        } restore{previous_window, previous_context, window_};
        if (packet.compute_fixture ==
            Nv2aGenericCombinerPacket::ComputeFixture::kDashboardProject2dFan) {
            if (packet.color_input.size() != 0x1400u * 480u ||
                packet.depth_input.size() != 0x1400u * 480u) {
                error = "OpenGL dashboard fixture received invalid surface sizes";
                return false;
            }
            std::array<unsigned int, 6> buffers{};
            gen_buffers_(static_cast<int>(buffers.size()), buffers.data());
            auto upload_buffer = [&](unsigned int binding, unsigned int buffer,
                                     const void* data, size_t size) {
                bind_buffer_(kGlShaderStorageBuffer, buffer);
                buffer_data_(kGlShaderStorageBuffer, static_cast<std::ptrdiff_t>(size),
                             data, kGlDynamicCopy);
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
            uniform_1i_(uniform_location_(dashboard_program_, "surface_width"), 1280);
            uniform_1i_(uniform_location_(dashboard_program_, "surface_height"), 480);
            uniform_1i_(uniform_location_(dashboard_program_, "surface_pitch_words"),
                        0x1400 / 4);
            uniform_2i_(uniform_location_(dashboard_program_, "vertex_a"),
                        fixed_xy(0, 0), fixed_xy(0, 1));
            uniform_2i_(uniform_location_(dashboard_program_, "vertex_b"),
                        fixed_xy(1, 0), fixed_xy(1, 1));
            uniform_2i_(uniform_location_(dashboard_program_, "vertex_c"),
                        fixed_xy(2, 0), fixed_xy(2, 1));
            uniform_2i_(uniform_location_(dashboard_program_, "vertex_d"),
                        fixed_xy(3, 0), fixed_xy(3, 1));
            uniform_1ui_(uniform_location_(dashboard_program_, "factor_stage2"),
                         packet.fixture_factor0_stage2_argb);
            uniform_1ui_(uniform_location_(dashboard_program_, "factor_stage4"),
                         packet.fixture_factor0_stage4_argb);
            dispatch_compute_((1280 + 7) / 8, (480 + 7) / 8, 1);
            memory_barrier_(kGlShaderStorageBarrierBit | kGlBufferUpdateBarrierBit);
            if (get_error_() != kGlNoError) {
                delete_buffers_(static_cast<int>(buffers.size()), buffers.data());
                error = "OpenGL dashboard fixture dispatch failed";
                return false;
            }
            result.color_output.resize(packet.color_input.size());
            bind_buffer_(kGlShaderStorageBuffer, buffers[0]);
            get_buffer_sub_data_(kGlShaderStorageBuffer, 0,
                                 static_cast<std::ptrdiff_t>(
                                     result.color_output.size()),
                                 result.color_output.data());
            delete_buffers_(static_cast<int>(buffers.size()), buffers.data());
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
        buffer_data_(kGlShaderStorageBuffer,
                     static_cast<std::ptrdiff_t>(packet.color_input.size()),
                     packet.color_input.data(), kGlDynamicCopy);
        bind_buffer_base_(kGlShaderStorageBuffer, 0, buffer);
        use_program_(fixture_program_);
        uniform_1i_(uniform_location_(fixture_program_, "surface_width"),
                    static_cast<int>((packet.surface_pitch & 0xffffu) / 4));
        uniform_1i_(uniform_location_(fixture_program_, "surface_height"),
                    static_cast<int>(packet.surface_height));
        uniform_1i_(uniform_location_(fixture_program_, "surface_pitch_words"),
                    static_cast<int>((packet.surface_pitch & 0xffffu) / 4));
        uniform_2i_(uniform_location_(fixture_program_, "vertex_a"),
                    packet.fixture_xy_16[0], packet.fixture_xy_16[1]);
        uniform_2i_(uniform_location_(fixture_program_, "vertex_b"),
                    packet.fixture_xy_16[2], packet.fixture_xy_16[3]);
        uniform_2i_(uniform_location_(fixture_program_, "vertex_c"),
                    packet.fixture_xy_16[4], packet.fixture_xy_16[5]);
        uniform_2i_(uniform_location_(fixture_program_, "clip_origin"),
                    static_cast<int>(packet.fixture_clip_x),
                    static_cast<int>(packet.fixture_clip_y));
        uniform_2i_(uniform_location_(fixture_program_, "clip_extent"),
                    static_cast<int>(packet.fixture_clip_width),
                    static_cast<int>(packet.fixture_clip_height));
        uniform_1ui_(uniform_location_(fixture_program_, "source_argb"),
                     packet.fixture_source_argb);
        dispatch_compute_(((packet.surface_pitch & 0xffffu) / 4 + 7) / 8,
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
               load_gl(uniform_1ui_, "glUniform1ui") &&
               load_gl(uniform_2i_, "glUniform2i") &&
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
               load_gl(gen_framebuffers_, "glGenFramebuffers") &&
               load_gl(bind_framebuffer_, "glBindFramebuffer") &&
               load_gl(framebuffer_texture_2d_, "glFramebufferTexture2D") &&
               load_gl(check_framebuffer_status_, "glCheckFramebufferStatus") &&
               load_gl(delete_framebuffers_, "glDeleteFramebuffers") &&
               load_gl(viewport_, "glViewport") &&
               load_gl(scissor_, "glScissor") &&
               load_gl(enable_, "glEnable") &&
               load_gl(disable_, "glDisable") &&
               load_gl(blend_func_, "glBlendFunc") &&
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
        return compile_fixture(error) && compile_dashboard(error) &&
               compile_resident_primary(error);
    }
private:
    struct ResidentSurface {
        bool valid = false;
        bool dirty = false;
        bool x8 = false;
        uint32_t address = 0;
        size_t size = 0;
        uint32_t pitch = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t format = 0;
    };
    static unsigned int gl_blend_factor(uint32_t factor) {
        switch (factor) {
            case 0: return kGlZero;
            case 1: return kGlOne;
            case 0x0302: return kGlSrcAlpha;
            default: return kGlOneMinusSrcAlpha;
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
                rgba[gl_offset + 3] = x8 ? 0xff : guest[guest_offset + 3];
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
                guest[guest_offset + 3] = x8 ? 0xff : rgba[gl_offset + 3];
            }
        }
    }
    bool ensure_resident_surface(const Nv2aGenericCombinerPacket& packet,
                                 uint32_t width, uint32_t height, size_t size,
                                 std::string& error) {
        const bool same = resident_.valid &&
                          resident_.address == packet.color_address &&
                          resident_.size == size &&
                          resident_.pitch == packet.surface_pitch &&
                          resident_.height == packet.surface_height &&
                          resident_.format == packet.surface_format;
        if (same) return true;
        if (resident_.valid && resident_.dirty) {
            error = "OpenGL resident primary target switch without prior resolve";
            return false;
        }
        if (packet.color_input.size() != size) {
            error = "OpenGL resident primary draw missing initial color snapshot";
            return false;
        }
        if (!resident_texture_) {
            gen_textures_(1, &resident_texture_);
            bind_texture_(kGlTexture2D, resident_texture_);
            tex_parameter_i_(kGlTexture2D, kGlTextureMinFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureMagFilter, kGlNearest);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapS, kGlClampToEdge);
            tex_parameter_i_(kGlTexture2D, kGlTextureWrapT, kGlClampToEdge);
        } else {
            bind_texture_(kGlTexture2D, resident_texture_);
        }
        std::vector<uint8_t> rgba;
        guest_words_to_rgba(packet.color_input, rgba, width, height,
                            packet.fixture_color_format_x8);
        pixel_store_i_(kGlUnpackAlignment, 1);
        tex_image_2d_(kGlTexture2D, 0, kGlRgba8, static_cast<int>(width),
                      static_cast<int>(height), 0, kGlRgba, kGlUnsignedByte,
                      rgba.data());
        if (!resident_fbo_) gen_framebuffers_(1, &resident_fbo_);
        bind_framebuffer_(kGlFramebuffer, resident_fbo_);
        framebuffer_texture_2d_(kGlFramebuffer, kGlColorAttachment0, kGlTexture2D,
                                resident_texture_, 0);
        if (check_framebuffer_status_(kGlFramebuffer) != kGlFramebufferComplete) {
            error = "OpenGL resident color framebuffer is incomplete";
            return false;
        }
        if (get_error_() != kGlNoError) {
            error = "OpenGL resident color upload failed";
            return false;
        }
        resident_.valid = true;
        resident_.dirty = false;
        resident_.x8 = packet.fixture_color_format_x8;
        resident_.address = packet.color_address;
        resident_.size = size;
        resident_.pitch = packet.surface_pitch;
        resident_.width = width;
        resident_.height = height;
        resident_.format = packet.surface_format;
        return true;
    }
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
    bool compile_dashboard(std::string& error) {
        return compile_program(kDashboardProject2dFanComputeSource, "dashboard",
                               dashboard_program_, error);
    }
    bool compile_resident_primary(std::string& error) {
        return compile_graphics_program(kResidentPrimaryVertexSource,
                                        kResidentPrimaryFragmentSource,
                                        "resident-primary", resident_program_,
                                        error);
    }
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool owns_video_ = false;
    unsigned int fixture_program_ = 0;
    unsigned int dashboard_program_ = 0;
    unsigned int resident_program_ = 0;
    unsigned int resident_texture_ = 0;
    unsigned int resident_fbo_ = 0;
    unsigned int resident_vbo_ = 0;
    unsigned int resident_vao_ = 0;
    ResidentSurface resident_;
    GlCreateShader create_shader_ = nullptr; GlShaderSource shader_source_ = nullptr;
    GlCompileShader compile_shader_ = nullptr; GlGetShaderiv get_shader_iv_ = nullptr;
    GlGetShaderInfoLog get_shader_log_ = nullptr; GlDeleteShader delete_shader_ = nullptr;
    GlCreateProgram create_program_ = nullptr; GlAttachShader attach_shader_ = nullptr;
    GlLinkProgram link_program_ = nullptr; GlGetProgramiv get_program_iv_ = nullptr;
    GlGetProgramInfoLog get_program_log_ = nullptr; GlDeleteProgram delete_program_ = nullptr;
    GlUseProgram use_program_ = nullptr; GlGetUniformLocation uniform_location_ = nullptr;
    GlUniform1i uniform_1i_ = nullptr; GlUniform1ui uniform_1ui_ = nullptr;
    GlUniform2i uniform_2i_ = nullptr; GlUniform2f uniform_2f_ = nullptr;
    GlUniform4f uniform_4f_ = nullptr; GlGenBuffers gen_buffers_ = nullptr;
    GlBindBuffer bind_buffer_ = nullptr; GlBufferData buffer_data_ = nullptr;
    GlBindBufferBase bind_buffer_base_ = nullptr; GlDispatchCompute dispatch_compute_ = nullptr;
    GlMemoryBarrier memory_barrier_ = nullptr; GlGetBufferSubData get_buffer_sub_data_ = nullptr;
    GlDeleteBuffers delete_buffers_ = nullptr; GlGenTextures gen_textures_ = nullptr;
    GlBindTexture bind_texture_ = nullptr; GlTexImage2D tex_image_2d_ = nullptr;
    GlTexSubImage2D tex_sub_image_2d_ = nullptr; GlTexParameteri tex_parameter_i_ = nullptr;
    GlDeleteTextures delete_textures_ = nullptr; GlGenFramebuffers gen_framebuffers_ = nullptr;
    GlBindFramebuffer bind_framebuffer_ = nullptr;
    GlFramebufferTexture2D framebuffer_texture_2d_ = nullptr;
    GlCheckFramebufferStatus check_framebuffer_status_ = nullptr;
    GlDeleteFramebuffers delete_framebuffers_ = nullptr; GlViewport viewport_ = nullptr;
    GlScissor scissor_ = nullptr; GlEnable enable_ = nullptr; GlDisable disable_ = nullptr;
    GlBlendFunc blend_func_ = nullptr; GlBlendEquation blend_equation_ = nullptr;
    GlColorMask color_mask_ = nullptr; GlGenVertexArrays gen_vertex_arrays_ = nullptr;
    GlBindVertexArray bind_vertex_array_ = nullptr;
    GlDeleteVertexArrays delete_vertex_arrays_ = nullptr;
    GlVertexAttribPointer vertex_attrib_pointer_ = nullptr;
    GlEnableVertexAttribArray enable_vertex_attrib_array_ = nullptr;
    GlDisableVertexAttribArray disable_vertex_attrib_array_ = nullptr;
    GlDrawArrays draw_arrays_ = nullptr; GlReadPixels read_pixels_ = nullptr;
    GlPixelStorei pixel_store_i_ = nullptr; GlGetError get_error_ = nullptr;
};
}  // namespace

std::unique_ptr<Nv2aRenderer> create_nv2a_opengl_renderer(std::string& error) {
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
        std::make_unique<OpenGlNv2aRenderer>(window, context, owns_video, 0, 0, 0);
    if (!renderer->initialize(error)) return nullptr;
    restore_previous_gl_context(previous_window, previous_context, window);
    std::printf("[nv2a-renderer] opengl context ready: GL %d.%d; narrow compute fixtures enabled\n",
                major, minor);
    return renderer;
}
#else
std::unique_ptr<Nv2aRenderer> create_nv2a_opengl_renderer(std::string& error) {
    error = "OpenGL renderer is unavailable: xlle was built without SDL3";
    return nullptr;
}
#endif

}  // namespace xlle
