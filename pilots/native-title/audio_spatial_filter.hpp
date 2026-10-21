#pragma once
#include <cmath>
#include <cstring>
#include "audio_backend.h"

/* Native presentation filter. SDK arithmetic supplies the selected HRIR data
 * and sends; the portable FIR/ITD path follows xemu's documented model. Exact
 * MCPX coefficient quantization/smoothing is a recorded hardware deferral.
 * No original table data or guest pointers are stored in this consumer. */
struct PilotSpatialFilter {
    float history[128]{},coefficients[2][31]{},delay=0,direct=0,room=0;
    unsigned cursor=0;
    bool initialized=false;
    void reset() {
        memset(history,0,sizeof(history));direct=room=0;cursor=0;
        // Parameter changes do not reset histories; Reset is a transport event.
    }
    void sample(float input,const PilotAudioSpatialState &p,float *out) {
        if(!initialized) {
            memcpy(coefficients,p.taps,sizeof(coefficients));delay=p.delay;initialized=true;
        } else {
            for(unsigned ear=0;ear<2;++ear)for(unsigned k=0;k<31;++k)
                coefficients[ear][k]+=.01f*(p.taps[ear][k]-coefficients[ear][k]);
            delay+=.01f*(p.delay-delay);
        }
        direct=(1-p.direct_pole)*input+p.direct_pole*direct;
        room=(1-p.room_pole)*input+p.room_pole*room;
        history[cursor]=direct;
        for(unsigned ear=0;ear<2;++ear) {
            float lag=std::fmax(0.0f,ear?-delay:delay);
            unsigned integer=(unsigned)lag;float fraction=lag-integer,signal=0;
            for(unsigned k=0;k<31;++k) {
                unsigned at=(cursor-integer-k)&127;
                float a=history[at],b=history[(at-1)&127];
                signal+=coefficients[ear][k]*(a+fraction*(b-a));
            }
            out[ear]=out[ear+2]=signal;
        }
        out[4]=room;out[5]=direct;cursor=(cursor+1)&127;
    }
};

static inline bool pilot_spatial_state_valid(const PilotAudioSpatialState &p) {
    if(!std::isfinite(p.delay) || std::fabs(p.delay)>42 ||
       !std::isfinite(p.direct_pole) || p.direct_pole<0 || p.direct_pole>=1 ||
       !std::isfinite(p.room_pole) || p.room_pole<0 || p.room_pole>=1 ||
       !std::isfinite(p.frequency_ratio) || p.frequency_ratio<1.0f/1024 || p.frequency_ratio>200000.0f/48000)return false;
    for(auto &ear:p.taps)for(float value:ear)
        if(!std::isfinite(value) || std::fabs(value)>1)return false;
    for(float value:p.matrix)if(!std::isfinite(value) || std::fabs(value)>16)return false;
    return true;
}
