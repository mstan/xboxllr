// Explicit EP CPU selection over the same maintained DSP/run/DMA ownership.
// No EP frame admission/cadence, timer, bit9 or AC97 behavior is supplied.
#pragma once
#include "machine/apu_gp_processor.hpp"
namespace xlle {
using ApuEpPhase=ApuGpPhase;
using ApuEpRun=ApuGpRun;
class ApuEpProcessor final:public ApuDspProcessor {
public:
    ApuEpProcessor(McpxApu& apu,FaultFn fault,ApuDmaDispatchPolicy policy={}):ApuDspProcessor(apu,std::move(fault),ApuDspKind::Ep,policy){}
};
} // namespace xlle
