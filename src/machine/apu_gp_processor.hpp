// Explicit GP CPU and mapped-bootstrap owner. No GPRST MMIO, silicon cadence,
// retail direct-speaker framework, FIFO completion or EP output is supplied.
#pragma once
#include "machine/apu.hpp"
#include "machine/apu_dsp_core.h"

namespace xlle {
enum class ApuGpPhase { Held,Ready,Running,Yielded,Stopped,Failed };
struct ApuGpRun {
    bool returned=false;
    uint32_t pc=0,power=0,sp=0;
    uint64_t sequence=0;
    bool yielded=false;
    uint32_t cycles=0,unused_cycles=0;
};

class ApuGpProcessor {
public:
    // APU banks and callback storage must outlive this sole CPU owner. All
    // owner/MMIO/DMA entry points are serialized by the caller. External P
    // publication uses host MMIO or GP DMA; raw bank pointers are retained
    // by the core/owner, not a second external executable-write path.
    ApuGpProcessor(McpxApu& apu,FaultFn fault);
    ~ApuGpProcessor();
    ApuGpProcessor(const ApuGpProcessor&)=delete;
    ApuGpProcessor& operator=(const ApuGpProcessor&)=delete;
    bool reset_cpu();
    // Fixed mapped profile from the pinned reference: 2048 little-endian
    // DWORDs from scratch offset0 become P[0..2047], masked to24 bits. This
    // profile remains distinct from an observed MCPX hardware bootstrap.
    bool bootstrap_from_scratch();
    bool run(uint32_t cycles,ApuGpRun& result);
    // Caller supplies cadence, not event contents: process the actual VP and
    // resume only after its complete bank publication. A disabled/idle quantum
    // remains yielded and posts nothing. MIX stays leased until actual yield.
    bool start_frame(ApuVoiceQuantum& result);
    ApuGpPhase phase()const{return phase_;}
    uint64_t bootstraps()const{return bootstraps_;}
private:
    bool fail(const char* why);
    bool permit_program_write();
    static uint32_t read(void* opaque,uint32_t address);
    static void write(void* opaque,uint32_t address,uint32_t value);
    static void core_fault(void* opaque,const char* why,uint32_t address);
    McpxApu& apu_;
    FaultFn fault_;
    XlleDspCore* core_=nullptr;
    ApuGpPhase phase_=ApuGpPhase::Held;
    uint64_t bootstraps_=0,runs_=0,program_revision_=0;
    bool executing_=false,bound_=false,frame_bound_=false,initializing_=false;
};
} // namespace xlle
