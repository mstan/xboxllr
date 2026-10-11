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
    uint32_t release[2]{};
    if(((state>>28)&15)==6 && !read_voice_word(voice,0x6c,release[0])) {
        device_fault("amplitude release configuration DMA failed");return false;
    }
    if(((state>>24)&15)==6 && !read_voice_word(voice,0x18,release[1])) {
        device_fault("filter release configuration DMA failed");return false;
    }
    float gains[2]{};
    bool force_release=false;
    auto history=voice_envelopes_[voice];
    // Evaluate both before writing either. Curve policy is explicitly nominal;
    // attack overflow/zero-rate and unknown phases remain guarded.
    for(unsigned i=0;i<2;++i) {
        const unsigned shift=i?24:28;
        unsigned phase=(state>>shift)&15,count=(counts>>(i*16))&0xffff,level=0;
        const unsigned rate=(configuration[i*2]&0xfff)*16;
        if(phase!=6)history[i]={};
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
        case 4: {
            const unsigned decay=(configuration[i*2+1]&0xfff)*16;
            if(decay && count>decay) {
                device_fault("envelope decay counter exceeds nominal interval");return false;
            }
            const uint8_t sustain=uint8_t(configuration[i*2+1]>>24);
            level=apu_envelope_curve::decay_level(sustain,count,decay);
            if(level<=sustain){phase=5;count=0;}
            else --count;
            break;
        }
        case 5:count=0;level=configuration[i*2+1]>>24;break;
        case 6: {
            const unsigned n=(release[i]&0xfff)*16;
            if(n && count>n) {
                device_fault("envelope release counter exceeds nominal interval");return false;
            }
            if(!n || !count){phase=7;count=0;level=0;history[i]={};break;}
            auto& origin=history[i];
            if(!origin.active || origin.expected_count!=count || origin.rate_count!=n)
                origin={true,uint8_t(levels[i]>>24),uint16_t(count),uint16_t(count),uint16_t(n)};
            level=apu_envelope_curve::exponential_level(origin.initial_level,count,origin.initial_count);
            origin.expected_count=uint16_t(--count);
            break;
        }
        case 7:count=0;level=0;break;
        default:device_fault("envelope unknown phase remains unqualified");return false;
        }
        if(!i && phase==7)force_release=true;
        state=(state&~(15u<<shift))|(phase<<shift);
        counts=(counts&~(0xffffu<<(i*16)))|(count<<(i*16));
        levels[i]=(levels[i]&0xffffff)|(level<<24);
        gains[i]=float(level)/255;
    }
    if(!write_voice_word(voice,0x34,counts) || !write_voice_word(voice,0x54,state) ||
       !write_voice_word(voice,0x58,levels[0]) || !write_voice_word(voice,0x5c,levels[1])) {
        device_fault("envelope result DMA failed");return false;
    }
    voice_envelopes_[voice]=history;
    result={true,gains[0],gains[1],force_release};return true;
}

bool McpxApu::source_signal_quantum(uint16_t voice,float* stereo,uint32_t& count,
                                   bool& ended,ApuEnvelopeStep& envelope) {
    count=0;ended=false;
    envelope={};
    if(faulted_)return false;
    if(!stereo || voice>=kMaxVoices) {device_fault("invalid enveloped source destination/voice");return false;}
    uint32_t configuration=0;
    if(!read_voice_word(voice,8,configuration)) {device_fault("envelope pitch configuration DMA failed");return false;}
    if(!step_envelopes(voice,envelope))return false;
    if(!envelope.running || envelope.force_release)return true;
    const int scale=int(configuration>>24)-((configuration&0x80000000)?256:0);
    return resample_source_impl(voice,stereo,32,count,ended,scale*32.0*envelope.filter);
}

bool McpxApu::source_envelope_quantum(uint16_t voice,float* stereo,uint32_t& count,bool& ended) {
    ApuEnvelopeStep envelope;
    if(!source_signal_quantum(voice,stereo,count,ended,envelope))return false;
    for(unsigned i=0;i<count*2;++i)stereo[i]*=envelope.amplitude;
    return true;
}

bool McpxApu::process_2d_voice(uint16_t voice,float* bins,uint32_t& count,bool& completed) {
    return process_2d_voice_impl(voice,bins,count,completed,true);
}

bool McpxApu::process_2d_voice_impl(uint16_t voice,float* bins,uint32_t& count,bool& completed,bool retire) {
    count=0;completed=false;
    if(faulted_)return false;
    if(voice<64 || voice>=kMaxVoices || !bins) {
        device_fault("invalid 2D processing voice/output");return false;
    }
    uint32_t state=0,format=0,misc=0,route=0;
    if(!read_voice_word(voice,0x54,state) || !read_voice_word(voice,4,format) ||
       !read_voice_word(voice,0x18,misc) || !read_voice_word(voice,0,route)) {
        device_fault("2D processing configuration DMA failed");return false;
    }
    if(!(state&kVoiceActive) || (state&(1u<<18)) || voice_locked_[voice])return true;
    // This executable composition covers the ordinary original 2D defaults.
    // Other filter/headroom/spatial modes must acquire their real stages first.
    if((misc&0x30000) || ((format>>13)&7)!=7 ||
       (format&((1u<<21)|(1u<<22)|(1u<<24))) || (route&0x80008000)) {
        device_fault("2D filter/headroom/owner mode remains unqualified");return false;
    }
    float samples[64];bool ended=false;ApuEnvelopeStep envelope;
    if(!source_signal_quantum(voice,samples,count,ended,envelope))return false;
    // An owned filter belongs here, before amplitude and final sends. This
    // batch retains only the already-qualified bypass mode/headroom defaults.
    for(unsigned i=0;i<count*2;++i)samples[i]*=envelope.amplitude;
    if(!mix_2d_samples(voice,samples,count,bins))return false;
    // The real final samples reach the destination before its completion/IRQ.
    // Never free buffered rate output merely because source DMA reached EBO.
    if(ended || envelope.force_release) {
        if(retire && !retire_buffer(voice))return false;
        completed=true;
    }
    return true;
}
} // namespace xlle
