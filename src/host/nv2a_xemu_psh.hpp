#pragma once
#include "machine/nv2a_renderer.hpp"

namespace xlle {
#if XLLE_NATIVE_DRAW
extern const char* const kNv2aNativeVertexSource;
#endif
// A real donor shader path with a deliberately bounded input contract. Decline
// means use the existing reference implementation, never synthesize success.
enum class XemuPshReject { kNone, kState, kCombiner, kTexture, kGeometry, kCount };
// NV2A TENT_NEARESTLOD / TENT_TENT_LOD reduce to base-level bilinear
// when the guest declares exactly one mip. Other mip chains stay unsupported.
inline unsigned nv2a_xemu_min_filter(uint32_t format, uint32_t filter) {
    const unsigned min = (filter >> 16) & 255;
    return ((min == 4 || min == 6) && ((format >> 16) & 15) == 1) ? 2 : min;
}
XemuPshReject nv2a_xemu_psh_supported(const Nv2aGeneratedShaderPacket& packet);
uint64_t nv2a_xemu_psh_key(const Nv2aGeneratedShaderPacket& packet);
std::string nv2a_xemu_fragment_glsl(const Nv2aGeneratedShaderPacket& packet);
extern const char* const kNv2aXemuVertexSource;
}
