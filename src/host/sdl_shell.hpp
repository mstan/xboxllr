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

// Opt-in live keyboard source. It owns a small SDL window for the duration of emulation and
// pumps events non-blockingly; the caller publishes state to OHCI on the emulation thread.
class SdlKeyboardInput {
public:
    SdlKeyboardInput();
    ~SdlKeyboardInput();
    SdlKeyboardInput(const SdlKeyboardInput&) = delete;
    SdlKeyboardInput& operator=(const SdlKeyboardInput&) = delete;

    bool open(const char* title);
    void poll();
    // Wall-clock throttle for expensive guest scanout capture (currently 30 Hz).
    bool frame_due();
    bool present_rgba(const uint8_t* rgba, int width, int height, size_t pitch);
    Ohci::XidGamepadState state() const;
    bool quit_requested() const;
    void close();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace xlle
