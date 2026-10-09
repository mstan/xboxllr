// Bounded MCPX encoder-field GPIO. Original XDK writes 1 to PM+C0 and
// samples bit 5. AMD-766 23167B p82 describes the corresponding input-control
// layout; xemu confirms Xbox field wiring. MCPX layout equivalence is an
// explicit board-profile inference, not a new silicon measurement.
#pragma once
#include "machine/io_device.hpp"

namespace xlle {
class McpxFieldInput {
public:
    explicit McpxFieldInput(FaultFn fault) : fault_(std::move(fault)) {}
    void set_level(bool high) {
        level_ = high;
        if (active()) latched_ = true;
    }
    uint8_t read() const {
        return control_ | (level_ ? 0x20 : 0) | (latched_ ? 0x40 : 0);
    }
    void write(uint32_t value) {
        // Input polarity/latch selection and W1C latch; RTIN is read-only.
        // Output, alternate-function and debounce modes need their real
        // providers. Do not accept them as inert configuration storage.
        if (value & ~0x63u) {
            fault_("MCPX field GPIO unsupported output/debounce/alternate mode");
            return;
        }
        control_ = value & 3;
        if (value & 0x40) latched_ = false;
    }
    bool input() const { return (control_ & 2) ? latched_ : active(); }
private:
    bool active() const { return level_ == ((control_ & 1) != 0); }
    FaultFn fault_;
    // Inherited active-high boot profile; original device init writes it too.
    uint8_t control_ = 1;
    bool level_ = false, latched_ = false;
};
} // namespace xlle
