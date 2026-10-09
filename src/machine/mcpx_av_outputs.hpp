// Bounded MCPX AV GPIO outputs. Register locations: NVIDIA MCP-1 0.15,
// section 5.15. Board wiring/status truth table: Xbox hardware specification
// 1.02 figures 16/55; retail launch schematic GPIO table (GPIO24 is unused).
// 04/05 low/high encoding: AMD-766 23167B p82, corroborated by original AV
// programming. This does not assume the AMD GPIO register address map.
#pragma once
#include "machine/io_device.hpp"

namespace xlle {
class McpxAvOutputs {
public:
    enum class ScartStatus : int { Unknown=-1, Undefined=-2, Television=0,
                                  Widescreen=1, Standard=2 };
    explicit McpxAvOutputs(FaultFn fault) : fault_(std::move(fault)) {}
    static bool owns(uint16_t port) {
        return port==0x80d3 || port==0x80d6 || port==0x80d8;
    }
    bool write(uint16_t port, uint32_t value) {
        if(!owns(port) || (value!=4 && value!=5)) {
            if(fault_)fault_("MCPX AV GPIO outside qualified static-output mode");
            return false;
        }
        const uint32_t bit=1u<<(port-0x80c0);
        known_ |= bit;
        levels_ = (levels_ & ~bit) | ((value&1) ? bit : 0);
        return true;
    }
    // Electrical output observation, not fabricated GPIO register readback.
    // Unknown warm pins remain unknown until the driver programs them.
    uint32_t known() const { return known_; }
    uint32_t levels() const { return levels_; }
    ScartStatus scart_status() const {
        constexpr uint32_t level0=1u<<22, level1=1u<<19;
        if((known_&(level0|level1))!=(level0|level1))return ScartStatus::Unknown;
        if(levels_&level1)
            return (levels_&level0) ? ScartStatus::Television : ScartStatus::Undefined;
        return (levels_&level0) ? ScartStatus::Widescreen : ScartStatus::Standard;
    }
private:
    FaultFn fault_;
    uint32_t known_=0,levels_=0;
};
} // namespace xlle
