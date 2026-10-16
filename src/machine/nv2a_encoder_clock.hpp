// Board coupling for the bounded synchronous Conexant/NV2A output profile.
// One CLKO edge drives one FP count and one encoder input count. The original
// programmed totals must agree; this does not infer a mux from VCLK_DB2's name.
#pragma once
#include "machine/cx25871_control.hpp"
#include "machine/nv2a_fp_registers.hpp"
#include "machine/nv2a_fp_timing.hpp"
#include "machine/rational_source_clock.hpp"

namespace xlle {
class Nv2aEncoderClock {
public:
    using Time=RationalSourceClock::Time;
    using Event=std::function<void(const Nv2aFpTiming::State&,bool)>;
    Nv2aEncoderClock(Cx25871Control& encoder,FaultFn fault,Event event)
        : encoder_(encoder),fault_(std::move(fault)),event_(std::move(event)),
          fp_([this](const std::string& why){reject(why.c_str());}) {}
    bool bind(Time time) {
        if(bound_ || !time.owner || !time.frequency)return reject("display clock invalid initial time owner");
        now_=time;bound_=true;return true;
    }
    // Advance OLD configuration before any timestamped GPU/encoder mutation.
    // Repeated reads at the same time produce no edges, interrupts or FIELD.
    bool advance(Time time) {
        if(failed_)return false;
        if(!bound_ || time.owner!=now_.owner || time.frequency!=now_.frequency || time.ticks<now_.ticks)
            return reject("display clock time owner/frequency/order changed");
        uint64_t edges=0;
        if(source_.state().bound && source_.advance(time,edges)!=RationalSourceClock::Error::None)
            return reject("display source edge accumulation failed");
        now_=time;
        if(!active_) {
            // No established incoming sync: never preserve a stale encoder
            // phase as current merely because no consumer sampled it yet.
            if(edges)encoder_live_=false;
            return true;
        }
        if(!fp_.advance(edges,[this](const Nv2aFpTiming::State& state){
            if(failed_)return;
            const uint64_t edge=fp_origin_+state.clocks;
            if(!advance_encoder(edge))return;
            const bool h=state.hsync&&!last_.hsync,v=state.vsync&&!last_.vsync;
            if(encoder_live_ && (h || v)) {
                if(!encoder_.positive_sync_input()) {reject("display encoder sync polarity is unavailable");return;}
                if(h ? !encoder_.input_sync(v) : !encoder_.input_vsync()) {failed_=true;return;}
            }
            publish(state);
        }))return false;
        if(failed_ || !advance_encoder(source_.state().edges))return false;
        Nv2aFpTiming::State state;fp_.state(state);publish(state);
        return !failed_;
    }
    // Called at the current board timestamp after an actual register write.
    // Slave selection with complete programming supplies a functional mode
    // boundary, with explicit nominal counter origin(0,0). No AV acknowledgment,
    // DEBUG value or encoder reset establishes/realigns that GPU phase.
    bool refresh(const Nv2aFpRegisters::Observation& input,bool slave,uint32_t routing) {
        if(failed_ || !bound_)return false;
        Cx25871Control::Clocks clocks;const char* reason=nullptr;
        const auto state=encoder_.observe_clocks(clocks,reason);
        const bool driven=state==Cx25871Control::ClockState::Driven;
        if(driven) {
            if(!equal(clocks.encoder,clocks.input) || !equal(clocks.input,clocks.clko)) {
                if(slave)return reject("display clock requires equal synchronous input/CLKO rates");
            } else {
                const RationalSourceClock::Rate rate{clocks.clko.numerator,clocks.clko.denominator};
                const auto error=source_.state().bound?source_.change_rate(now_,rate):source_.bind(now_,rate);
                if(error!=RationalSourceClock::Error::None)return reject("display clock rate change failed");
            }
        }
        if(encoder_.timing_epoch()!=encoder_epoch_) {
            encoder_epoch_=encoder_.timing_epoch();encoder_live_=active_ && encoder_.timing_known();
            encoder_edge_=source_.state().edges;
        } else if(!encoder_.timing_known())encoder_live_=false;
        if(!slave || !driven || !input.control_known || !input.debug_known || input.known!=0x3fff) {
            active_=false;encoder_live_=false;return true;
        }
        if(routing!=0x100a0000u)return reject("display clock routing outside measured initialized profile");
        if((input.debug&(1u<<28)) || (input.control&0x1000000u) ||
           (input.control&0x10000011u)!=0x10000011u)
            return reject("display clock output gating/serialization/polarity outside supported profile");
        auto control=input.control&~Nv2aFpRegisters::read_programmed;
        if(active_) {
            if(words_!=input.words || control_!=control)
                return reject("display timing changed while slaved counters are active");
        } else {
            if(ever_active_)
                return reject("display counter phase after routing/clock interruption remains unqualified");
            const auto axis=[&](unsigned at) {
                return Nv2aFpTiming::Axis{input.words[at],input.words[at+1],input.words[at+3],
                    input.words[at+4],input.words[at+5],input.words[at+6]};
            };
            const Nv2aFpTiming::Mode mode{axis(7),axis(0),control};
            if(!fp_.establish(mode,0,0))return false;
            words_=input.words;control_=control;fp_origin_=source_.state().edges;active_=ever_active_=true;
            fp_.state(last_);vertical_blank_=false;
        }
        // Only a real timing-reset epoch establishes a fresh encoder origin.
        // A later sync connection cannot revive a phase that lost its clock.
        publish(last_);return !failed_;
    }
    bool active() const {return active_ && !failed_;}
    bool encoder_live() const {return encoder_live_ && active();}
    bool state(Nv2aFpTiming::State& output) const {return active() && fp_.state(output);}
private:
    static bool equal(Cx25871Control::Rate a,Cx25871Control::Rate b) {
        return a.numerator==b.numerator && a.denominator==b.denominator;
    }
    bool advance_encoder(uint64_t edge) {
        if(encoder_live_) {
            if(edge<encoder_edge_ || !encoder_.advance_timing(edge-encoder_edge_)) {failed_=true;return false;}
        }
        encoder_edge_=edge;return true;
    }
    void publish(const Nv2aFpTiming::State& state) {
        const bool blank=state.vertical_blank;
        const bool leading=blank&&!vertical_blank_;
        last_=state;vertical_blank_=blank;
        if(event_)event_(state,leading);
    }
    bool reject(const char* why) {failed_=true;if(fault_)fault_(why);return false;}
    Cx25871Control& encoder_;
    FaultFn fault_;Event event_;
    Nv2aFpTiming fp_;
    RationalSourceClock source_;
    Time now_{};
    Nv2aFpRegisters::Words words_{};
    Nv2aFpTiming::State last_{};
    uint32_t control_=0;
    uint64_t fp_origin_=0,encoder_epoch_=0,encoder_edge_=0;
    bool bound_=false,active_=false,ever_active_=false,encoder_live_=false,vertical_blank_=false,failed_=false;
};
} // namespace xlle
