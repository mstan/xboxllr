// xlle — host shell implementation.
#include "host/sdl_shell.hpp"

#include <cstdio>

#if defined(XLLE_WITH_SDL)
#include <SDL3/SDL.h>

namespace xlle {
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
}  // namespace xlle
#endif
