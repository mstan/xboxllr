#pragma once

#include <array>
#include <cstdint>

namespace xlle {

enum class ContextInitializerResult { Unhandled, Ready, Rejected };

// Explicit method-produced state only. Reset this owner on an actual admitted
// LOAD/reset. Zero-initialized storage is never marked as a known producer.
// These receipts are not a serialized context map or hardware register readback.
struct ContextInitializerState {
    struct Word {
        uint32_t value = 0;
        bool known = false;
    };
    struct Binding {
        bool known = false;
        uint32_t handle = 0, instance = 0;
        std::array<uint32_t, 3> words{};
        uint8_t words_known = 0; // NULL needs only word0; DMA needs all three.
    };
    struct Object {
        bool used = false;
        uint8_t channel = 0, object_class = 0;
        uint32_t instance = 0, identity_word = 0;
        uint8_t pramin_readback_unknown = 0; // affected bytes of owner words0/1
        Binding notifier, state_dma;
        // 9F: chroma, clip, pattern, ROP, beta, beta4, in method order.
        std::array<Binding, 6> null_selectors{};
    };
    // The first retained prefix uses three owners. A new unsupported owner
    // cannot evict another object's knowledge or inherit its bindings.
    std::array<Object, 16> objects{};

    Word edge_flag, shadow_threshold, line_width, point_size;
    Word window_clip_type, polygon_stipple, two_side_lighting;
    Word multisample, context_write_enable;
    Word opaque_raster_09f8, opaque_z_1d84;
    std::array<Word, 4> color_key{};
};

} // namespace xlle
