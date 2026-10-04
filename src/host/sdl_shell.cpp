// xlle — host shell implementation.
#include "host/sdl_shell.hpp"
#include "host/xid_input.hpp"

#include <chrono>
#include <algorithm>
#include <cstdio>

namespace xlle {
uint64_t present_now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

namespace {
double per_second(uint64_t count, uint64_t wall_ns) {
    if (!wall_ns) return 0.0;
    return static_cast<double>(count) * 1e9 / static_cast<double>(wall_ns);
}
double us_avg(uint64_t total_ns, uint64_t n) {
    if (!n) return 0.0;
    return static_cast<double>(total_ns) / static_cast<double>(n) / 1000.0;
}
}  // namespace

// One line, one interval, every rate divided by a denominator carried in the same
// struct (methodology §2 rule 9). `presented` and `guest` are deliberately separate
// numbers: §2 rule 5 forbids reporting presented frames as effective_fps.
void format_present_line(char* out, size_t out_size, const char* label, uint64_t wall_ns,
                         const PresentStats& now, const PresentStats& prev,
                         uint64_t guest_vblanks, uint64_t guest_flips, uint64_t guest_insns,
                         uint64_t guest_hz) {
    const uint64_t presents = now.presents - prev.presents;
    const uint64_t captures = now.captures - prev.captures;
    const uint64_t polls = now.input_polls - prev.input_polls;
    const uint64_t checks = now.frame_checks - prev.frame_checks;
    const double guest_speed =
        (wall_ns && guest_hz)
            ? static_cast<double>(guest_insns) * 1e9 /
                  (static_cast<double>(guest_hz) * static_cast<double>(wall_ns))
            : 0.0;
    std::snprintf(
        out, out_size,
        "[host] present scope=%s wall_ms=%.1f presented=%llu presented_fps=%.2f "
        "guest_vblanks=%llu guest_vblank_fps=%.2f guest_flips=%llu guest_flips_per_s=%.2f "
        "guest_speed=%.3f capture_to_present_us_avg=%.1f capture_to_present_us_max_total=%.1f capture_us_avg=%.1f "
        "upload_us_avg=%.1f draw_us_avg=%.1f swap_us_avg=%.1f swap_us_max_total=%.1f "
        "zero_copy=%llu converted=%llu capture_MB=%.1f input_polls=%llu "
        "input_poll_hz=%.1f input_us_avg=%.2f frame_checks=%llu skip_same_frame=%llu "
        "skip_refresh_cap=%llu skip_legacy_throttle=%llu present_fail=%llu "
        "refresh_hz=%.2f refresh_cap_ms=%.2f vsync=%d clock_reads=%llu resident_presents=%llu",
        label, static_cast<double>(wall_ns) / 1e6, (unsigned long long)presents,
        per_second(presents, wall_ns), (unsigned long long)guest_vblanks,
        per_second(guest_vblanks, wall_ns), (unsigned long long)guest_flips,
        per_second(guest_flips, wall_ns), guest_speed,
        us_avg(now.latency_ns - prev.latency_ns, presents),
        static_cast<double>(now.latency_max_ns) / 1000.0,
        us_avg(now.capture_ns - prev.capture_ns, captures),
        us_avg(now.upload_ns - prev.upload_ns, presents),
        us_avg(now.draw_ns - prev.draw_ns, presents),
        us_avg(now.swap_ns - prev.swap_ns, presents),
        static_cast<double>(now.swap_max_ns) / 1000.0,
        (unsigned long long)(now.zero_copy_captures - prev.zero_copy_captures),
        (unsigned long long)(now.converted_captures - prev.converted_captures),
        static_cast<double>(now.capture_bytes - prev.capture_bytes) / (1024.0 * 1024.0),
        (unsigned long long)polls, per_second(polls, wall_ns),
        us_avg(now.input_poll_ns - prev.input_poll_ns, polls), (unsigned long long)checks,
        (unsigned long long)(now.skipped_same_guest_frame - prev.skipped_same_guest_frame),
        (unsigned long long)(now.skipped_refresh_cap - prev.skipped_refresh_cap),
        (unsigned long long)(now.legacy_throttle_skips - prev.legacy_throttle_skips),
        (unsigned long long)(now.present_failures - prev.present_failures),
        static_cast<double>(now.display_refresh_hz),
        static_cast<double>(now.refresh_cap_ns) / 1e6, now.vsync,
        (unsigned long long)(now.clock_reads - prev.clock_reads),
        (unsigned long long)(now.resident_presents - prev.resident_presents));
}
}  // namespace xlle

