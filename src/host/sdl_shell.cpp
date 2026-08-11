// xlle — host shell implementation.
#include "host/sdl_shell.hpp"
#include "host/xid_input.hpp"

#include <cstdio>

#if defined(XLLE_WITH_SDL)
#include <SDL3/SDL.h>

namespace xlle {
class SdlKeyboardInput::Impl {
public:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    int texture_width = 0;
    int texture_height = 0;
    Uint64 last_frame_tick = 0;
    XidKeyboardMapper mapper;
    bool quit = false;
    bool owns_video = false;
};

SdlKeyboardInput::SdlKeyboardInput() : impl_(std::make_unique<Impl>()) {}
SdlKeyboardInput::~SdlKeyboardInput() { close(); }

bool SdlKeyboardInput::open(const char* title) {
    if (impl_->window) return true;
    impl_->owns_video = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
    if (impl_->owns_video && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[input] SDL_Init failed: %s\n", SDL_GetError());
        impl_->owns_video = false;
        return false;
    }
    impl_->window = SDL_CreateWindow(title, 640, 480, SDL_WINDOW_RESIZABLE);
    impl_->renderer = impl_->window ? SDL_CreateRenderer(impl_->window, nullptr) : nullptr;
    if (!impl_->window || !impl_->renderer) {
        std::fprintf(stderr, "[input] SDL window/renderer failed: %s\n", SDL_GetError());
        if (impl_->renderer) SDL_DestroyRenderer(impl_->renderer);
        if (impl_->window) SDL_DestroyWindow(impl_->window);
        impl_->renderer = nullptr;
        impl_->window = nullptr;
        if (impl_->owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
        impl_->owns_video = false;
        return false;
    }
    return true;
}

void SdlKeyboardInput::poll() {
    if (!impl_->window) return;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) { impl_->quit = true; continue; }
        // SDL may not deliver key-up events after focus moves elsewhere. Never leave a
        // guest button latched merely because the host window lost focus.
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            impl_->mapper = XidKeyboardMapper{};
            continue;
        }
        if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) continue;
        const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
        switch (event.key.scancode) {
            case SDL_SCANCODE_UP: impl_->mapper.set(XidHostKey::Up, pressed); break;
            case SDL_SCANCODE_DOWN: impl_->mapper.set(XidHostKey::Down, pressed); break;
            case SDL_SCANCODE_LEFT: impl_->mapper.set(XidHostKey::Left, pressed); break;
            case SDL_SCANCODE_RIGHT: impl_->mapper.set(XidHostKey::Right, pressed); break;
            case SDL_SCANCODE_Z: impl_->mapper.set(XidHostKey::A, pressed); break;
            case SDL_SCANCODE_X: impl_->mapper.set(XidHostKey::B, pressed); break;
            case SDL_SCANCODE_RETURN: impl_->mapper.set(XidHostKey::Start, pressed); break;
            case SDL_SCANCODE_BACKSPACE: impl_->mapper.set(XidHostKey::Back, pressed); break;
            default: break;
        }
    }
}

bool SdlKeyboardInput::frame_due() {
    if (!impl_->window) return false;
    const Uint64 now = SDL_GetTicks();
    if (now - impl_->last_frame_tick < 33) return false;
    impl_->last_frame_tick = now;
    return true;
}

bool SdlKeyboardInput::present_rgba(const uint8_t* rgba, int width, int height,
                                    size_t pitch) {
    if (!impl_->renderer || !rgba || width <= 0 || height <= 0) return false;
    if (!impl_->texture || width != impl_->texture_width || height != impl_->texture_height) {
        if (impl_->texture) SDL_DestroyTexture(impl_->texture);
        impl_->texture = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!impl_->texture) return false;
        impl_->texture_width = width;
        impl_->texture_height = height;
        SDL_SetTextureScaleMode(impl_->texture, SDL_SCALEMODE_LINEAR);
        SDL_SetWindowSize(impl_->window, width, height);
    }
    if (!SDL_UpdateTexture(impl_->texture, nullptr, rgba, static_cast<int>(pitch))) return false;
    SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255);
    SDL_RenderClear(impl_->renderer);
    SDL_RenderTexture(impl_->renderer, impl_->texture, nullptr, nullptr);
    SDL_RenderPresent(impl_->renderer);
    return true;
}

