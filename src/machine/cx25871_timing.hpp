// CX25871 synchronous progressive-input/interlaced-output timing floor.
// Authored from Conexant 100381B sections 1.3.4, 1.3.14/15, 1.3.26/27 and 1.3.31.
// Clock events are supplied by the board. Slave sync is never synthesized.
#pragma once
#include "machine/io_device.hpp"
#include <algorithm>
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
        // A nonaligned first slave frame establishes FIELD, not the otherwise
        // unspecified transient analog raster/color phase. Do not consume the
        // corresponding numeric fields until these flags are true.
        bool output_phase_known=false,subcarrier_phase_known=false;
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
        last_hsync_=last_vsync_=frames_received_=raster_origin_=0;
        input_line_=0;hsync_seen_=vsync_seen_=vsync_pending_=false;
        raster_known_=color_known_=true;
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
        if(config_.slave && next>(vsync_seen_?last_vsync_:0)+field_clocks_)
            return reject("CX25871 slave timing requires the next actual VSYNC edge");
        cycles_=next;
        if(pending_ && cycles_>=field_change_) {
            if(field_even_!=pending_even_)++field_transitions_;
            field_even_=pending_even_;pending_=false;
        }
        return true;
    }
    // A progressive VSYNC may precede HSYNC. The next coincident/subsequent
    // HSYNC begins the frame (100381B 1.3.15). There is no implicit sync at reset.
    bool vertical_sync() {
        if(!vertical_valid())return reject("CX25871 slave VSYNC outside qualified progressive timing");
        last_vsync_=cycles_;vsync_seen_=vsync_pending_=true;return true;
    }
    // Leading digital HSYNC edge; vertical says a leading VSYNC coincides.
    // The first edge may occur anywhere in the first line after reset. Later
    // edges must retain the programmed cadence; missing edges are not invented.
    bool sync(bool vertical) {
        if(!known_ || !config_.slave || (hsync_seen_?cycles_!=next_sync_:cycles_>next_sync_))
            return reject("CX25871 slave sync outside qualified clock phase");
        if((vertical && !vertical_valid()) ||
           (!vertical && !vsync_pending_ && cycles_==(vsync_seen_?last_vsync_:0)+field_clocks_) ||
           cycles_>std::numeric_limits<uint64_t>::max()-std::max(config_.input_clocks,2u))
            return reject("CX25871 slave VSYNC does not match programmed progressive timing");
        if(vertical) { last_vsync_=cycles_;vsync_seen_=vsync_pending_=true; }
        last_hsync_=cycles_;next_sync_=cycles_+config_.input_clocks;hsync_seen_=true;
        input_line_=(input_line_+1)%config_.input_lines;
        if(vsync_pending_) {
            vsync_pending_=false;input_line_=0;++frames_received_;
            // 1.3.4/1.3.31: reset selects odd; the first actual combination
            // selects even and the second re-synchronizes to odd. FIELD changes
            // two CLKI cycles after HSYNC, independently of the reset phase.
            pending_even_=(frames_received_&1)!=0;
            field_change_=cycles_+2;pending_=true;
            if(!pending_even_) { raster_origin_=cycles_;raster_known_=true; }
            else if((cycles_-raster_origin_)%(field_clocks_*2)!=field_clocks_)
                raster_known_=false;
            // The specification does not establish the transient analog
            // color-frame counter after a phase correction. Keep its phase
            // unknown until an explicit timing reset; never invent a new
            // color-frame origin. DIS_SCRST preserves a free-running phase.
            if(!config_.disable_subcarrier_reset && !raster_known_)color_known_=false;
        }
        return true;
    }
    bool state(State& output) const {
        if(!known_)return false;
        State value;value.clocks=cycles_;
        if(config_.slave) {
            value.input_x=uint32_t(cycles_-last_hsync_);
            value.input_y=input_line_;
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
        value.output_phase_known=raster_known_;
        value.subcarrier_phase_known=color_known_;
        if(value.output_phase_known) {
            const uint64_t raster=(cycles_-raster_origin_)%(field_clocks_*2);
            value.output_x=uint32_t(raster%config_.output_clocks);
            value.output_y=uint32_t(raster/config_.output_clocks);
        }
        if(value.subcarrier_phase_known) {
            const uint64_t phase_clocks=config_.disable_subcarrier_reset ? cycles_ :
                cycles_%(field_clocks_*(config_.pal?8:4));
            value.subcarrier_phase=uint32_t(uint64_t(uint32_t(phase_clocks))*config_.subcarrier_increment);
        }
        output=value;return true;
    }
private:
    bool vertical_valid() const {
        return known_ && config_.slave && !vsync_pending_ &&
            (!vsync_seen_ || cycles_-last_vsync_==field_clocks_) &&
            cycles_<=std::numeric_limits<uint64_t>::max()-field_clocks_;
    }
    bool reject(const char* reason) { if(fault_)fault_(reason);return false; }
    FaultFn fault_;
    Config config_;
    uint64_t cycles_=0,field_clocks_=0,next_sync_=0,field_change_=0;
    uint64_t field_transitions_=0;
    uint64_t last_hsync_=0,last_vsync_=0,frames_received_=0,raster_origin_=0;
    uint32_t input_line_=0;
    bool hsync_seen_=false,vsync_seen_=false,vsync_pending_=false;
    bool raster_known_=false,color_known_=false;
    bool known_=false,field_even_=false,pending_even_=false,pending_=false;
};
} // namespace xlle
