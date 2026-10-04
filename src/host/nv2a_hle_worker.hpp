#pragma once
#include "machine/nv2a_renderer.hpp"
#include <functional>
#include <memory>
namespace xlle {
std::unique_ptr<Nv2aRenderer> wrap_hle_worker(
    std::unique_ptr<Nv2aRenderer> renderer, std::function<void()> release_context);
}
