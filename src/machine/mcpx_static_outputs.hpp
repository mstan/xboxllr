// Bounded MCPX GPIO static outputs. NVIDIA MCP-1 0.15 section 5.15
// establishes the contiguous control map. The 04/05 encoding follows
// AMD-766 23167B p82 and observed original Xbox programming; applying that
// compatible encoding to MCPX is an explicit board-profile inference.
#pragma once
#include "machine/io_device.hpp"

namespace xlle {
class McpxStaticOutputs {
public:
    McpxStaticOutputs(uint32_t pins, FaultFn fault)
        : pins_(pins & 0x03ffffffu), fault_(std::move(fault)) {}
    bool owns(uint16_t port) const {
        return port >= 0x80c0 && port < 0x80da &&
               (pins_ & (1u << (port - 0x80c0)));
    }
    bool write(uint16_t port, uint32_t value) {
        if (!owns(port) || (value != 4 && value != 5)) {
            if (fault_) fault_("MCPX GPIO outside qualified static-output mode");
            return false;
        }
        const uint32_t bit = 1u << (port - 0x80c0);
        known_ |= bit;
        levels_ = (levels_ & ~bit) | ((value & 1) ? bit : 0);
        return true;
    }
    // Output-level observation only. Input, latch and full register readback
    // need their own contract; unknown warm outputs stay unknown.
    uint32_t known() const { return known_; }
    uint32_t levels() const { return levels_; }
private:
    uint32_t pins_, known_ = 0, levels_ = 0;
    FaultFn fault_;
};
} // namespace xlle
