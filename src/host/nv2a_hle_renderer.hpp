#pragma once
#include "machine/nv2a_renderer.hpp"
#include <memory>
namespace xlle {
std::unique_ptr<Nv2aRenderer> create_nv2a_hle_renderer(std::string &error, bool worker = false);
}
