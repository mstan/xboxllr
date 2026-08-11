#include "host/nv2a_opengl_renderer.hpp"

#include <cstdio>
#include <utility>

#if defined(XLLE_WITH_SDL)
#include <SDL3/SDL.h>
#endif

namespace xlle {

#if defined(XLLE_WITH_SDL)
namespace {
using GlGetIntegerv = void (*)(unsigned int, int*);
constexpr unsigned int kGlMajorVersion = 0x821B;
constexpr unsigned int kGlMinorVersion = 0x821C;

class OpenGlNv2aRenderer final : public Nv2aRenderer {
public:
    OpenGlNv2aRenderer(SDL_Window* window, SDL_GLContext context, bool owns_video)
        : window_(window), context_(context), owns_video_(owns_video) {}
    ~OpenGlNv2aRenderer() override {
        if (context_) SDL_GL_DestroyContext(context_);
        if (window_) SDL_DestroyWindow(window_);
        if (owns_video_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    const char* name() const override { return "opengl"; }
    bool supports_generic_combiner(const Nv2aGenericCombinerPacket&) const override {
        // Stage one owns a verified context but has no shader implementation yet.
        return false;
    }
    bool submit_generic_combiner(const Nv2aGenericCombinerPacket&,
                                 Nv2aGenericCombinerResult&,
                                 std::string& error) override {
        error = "OpenGL generic-combiner submission is not implemented";
        return false;
    }
private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool owns_video_ = false;
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
    std::printf("[nv2a-renderer] opengl context ready: GL %d.%d; generic batches currently fall back to software\n",
                major, minor);
    return std::make_unique<OpenGlNv2aRenderer>(window, context, owns_video);
}
#else
std::unique_ptr<Nv2aRenderer> create_nv2a_opengl_renderer(std::string& error) {
    error = "OpenGL renderer is unavailable: xlle was built without SDL3";
    return nullptr;
}
#endif

}  // namespace xlle
