// xlle — host shell implementation.
#include "host/sdl_shell.hpp"

#include <cstdio>

#if defined(XLLE_WITH_SDL)
#include <SDL3/SDL.h>

namespace xlle {
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
    if (!SDL_Init(SDL_INIT_VIDEO)) {
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
        SDL_Quit();
        return false;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    pump_window(win, renderer, texture, w, h);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return true;
}

bool run_window(const char* title, int w, int h) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[host] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_Window* win = SDL_CreateWindow(title, w, h, 0);
    if (!win) {
        std::fprintf(stderr, "[host] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
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
    SDL_Quit();
    return true;
}
}  // namespace xlle

#else  // no SDL

namespace xlle {
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
