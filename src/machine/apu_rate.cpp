// Authored MCPX source/pitch adapter around unmodified libsamplerate (BSD-2).
// SRC_SINC_FASTEST follows xemu's reference choice, not a silicon-exact claim.
// Unlike its starvation workaround, this adapter never invents source silence.
#include "machine/apu.hpp"
#include <samplerate.h>
#include <algorithm>
#include <array>

namespace xlle {
struct ApuRateState {
    SRC_STATE* converter=nullptr;
    std::array<uint32_t,5> binding{}; // format, base, loop, end, next real CBO
    float input[64]{};
    uint32_t available=0,used=0;
    bool source_end=false,drained=false;
    ~ApuRateState(){if(converter)src_delete(converter);}
};

bool McpxApu::resample_source(uint16_t voice,float* stereo,uint32_t capacity,uint32_t& count,bool& ended) {
    count=0;ended=false;
    if(faulted_)return false;
    uint32_t env=0;
    if(voice>=kMaxVoices || !read_voice_word(voice,8,env)) {
        device_fault("source-rate envelope configuration DMA failed");return false;
    }
    if(env>>24) {device_fault("pitch modulation requires the envelope stage");return false;}
    return resample_source_impl(voice,stereo,capacity,count,ended,0);
}

bool McpxApu::resample_source_impl(uint16_t voice,float* stereo,uint32_t capacity,uint32_t& count,bool& ended,
                                  double pitch_modulation) {
    count=0;ended=false;
    if(faulted_)return false;
    if(voice>=kMaxVoices || capacity>65536 || (capacity && !stereo)) {
        device_fault("invalid source-rate destination/voice/extent");return false;
    }
    if(!capacity)return true;
    uint32_t state=0,pitch=0;
    if(!read_voice_word(voice,0x54,state)) {device_fault("source-rate state DMA failed");return false;}
    if(!(state&kVoiceActive) || (state&(1u<<18)) || voice_locked_[voice])return true;
    std::array<uint32_t,5> binding;
    constexpr uint32_t fields[]={4,0x20,0x24,0x5c,0x58};
    for(unsigned i=0;i<binding.size();++i)
        if(!read_voice_word(voice,fields[i],binding[i])) {device_fault("source-rate binding DMA failed");return false;}
    binding[0]&=0xfffffc00; // final-send routing does not change source history
    for(unsigned i=1;i<binding.size();++i)binding[i]&=0xffffff;
    if(!read_voice_word(voice,0x7c,pitch)) {
        device_fault("source-rate pitch DMA failed");return false;
    }
    const int signed_pitch=int(pitch>>16)-((pitch&0x80000000)?65536:0);
    const double ratio=std::exp2(-(double(signed_pitch)+pitch_modulation)/4096.0); // output frames per source frame
    auto& owned=voice_rates_[voice];
    if(owned && owned->binding!=binding)owned.reset();
    if(!owned) {
        auto candidate=std::make_shared<ApuRateState>();int error=0;
        candidate->converter=src_new(SRC_SINC_FASTEST,2,&error);
        if(!candidate->converter || src_set_ratio(candidate->converter,ratio)) {
            device_fault("source-rate converter creation failed");return false;
        }
        candidate->binding=binding;owned=std::move(candidate);
    }
    auto& rate=*owned;
    if(rate.drained){ended=true;return true;}
    while(count<capacity) {
        SRC_DATA step{};
        step.data_in=rate.input+rate.used*2;step.input_frames=rate.available-rate.used;
        step.data_out=stereo+count*2;step.output_frames=capacity-count;
        step.src_ratio=ratio;step.end_of_input=rate.source_end;
        if(src_process(rate.converter,&step)) {device_fault("source-rate conversion failed");return false;}
        rate.used+=static_cast<uint32_t>(step.input_frames_used);
        count+=static_cast<uint32_t>(step.output_frames_gen);
        if(!step.input_frames_used && !step.output_frames_gen) {
            if(rate.used!=rate.available) {device_fault("source-rate conversion made no progress");return false;}
            if(rate.source_end) {rate.drained=ended=true;break;}
            // Ask the converter for already-buffered output before performing
            // more guest DMA. Prefetching unconditionally here makes CBO depend
            // on the caller's request partition even at a constant pitch.
            rate.available=rate.used=0;
            if(!source_frames(voice,rate.input,32,rate.available,rate.source_end))return false;
            uint32_t cursor=0;
            if(!read_voice_word(voice,0x58,cursor)) {device_fault("source-rate cursor DMA failed");return false;}
            rate.binding[4]=cursor&0xffffff;
            if(!rate.available && !rate.source_end) {device_fault("source-rate source made no progress");return false;}
        }
    }
    return true;
}
} // namespace xlle
