#include "machine/apu_gp_processor.hpp"
#include <array>
#include <climits>
#include <cstdlib>
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
    program_revision_=apu_.gp_program_revision();
}
ApuGpProcessor::~ApuGpProcessor() {
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
    xlle_dsp_core_reset(core_);
    phase_=ApuGpPhase::Held;
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
    ++bootstraps_;phase_=ApuGpPhase::Ready;
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
    result.returned=true;result.sequence=++runs_;
    phase_=result.power==2?ApuGpPhase::Stopped:ApuGpPhase::Running;
    return true;
}
uint32_t ApuGpProcessor::read(void* opaque,uint32_t address) {
    auto& owner=*static_cast<ApuGpProcessor*>(opaque);
    uint32_t value=0;
    if(!owner.apu_.gp_read(address,value)) {
        owner.fail("GP CPU peripheral read failed");std::abort();
    }
    return value;
}
void ApuGpProcessor::write(void* opaque,uint32_t address,uint32_t value) {
    auto& owner=*static_cast<ApuGpProcessor*>(opaque);
    if(!owner.apu_.gp_write(address,value)) {
        owner.fail("GP CPU peripheral write failed");std::abort();
    }
}
void ApuGpProcessor::core_fault(void* opaque,const char* why,uint32_t) {
    auto& owner=*static_cast<ApuGpProcessor*>(opaque);
    owner.fail(why);std::abort();
}
} // namespace xlle
