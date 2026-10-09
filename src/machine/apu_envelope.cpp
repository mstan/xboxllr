// Authored from the documented MCPX phase/counter interface and original
// caller encodings. No xemu implementation body is incorporated.
#include "machine/apu.hpp"

namespace xlle {
bool McpxApu::step_envelopes(uint16_t voice,ApuEnvelopeStep& result) {
    result={};
    if(faulted_)return false;
    uint32_t state=0,counts=0,configuration[4]{},levels[2]{};
    if(voice>=kMaxVoices || !read_voice_word(voice,0x54,state)) {
        device_fault("envelope voice/state DMA failed");return false;
    }
    if(!(state&kVoiceActive) || (state&(1u<<18)) || voice_locked_[voice])return true;
    if(!read_voice_word(voice,0x34,counts) || !read_voice_word(voice,0x58,levels[0]) ||
       !read_voice_word(voice,0x5c,levels[1])) {
        device_fault("envelope counter/level DMA failed");return false;
    }
    for(unsigned i=0;i<4;++i)if(!read_voice_word(voice,8+i*4,configuration[i])) {
        device_fault("envelope configuration DMA failed");return false;
    }
    float gains[2]{};
    // Evaluate both before writing either. Decay, release, forced release and
    // attack overflow/zero-rate behavior need stronger independent evidence.
    for(unsigned i=0;i<2;++i) {
        const unsigned shift=i?24:28;
        unsigned phase=(state>>shift)&15,count=(counts>>(i*16))&0xffff,level=0;
        const unsigned rate=(configuration[i*2]&0xfff)*16;
        switch(phase) {
        case 0:count=0;level=255;break;
        case 1:
            if(count)--count;else phase=2;
            break;
        case 2:
            if(!rate || count>rate) {
                device_fault("envelope zero-rate/overflow attack remains unqualified");return false;
            }
            level=count*255/rate;
            if(count==rate){phase=3;count=((configuration[i*2+1]>>12)&0xfff)*16;}
            else ++count;
            break;
        case 3:
            level=255;
            if(count)--count;else {phase=4;count=(configuration[i*2+1]&0xfff)*16;}
            break;
        case 5:count=0;level=configuration[i*2+1]>>24;break;
        default:device_fault("envelope decay/release/unknown phase remains unqualified");return false;
        }
        state=(state&~(15u<<shift))|(phase<<shift);
        counts=(counts&~(0xffffu<<(i*16)))|(count<<(i*16));
        levels[i]=(levels[i]&0xffffff)|(level<<24);
        gains[i]=float(level)/255;
    }
    if(!write_voice_word(voice,0x34,counts) || !write_voice_word(voice,0x54,state) ||
       !write_voice_word(voice,0x58,levels[0]) || !write_voice_word(voice,0x5c,levels[1])) {
        device_fault("envelope result DMA failed");return false;
    }
    result={true,gains[0],gains[1]};return true;
}

bool McpxApu::source_envelope_quantum(uint16_t voice,float* stereo,uint32_t& count,bool& ended) {
    count=0;ended=false;
    if(faulted_)return false;
    if(!stereo || voice>=kMaxVoices) {device_fault("invalid enveloped source destination/voice");return false;}
    uint32_t configuration=0;
    if(!read_voice_word(voice,8,configuration)) {device_fault("envelope pitch configuration DMA failed");return false;}
    ApuEnvelopeStep envelope;
    if(!step_envelopes(voice,envelope))return false;
    if(!envelope.running)return true;
    const int scale=int(configuration>>24)-((configuration&0x80000000)?256:0);
    if(!resample_source_impl(voice,stereo,32,count,ended,scale*32.0*envelope.filter))return false;
    for(unsigned i=0;i<count*2;++i)stereo[i]*=envelope.amplitude;
    return true;
}
} // namespace xlle