#if defined(XLLE_WITH_SDL)
#include <SDL3/SDL.h>

namespace xlle {
namespace {
SDL_PixelFormat sdl_format(PresentFormat format) {
    switch (format) {
        // The guest word is little-endian A8R8G8B8, so its bytes in memory are
        // B,G,R,A. SDL_PIXELFORMAT_XRGB8888 is SDL_PIXELFORMAT_BGRX32 -- the same
        // bytes in the same order. No shuffle, no staging buffer.
        case PresentFormat::kXrgb8888: return SDL_PIXELFORMAT_XRGB8888;
        case PresentFormat::kRgb565: return SDL_PIXELFORMAT_RGB565;
        case PresentFormat::kXrgb1555: return SDL_PIXELFORMAT_XRGB1555;
        case PresentFormat::kRgba32: return SDL_PIXELFORMAT_RGBA32;
        default: break;
    }
    return SDL_PIXELFORMAT_UNKNOWN;
}
}  // namespace

class SdlKeyboardInput::Impl {
public:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    int texture_width = 0;
    int texture_height = 0;
    SDL_PixelFormat texture_format = SDL_PIXELFORMAT_UNKNOWN;
    // Arm A only: the pre-1.15.8 wall-clock throttle, restored by a runtime flag on
    // the same binary. 0 means the guest's frame counter paces the presenter.
    unsigned legacy_throttle_ms = 0;
    Uint64 last_frame_tick = 0;
    uint64_t last_guest_frame = UINT64_MAX;
    uint64_t last_present_ns = 0;
    XidKeyboardMapper mapper;
    bool quit = false;
    bool owns_video = false;
    PresentStats stats;
};

SdlKeyboardInput::SdlKeyboardInput() : impl_(std::make_unique<Impl>()) {}
SdlKeyboardInput::~SdlKeyboardInput() { close(); }

bool SdlKeyboardInput::open(const char* title, bool native_gpu_window) {
    if (impl_->window) return true;
    impl_->owns_video = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0;
    if (impl_->owns_video && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "[input] SDL_Init failed: %s\n", SDL_GetError());
        impl_->owns_video = false;
        return false;
    }
    // The native renderer may bind its GL context to this window. SDL's GL
    // renderer remains the real upload path for CPU-produced startup frames.
    impl_->window = SDL_CreateWindow(title, 640, 480, SDL_WINDOW_RESIZABLE |
                                    (native_gpu_window ? SDL_WINDOW_OPENGL : 0));
    impl_->renderer = impl_->window ? SDL_CreateRenderer(impl_->window,
                                    native_gpu_window ? "opengl" : nullptr) : nullptr;
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
    // The emulation thread calls SDL_RenderPresent inline, so a vsync wait would
    // block the GUEST, not just the presenter. SDL3 already defaults a new renderer
    // to SDL_RENDERER_VSYNC_DISABLED; say it anyway and read it back, so the value
    // in the report is what the renderer is actually doing rather than what the
    // documentation says it should be. Pacing is ours (frame_due).
    const bool disabled = SDL_SetRenderVSync(impl_->renderer, SDL_RENDERER_VSYNC_DISABLED);
    int vsync = -1;
    if (SDL_GetRenderVSync(impl_->renderer, &vsync)) impl_->stats.vsync = vsync;
    std::printf("[host] presenter renderer=%s vsync=%d disable_vsync_ok=%d\n",
                SDL_GetRendererName(impl_->renderer), impl_->stats.vsync, disabled ? 1 : 0);

