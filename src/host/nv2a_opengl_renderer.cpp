#include "host/nv2a_opengl_renderer.hpp"

#include <cstdio>
#include <array>
#include <utility>

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
using GlGenBuffers = void (*)(int, unsigned int*);
using GlBindBuffer = void (*)(unsigned int, unsigned int);
using GlBufferData = void (*)(unsigned int, std::ptrdiff_t, const void*, unsigned int);
using GlBindBufferBase = void (*)(unsigned int, unsigned int, unsigned int);
using GlDispatchCompute = void (*)(unsigned int, unsigned int, unsigned int);
using GlMemoryBarrier = void (*)(unsigned int);
using GlGetBufferSubData = void (*)(unsigned int, std::ptrdiff_t, std::ptrdiff_t, void*);
using GlDeleteBuffers = void (*)(int, const unsigned int*);
using GlGetError = unsigned int (*)();
constexpr unsigned int kGlMajorVersion = 0x821B;
constexpr unsigned int kGlMinorVersion = 0x821C;
constexpr unsigned int kGlComputeShader = 0x91B9;
constexpr unsigned int kGlCompileStatus = 0x8B81;
constexpr unsigned int kGlLinkStatus = 0x8B82;
constexpr unsigned int kGlShaderStorageBuffer = 0x90D2;
constexpr unsigned int kGlDynamicCopy = 0x88EA;
constexpr unsigned int kGlShaderStorageBarrierBit = 0x2000;
constexpr unsigned int kGlBufferUpdateBarrierBit = 0x0200;
constexpr unsigned int kGlNoError = 0;

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

template <typename T>
bool load_gl(T& destination, const char* name) {
    destination = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    return destination != nullptr;
}

class OpenGlNv2aRenderer final : public Nv2aRenderer {
public:
    OpenGlNv2aRenderer(SDL_Window* window, SDL_GLContext context, bool owns_video,
                       unsigned int fixture_program)
        : window_(window), context_(context), owns_video_(owns_video),
          fixture_program_(fixture_program) {}
    ~OpenGlNv2aRenderer() override {
        // Destruction can happen after an SDL presentation context was made
        // current; GL object deletion must be issued against this renderer's own
        // context, not whichever context happened to be current last.
        if (window_ && context_) SDL_GL_MakeCurrent(window_, context_);
        if (fixture_program_ && delete_program_) delete_program_(fixture_program_);
        if (context_) SDL_GL_DestroyContext(context_);
        if (window_) SDL_DestroyWindow(window_);
        if (owns_video_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    const char* name() const override { return "opengl"; }
    bool supports_generic_combiner(const Nv2aGenericCombinerPacket& packet) const override {
        return fixture_program_ != 0 &&
               packet.compute_fixture ==
                   Nv2aGenericCombinerPacket::ComputeFixture::kUntexturedPrimaryTriangle &&
               packet.primitive_mode == 5 && packet.vertices.size() == 3 &&
               packet.depth_input.empty() && packet.surface_pitch != 0 &&
               packet.surface_height != 0 && packet.fixture_clip_width != 0 &&
               packet.fixture_clip_height != 0 &&
               packet.fixture_clip_x + packet.fixture_clip_width <=
                   (packet.surface_pitch & 0xffffu) / 4 &&
               packet.fixture_clip_y + packet.fixture_clip_height <= packet.surface_height;
    }
    bool submit_generic_combiner(const Nv2aGenericCombinerPacket& packet,
                                 Nv2aGenericCombinerResult& result,
                                 std::string& error) override {
        if (!SDL_GL_MakeCurrent(window_, context_)) {
            error = std::string("OpenGL compute context lost: ") + SDL_GetError();
            return false;
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
               load_gl(gen_buffers_, "glGenBuffers") &&
               load_gl(bind_buffer_, "glBindBuffer") &&
               load_gl(buffer_data_, "glBufferData") &&
               load_gl(bind_buffer_base_, "glBindBufferBase") &&
               load_gl(dispatch_compute_, "glDispatchCompute") &&
               load_gl(memory_barrier_, "glMemoryBarrier") &&
               load_gl(get_buffer_sub_data_, "glGetBufferSubData") &&
               load_gl(delete_buffers_, "glDeleteBuffers") &&
               load_gl(get_error_, "glGetError"))) {
            error = "OpenGL 4.3 compute fixture is missing a required entry point";
            return false;
        }
        return compile_fixture(error);
    }
private:
    bool compile_fixture(std::string& error) {
        const unsigned int shader = create_shader_(kGlComputeShader);
        shader_source_(shader, 1, &kFixtureComputeSource, nullptr);
        compile_shader_(shader);
        int success = 0;
        get_shader_iv_(shader, kGlCompileStatus, &success);
        if (!success) {
            std::array<char, 1024> log{};
            get_shader_log_(shader, static_cast<int>(log.size()), nullptr, log.data());
            error = std::string("OpenGL compute fixture compile failed: ") + log.data();
            delete_shader_(shader);
            return false;
        }
        fixture_program_ = create_program_();
        attach_shader_(fixture_program_, shader);
        link_program_(fixture_program_);
        delete_shader_(shader);
        get_program_iv_(fixture_program_, kGlLinkStatus, &success);
        if (!success) {
            std::array<char, 1024> log{};
            get_program_log_(fixture_program_, static_cast<int>(log.size()), nullptr, log.data());
            error = std::string("OpenGL compute fixture link failed: ") + log.data();
            delete_program_(fixture_program_);
            fixture_program_ = 0;
            return false;
        }
        return true;
    }
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool owns_video_ = false;
    unsigned int fixture_program_ = 0;
    GlCreateShader create_shader_ = nullptr; GlShaderSource shader_source_ = nullptr;
    GlCompileShader compile_shader_ = nullptr; GlGetShaderiv get_shader_iv_ = nullptr;
    GlGetShaderInfoLog get_shader_log_ = nullptr; GlDeleteShader delete_shader_ = nullptr;
    GlCreateProgram create_program_ = nullptr; GlAttachShader attach_shader_ = nullptr;
    GlLinkProgram link_program_ = nullptr; GlGetProgramiv get_program_iv_ = nullptr;
    GlGetProgramInfoLog get_program_log_ = nullptr; GlDeleteProgram delete_program_ = nullptr;
    GlUseProgram use_program_ = nullptr; GlGetUniformLocation uniform_location_ = nullptr;
    GlUniform1i uniform_1i_ = nullptr; GlUniform1ui uniform_1ui_ = nullptr;
    GlUniform2i uniform_2i_ = nullptr; GlGenBuffers gen_buffers_ = nullptr;
    GlBindBuffer bind_buffer_ = nullptr; GlBufferData buffer_data_ = nullptr;
    GlBindBufferBase bind_buffer_base_ = nullptr; GlDispatchCompute dispatch_compute_ = nullptr;
    GlMemoryBarrier memory_barrier_ = nullptr; GlGetBufferSubData get_buffer_sub_data_ = nullptr;
    GlDeleteBuffers delete_buffers_ = nullptr; GlGetError get_error_ = nullptr;
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
    auto renderer = std::make_unique<OpenGlNv2aRenderer>(window, context, owns_video, 0);
    if (!renderer->initialize(error)) return nullptr;
    std::printf("[nv2a-renderer] opengl context ready: GL %d.%d; narrow compute fixture enabled\n",
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
