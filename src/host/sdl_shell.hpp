// xlle — host shell (SDL3). A0 scope: prove the host layer links and opens a window.
// The presentation/host layer is borrowed, not faithfulness-critical (DESIGN §7).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "machine/ohci.hpp"

namespace xlle {
// Opens a window and pumps events until closed or ~2s elapse. Returns true on success.
// Compiled only when XLLE_WITH_SDL is defined; a stub returns false otherwise.
bool run_window(const char* title, int w, int h);
// Present an RGBA8888 guest frame. The pitch is in bytes.
bool run_frame_window(const char* title, int w, int h, const uint8_t* rgba, size_t pitch);

// Monotonic host nanoseconds. One steady_clock read; the presenter counts its own
// reads in PresentStats::clock_reads (methodology §2 rule 9).
uint64_t present_now_ns();

// The guest scanout layout, handed to SDL without a conversion pass. The guest's
// A8R8G8B8 word is little-endian, so its bytes in memory are B,G,R,A -- which is
// exactly SDL_PIXELFORMAT_XRGB8888 (== SDL_PIXELFORMAT_BGRX32). The two 16-bit
// modes map the same way onto SDL_PIXELFORMAT_RGB565 / _XRGB1555. kRgba32 is the
// LEGACY arm: the format produced by Nv2a::capture_scanout's per-pixel scalar loop.
enum class PresentFormat { kUnknown = 0, kXrgb8888, kRgb565, kXrgb1555, kRgba32 };

// Always-on interactive presentation counters (beads-xbox.1.15.8). Printed once per
// interval and again at shutdown; every rate in the line is derived from a pair of
// counters in this struct, never from a stored constant. Costs a handful of
// steady_clock reads per presented frame plus two per input poll. `clock_reads`
// exports the count, including pacing and reporting reads.
struct PresentStats {
    uint64_t clock_reads = 0;
    uint64_t resident_presents = 0;

    // Input service, once per guest millisecond through XboxMachine's host poll.
    uint64_t input_polls = 0;
    uint64_t input_poll_ns = 0;

    // Pacing. `frame_checks` is every host poll that asked; `new_guest_frames` is
    // how many of those saw the guest's own frame counter advance.
    uint64_t frame_checks = 0;
    uint64_t new_guest_frames = 0;
    uint64_t skipped_same_guest_frame = 0;
    uint64_t skipped_refresh_cap = 0;
    uint64_t legacy_throttle_skips = 0;

    // Capture: getting the guest scanout into a form SDL can upload.
    uint64_t captures = 0;
    uint64_t capture_ns = 0;
    uint64_t capture_bytes = 0;
    uint64_t zero_copy_captures = 0;   // no conversion pass and no staging copy
    uint64_t converted_captures = 0;   // Nv2a::capture_scanout's per-pixel loop

    // Present: the three phases, so a stall shows up where it happens.
    uint64_t presents = 0;
    uint64_t present_failures = 0;
    uint64_t upload_ns = 0;   // SDL_UpdateTexture
    uint64_t draw_ns = 0;     // SDL_RenderClear + SDL_RenderTexture
    uint64_t swap_ns = 0;     // SDL_RenderPresent -- where a vsync wait would land
    uint64_t swap_max_ns = 0;
    uint64_t present_bytes = 0;

    // Capture start to present return, NOT input-to-visible or photon latency.
    uint64_t latency_ns = 0;
    uint64_t latency_max_ns = 0;

    // Environment, resolved once at open().
    float display_refresh_hz = 0.0f;   // 0 = unknown
    uint64_t refresh_cap_ns = 0;       // 0 = no cap (unknown refresh)
    int vsync = -1;                    // read back from SDL after we disable it
};

// Opt-in live keyboard source. It owns a small SDL window for the duration of emulation and
// pumps events non-blockingly; the caller publishes state to OHCI on the emulation thread.
class SdlKeyboardInput {
public:
    SdlKeyboardInput();
    ~SdlKeyboardInput();
    SdlKeyboardInput(const SdlKeyboardInput&) = delete;
    SdlKeyboardInput& operator=(const SdlKeyboardInput&) = delete;

    bool open(const char* title, bool native_gpu_window = false);
    void* native_window() const;
    void note_resident_present(uint64_t start_ns);
    uint64_t now_ns();  // counted host clock read; never fed into guest state
    // `legacy_throttle_ms` restores the pre-beads-xbox.1.15.8 wall-clock present
    // throttle on the SAME binary (arm A of the interactive A/B). 0 -- the default --
    // paces on the guest's own frame counter instead.
    void set_legacy_throttle_ms(unsigned ms);
    void poll();
    // True when the GUEST has produced a frame this presenter has not shown yet.
    // `guest_frame_counter` is Nv2a::vblanks(), the guest's own frame clock, derived
    // from retired instructions -- so presented frames per second track guest speed
    // and no host wall clock enters the decision. The one host-side term is the
    // display-refresh cap, whose period comes from the current display mode; it is
    // inert whenever the guest is slower than the panel.
    bool frame_due(uint64_t guest_frame_counter);
    // Present guest bytes directly. `pitch` is the guest surface pitch; SDL strides
    // over it, so a surface wider than the visible area needs no repack.
    bool present_pixels(const void* pixels, PresentFormat format, int width, int height,
                        size_t pitch, uint64_t capture_start_ns);
    // Legacy arm: the RGBA32 buffer Nv2a::capture_scanout's scalar loop produced.
    bool present_rgba(const uint8_t* rgba, int width, int height, size_t pitch,
                      uint64_t capture_start_ns);
    Ohci::XidGamepadState state() const;
    bool quit_requested() const;
    void close();

    // Instrument plumbing. `note_input_poll` and `note_capture` are how the caller
    // folds the two costs it owns (the SDL event pump and the guest-side capture)
    // into the same accumulator the present phases land in.
    void note_input_poll(uint64_t start_ns);
    void note_capture(uint64_t start_ns, uint64_t bytes, bool zero_copy);
    const PresentStats& stats() const;
    // Snapshot for an interval line: the caller keeps the previous copy and prints
    // the delta, so every printed rate carries its own denominator.
    PresentStats stats_copy() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// Format one interval (or cumulative) present line from a pair of snapshots.
// `label` is the scope token the harness keys on; `wall_ns` is the interval the two
// snapshots span; the guest terms come from the caller's own counters.
void format_present_line(char* out, size_t out_size, const char* label, uint64_t wall_ns,
                         const PresentStats& now, const PresentStats& prev,
                         uint64_t guest_vblanks, uint64_t guest_flips, uint64_t guest_insns,
                         uint64_t guest_hz);
}  // namespace xlle
