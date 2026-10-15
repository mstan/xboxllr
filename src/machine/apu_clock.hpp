// Shared 48 kHz time conversion. This alone is not MCPX XGSCNT: that register
// has a retained value, a writable zero and a sequencer-controlled gate.
// Time comes from the machine's selected clock, never from register reads.
#pragma once
#include <cstdint>
namespace xlle {
inline uint32_t apu_sample_counter(uint64_t ticks,uint64_t frequency)
{
    // The machine uses 733MHz instruction time; the native adapter uses QPC.
    // Integer seconds are reduced to 32 bits before the intentional wrap.
    return static_cast<uint32_t>(ticks/frequency)*48000u+
           static_cast<uint32_t>((ticks%frequency)*48000u/frequency);
}

class ApuSampleCounter {
public:
    bool advance(uint64_t ticks,uint64_t frequency,bool running) {
        if(!frequency || frequency>UINT64_MAX/48000u || ticks<ticks_ ||
           (frequency_ && frequency!=frequency_))return false;
        const uint32_t sample=apu_sample_counter(ticks,frequency);
        // Keep the shared sample-clock phase while stopped. Resuming counts
        // subsequent sample edges; it never adds the stopped interval.
        if(running)value_+=sample-previous_sample_;
        ticks_=ticks;frequency_=frequency;previous_sample_=sample;return true;
    }
    void clear() {value_=0;}
    uint32_t value()const{return value_;}
    uint64_t ticks()const{return ticks_;}
private:
    uint64_t ticks_=0,frequency_=0;
    uint32_t previous_sample_=0,value_=0;
};
}
