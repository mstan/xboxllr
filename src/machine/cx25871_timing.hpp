// CX25871 synchronous progressive-input/interlaced-output timing floor.
// Authored from Conexant 100381B sections 1.3.4, 1.3.26/27 and 1.3.31.
// Clock events are supplied by the board. Slave sync is never synthesized.
#pragma once
#include "machine/io_device.hpp"
#include <limits>

namespace xlle {
class Cx25871Timing {
public:
    struct Config {
        uint32_t input_clocks=0,input_lines=0,output_clocks=0,output_lines=0;
        uint32_t subcarrier_increment=0;
        bool pal=false,disable_subcarrier_reset=false,slave=false;
    };
    struct State {
        uint64_t clocks=0;
        uint64_t field_transitions=0;
        uint32_t input_x=0,input_y=0,output_x=0,output_y=0,subcarrier_phase=0;
        bool field_even=false;
    };
    explicit Cx25871Timing(FaultFn fault) : fault_(std::move(fault)) {}
    bool reset(const Config& config) {
        const uint64_t input=uint64_t(config.input_clocks)*config.input_lines;
        const uint64_t output=uint64_t(config.output_clocks)*config.output_lines;
        if(!config.input_clocks || config.input_clocks>2047 ||
           !config.input_lines || config.input_lines>2047 ||
           !config.output_clocks || config.output_clocks>4095 ||
           (config.output_clocks&1) ||
           (config.output_lines!=525 && config.output_lines!=625) || input*2!=output)
            return reject("CX25871 timing requires coherent synchronous progressive input");
        config_=config;field_clocks_=input;cycles_=0;
        next_sync_=config.input_clocks;field_change_=0;
        field_even_=pending_even_=false;pending_=false;field_transitions_=0;known_=true;return true;
    }
    void invalidate() { known_=false; }
    bool known() const { return known_; }
    bool advance(uint64_t clocks) {
        if(!known_)return reject("CX25871 timing has no established reset origin");
        if(clocks>std::numeric_limits<uint64_t>::max()-cycles_)
            return reject("CX25871 timing clock range exceeded");
        const uint64_t next=cycles_+clocks;
        if(config_.slave && next>next_sync_)
            return reject("CX25871 slave timing requires the next actual HSYNC edge");
        cycles_=next;
        if(pending_ && cycles_>=field_change_) {
            if(field_even_!=pending_even_)++field_transitions_;
            field_even_=pending_even_;pending_=false;
        }
        return true;
    }
    // Leading digital HSYNC edge; vertical says a leading VSYNC coincides.
    // This bounded profile supports aligned progressive sync. Other phases
    // stop before mutating state and need independent qualification.
    bool sync(bool vertical) {
        if(!known_ || !config_.slave || cycles_!=next_sync_)
            return reject("CX25871 slave sync outside qualified clock phase");
        const bool boundary=(cycles_%field_clocks_)==0;
        if(vertical!=boundary || next_sync_>std::numeric_limits<uint64_t>::max()-config_.input_clocks)
            return reject("CX25871 slave VSYNC does not match programmed progressive timing");
        next_sync_+=config_.input_clocks;
        if(vertical) {
            // First progressive frame after reset corresponds to even;
            // second re-synchronizes odd. The qualified phase is already at
            // that encoder origin, so no unobserved pixel work is discarded.
            pending_even_=((cycles_/field_clocks_)&1)!=0;
            field_change_=cycles_+2;pending_=true;
        }
        return true;
    }
    bool state(State& output) const {
        if(!known_)return false;
        State value;value.clocks=cycles_;
        if(config_.slave) {
            const uint64_t line_start=next_sync_-config_.input_clocks;
            value.input_x=uint32_t(cycles_-line_start);
            value.input_y=uint32_t((line_start/config_.input_clocks)%config_.input_lines);
            value.field_even=field_even_;
            value.field_transitions=field_transitions_;
        } else {
            value.input_x=uint32_t(cycles_%config_.input_clocks);
            value.input_y=uint32_t((cycles_/config_.input_clocks)%config_.input_lines);
            value.field_even=(((cycles_>=2?cycles_-2:0)/field_clocks_)&1)!=0;
            value.field_transitions=(cycles_>=2?cycles_-2:0)/field_clocks_;
        }
        // Full-frame raster coordinates retain the half-line phase of the
        // even field instead of rounding 262.5/312.5 lines to an integer.
        const uint64_t raster=cycles_%(field_clocks_*2);
        value.output_x=uint32_t(raster%config_.output_clocks);
        value.output_y=uint32_t(raster/config_.output_clocks);
        const uint64_t phase_clocks=config_.disable_subcarrier_reset ? cycles_ :
            cycles_%(field_clocks_*(config_.pal?8:4));
        value.subcarrier_phase=uint32_t(uint64_t(uint32_t(phase_clocks))*config_.subcarrier_increment);
        output=value;return true;
    }
private:
    bool reject(const char* reason) { if(fault_)fault_(reason);return false; }
    FaultFn fault_;
    Config config_;
    uint64_t cycles_=0,field_clocks_=0,next_sync_=0,field_change_=0;
    uint64_t field_transitions_=0;
    bool known_=false,field_even_=false,pending_even_=false,pending_=false;
};
} // namespace xlle
