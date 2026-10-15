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
    if(kind_==ApuDspKind::Gp?apu_.gp_host_requested_:apu_.ep_frame_requested_)
        {fail("GP control request requires its retained CPU owner; warm rebind is unqualified");return;}
    if(kind_==ApuDspKind::Ep && apu_.ep_control_written_)
        {fail("EP control requires its retained CPU owner; warm rebind is unqualified");return;}
    if(kind_==ApuDspKind::Gp?apu_.gp_counter_profile_touched_:apu_.ep_counter_profile_touched_)
        {fail("GP counter profile requires its retained CPU owner; warm rebind is unqualified");return;}
    if(!policy_.retired_instructions_per_quantum || policy_.retired_instructions_per_quantum>4096)
        {fail("invalid GP nominal DMA dispatch policy");return;}
    if(apu_.dsp_work_active() || apu_.dsp_work_pending(kind_))
        {fail("GP CPU owner requires idle DMA ownership");return;}
    core_=kind_==ApuDspKind::Gp?
        xlle_dsp_core_create(apu_.gp_word(0x2800),apu_.gp_word(0),apu_.gp_word(0x1800),apu_.gp_word(0x1400),this,read,write,core_fault):
        xlle_dsp_core_create_ep_lookup(apu_.ep_word(0x2800),apu_.ep_word(0),apu_.ep_word(0x1800),this,read,write,core_fault,read_ep_lookup);
    if(!core_){fail("GP CPU allocation failed");return;}
    auto reset=[this](uint32_t previous,uint32_t next){return reset_control(previous,next);};
    bound_=kind_==ApuDspKind::Gp?apu_.bind_gp_program_owner(this,[this]{return permit_program_write();},reset):
                               apu_.bind_ep_program_owner(this,[this]{return permit_program_write();},[this]{
                                   if(executing_ || working_)return fail("reentrant EP frame publication");
                                   return phase_==ApuGpPhase::Ready || phase_==ApuGpPhase::Running;
                               },reset);
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
bool ApuDspProcessor::reset_control(uint32_t previous,uint32_t next) {
    if(phase_==ApuGpPhase::Failed)return false;
    if(executing_ || working_ || resetting_ || apu_.dsp_work_active())return fail("reentrant GP reset control");
    struct ResetScope {bool& busy;ResetScope(bool& b):busy(b){busy=true;}~ResetScope(){busy=false;}} scope(resetting_);
    if(next!=3)return reset_cpu(next==(kind_==ApuDspKind::Gp?0u:1u));
    // Rewriting released control does not refetch P or restart the CPU.
    if(previous==3) {
        if(phase_==ApuGpPhase::Held)return fail("GP reset control disagrees with held CPU");
        return true;
    }
    return bootstrap_from_scratch();
}
bool ApuDspProcessor::reset_cpu() {
    // A direct fixture reset has no observed hardware control transition.
    return reset_cpu(false);
}
bool ApuDspProcessor::reset_cpu(bool original_hold) {
    if(phase_==ApuGpPhase::Failed)return false;
    if(apu_.faulted())return fail("GP CPU reset on a faulted APU");
    if(executing_ || working_ || apu_.dsp_work_active())return fail("reentrant GP CPU reset");
    if(apu_.dsp_work_pending(kind_))return fail("GP CPU reset with pending peripheral work remains unqualified");
    if(kind_==ApuDspKind::Gp && apu_.gp_frame_outstanding())return fail("GP CPU reset with an outstanding frame remains unqualified");
    if(kind_==ApuDspKind::Ep && ((apu_.ep_frame_status_&2u) ||
       (apu_.ep_control_written_ && !apu_.ep_frame_requested_)) &&
       !(original_hold && apu_.ep_computation_retirement_allowed()))
        return fail("EP CPU reset with an outstanding frame remains unqualified");
    const bool configured=counter_profile_.touched() ||
                          (kind_==ApuDspKind::Ep && apu_.ep_control_written_);
    if(configured && (!original_hold || !counter_profile_.active()))
        return fail(kind_==ApuDspKind::Ep && apu_.ep_control_written_?
                    "EP CPU reset after control publication requires the observed configured hold":
                    "GP CPU reset after counter configuration requires the observed complete profile");
    if(configured && (apu_.reg32(McpxApu::kSectl)&0x18))
        return fail("GP configured reset requires stopped setup admission");
    xlle_dsp_core_reset(core_);
    if(configured) {
        counter_profile_={};
        if(kind_==ApuDspKind::Gp)apu_.gp_counter_profile_touched_=false;
        else {
            apu_.ep_counter_profile_touched_=false;
            // The physical EP counter remains nonzero before reconfiguration.
            // Its unknown warm epoch must not become the GP's known zero.
            apu_.ep_counter_initial_known_=false;
            // Revoke the old CPU's control publication, retaining the raw CPU
            // request latch observed across EP1. A new C4 arms the new program.
            apu_.ep_control_written_=false;apu_.ep_control_value_=0;
        }
    }
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
    // ACK ordering is independent of the selectable DMA transfer quantum.
    // Every real retired instruction supplies one nominal peripheral tick.
    working_=true;const bool acknowledged=apu_.service_dsp_acknowledgements(kind_);working_=false;
    if(!acknowledged)return fail("GP queued acknowledgement work failed");
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
    if(address==0xffffb3) {
        if(!owner.executing_ || owner.phase_==ApuGpPhase::Failed) {
            owner.fail("GP counter read requires the serialized CPU owner");std::abort();
        }
        if(owner.kind_==ApuDspKind::Ep && !owner.apu_.ep_counter_initial_known_ &&
           !owner.counter_profile_.active()) {
            owner.fail("EP warm counter read before reconfiguration remains unqualified");std::abort();
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
    if(address>=0xffffb0 && address<=0xffffb3) {
        if(!owner.executing_ ||
           owner.phase_==ApuGpPhase::Failed || !owner.counter_profile_.permits(address,value)) {
            owner.fail("unqualified GP counter configuration profile");std::abort();
        }
        if(!owner.counter_profile_.touched()) {
            if((owner.kind_==ApuDspKind::Gp?owner.apu_.gp_program_owner_:owner.apu_.ep_program_owner_)!=&owner) {
                owner.fail("GP counter configuration lacks program ownership");std::abort();
            }
            if(owner.kind_==ApuDspKind::Gp)owner.apu_.gp_counter_profile_touched_=true;
            else owner.apu_.ep_counter_profile_touched_=true;
        }
        owner.counter_profile_.write(address,value);return;
    }
    if(address==0xffffc4) {
        if(value!=1 || !owner.executing_) {
            owner.fail("unqualified GP vendor yield control");std::abort();
        }
        if(owner.kind_==ApuDspKind::Ep) {
            // Hardware executes subsequent instructions with C5 still zero.
            // C4=1 is a control publication, not a DSP power/halt operation.
            // Let the original C5 polling instructions wait for a real event.
            if((owner.apu_.ep_frame_status_&2u) && !(owner.apu_.ep_ack_mask_&2u)) {
                owner.fail("EP ready control with an unacknowledged event remains unqualified");std::abort();
            }
            owner.apu_.ep_control_value_=value;owner.apu_.ep_control_written_=true;
            owner.apu_.ep_frame_requested_=true;
            owner.initializing_=false;return;
        }
        xlle_dsp_core_set_halted(owner.core_,1);return;
    }
    if(owner.kind_==ApuDspKind::Ep && address==0xffffc5 && value==2) {
        // Hardware retains102 immediately, then reads100 before another C4,
        // also when no immediate read occurred. Queue the acknowledgement on
        // the existing peripheral-work owner, independent of ready publication.
        uint32_t events=0;
        if(!owner.executing_ || !owner.apu_.ep_control_written_ || owner.initializing_ ||
           owner.phase_==ApuGpPhase::Failed ||
           owner.apu_.dsp_dma_pending(ApuDspKind::Ep) ||
           !owner.apu_.ep_read(address,events) || (events&~0x102u)) {
            owner.fail("EP frame command requires idle control ownership and observed event bits");std::abort();
        }
        if(!owner.apu_.queue_ep_ack(2)) {
            owner.fail("EP frame command failed");std::abort();
        }
        return;
    }
    if(owner.kind_==ApuDspKind::Ep && address==0xffffc5 && value==0xfff) {
        // The original bootstrap clears C5 on cold entry and after its
        // drained reset. Apply the existing W1C contract to retained bit8;
        // neither a pending frame nor DMA EOL may be discarded by this path.
        // The warm epoch must come from the observed configured hold, on the
        // same retained CPU owner, before its new control publication.
        uint32_t events=0;
        const bool warm=!owner.apu_.ep_counter_initial_known_;
        if(!owner.executing_ || !owner.initializing_ ||
           owner.phase_==ApuGpPhase::Failed || (!warm && owner.apu_.ep_dma_nodes()) ||
           owner.apu_.dsp_work_pending(ApuDspKind::Ep) ||
           !owner.apu_.ep_read(address,events) || (events&~(warm?0x100u:0u))) {
            owner.fail("EP startup event clear requires an empty initial context");std::abort();
        }
        if(!owner.apu_.ep_write(address,0x80)) {
            owner.fail("EP startup event acknowledgement failed");std::abort();
        }
        owner.apu_.ep_frame_status_&=~0x100u;
        return;
    }
    if(!(owner.kind_==ApuDspKind::Gp?owner.apu_.gp_write(address,value):owner.apu_.ep_write(address,value))) {
        char why[80];std::snprintf(why,sizeof(why),"GP CPU peripheral write failed at %06x",address);
        owner.fail(why);std::abort();
    }
}
uint32_t ApuDspProcessor::read_ep_lookup(void* opaque,uint32_t address) {
    auto& owner=*static_cast<ApuDspProcessor*>(opaque);uint32_t value=0;
    if(owner.kind_==ApuDspKind::Ep && owner.apu_.ep_y_lookup_read(address,value))return value;
    core_fault(opaque,"unmapped DSP read",address);std::abort();
}
void ApuDspProcessor::core_fault(void* opaque,const char* why,uint32_t address) {
    auto& owner=*static_cast<ApuDspProcessor*>(opaque);
    char message[160];std::snprintf(message,sizeof(message),"%s at %06x",why,address);
    owner.fail(message);std::abort();
}
} // namespace xlle
