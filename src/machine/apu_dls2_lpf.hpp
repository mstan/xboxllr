#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace xlle {
// Independently authored portable state-variable LPF. The default linear policy
// is retained. XemuStateVariableV1 adds the documented input normalization and
// cubic band stabilization from the pinned software reference, not silicon
// precision. No external implementation body is imported; source/provenance
// and limits are recorded in docs/NATIVE-BOOT-INTRO.md.
struct ApuDls2Coefficients {
    double frequency=1, damping=1;
    bool xemu_state_variable=false;
    static ApuDls2Coefficients decode(uint32_t packed,bool reference=false) {
        const int cutoff=int(packed&65535)-((packed&32768)?65536:0);
        return {std::clamp(std::exp2(double(cutoff)/4096.0),0.003906,1.0),
                std::clamp(double(packed>>16)/32768.0,0.079407,1.0),reference};
    }
};
struct ApuDls2Lpf {
    struct Channel {double low=0,band=0;};
    Channel channels[2]{};
    void reset(){*this={};}
    // Coefficients are a block snapshot; changing them never resets history.
    // Validate the entire owned block before mutating samples or histories.
    bool process(const ApuDls2Coefficients& c,float* stereo,size_t frames) {
        if(frames>size_t(-1)/2 || (frames && !stereo) || !std::isfinite(c.frequency) || !std::isfinite(c.damping) ||
           c.frequency<=0 || c.frequency>1 || c.damping<=0 || c.damping>1)return false;
        for(const auto& h:channels)if(!std::isfinite(h.low) || !std::isfinite(h.band))return false;
        for(size_t i=0;i<frames*2;++i)if(!std::isfinite(stereo[i]))return false;
        const double input_scale=c.xemu_state_variable?std::sqrt(c.damping*.5+.01):1;
        for(size_t i=0;i<frames;++i)for(unsigned channel=0;channel<2;++channel) {
            auto& h=channels[channel];
            double band;
            if(c.xemu_state_variable) {
                const double stabilized=h.band*(1-.001*h.band*h.band);
                const double acceleration=input_scale*double(stereo[2*i+channel])-
                                          h.low-c.damping*stabilized;
                band=stabilized+c.frequency*acceleration;
            } else {
                band=(1-c.frequency*c.damping)*h.band-
                     c.frequency*h.low+c.frequency*double(stereo[2*i+channel]);
            }
            h.low+=c.frequency*band;h.band=band;
            // VP normalized-output saturation is distinct from filter history.
            stereo[2*i+channel]=float(std::clamp(h.low,-1.0,1.0));
        }
        return true;
    }
};
} // namespace xlle
