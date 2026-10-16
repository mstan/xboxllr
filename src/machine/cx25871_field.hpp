// Conexant100381B1.3.4/6/22/31: the encoder output generator keeps running
// from its encoding clock independently of acquisition of input pixel timing.
// Only real resets establish its origin. Live total writes preserve counters;
// wrapping at the next encoding edge is the nominal transient update policy.
// Exact register-latch/sync phase during mode programming is not asserted.
#pragma once
#include <cstdint>
#include <limits>

namespace xlle {
class Cx25871Field {
public:
    bool reset(uint32_t horizontal,uint32_t lines) {
        if(!valid(horizontal,lines))return false;
        *this={};horizontal_=horizontal;lines_=lines;known_=true;return true;
    }
    void invalidate() {known_=false;}
    bool known() const {return known_;}
    bool configure(uint32_t horizontal,uint32_t lines) {
        if(!known_)return true;
        if(!valid(horizontal,lines)) {known_=false;return false;}
        horizontal_=horizontal;lines_=lines;return true;
    }
    bool advance(uint64_t clocks) {
        if(!known_ || clocks>std::numeric_limits<uint64_t>::max()-clocks_)return false;
        if(!clocks)return true;
        clocks_+=clocks;
        if(clocks==1) {
            free_run(1);even_=history_[0];transitions_=history_count_[0];
            history_[0]=history_[1];history_count_[0]=history_count_[1];
        } else {
            free_run(clocks-2);even_=raw_even_;transitions_=raw_transitions_;
            free_run(1);history_[0]=raw_even_;history_count_[0]=raw_transitions_;
            free_run(1);
        }
        history_[1]=raw_even_;history_count_[1]=raw_transitions_;
        return true;
    }
    // The next HSYNC coincident with/after VSYNC starts an input frame.
    // FIELD publication follows that actual HSYNC by two input clocks in the
    // admitted synchronous encoder/input-clock profile; no sync is invented.
    bool hsync(bool vertical) {
        if(!known_)return false;
        if(vertical)vsync_=true;
        if(vsync_) {
            vsync_=false;raw_even_=(++frames_&1)!=0;line_=pixel_=0;
            // This input corrects the RAW generator at the current edge.
            // Both natural and corrected FIELD edges use the same2CLK delay.
            // Replace an unpublished same-edge transition, never create a
            // zero-duration pin pulse merely by processing two owners in order.
            raw_transitions_=history_count_[0]+(raw_even_!=history_[0]);
            history_[1]=raw_even_;history_count_[1]=raw_transitions_;
        }
        return true;
    }
    bool vsync() {if(!known_)return false;vsync_=true;return true;}
    bool even() const {return even_;}
    uint64_t clocks() const {return clocks_;}
    uint64_t transitions() const {return transitions_;}
private:
    static bool valid(uint32_t horizontal,uint32_t lines) {
        return horizontal>=2 && horizontal<=4095 && !(horizontal&1) && (lines==525 || lines==625);
    }
    void free_run(uint64_t clocks) {
        if(!clocks)return;
        const uint32_t half=horizontal_/2;
        // Preserve raw half-line/pixel counters across a live total write.
        // A counter already beyond its new comparator wraps at the next edge.
        while(clocks && (pixel_>=half || line_>=lines_)) {
            const uint64_t count=pixel_>=half?1:((clocks<half-pixel_)?clocks:half-pixel_);
            clocks-=count;pixel_+=unsigned(count);
            if(pixel_>=half) {pixel_=0;if(++line_>=lines_) {line_=0;raw_even_=!raw_even_;++raw_transitions_;}}
        }
        if(pixel_>=half || line_>=lines_)return;
        const uint64_t period=uint64_t(half)*lines_;
        const uint64_t position=uint64_t(line_)*half+pixel_;
        const uint64_t remainder=position+clocks%period;
        const uint64_t fields=clocks/period+remainder/period;
        raw_transitions_+=fields;if(fields&1)raw_even_=!raw_even_;
        const uint64_t next=remainder%period;
        line_=uint32_t(next/half);pixel_=uint32_t(next%half);
    }
    uint64_t clocks_=0,frames_=0,transitions_=0,raw_transitions_=0,history_count_[2]{};
    uint32_t horizontal_=0,lines_=0,line_=0,pixel_=0;
    bool known_=false,even_=false,raw_even_=false,vsync_=false,history_[2]{};
};
} // namespace xlle
