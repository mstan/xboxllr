// Bounded MCPX AV GPIO outputs. Register locations: NVIDIA MCP-1 0.15,
// section 5.15. Board wiring/status truth table: Xbox hardware specification
// 1.02 figures 16/55; retail launch schematic GPIO table (GPIO24 is unused).
// 04/05 low/high encoding: AMD-766 23167B p82, corroborated by original AV
// programming. This does not assume the AMD GPIO register address map.
#pragma once
#include "machine/mcpx_static_outputs.hpp"

namespace xlle {
class McpxAvOutputs {
public:
    enum class ScartStatus : int { Unknown=-1, Undefined=-2, Television=0,
                                  Widescreen=1, Standard=2 };
    explicit McpxAvOutputs(FaultFn fault)
        : pins_((1u<<19)|(1u<<22)|(1u<<24), std::move(fault)) {}
    static bool owns(uint16_t port) {
        return port==0x80d3 || port==0x80d6 || port==0x80d8;
    }
    bool write(uint16_t port, uint32_t value) {
        return pins_.write(port,value);
    }
    // Electrical output observation, not fabricated GPIO register readback.
    // Unknown warm pins remain unknown until the driver programs them.
    uint32_t known() const { return pins_.known(); }
    uint32_t levels() const { return pins_.levels(); }
    ScartStatus scart_status() const {
        constexpr uint32_t level0=1u<<22, level1=1u<<19;
        if((known()&(level0|level1))!=(level0|level1))return ScartStatus::Unknown;
        if(levels()&level1)
            return (levels()&level0) ? ScartStatus::Television : ScartStatus::Undefined;
        return (levels()&level0) ? ScartStatus::Widescreen : ScartStatus::Standard;
    }
private:
    McpxStaticOutputs pins_;
};
} // namespace xlle
