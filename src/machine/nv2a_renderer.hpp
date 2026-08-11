// Optional host acceleration seam for NV2A generic-combiner batches.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace xlle {

// This is deliberately an owned snapshot.  A renderer must never retain pointers into
// Nv2a's mutable register or vertex state after submit_generic_combiner returns.
struct Nv2aGenericCombinerVertex {
    std::array<float, 4> position{};
    std::array<float, 4> diffuse{};
    std::array<float, 4> specular{};
    std::array<std::array<float, 4>, 4> texture{};
};

struct Nv2aGenericCombinerPacket {
    enum class TextureLayout : uint8_t { kLinear, kSwizzled };
    enum class TextureFormat : uint8_t { kArgb8, kA8 };
    enum class TextureMode : uint8_t { kProject2d, kCubemap };
    struct Texture {
        bool referenced = false;
        TextureLayout layout = TextureLayout::kSwizzled;
        TextureFormat format = TextureFormat::kArgb8;
        TextureMode mode = TextureMode::kProject2d;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t pitch = 0;
        uint32_t address_mode = 0;
        uint32_t border_color = 0;
        std::vector<uint8_t> input;
    };

    std::vector<Nv2aGenericCombinerVertex> vertices;
    std::vector<uint32_t> raster_state;
    std::array<Texture, 4> textures{};
    uint8_t primitive_mode = 0;
    uint32_t surface_format = 0;
    uint32_t surface_pitch = 0;
    uint32_t surface_height = 0;
    uint32_t color_address = 0;
    uint32_t depth_address = 0;
    std::vector<uint8_t> color_input;
    std::vector<uint8_t> depth_input;
};

// A successful synchronous submission returns complete replacement images for the
// packet's color surface and, when present, depth surface. Nv2a validates their
// sizes and commits them through its normal guest-memory write path. This is the
// correctness-first protocol; a future resident backend must use a separate,
// explicit resolve/invalidate contract rather than weakening this one.
struct Nv2aGenericCombinerResult {
    std::vector<uint8_t> color_output;
    std::vector<uint8_t> depth_output;
    uint64_t triangles = 0;
    uint64_t fragments = 0;
    uint64_t writes = 0;
};

class Nv2aRenderer {
public:
    virtual ~Nv2aRenderer() = default;
    virtual const char* name() const = 0;
    // This query must have no side effects. Nv2a snapshots the potentially large
    // guest surfaces only after a renderer accepts the validated descriptor.
    virtual bool supports_generic_combiner(
        const Nv2aGenericCombinerPacket& descriptor) const = 0;
    // Once supports_generic_combiner returned true, failure is fatal for this draw:
    // silently replaying it in software could double-apply partially submitted work.
    virtual bool submit_generic_combiner(
        const Nv2aGenericCombinerPacket& packet,
        Nv2aGenericCombinerResult& result, std::string& error) = 0;
};

}  // namespace xlle
