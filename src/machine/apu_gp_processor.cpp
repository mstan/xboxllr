#include "machine/apu_gp_processor.hpp"
#include <array>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <utility>

namespace xlle {
ApuDspProcessor::ApuDspProcessor(McpxApu& apu,FaultFn fault,ApuDspKind kind,ApuDmaDispatchPolicy policy)
    :apu_(apu),kind_(kind),policy_(policy),fault_(std::move(fault)) {
    if(apu_.faulted()){fail("GP owner requires an available APU");return;}
    if(kind_==ApuDspKind::Gp && apu_.gp_counter_profile_touched_)
        {fail("GP counter profile requires its retained CPU owner; warm rebind is unqualified");return;}
    if(!policy_.retired_instructions_per_quantum || policy_.retired_instructions_per_quantum>4096)
        {fail("invalid GP nominal DMA dispatch policy");return;}
    if(apu_.dsp_work_active() || apu_.dsp_dma_pending(kind_))
        {fail("GP CPU owner requires idle DMA ownership");return;}
    core_=kind_==ApuDspKind::Gp?
        xlle_dsp_core_create(apu_.gp_word(0x2800),apu_.gp_word(0),apu_.gp_word(0x1800),apu_.gp_word(0x1400),this,read,write,core_fault):
        xlle_dsp_core_create_ep(apu_.ep_word(0x2800),apu_.ep_word(0),apu_.ep_word(0x1800),this,read,write,core_fault);
    if(!core_){fail("GP CPU allocation failed");return;}
    bound_=kind_==ApuDspKind::Gp?apu_.bind_gp_program_owner(this,[this]{return permit_program_write();}):
                               apu_.bind_ep_program_owner(this,[this]{return permit_program_write();});
    if(!bound_){fail("GP CPU program banks already have an owner");return;}
    if(kind_==ApuDspKind::Gp) {
        frame_bound_=apu_.bind_gp_frame_owner(this,[this]{return executing_ && initializing_ &&
            phase_!=ApuGpPhase::Failed && !apu_.gp_frame_outstanding();});
        if(!frame_bound_){fail("GP frame banks already have an owner");return;}
    }
    program_revision_=kind_==ApuDspKind::Gp?apu_.gp_program_revision():apu_.ep_program_revision();
}
ApuDspProcessor::~ApuDspProcessor() {
    if(frame_bound_)apu_.unbind_gp_frame_owner(this);
    if(bound_) {
        if(kind_==ApuDspKind::Gp)apu_.unbind_gp_program_owner(this);
        else apu_.unbind_ep_program_owner(this);
    }
    xlle_dsp_core_destroy(core_);
}
bool ApuDspProcessor::fail(const char* why) {
    phase_=ApuGpPhase::Failed;
    if(fault_) {
        std::string reason(why);
        if(kind_==ApuDspKind::Ep)for(size_t at=0;(at=reason.find("GP",at))!=std::string::npos;at+=2)reason.replace(at,2,"EP");
        fault_(reason);
    }
    return false;
}
bool ApuDspProcessor::permit_program_write() {
    if(phase_==ApuGpPhase::Failed)return false;
    if(apu_.faulted())return fail("GP program publication on a faulted APU");
    if(executing_)return fail("GP executable DMA/host writes during execution remain unqualified");
    return true;
}
bool ApuDspProcessor::reset_cpu() {
    if(phase_==ApuGpPhase::Failed)return false;
    if(apu_.faulted())return fail("GP CPU reset on a faulted APU");
    if(executing_ || working_ || apu_.dsp_work_active())return fail("reentrant GP CPU reset");
    if(apu_.dsp_dma_pending(kind_))return fail("GP CPU reset with pending DMA remains unqualified");
    if(kind_==ApuDspKind::Gp && apu_.gp_frame_outstanding())return fail("GP CPU reset with an outstanding frame remains unqualified");
    if(counter_profile_.touched())return fail("GP CPU reset after counter configuration remains unqualified");
    xlle_dsp_core_reset(core_);
    initializing_=false;retired_work_instructions_=0;phase_=ApuGpPhase::Held;
    return true;
}
bool ApuDspProcessor::bootstrap_from_scratch() {
    if(phase_==ApuGpPhase::Failed)return false;
    if(phase_!=ApuGpPhase::Held || executing_ || working_ || apu_.dsp_work_active())return fail("GP bootstrap requires a held CPU");
    std::array<uint8_t,8192> raw;
    if(!(kind_==ApuDspKind::Gp?apu_.gp_scratch(0,raw.data(),uint32_t(raw.size()),false):
                              apu_.ep_scratch(0,raw.data(),uint32_t(raw.size()),false)))
        return fail("GP mapped bootstrap read failed");
    // Temporary transfer storage does not become another persistent P bank.
    auto* p=kind_==ApuDspKind::Gp?apu_.gp_word(0x2800):apu_.ep_word(0x2800);
    for(unsigned i=0;i<2048;++i)
        p[i]=uint32_t(raw[i*4])|(uint32_t(raw[i*4+1])<<8)|(uint32_t(raw[i*4+2])<<16);
    xlle_dsp_core_invalidate_program(core_);
    program_revision_=kind_==ApuDspKind::Gp?apu_.gp_program_revision():apu_.ep_program_revision();
    ++bootstraps_;initializing_=true;phase_=ApuGpPhase::Ready;
    return true;
}
void ApuDspProcessor::invalidate_publication() {
    const auto revision=kind_==ApuDspKind::Gp?apu_.gp_program_revision():apu_.ep_program_revision();
    if(program_revision_!=revision) {
        xlle_dsp_core_invalidate_program(core_);
        program_revision_=revision;
    }
}
bool ApuDspProcessor::dispatch_work() {
    if(!apu_.dsp_dma_pending(kind_)){retired_work_instructions_=0;return true;}
    if(++retired_work_instructions_<policy_.retired_instructions_per_quantum)return true;
    retired_work_instructions_=0;working_=true;
    const bool done=apu_.service_dsp_dma(kind_);
    working_=false;
    if(!done || phase_==ApuGpPhase::Failed || apu_.faulted())return fail("GP queued DMA work failed");
    // P can be published here: the real instruction/callback has returned and
    // its registers/PC/stack have spilled. Resume that CPU at its actual PC.
    invalidate_publication();return true;
}
bool ApuDspProcessor::run(uint32_t cycles,ApuGpRun& result) {
    result={};
    if(phase_==ApuGpPhase::Failed)return false;
    if(apu_.faulted())return fail("GP run on a faulted APU");
    if(executing_ || working_ || apu_.dsp_work_active() ||
       (phase_!=ApuGpPhase::Ready && phase_!=ApuGpPhase::Running))
        return fail("GP run requires a bootstrapped running CPU");
    if(!cycles || cycles>INT_MAX)return fail("invalid GP cycle budget");
    invalidate_publication();
    if(!xlle_dsp_core_add_budget(core_,cycles))return fail("GP CPU cycle budget overflow");
    for(;;) {
        if(phase_==ApuGpPhase::Failed || apu_.faulted())return fail("GP CPU/device work failed");
        uint32_t pc,power,sp;xlle_dsp_core_result(core_,&pc,&power,&sp);
        if(xlle_dsp_core_halted(core_) || power!=0 || xlle_dsp_core_budget(core_)<=0)break;
        // Ordinary MOVE may write a peripheral through an effective address.
        // The pinned block escape ABI only covers selected peripheral opcodes;
        // its single-instruction entry supplies the boundary for every form.
        // This correctness owner is absent from the default fast HLE path.
        retired_before_instruction_=xlle_dsp_core_cycles(core_);
        executing_=true;const bool stepped=xlle_dsp_core_step_budget(core_);executing_=false;
        // All FFI access occurs outside the callback. Commit a completed B1
        // instruction before any subsequent read or queued device dispatch.
        if(stepped && phase_!=ApuGpPhase::Failed && !apu_.faulted())
            counter_profile_.retired(xlle_dsp_core_cycles(core_));
        if(!stepped)return fail("GP actual instruction budget accounting failed");
        if(phase_==ApuGpPhase::Failed || apu_.faulted())return fail("GP CPU/device work failed");
        if(!dispatch_work())return false;
    }
    xlle_dsp_core_result(core_,&result.pc,&result.power,&result.sp);
    if(result.power!=0 && result.power!=2)return fail("unqualified GP power state");
    result.cycles=xlle_dsp_core_cycles(core_);
    if(xlle_dsp_core_halted(core_)) {
        if(result.power!=0)return fail("GP vendor yield cannot replace a CPU power transition");
        if(apu_.dsp_dma_pending(kind_))return fail("GP vendor yield with pending DMA remains unqualified");
        result.unused_cycles=xlle_dsp_core_discard_unused_budget(core_);
        if(kind_==ApuDspKind::Gp && !apu_.gp_yield_frame(this))return fail("GP frame yield failed");
        initializing_=false;result.yielded=true;phase_=ApuGpPhase::Yielded;
    } else phase_=result.power==2?ApuGpPhase::Stopped:ApuGpPhase::Running;
    result.returned=true;result.sequence=++runs_;
    return true;
}
bool ApuDspProcessor::start_gp_frame(ApuVoiceQuantum& result) {
    result={};
    if(kind_!=ApuDspKind::Gp)return fail("EP frame admission/cadence remains unqualified");
    if(phase_==ApuGpPhase::Failed)return false;
    if(apu_.faulted())return fail("GP frame start on a faulted APU");
    if(executing_ || working_ || apu_.dsp_work_active() || phase_!=ApuGpPhase::Yielded || !xlle_dsp_core_halted(core_))
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
uint32_t ApuDspProcessor::read(void* opaque,uint32_t address) {
    auto& owner=*static_cast<ApuDspProcessor*>(opaque);
    if(address==0xffffb3 && owner.kind_==ApuDspKind::Gp) {
        if(!owner.executing_ || owner.phase_==ApuGpPhase::Failed) {
            owner.fail("GP counter read requires the serialized CPU owner");std::abort();
        }
        return owner.counter_profile_.observe(owner.retired_before_instruction_);
    }
    uint32_t value=0;
    if(!(owner.kind_==ApuDspKind::Gp?owner.apu_.gp_read(address,value):owner.apu_.ep_read(address,value))) {
        char why[80];std::snprintf(why,sizeof(why),"GP CPU peripheral read failed at %06x",address);
        owner.fail(why);std::abort();
    }
    return value;
}
void ApuDspProcessor::write(void* opaque,uint32_t address,uint32_t value) {
    auto& owner=*static_cast<ApuDspProcessor*>(opaque);
    if(address>=0xffffb0 && address<=0xffffb3 && owner.kind_==ApuDspKind::Gp) {
        if(owner.kind_!=ApuDspKind::Gp || !owner.executing_ ||
           owner.phase_==ApuGpPhase::Failed || !owner.counter_profile_.permits(address,value)) {
            owner.fail("unqualified GP counter configuration profile");std::abort();
        }
        if(!owner.counter_profile_.touched()) {
            if(owner.apu_.gp_program_owner_!=&owner) {
                owner.fail("GP counter configuration lacks program ownership");std::abort();
            }
            owner.apu_.gp_counter_profile_touched_=true;
        }
        owner.counter_profile_.write(address,value);return;
    }
    if(address==0xffffc4) {
        if(value!=1 || !owner.executing_) {
            owner.fail("unqualified GP vendor yield control");std::abort();
        }
        xlle_dsp_core_set_halted(owner.core_,1);return;
    }
    if(!(owner.kind_==ApuDspKind::Gp?owner.apu_.gp_write(address,value):owner.apu_.ep_write(address,value))) {
        char why[80];std::snprintf(why,sizeof(why),"GP CPU peripheral write failed at %06x",address);
        owner.fail(why);std::abort();
    }
}
void ApuDspProcessor::core_fault(void* opaque,const char* why,uint32_t) {
    auto& owner=*static_cast<ApuDspProcessor*>(opaque);
    owner.fail(why);std::abort();
}
} // namespace xlle