Ohci::XidGamepadState SdlKeyboardInput::state() const { return impl_->mapper.state(); }
bool SdlKeyboardInput::quit_requested() const { return impl_->quit; }
void SdlKeyboardInput::close() {
    if (!impl_ || !impl_->window) return;
    if (impl_->texture) SDL_DestroyTexture(impl_->texture);
    if (impl_->renderer) SDL_DestroyRenderer(impl_->renderer);
    SDL_DestroyWindow(impl_->window);
    impl_->texture = nullptr;
    impl_->renderer = nullptr;
    impl_->window = nullptr;
    if (impl_->owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    impl_->owns_video = false;
}

static bool pump_window(SDL_Window* win, SDL_Renderer* renderer, SDL_Texture* texture,
                        int w, int h) {
    std::printf("[host] frame window %dx%d up; close it or wait ~5s\n", w, h);
    Uint64 start = SDL_GetTicks();
    bool run = true;
    while (run && SDL_GetTicks() - start < 5000) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) run = false;
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    return true;
}

bool run_frame_window(const char* title, int w, int h, const uint8_t* rgba, size_t pitch) {
    if (!rgba || w <= 0 || h <= 0) return false;
    const bool owns_video = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
    if (owns_video && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[host] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_Window* win = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = win ? SDL_CreateRenderer(win, nullptr) : nullptr;
    SDL_Texture* texture =
        renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STATIC, w, h)
                 : nullptr;
    if (!win || !renderer || !texture ||
        !SDL_UpdateTexture(texture, nullptr, rgba, static_cast<int>(pitch))) {
        std::fprintf(stderr, "[host] frame presentation setup failed: %s\n", SDL_GetError());
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (win) SDL_DestroyWindow(win);
        if (owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    pump_window(win, renderer, texture, w, h);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    if (owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return true;
}

bool run_window(const char* title, int w, int h) {
    const bool owns_video = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
    if (owns_video && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[host] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_Window* win = SDL_CreateWindow(title, w, h, 0);
    if (!win) {
        std::fprintf(stderr, "[host] SDL_CreateWindow failed: %s\n", SDL_GetError());
        if (owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }
    std::printf("[host] window '%s' %dx%d up; close it or wait ~2s\n", title, w, h);
    Uint64 start = SDL_GetTicks();
    bool run = true;
    while (run && SDL_GetTicks() - start < 2000) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) run = false;
        }
        SDL_Delay(16);
    }
    SDL_DestroyWindow(win);
    if (owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return true;
}
}  // namespace xlle

#else  // no SDL

namespace xlle {
class SdlKeyboardInput::Impl {};
SdlKeyboardInput::SdlKeyboardInput() : impl_(std::make_unique<Impl>()) {}
SdlKeyboardInput::~SdlKeyboardInput() = default;
bool SdlKeyboardInput::open(const char*) {
    std::fprintf(stderr, "[input] built without SDL (XLLE_WITH_SDL off)\n");
    return false;
}
void SdlKeyboardInput::poll() {}
bool SdlKeyboardInput::frame_due() { return false; }
bool SdlKeyboardInput::present_rgba(const uint8_t*, int, int, size_t) { return false; }
Ohci::XidGamepadState SdlKeyboardInput::state() const { return {}; }
bool SdlKeyboardInput::quit_requested() const { return false; }
void SdlKeyboardInput::close() {}
bool run_window(const char*, int, int) {
    std::fprintf(stderr, "[host] built without SDL (XLLE_WITH_SDL off)\n");
    return false;
}
bool run_frame_window(const char*, int, int, const uint8_t*, size_t) {
    std::fprintf(stderr, "[host] built without SDL (XLLE_WITH_SDL off)\n");
    return false;
}
}  // namespace xlle
#endif
