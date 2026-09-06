// Host presentation contract, without firmware or a physical display.
#include <cstdio>
#include <cstring>
#include <vector>
#include <SDL3/SDL.h>
#include "host/sdl_shell.hpp"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL: %s (line %d): %s\n", #c, __LINE__, SDL_GetError()); ++fails; } } while (0)

int main() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    xlle::SdlKeyboardInput presenter;
    CHECK(presenter.open("presentation contract test"));
    CHECK(presenter.stats().vsync == 0);
    // No wall-clock sleep: advancing guest frames can present faster than 30 Hz.
    const uint8_t bgra[] = {0, 0, 255, 0, 0, 255, 0, 0};
    for (uint64_t frame = 0; frame < 120; ++frame) {
        CHECK(presenter.frame_due(frame));
        CHECK(!presenter.frame_due(frame));
        const uint64_t start = presenter.now_ns();
        presenter.note_capture(start, sizeof(bgra), true);
        CHECK(presenter.present_pixels(bgra, xlle::PresentFormat::kXrgb8888, 2, 1, 8, start));
    }
    CHECK(presenter.stats().presents == 120);
    CHECK(presenter.stats().skipped_same_guest_frame == 120);
    CHECK(presenter.stats().zero_copy_captures == 120);
    // Texture recreation on format changes, and legacy RGBA support.
    const uint16_t rgb565[] = {0xf800, 0x07e0};
    const uint16_t rgb1555[] = {0x7c00, 0x03e0};
    CHECK(presenter.present_pixels(rgb565, xlle::PresentFormat::kRgb565, 2, 1, 4, presenter.now_ns()));
    CHECK(presenter.present_pixels(rgb1555, xlle::PresentFormat::kXrgb1555, 2, 1, 4, presenter.now_ns()));
    CHECK(presenter.present_rgba(bgra, 2, 1, 8, presenter.now_ns()));
    CHECK(!presenter.present_pixels(bgra, xlle::PresentFormat::kXrgb8888, 2, 1, 1, 0));
    CHECK(!presenter.present_pixels(nullptr, xlle::PresentFormat::kXrgb8888, 2, 1, 8, 0));
    CHECK(presenter.stats().presents == 123);
    CHECK(presenter.stats().present_failures == 2);

    // SDL's native layouts reproduce RGB channels and ignore the X/alpha byte.
    const uint8_t expected[] = {255, 0, 0, 255, 0, 255, 0, 255};
    uint8_t rgba[8]{};
    CHECK(SDL_ConvertPixels(2, 1, SDL_PIXELFORMAT_XRGB8888, bgra, 8,
                           SDL_PIXELFORMAT_RGBA32, rgba, 8));
    CHECK(std::memcmp(rgba, expected, sizeof(rgba)) == 0);
    CHECK(SDL_ConvertPixels(2, 1, SDL_PIXELFORMAT_RGB565, rgb565, 4,
                           SDL_PIXELFORMAT_RGBA32, rgba, 8));
    // SDL 3.2's 565 conversion expands full green to 254; scalar capture uses 255.
    CHECK(rgba[0] == 255 && rgba[1] == 0 && rgba[2] == 0);
    CHECK(rgba[4] == 0 && rgba[5] >= 254 && rgba[6] == 0);
    CHECK(SDL_ConvertPixels(2, 1, SDL_PIXELFORMAT_XRGB1555, rgb1555, 4,
                           SDL_PIXELFORMAT_RGBA32, rgba, 8));
    CHECK(std::memcmp(rgba, expected, sizeof(rgba)) == 0);

    // Exhaust the 16-bit modes against the guest capture's integer expansion.
    // SDL may round differently by one LSB; channel order and alpha must agree.
    std::vector<uint16_t> words(65536);
    std::vector<uint8_t> converted(65536 * 4);
    for (unsigned p = 0; p < words.size(); ++p) words[p] = uint16_t(p);
    for (bool is565 : {false, true}) {
        CHECK(SDL_ConvertPixels(256, 256,
            is565 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_XRGB1555,
            words.data(), 512, SDL_PIXELFORMAT_RGBA32, converted.data(), 1024));
        unsigned bad = 0;
        for (unsigned p = 0; p < words.size(); ++p) {
            const unsigned reference[] = {
                ((p >> (is565 ? 11 : 10)) & 31) * 255 / 31,
                ((p >> 5) & (is565 ? 63 : 31)) * 255 / (is565 ? 63 : 31),
                (p & 31) * 255 / 31, 255};
            for (unsigned c = 0; c < 4; ++c) {
                const int delta = int(converted[p * 4 + c]) - int(reference[c]);
                if (delta < -1 || delta > 1 || (c == 3 && delta)) ++bad;
            }
        }
        CHECK(bad == 0);
    }

    // Distinguish API presents, scanout writes, guest vblanks, and guest speed.
    xlle::PresentStats stats{};
    stats.presents = 90;
    stats.captures = 90;
    stats.capture_ns = 900000;
    char line[2048];
    xlle::format_present_line(line, sizeof(line), "test", 2000000000, stats, {},
                             60, 120, 733333333, 733333333);
    CHECK(std::strstr(line, "presented_fps=45.00"));
    CHECK(std::strstr(line, "guest_vblank_fps=30.00"));
    CHECK(std::strstr(line, "guest_flips_per_s=60.00"));
    CHECK(std::strstr(line, "guest_speed=0.500"));
    CHECK(std::strstr(line, "capture_us_avg=10.0"));
    presenter.close();
    std::printf("test_present: %d failures\n", fails);
    return fails ? 1 : 0;
}
