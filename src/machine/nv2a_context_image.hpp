#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace xlle {

// The original producer owns 0x37f0 bytes. The physical SAVE observation writes
// a 0x36a0 prefix; that does not establish a general hardware LOAD read extent.
// Retain the entire producer allocation, including its opaque tail. Only fields
// with independently established consumers may be projected into live state.
// See BLINX-QUALIFICATION-CLOSURE.md, 2026-10-16 surface mapping/slot review.
class Nv2aContextImage {
public:
    static constexpr size_t kSourceBytes = 0x37f0;
    static constexpr size_t kMeasuredSavePrefixBytes = 0x36a0;

    // Decode into a staged owner. Failure leaves this object unchanged. There
    // is no guessed default for the rest of the context and no method replay.
    bool load(const uint8_t* bytes, size_t size, const char*& error) {
        error = nullptr;
        if (!bytes || size != kSourceBytes) {
            error = "NV2A context image extent is unsupported"; return false;
        }
        auto word = [&](size_t offset) {
            return uint32_t(bytes[offset]) | (uint32_t(bytes[offset+1]) << 8) |
                   (uint32_t(bytes[offset+2]) << 16) | (uint32_t(bytes[offset+3]) << 24);
        };
        // Channel zero, first initialized 3D context and its measured inverse
        // LOAD. This is a bounded observed profile, not a universal OR-0x10 law.
        if (word(0) != 1 && word(0) != 0x11) {
            error = "NV2A context image header profile is unsupported"; return false;
        }
        std::array<uint32_t, 23> surfaces;
        for (size_t i = 0; i < surfaces.size(); ++i) {
            uint32_t value = word(0xe0 + i*4);
            if (i < 12) {
                if (value & ~0x3fffffc0u) {
                    error = "NV2A context surface offset/base encoding is unsupported"; return false;
                }
            } else if (i < 17) {
                if (value & ~0xffc0u) {
                    error = "NV2A context surface pitch encoding is unsupported"; return false;
                }
            } else {
                // Six fixed-low-bit limit registers. Original input uses0;
                // measured SAVE/inverse input uses3F. Keep the raw input too.
                if ((value & ~0xbfffffffu) || ((value & 0x3f) != 0 && (value & 0x3f) != 0x3f)) {
                    error = "NV2A context surface limit encoding is unsupported"; return false;
                }
                value |= 0x3f;
            }
            surfaces[i] = value;
        }
        std::copy_n(bytes, kSourceBytes, source_.begin());
        surfaces_ = surfaces;
        return true;
    }

    // Physical tag/SAVE/clear/LOAD maps these 23 contiguous registers to the
    // 23 image words at E0..138. BASE/LIMIT do NOT identify a DMA object.
    uint32_t surface_word(size_t index) const { return surfaces_.at(index); }
    const std::array<uint8_t, kSourceBytes>& source() const { return source_; }

private:
    std::array<uint8_t, kSourceBytes> source_{};
    std::array<uint32_t, 23> surfaces_{};
};

} // namespace xlle
