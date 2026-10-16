// FD6806A0 request/ack transport, independently of FP bank activation.
// Xbox observations: idle 00/11, pending 01/10, completion in both directions.
// The reached original AV3 consumer is invariant under exchanging the phases.
#pragma once
#include <cstdint>

namespace xlle {
class Nv2aAvHandshake {
public:
    // Explicit initialized-board input, not a silicon power-on/reset default.
    // A fresh quiescent owner may choose either equivalent phase. Never erase
    // an existing request, or reinitialize the owner after guest activity.
    bool establish_idle(uint32_t phase) {
        if(known_ || phase>1)return reject("NV2A AV idle profile requires a fresh owner and phase 0/1");
        request_=ack_=phase;known_=true;return true;
    }
    bool known() const {return known_;}
    uint32_t read() const {return request_ | (ack_<<4);}
    bool write(uint32_t request) {
        if(!known_ || request>1 || request_!=ack_ || request==ack_)
            return reject("NV2A AV request outside idle opposite-phase protocol");
        request_=request;return true;
    }
    void advance(uint64_t core_edges) {
        // Declared nominal service: the next actual NV2A core edge. The exact
        // hardware edge/latency is unresolved; the observed upper bracket is
        // 7.111us. Polls and frozen clocks never manufacture completion. This
        // transaction does not copy timing banks or create a retrace event.
        if(known_ && core_edges)ack_=request_;
    }
    const char* error() const {return error_;}
private:
    bool reject(const char* why){error_=why;return false;}
    uint32_t request_=0,ack_=0;
    bool known_=false;
    const char* error_=nullptr;
};
} // namespace xlle
