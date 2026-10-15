// Elapsed guest time supplies instruction budget to the existing sole CPUs.
// Frame/event production stays with the APU. This class never invents a wake
// or skips an original polling loop; elapsed time, not read count, supplies work.
#pragma once
#include "machine/apu_gp_processor.hpp"
#include "machine/apu_ep_processor.hpp"
#include <algorithm>

namespace xlle {
class ApuDspClock {
public:
    // Effective nominal cycle rate: 711000 cycles per 256/48000s,
    // calibrated to the retained EP polling deltas710998..711002. This is
    // not an identification of the silicon oscillator or all timing modes,
    // and does not by itself qualify the original EP708000 threshold.
    static constexpr uint64_t kCyclesPerSecond=133312500;
    ApuDspClock(McpxApu& apu,ApuGpProcessor& gp,ApuEpProcessor& ep,
                uint64_t ticks,uint64_t frequency,FaultFn fault)
        :apu_(apu),gp_(gp),ep_(ep),ticks_(ticks),frequency_(frequency),fault_(std::move(fault)) {
        if(!frequency || frequency>1000000000u)fail("DSP clock frequency outside supported shared clocks");
    }
    bool advance(uint64_t target) {
        if(failed_)return false;
        if(target<ticks_)return fail("DSP shared clock moved backwards");
        if(apu_.faulted())return fail("DSP clock on a faulted APU");
        // Up to1ms per dispatch bounds arithmetic and supplies both retained
        // CPU owners regularly. The caller must split at actual event/MMIO
        // boundaries; no device state may be published retroactively.
        while(ticks_<target) {
            const uint64_t count=std::min(target-ticks_,std::max(uint64_t(1),frequency_/1000));
            const uint64_t numerator=count*kCyclesPerSecond+fraction_;
            const uint32_t cycles=uint32_t(numerator/frequency_);
            fraction_=numerator%frequency_;
            ticks_+=count;apu_.set_time(ticks_,frequency_);
            if(apu_.faulted())return fail("DSP clock rejected shared time");
            if(!dispatch(gp_,0,cycles) || !dispatch(ep_,1,cycles))return false;
        }
        return true;
    }
    uint64_t ticks()const{return ticks_;}
    uint64_t retired(bool ep)const{return retired_[ep?1:0];}
    const ApuGpRun& last(bool ep)const{return last_[ep?1:0];}
private:
    bool dispatch(ApuDspProcessor& cpu,unsigned slot,uint32_t cycles) {
        if(cpu.phase()==ApuGpPhase::Failed)return fail("DSP clock owns a failed CPU");
        if(cpu.bootstraps()!=bootstraps_[slot]) {
            bootstraps_[slot]=cpu.bootstraps();previous_[slot]=0;
        }
        // Held/yielded/STOP time is discarded, not banked for later resume.
        // The CPU itself retains only genuine negative instruction debt.
        if(!cycles || (cpu.phase()!=ApuGpPhase::Ready && cpu.phase()!=ApuGpPhase::Running))return true;
        ApuGpRun result;
        if(!cpu.run(cycles,result) || !result.returned)return fail("DSP shared-clock instruction execution failed");
        retired_[slot]+=uint32_t(result.cycles-previous_[slot]);previous_[slot]=result.cycles;last_[slot]=result;
        return true;
    }
    bool fail(const char* why){if(!failed_ && fault_)fault_(why);failed_=true;return false;}
    McpxApu& apu_;
    ApuGpProcessor& gp_;
    ApuEpProcessor& ep_;
    uint64_t ticks_=0,frequency_=0,fraction_=0;
    uint64_t retired_[2]={},bootstraps_[2]={};
    uint32_t previous_[2]={};
    ApuGpRun last_[2]={};
    FaultFn fault_;
    bool failed_=false;
};
}
