#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace xlle {
// Independently authored nominal, linear state-variable LPF. Packed FC fields
// follow source references; projecting FC1 to damping and the linear recurrence
// are this model policy, not established MCPX encoding/precision. No silicon
// qualification or xemu waveform equivalence is claimed.
struct ApuDls2Coefficients {
    double frequency=1, damping=1;
    static ApuDls2Coefficients decode(uint32_t packed) {
        const int cutoff=int(packed&65535)-((packed&32768)?65536:0);
        return {std::clamp(std::exp2(double(cutoff)/4096.0),0.003906,1.0),
                std::clamp(double(packed>>16)/32768.0,0.079407,1.0)};
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
        for(size_t i=0;i<frames;++i)for(unsigned channel=0;channel<2;++channel) {
            auto& h=channels[channel];
            const double band=(1-c.frequency*c.damping)*h.band-
                              c.frequency*h.low+c.frequency*double(stereo[2*i+channel]);
            h.low+=c.frequency*band;h.band=band;
            // VP normalized-output saturation is distinct from filter history.
            stereo[2*i+channel]=float(std::clamp(h.low,-1.0,1.0));
        }
        return true;
    }
};
} // namespace xlle
