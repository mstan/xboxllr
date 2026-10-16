// Bounded extended-color DAC upload used by the original Xbox driver.
// nxdk XVideoSetGammaRamp writes index zero followed by 256 RGB byte triples;
// nv_vga.xml identifies the PRMDIO byte ports and nv3_pramdac.xml BPC_8BITS.
// Nonzero-index updates have a documented NV2A caveat in nxdk and stay guarded.
// Scanout during an incomplete upload is unqualified: this does not invent an
// atomic hardware bank swap or claim the timing of partially uploaded entries.
#pragma once
#include <array>
#include <cstdint>

namespace xlle {
class Nv2aDacGamma {
public:
    static bool handles(uint32_t port) { return port >= 0x3c6 && port <= 0x3c9; }
    bool write(uint32_t port, uint32_t value, uint32_t general_control) {
        if (value > 255) return reject("NV2A DAC byte value out of range");
        // Actual extended-color AV mode. Other DAC widths/mixing modes need
        // their own transport and output contract, not implicit truncation.
        if (general_control != 0x00100030)
            return reject("NV2A DAC upload outside eight-bit extended-color profile");
        if (port == 0x3c8) {
            if (value != 0) return reject("NV2A DAC partial-index upload remains unqualified");
            started_ = true;
            complete_ = false;
            next_ = 0;
            identity_ = true;
            return true;
        }
        if (port != 0x3c9) return reject("NV2A DAC register operation remains unqualified");
        if (!started_) return reject("NV2A DAC data without an owned write index");
        if (complete_) { complete_ = false; identity_ = true; }
        entries_[next_ / 3][next_ % 3] = static_cast<uint8_t>(value);
        identity_ = identity_ && value == next_ / 3;
        if (++next_ == 256 * 3) { next_ = 0; complete_ = true; }
        return true;
    }
    bool started() const { return started_; }
    bool complete() const { return complete_; }
    bool identity() const { return complete_ && identity_; }
    uint8_t map(unsigned channel, uint8_t input) const { return entries_[input][channel]; }
    const char* error() const { return error_; }
private:
    bool reject(const char* why) { error_ = why; return false; }
    std::array<std::array<uint8_t, 3>, 256> entries_{};
    unsigned next_ = 0;
    bool started_ = false, complete_ = false, identity_ = false;
    const char* error_ = nullptr;
};
} // namespace xlle