    // The one host-side pacing term, and its period is READ from the display rather
    // than written into the source. Three quarters of a refresh period, so a guest
    // running at exactly the panel rate is not halved by beat between two
    // unsynchronised 60 Hz clocks.
    const SDL_DisplayID display = SDL_GetDisplayForWindow(impl_->window);
    const SDL_DisplayMode* mode =
        SDL_GetCurrentDisplayMode(display ? display : SDL_GetPrimaryDisplay());
    if (mode && mode->refresh_rate > 0.0f) {
        impl_->stats.display_refresh_hz = mode->refresh_rate;
        impl_->stats.refresh_cap_ns =
            static_cast<uint64_t>(1.0e9 / static_cast<double>(mode->refresh_rate) * 0.75);
    }
    return true;
}

void SdlKeyboardInput::set_legacy_throttle_ms(unsigned ms) { impl_->legacy_throttle_ms = ms; }

uint64_t SdlKeyboardInput::now_ns() {
    ++impl_->stats.clock_reads;
    return present_now_ns();
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

bool SdlKeyboardInput::frame_due(uint64_t guest_frame_counter) {
    if (!impl_->window) return false;
    ++impl_->stats.frame_checks;
    if (impl_->legacy_throttle_ms) {
        // Arm A: the wall-clock throttle. Its ceiling is 1000 / legacy_throttle_ms
        // presented frames per second REGARDLESS of what the guest produced, which
        // is the ceiling this stage exists to remove.
        const Uint64 now = SDL_GetTicks();
        ++impl_->stats.clock_reads;
        if (now - impl_->last_frame_tick < impl_->legacy_throttle_ms) {
            ++impl_->stats.legacy_throttle_skips;
            return false;
        }
        impl_->last_frame_tick = now;
        return true;
    }
    // Arm B: the guest's own frame clock decides. Nothing here reads the host wall
    // clock unless the panel cap is armed, so presented fps == guest frame rate for
    // as long as the guest is slower than the display.
    if (guest_frame_counter == impl_->last_guest_frame) {
        ++impl_->stats.skipped_same_guest_frame;
        return false;
    }
    if (impl_->stats.refresh_cap_ns) {
        const uint64_t now = now_ns();
        if (impl_->last_present_ns &&
            now - impl_->last_present_ns < impl_->stats.refresh_cap_ns) {
            // Do NOT consume the guest frame: the next poll re-offers it.
            ++impl_->stats.skipped_refresh_cap;
            return false;
        }
    }
    impl_->last_guest_frame = guest_frame_counter;
    ++impl_->stats.new_guest_frames;
    return true;
}

bool SdlKeyboardInput::present_pixels(const void* pixels, PresentFormat format, int width,
                                      int height, size_t pitch, uint64_t capture_start_ns) {
    if (!impl_->renderer || !pixels || width <= 0 || height <= 0 || pitch > INT32_MAX) {
        ++impl_->stats.present_failures;
        return false;
    }
    const SDL_PixelFormat want = sdl_format(format);
    if (want == SDL_PIXELFORMAT_UNKNOWN ||
        pitch < static_cast<size_t>(width) * SDL_BYTESPERPIXEL(want)) {
        ++impl_->stats.present_failures;
        return false;
    }
    if (!impl_->texture || width != impl_->texture_width || height != impl_->texture_height ||
        want != impl_->texture_format) {
        if (impl_->texture) SDL_DestroyTexture(impl_->texture);
        impl_->texture = SDL_CreateTexture(impl_->renderer, want,
                                           SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!impl_->texture) {
            impl_->texture_format = SDL_PIXELFORMAT_UNKNOWN;
            ++impl_->stats.present_failures;
            return false;
        }
        impl_->texture_width = width;
        impl_->texture_height = height;
        impl_->texture_format = want;
        SDL_SetTextureBlendMode(impl_->texture, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(impl_->texture, SDL_SCALEMODE_LINEAR);
        SDL_SetWindowSize(impl_->window, width, height);
    }
    const uint64_t t_upload = now_ns();
    if (!SDL_UpdateTexture(impl_->texture, nullptr, pixels, static_cast<int>(pitch))) {
        ++impl_->stats.present_failures;
        return false;
    }
    const uint64_t t_draw = now_ns();
    if (!SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255) ||
        !SDL_RenderClear(impl_->renderer) ||
        !SDL_RenderTexture(impl_->renderer, impl_->texture, nullptr, nullptr)) {
        ++impl_->stats.present_failures;
        return false;
    }
    const uint64_t t_swap = now_ns();
    if (!SDL_RenderPresent(impl_->renderer)) {
        ++impl_->stats.present_failures;
        return false;
    }
    const uint64_t t_end = now_ns();

    impl_->stats.upload_ns += t_draw - t_upload;
    impl_->stats.draw_ns += t_swap - t_draw;
    const uint64_t swap = t_end - t_swap;
    impl_->stats.swap_ns += swap;
    if (swap > impl_->stats.swap_max_ns) impl_->stats.swap_max_ns = swap;
    impl_->stats.present_bytes += static_cast<uint64_t>(pitch) * static_cast<uint64_t>(height);
    ++impl_->stats.presents;
    impl_->last_present_ns = t_end;
    if (capture_start_ns && t_end > capture_start_ns) {
        const uint64_t latency = t_end - capture_start_ns;
        impl_->stats.latency_ns += latency;
        if (latency > impl_->stats.latency_max_ns) impl_->stats.latency_max_ns = latency;
    }
    return true;
}

void* SdlKeyboardInput::native_window() const { return impl_->window; }
void SdlKeyboardInput::note_resident_present(uint64_t start_ns) {
    const uint64_t now = now_ns();
    ++impl_->stats.presents;
    ++impl_->stats.resident_presents;
    impl_->last_present_ns = now;
    if (now > start_ns) {
        const uint64_t elapsed = now - start_ns;
        impl_->stats.latency_ns += elapsed;
        impl_->stats.draw_ns += elapsed;
        impl_->stats.latency_max_ns = std::max(impl_->stats.latency_max_ns, elapsed);
    }
}

bool SdlKeyboardInput::present_rgba(const uint8_t* rgba, int width, int height, size_t pitch,
                                    uint64_t capture_start_ns) {
    return present_pixels(rgba, PresentFormat::kRgba32, width, height, pitch,
                          capture_start_ns);
}

void SdlKeyboardInput::note_input_poll(uint64_t start_ns) {
    const uint64_t now = now_ns();
    ++impl_->stats.input_polls;
    if (now > start_ns) impl_->stats.input_poll_ns += now - start_ns;
}

void SdlKeyboardInput::note_capture(uint64_t start_ns, uint64_t bytes, bool zero_copy) {
    const uint64_t now = now_ns();
    ++impl_->stats.captures;
    if (now > start_ns) impl_->stats.capture_ns += now - start_ns;
    impl_->stats.capture_bytes += bytes;
    if (zero_copy)
        ++impl_->stats.zero_copy_captures;
    else
        ++impl_->stats.converted_captures;
}

const PresentStats& SdlKeyboardInput::stats() const { return impl_->stats; }
PresentStats SdlKeyboardInput::stats_copy() const { return impl_->stats; }

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
class SdlKeyboardInput::Impl {
public:
    PresentStats stats;
};
SdlKeyboardInput::SdlKeyboardInput() : impl_(std::make_unique<Impl>()) {}
SdlKeyboardInput::~SdlKeyboardInput() = default;
bool SdlKeyboardInput::open(const char*, bool) {
    std::fprintf(stderr, "[input] built without SDL (XLLE_WITH_SDL off)\n");
    return false;
}
void SdlKeyboardInput::set_legacy_throttle_ms(unsigned) {}
uint64_t SdlKeyboardInput::now_ns() {
    ++impl_->stats.clock_reads;
    return present_now_ns();
}
void* SdlKeyboardInput::native_window() const { return nullptr; }
void SdlKeyboardInput::note_resident_present(uint64_t) {}
void SdlKeyboardInput::poll() {}
bool SdlKeyboardInput::frame_due(uint64_t) { return false; }
bool SdlKeyboardInput::present_pixels(const void*, PresentFormat, int, int, size_t, uint64_t) {
    return false;
}
bool SdlKeyboardInput::present_rgba(const uint8_t*, int, int, size_t, uint64_t) { return false; }
void SdlKeyboardInput::note_input_poll(uint64_t) {}
void SdlKeyboardInput::note_capture(uint64_t, uint64_t, bool) {}
const PresentStats& SdlKeyboardInput::stats() const { return impl_->stats; }
PresentStats SdlKeyboardInput::stats_copy() const { return impl_->stats; }
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
