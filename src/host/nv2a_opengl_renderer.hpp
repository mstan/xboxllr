#pragma once

#include <memory>
#include <string>

#include "machine/nv2a_renderer.hpp"

namespace xlle {

// Creates the optional GL 4.3 host backend for qualified NV2A compute/raster
// paths and resident surfaces. Unsupported packets explicitly decline to the
// existing LLE renderer; this is host rendering, not a guest CPU AOT fallback.
std::unique_ptr<Nv2aRenderer> create_nv2a_opengl_renderer(
    std::string& error, bool enhanced_xemu = false);

}  // namespace xlle
