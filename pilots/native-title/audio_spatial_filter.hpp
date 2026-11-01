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
#if defined(PILOT_AUDIO_DORMANT_OWNER) && PILOT_AUDIO_DORMANT_OWNER
    void parameters(const PilotAudioSpatialState &p) {
        if(!initialized) {
            memcpy(coefficients,p.taps,sizeof(coefficients));delay=p.delay;initialized=true;
        } else {
            for(unsigned ear=0;ear<2;++ear)for(unsigned k=0;k<31;++k)
                coefficients[ear][k]+=.01f*(p.taps[ear][k]-coefficients[ear][k]);
            delay+=.01f*(p.delay-delay);
        }
    }
    /* Only for a reset signal history and nearest rounding. Keep every
     * smoothing step: freezing it would change the first audible sample. */
    bool silent_pristine_supported(const PilotAudioSpatialState &p) const {
        auto ordinary=[](float value) {
            uint32_t bits;memcpy(&bits,&value,sizeof(bits));
            return (bits&0x7f800000u)!=0 || (bits&0x7fffffffu)==0;
        };
        if(!ordinary(delay) || !ordinary(p.delay))return false;
        for(unsigned ear=0;ear<2;++ear)for(unsigned k=0;k<31;++k)
            if(!ordinary(coefficients[ear][k]) || !ordinary(p.taps[ear][k]))return false;
        return true;
    }
    void silent_pristine(unsigned frames,const PilotAudioSpatialState &p) {
        for(unsigned frame=0;frame<frames;++frame) {
            parameters(p);
            direct=(1-p.direct_pole)*0.0f+p.direct_pole*direct;
            room=(1-p.room_pole)*0.0f+p.room_pole*room;
            history[cursor]=direct;
            /* Truncating a fractional lag raises the original inexact flag.
             * Retain those conversions even though the FIR signal is zero. */
            for(unsigned ear=0;ear<2;++ear) {
                float lag=std::fmax(0.0f,ear?-delay:delay);
                volatile unsigned integer=(unsigned)lag;
                volatile float fraction=lag-integer;(void)fraction;
            }
            /* Cancellation can create a subnormal during this block. The
             * original zero-valued FIR multiplication still reads it and
             * raises denormal status. Retain an actual zero multiplication,
             * with exceptions masked by the caller's admission predicate. */
            uint32_t denormal=0;
            for(auto &ear:coefficients)for(float value:ear) {
                uint32_t bits;memcpy(&bits,&value,sizeof(bits));
                if(!(bits&0x7f800000u) && (bits&0x7fffffu))denormal=bits;
            }
            if(denormal) {
                float value;memcpy(&value,&denormal,sizeof(value));
                volatile float coefficient=value,zero=0;
                volatile float contribution=coefficient*zero;(void)contribution;
            }
            cursor=(cursor+1)&127;
        }
    }
#endif
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
