// xlle — host shell (SDL3). A0 scope: prove the host layer links and opens a window.
// The presentation/host layer is borrowed, not faithfulness-critical (DESIGN §7).
#pragma once

#include <cstddef>
#include <cstdint>

namespace xlle {
// Opens a window and pumps events until closed or ~2s elapse. Returns true on success.
// Compiled only when XLLE_WITH_SDL is defined; a stub returns false otherwise.
bool run_window(const char* title, int w, int h);
// Present an RGBA8888 guest frame. The pitch is in bytes.
bool run_frame_window(const char* title, int w, int h, const uint8_t* rgba, size_t pitch);
}  // namespace xlle
