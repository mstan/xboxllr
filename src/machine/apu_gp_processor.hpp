// Explicit GP/EP CPU and mapped-bootstrap ownership. Hardware reset/cadence,
// original graph/output and the audio-codec consumer remain separate contracts.
#pragma once
#include "machine/apu.hpp"
#include "machine/apu_dsp_core.h"
#include "machine/apu_gp_counter.hpp"
#include <utility>

namespace xlle {
enum class ApuGpPhase { Held,Ready,Running,Yielded,Stopped,Failed };
struct ApuGpRun {
    bool returned=false;
    uint32_t pc=0,power=0,sp=0;
    uint64_t sequence=0;
    bool yielded=false;
    uint32_t cycles=0,unused_cycles=0;
};
// A work quantum admits a real descriptor or performs its actual transfer.
// Fair instruction/device interleaving is an explicit nominal floor, not a
// silicon bus-frequency/phase claim. Reads alone never supply a quantum.
struct ApuDmaDispatchPolicy {uint32_t retired_instructions_per_quantum=1;};

// Common lifetime/cache/run owner with a fixed hardware instance. Public
// wrappers select GP or EP explicitly; only GP exposes real VP frame admission.
class ApuDspProcessor {
public:
    // APU banks and callback storage must outlive this sole CPU owner. All
    // owner/MMIO/DMA entry points are serialized by the caller. External P
    // publication uses host MMIO or that DSP's DMA; raw bank pointers are retained
    // by the core/owner, not a second external executable-write path.
    ApuDspProcessor(const ApuDspProcessor&)=delete;
    ApuDspProcessor& operator=(const ApuDspProcessor&)=delete;
    bool reset_cpu();
    // Fixed mapped profile from the pinned reference: 2048 little-endian
    // DWORDs from scratch offset0 become P[0..2047], masked to24 bits. This
    // profile remains distinct from an observed MCPX hardware bootstrap.
    bool bootstrap_from_scratch();
    bool run(uint32_t cycles,ApuGpRun& result);
    ApuGpPhase phase()const{return phase_;}
    uint64_t bootstraps()const{return bootstraps_;}
protected:
    ApuDspProcessor(McpxApu& apu,FaultFn fault,ApuDspKind kind,ApuDmaDispatchPolicy policy);
    ~ApuDspProcessor();
    bool start_gp_frame(ApuVoiceQuantum& result);
private:
    bool fail(const char* why);
    bool permit_program_write();
    bool dispatch_work();
    void invalidate_publication();
    static uint32_t read(void* opaque,uint32_t address);
    static void write(void* opaque,uint32_t address,uint32_t value);
    static void core_fault(void* opaque,const char* why,uint32_t address);
    McpxApu& apu_;
    const ApuDspKind kind_;
    const ApuDmaDispatchPolicy policy_;
    FaultFn fault_;
    XlleDspCore* core_=nullptr;
    ApuGpPhase phase_=ApuGpPhase::Held;
    uint64_t bootstraps_=0,runs_=0,program_revision_=0;
    bool executing_=false,bound_=false,frame_bound_=false,initializing_=false;
    bool working_=false;
    uint32_t retired_work_instructions_=0;
    ApuGpCounterProfile counter_profile_;
    uint32_t retired_before_instruction_=0;
};
class ApuGpProcessor final:public ApuDspProcessor {
public:
    ApuGpProcessor(McpxApu& apu,FaultFn fault,ApuDmaDispatchPolicy policy={}):ApuDspProcessor(apu,std::move(fault),ApuDspKind::Gp,policy){}
    // Caller supplies cadence, not event contents: process the actual VP and
    // resume only after its complete bank publication. A disabled/idle quantum
    // remains yielded and posts nothing. MIX stays leased until actual yield.
    bool start_frame(ApuVoiceQuantum& result){return start_gp_frame(result);}
};
} // namespace xlle
