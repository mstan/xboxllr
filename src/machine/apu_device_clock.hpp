// Shared guest-time ordering for the existing APU, sole GP/EP CPUs and codec.
// This nominal schedule needs original active-route validation before native
// integration; it does not establish silicon edge phase or overrun behavior.
#pragma once
#include "machine/apu_dsp_clock.hpp"
#include "machine/aci.hpp"

namespace xlle {
class ApuDeviceClock {
public:
    ApuDeviceClock(McpxApu& apu,ApuGpProcessor& gp,ApuEpProcessor& ep,McpxAci& aci,
                   uint64_t ticks,uint64_t frequency,FaultFn fault)
        :apu_(apu),gp_(gp),ep_(ep),aci_(aci),fault_(std::move(fault)),
         dsp_(apu,gp,ep,ticks,frequency,fault_),frequency_(frequency) {
        if(frequency<48000 || frequency>1000000000u) {
            fail("audio device clock frequency outside supported shared clocks");return;
        }
        quantum_=quantum_at(ticks);
        setup_frame_=quantum_/8;
        if(!apu_.prime_frame_clock(quantum_/8) || !aci_.prime_frame_clock(quantum_/8)) {
            fail("audio device clock could not prime fresh device owners");return;
        }
        apu_.set_time(ticks,frequency_);aci_.set_time(ticks,frequency_);
        check_devices();
    }
    ApuDeviceClock(const ApuDeviceClock&)=delete;
    ApuDeviceClock& operator=(const ApuDeviceClock&)=delete;

    bool advance(uint64_t target) {
        if(failed_)return false;
        if(advancing_)return fail("reentrant audio device clock");
        if(target<ticks())return fail("audio device clock moved backwards");
        if(!check_devices())return false;
        // Original programs restart their scratch-ring cursors at bootstrap.
        // A new producer/consumer generation cannot inherit a partial group
        // from its predecessor. Ordinary SE pauses retain the current group.
        if(gp_generation_!=gp_.bootstraps() || ep_generation_!=ep_.bootstraps()) {
            if(generation_active_)
                restarted_|=(gp_generation_!=gp_.bootstraps()?1u:0u)|
                            (ep_generation_!=ep_.bootstraps()?2u:0u);
            gp_generation_=gp_.bootstraps();ep_generation_=ep_.bootstraps();
            setup_quanta_=0;
        }
        struct Scope {bool& active;explicit Scope(bool& value):active(value){active=true;}~Scope(){active=false;}} scope(advancing_);
        // A fixed one-guest-tick dispatch makes GP/EP shared-RAM ordering
        // independent of the caller's catch-up partitions. The DSP clock's
        // rational budget carries fractions and each CPU's instruction debt.
        // At the native1125000Hz clock this is a nominal GP-then-EP sub-us
        // interleave, not an assertion of silicon instruction/bus simultaneity.
        while(ticks()<target) {
            if(!dsp_.advance(ticks()+1))return fail("audio device DSP execution failed");
            // Prior synchronous FE/VP publications precede this queued RAM
            // token. Service comes from advancing the sole device clock, not
            // from CPU/MMIO polls or a fabricated DSP/voice completion.
            if(!apu_.service_fe_fence())return fail("audio device FE fence publication failed");
            aci_.set_time(ticks(),frequency_);
            if(!check_devices())return false;
            const uint64_t next=quantum_at(ticks());
            if(next==quantum_)continue;
            quantum_=next;
            // Work completed over the preceding interval is visible first.
            // These events admit subsequent DSP work; no immediate DSP finish
            // is fabricated to feed the codec at the same boundary.
            if(apu_.setup_processing_enabled()) {
                // Restarting one live ring cursor does not realign the other.
                // Normal original retirement reboots both; a never-created GP
                // remains valid for the independently observed EP-only route.
                const unsigned participants=(gp_generation_?1u:0u)|(ep_generation_?2u:0u);
                if(restarted_ && restarted_!=participants)
                    return fail("unpaired GP/EP ring restart remains unqualified");
                restarted_=0;generation_active_=true;
                if(gp_.phase()!=ApuGpPhase::Yielded && gp_.phase()!=ApuGpPhase::Held)
                    return fail("GP unfinished or stopped at active frame boundary; overrun remains unqualified");
                // The original GP writes32 samples at a time into each512-
                // sample ring; EP starts with its first256-sample half. Admit
                // EP only after eight completed slots, while GP can fill the
                // opposite half. An absolute EP phase independent of startup
                // read one new chunk plus seven stale chunks on every event.
                // GP-held SE slots still supply the observed EP-only cadence.
                // Exact silicon sub-frame phase is not established by this
                // nominal, producer-consistent schedule.
                if(setup_quanta_==8) {
                    const uint64_t events=apu_.ep_frame_events();
                    apu_.set_frame(++setup_frame_);
                    if(!check_devices())return false;
                    // Idle-voice delivery can trap FE before EP publication.
                    // Retain the completed half across that pause, rather than
                    // admitting another eight chunks over unread contents.
                    if(apu_.setup_processing_enabled()) {
                        if(ep_.phase()!=ApuGpPhase::Held && apu_.ep_frame_events()==events)
                            return fail("EP missed completed GP group; overrun remains unqualified");
                        setup_quanta_=0;
                    }
                }
                if(gp_.phase()==ApuGpPhase::Yielded) {
                    ApuVoiceQuantum result;
                    if(!gp_.start_frame(result))return fail("audio device GP frame admission failed");
                }
                if(apu_.setup_processing_enabled())++setup_quanta_;
            }
            if(!check_devices())return false;
            if(!(quantum_&7)) {
                // PCM callbacks observe ticks() at this exact serviced event,
                // never the final target time of a multi-frame catch-up call.
                aci_.set_frame(quantum_/8);
                if(!check_devices())return false;
            }
        }
        return true;
    }
    uint64_t ticks()const{return dsp_.ticks();}
    uint64_t retired(bool ep)const{return dsp_.retired(ep);}
    const ApuGpRun& last(bool ep)const{return dsp_.last(ep);}
    bool failed()const{return failed_;}
private:
    uint64_t quantum_at(uint64_t tick)const {
        // 48000/32=1500. With the admitted frequency range both products fit
        // uint64_t even at UINT64_MAX; no target-specific wide integer needed.
        return (tick/frequency_)*1500+((tick%frequency_)*1500)/frequency_;
    }
    bool check_devices() {
        if(failed_)return false;
        return (!apu_.faulted() && !aci_.faulted() && gp_.phase()!=ApuGpPhase::Failed &&
                ep_.phase()!=ApuGpPhase::Failed) || fail("audio device clock owns a faulted device");
    }
    bool fail(const char* why) {
        if(!failed_) {failed_=true;if(fault_)fault_(why);}
        return false;
    }
    McpxApu& apu_;
    ApuGpProcessor& gp_;
    ApuEpProcessor& ep_;
    McpxAci& aci_;
    FaultFn fault_;
    ApuDspClock dsp_;
    uint64_t frequency_=0,quantum_=0;
    uint64_t setup_frame_=0,gp_generation_=0,ep_generation_=0;
    unsigned setup_quanta_=0,restarted_=0;
    bool generation_active_=false;
    bool failed_=false,advancing_=false;
};
} // namespace xlle
