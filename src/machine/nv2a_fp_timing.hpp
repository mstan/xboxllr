// NV2A digital output timing, given an independently established active mode.
// Register encoding/polarity facts: envytools nv3_pramdac.xml and Nouveau
// v6.12 dispnv04/dfp.c, nvreg.h. Authored clock/event traversal, no donor bodies.
// This does not model or guess how FD6806A0 activates programmed registers.
#pragma once
#include "machine/io_device.hpp"
#include <algorithm>
#include <functional>
#include <limits>

namespace xlle {
class Nv2aFpTiming {
public:
    struct Axis {
        // FP_DISPLAY_END, TOTAL, CRTC, SYNC_START, SYNC_END, VALID_START/END.
        // The CRTC trigger is an internal upstream interface, not qualified by
        // these output signals. Its value must be retained by the caller.
        uint32_t display_end=0,total=0,sync_start=0,sync_end=0,valid_start=0,valid_end=0;
    };
    struct Mode { Axis horizontal,vertical; uint32_t control=0; };
    struct State {
        uint64_t clocks=0,frames=0;
        uint32_t x=0,y=0;
        bool hsync=false,vsync=false,display_enable=false; // electrical levels
        bool hsync_active=false,vsync_active=false,display_active=false;
    };
    using Event=std::function<void(const State&)>;
    explicit Nv2aFpTiming(FaultFn fault) : fault_(std::move(fault)) {}

    // The board must supply an established active bank and counter origin.
    // This is not a guest command, hardware-reset definition or auto-commit.
    bool establish(const Mode& mode,uint32_t x,uint32_t y) {
        // Native and centered output timing only. Scaled pixel generation,
        // 12-bit serialization and source/display clock routing are separate.
        const uint32_t encoding=(mode.control>>8)&3;
        if(!axis_valid(mode.horizontal) || !axis_valid(mode.vertical) ||
           (mode.control&~0x11100311u) || (encoding!=1 && encoding!=2) ||
           x>mode.horizontal.total || y>mode.vertical.total)
            return reject("NV2A FP active mode/origin outside qualified digital timing");
        mode_=mode;state_={};state_.x=x;state_.y=y;known_=true;signals();return true;
    }
    bool state(State& output) const { if(!known_)return false;output=state_;return true; }
    bool advance(uint64_t clocks,const Event& event={}) {
        if(!known_)return reject("NV2A FP timing requires an established active mode/origin");
        if(clocks>std::numeric_limits<uint64_t>::max()-state_.clocks)
            return reject("NV2A FP clock range exceeded");
        const uint32_t width=mode_.horizontal.total+1,height=mode_.vertical.total+1;
        if(!event) {
            // Keep large observation-only advances bounded without overflowing
            // clocks + initial position. Every source edge still contributes.
            const uint64_t frame=uint64_t(width)*height;
            const uint64_t offset=uint64_t(state_.y)*width+state_.x+clocks%frame;
            state_.frames+=clocks/frame+offset/frame;
            const uint64_t position=offset%frame;
            state_.x=uint32_t(position%width);state_.y=uint32_t(position/width);
            state_.clocks+=clocks;signals();return true;
        }
        // Visit output boundaries only. No host frame counter or fixed refresh
        // rate is used; zero pixel-clock edges leave all counters unchanged.
        while(clocks) {
            const auto& h=mode_.horizontal;
            uint32_t distance=width-state_.x;
            for(uint32_t boundary:{h.sync_start+1,h.sync_end+1,h.valid_start,h.valid_end+1})
                if(boundary>state_.x && boundary-state_.x<distance)distance=boundary-state_.x;
            const uint64_t step=std::min(clocks,uint64_t(distance));
            const State before=state_;
            state_.clocks+=step;state_.x+=uint32_t(step);clocks-=step;
            if(state_.x==width) {
                state_.x=0;
                if(++state_.y==height) { state_.y=0;++state_.frames; }
            }
            signals();
            if(before.hsync!=state_.hsync || before.vsync!=state_.vsync ||
               before.display_enable!=state_.display_enable)event(state_);
        }
        return true;
    }
private:
    static bool axis_valid(const Axis& a) {
        // Bounded non-wrapping sync/valid intervals. Total/end/sync values are
        // programmed as pixel/line counts minus one; valid start is unshifted.
        return a.total<4096 && a.display_end<a.total &&
            a.sync_start>=a.display_end && a.sync_start<a.sync_end && a.sync_end<=a.total &&
            a.valid_start<=a.valid_end && a.valid_end<=a.display_end;
    }
    static bool level(bool active,uint32_t polarity) {
        // Only negative/positive polarity is qualified. Disabled-output
        // electrical behavior is unknown and rejected at configuration.
        return polarity==1 ? active : !active;
    }
    void signals() {
        const auto& h=mode_.horizontal;const auto& v=mode_.vertical;
        state_.hsync_active=state_.x>h.sync_start && state_.x<=h.sync_end;
        state_.vsync_active=state_.y>v.sync_start && state_.y<=v.sync_end;
        state_.display_active=state_.x>=h.valid_start && state_.x<=h.valid_end &&
                              state_.y>=v.valid_start && state_.y<=v.valid_end;
        state_.hsync=level(state_.hsync_active,(mode_.control>>4)&3);
        state_.vsync=level(state_.vsync_active,mode_.control&3);
        state_.display_enable=level(state_.display_active,(mode_.control>>28)&3);
    }
    bool reject(const char* why) { if(fault_)fault_(why);return false; }
    FaultFn fault_;
    Mode mode_;
    State state_;
    bool known_=false;
};
} // namespace xlle
