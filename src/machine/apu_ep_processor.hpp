// Explicit EP CPU selection over the same maintained DSP/run/DMA ownership.
// EP events use the maintained setup-frame clock and observed C4/C5 handshake.
// CPU reset, exact timer/708000 threshold and connected title output remain open.
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
