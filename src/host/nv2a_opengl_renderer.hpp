#pragma once

#include <memory>
#include <string>

#include "machine/nv2a_renderer.hpp"

namespace xlle {

// Creates the optional GL 4.3 compute-capable host context. The returned renderer
// currently declines packets; it exists to validate host capability and lifetime.
std::unique_ptr<Nv2aRenderer> create_nv2a_opengl_renderer(std::string& error);

}  // namespace xlle
