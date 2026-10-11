#include "machine/apu_gp_processor.hpp"
#include <array>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <utility>

namespace xlle {
ApuGpProcessor::ApuGpProcessor(McpxApu& apu,FaultFn fault)
    :apu_(apu),fault_(std::move(fault)) {
    if(apu_.faulted()){fail("GP owner requires an available APU");return;}
    core_=xlle_dsp_core_create(apu_.gp_word(0x2800),apu_.gp_word(0),
        apu_.gp_word(0x1800),apu_.gp_word(0x1400),this,read,write,core_fault);
    if(!core_){fail("GP CPU allocation failed");return;}
    bound_=apu_.bind_gp_program_owner(this,[this]{return permit_program_write();});
    if(!bound_){fail("GP CPU program banks already have an owner");return;}
    frame_bound_=apu_.bind_gp_frame_owner(this,[this]{return executing_ && initializing_ &&
        phase_!=ApuGpPhase::Failed && !apu_.gp_frame_outstanding();});
    if(!frame_bound_){fail("GP frame banks already have an owner");return;}
    program_revision_=apu_.gp_program_revision();
}
ApuGpProcessor::~ApuGpProcessor() {
    if(frame_bound_)apu_.unbind_gp_frame_owner(this);
    if(bound_)apu_.unbind_gp_program_owner(this);
    xlle_dsp_core_destroy(core_);
}
bool ApuGpProcessor::fail(const char* why) {
    phase_=ApuGpPhase::Failed;
    if(fault_)fault_(why);
    return false;
}
bool ApuGpProcessor::permit_program_write() {
    if(phase_==ApuGpPhase::Failed)return false;
    if(apu_.faulted())return fail("GP program publication on a faulted APU");
    if(executing_)return fail("GP executable DMA/host writes during execution remain unqualified");
    return true;
}
bool ApuGpProcessor::reset_cpu() {
    if(phase_==ApuGpPhase::Failed)return false;
    if(apu_.faulted())return fail("GP CPU reset on a faulted APU");
    if(executing_)return fail("reentrant GP CPU reset");
    if(apu_.gp_frame_outstanding())return fail("GP CPU reset with an outstanding frame remains unqualified");
    xlle_dsp_core_reset(core_);
    initializing_=false;phase_=ApuGpPhase::Held;
    return true;
}
bool ApuGpProcessor::bootstrap_from_scratch() {
    if(phase_==ApuGpPhase::Failed)return false;
    if(phase_!=ApuGpPhase::Held || executing_)return fail("GP bootstrap requires a held CPU");
    std::array<uint8_t,8192> raw;
    if(!apu_.gp_scratch(0,raw.data(),uint32_t(raw.size()),false))
        return fail("GP mapped bootstrap read failed");
    // Temporary transfer storage does not become another persistent P bank.
    auto* p=apu_.gp_word(0x2800);
    for(unsigned i=0;i<2048;++i)
        p[i]=uint32_t(raw[i*4])|(uint32_t(raw[i*4+1])<<8)|(uint32_t(raw[i*4+2])<<16);
    xlle_dsp_core_invalidate_program(core_);
    program_revision_=apu_.gp_program_revision();
    ++bootstraps_;initializing_=true;phase_=ApuGpPhase::Ready;
    return true;
}
bool ApuGpProcessor::run(uint32_t cycles,ApuGpRun& result) {
    result={};
    if(phase_==ApuGpPhase::Failed)return false;
    if(apu_.faulted())return fail("GP run on a faulted APU");
    if(executing_ || (phase_!=ApuGpPhase::Ready && phase_!=ApuGpPhase::Running))
        return fail("GP run requires a bootstrapped running CPU");
    if(!cycles || cycles>INT_MAX)return fail("invalid GP cycle budget");
    if(program_revision_!=apu_.gp_program_revision()) {
        xlle_dsp_core_invalidate_program(core_);
        program_revision_=apu_.gp_program_revision();
    }
    executing_=true;
    xlle_dsp_core_run(core_,cycles);
    executing_=false;
    xlle_dsp_core_result(core_,&result.pc,&result.power,&result.sp);
    if(result.power!=0 && result.power!=2)return fail("unqualified GP power state");
    result.cycles=xlle_dsp_core_cycles(core_);
    if(xlle_dsp_core_halted(core_)) {
        if(result.power!=0)return fail("GP vendor yield cannot replace a CPU power transition");
        result.unused_cycles=xlle_dsp_core_discard_unused_budget(core_);
        if(!apu_.gp_yield_frame(this))return fail("GP frame yield failed");
        initializing_=false;result.yielded=true;phase_=ApuGpPhase::Yielded;
    } else phase_=result.power==2?ApuGpPhase::Stopped:ApuGpPhase::Running;
    result.returned=true;result.sequence=++runs_;
    return true;
}
bool ApuGpProcessor::start_frame(ApuVoiceQuantum& result) {
    result={};
    if(phase_==ApuGpPhase::Failed)return false;
    if(apu_.faulted())return fail("GP frame start on a faulted APU");
    if(executing_ || phase_!=ApuGpPhase::Yielded || !xlle_dsp_core_halted(core_))
        return fail("GP frame start requires an actually yielded CPU");
    executing_=true;
    const bool published=apu_.gp_process_frame(this,result);
    executing_=false;
    if(!published || phase_==ApuGpPhase::Failed || apu_.faulted())
        return fail("GP real VP frame publication failed");
    if(!result.published)return true;
    if(!apu_.gp_post_frame(this))return fail("GP frame event publication failed");
    xlle_dsp_core_set_halted(core_,0);phase_=ApuGpPhase::Running;return true;
}
uint32_t ApuGpProcessor::read(void* opaque,uint32_t address) {
    auto& owner=*static_cast<ApuGpProcessor*>(opaque);
    uint32_t value=0;
    if(!owner.apu_.gp_read(address,value)) {
        char why[80];std::snprintf(why,sizeof(why),"GP CPU peripheral read failed at %06x",address);
        owner.fail(why);std::abort();
    }
    return value;
}
void ApuGpProcessor::write(void* opaque,uint32_t address,uint32_t value) {
    auto& owner=*static_cast<ApuGpProcessor*>(opaque);
    if(address==0xffffc4) {
        if(value!=1 || !owner.executing_) {
            owner.fail("unqualified GP vendor yield control");std::abort();
        }
        xlle_dsp_core_set_halted(owner.core_,1);return;
    }
    if(!owner.apu_.gp_write(address,value)) {
        char why[80];std::snprintf(why,sizeof(why),"GP CPU peripheral write failed at %06x",address);
        owner.fail(why);std::abort();
    }
}
void ApuGpProcessor::core_fault(void* opaque,const char* why,uint32_t) {
    auto& owner=*static_cast<ApuGpProcessor*>(opaque);
    owner.fail(why);std::abort();
}
} // namespace xlle
