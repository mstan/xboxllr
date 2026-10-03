#pragma once
#include "machine/nv2a_renderer.hpp"

namespace xlle {
// A real donor shader path with a deliberately bounded input contract. Decline
// means use the existing reference implementation, never synthesize success.
enum class XemuPshReject { kNone, kState, kCombiner, kTexture, kGeometry, kCount };
XemuPshReject nv2a_xemu_psh_supported(const Nv2aGeneratedShaderPacket& packet);
uint64_t nv2a_xemu_psh_key(const Nv2aGeneratedShaderPacket& packet);
std::string nv2a_xemu_fragment_glsl(const Nv2aGeneratedShaderPacket& packet);
extern const char* const kNv2aXemuVertexSource;
}
