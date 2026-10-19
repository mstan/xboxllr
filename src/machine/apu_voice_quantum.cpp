// Device-owned VP -> GP boundary. Existing original ownership/default fields
// select real source, rate, envelope and send stages; no host voice registry.
#include "machine/apu.hpp"
#include "machine/apu_mix_word.hpp"
#include <algorithm>
#include <cstdio>

namespace xlle {
bool McpxApu::process_2d_quantum(ApuVoiceQuantum& result) {
    result={};
    if(faulted_)return false;
    if(!gp_mix_publication(true))return false;
    if(processing_quantum_) {device_fault("recursive VP quantum admission");return false;}
    if(!(reg32(kSectl)&0x18) || (reg32(kFectl)&kFectlMethodMode))return true;
    if(uint16_t(reg32(kVoiceTop[1]))!=kNullVoice || uint16_t(reg32(kVoiceTop[2]))!=kNullVoice) {
        device_fault("VP quantum 3D/multipass list remains unqualified");return false;
    }
    processing_quantum_=true;
    struct Scope {bool& flag;~Scope(){flag=false;}} scope{processing_quantum_};
    std::array<uint16_t,kMaxVoices> voices{},nexts{};
    std::array<bool,kMaxVoices> seen{},retiring{};
    unsigned total=0;
    uint16_t idle=kNullVoice,voice=uint16_t(reg32(kVoiceTop[0]));
    // Preflight the complete actual list before any sample/cursor work. The
    // fresh local traversal is not a second lifetime owner or ready queue.
    while(voice!=kNullVoice) {
        if(voice<64 || voice>=kMaxVoices || seen[voice]) {
            device_fault("VP quantum invalid ordinary voice list/cycle");return false;
        }
        seen[voice]=true;
        uint32_t state=0,format=0,misc=0,route=0,link=0,control50=0,control70=0;
        if(!read_voice_word(voice,0x54,state) || !read_voice_word(voice,4,format) ||
           !read_voice_word(voice,0x18,misc) || !read_voice_word(voice,0,route) ||
           !read_voice_word(voice,kVoicePitchLink,link) ||
           !read_voice_word(voice,0x50,control50) || !read_voice_word(voice,0x70,control70)) {
            device_fault("VP quantum list/configuration DMA failed");return false;
        }
        if(!(state&kVoiceActive)) {if(idle==kNullVoice)idle=voice;}
        else if(!(state&(1u<<18)) && !voice_locked_[voice]) {
            bool filtered=false;ApuDls2Coefficients coefficients;
            if(!prepare_2d_filter(voice,format,misc,route,control50,control70,filtered,coefficients))return false;
        }
        voices[total]=voice;nexts[total++]=uint16_t(link);voice=uint16_t(link);
    }
    if(idle!=kNullVoice) {
        // The existing original ISR owns unlinking idle voices. Leave the
        // previous mix bank intact while its trap awaits that real consumer.
        emit_idle_voice(idle);return !faulted_;
    }
    std::array<float,1024> bins{};
    for(unsigned i=0;i<total;++i) {
        set_reg32(kVoiceCurrent[0],voices[i]);set_reg32(kVoiceNext[0],nexts[i]);
        uint32_t count=0;bool complete=false;
        if(!process_2d_voice_impl(voices[i],bins.data(),count,complete,false))return false;
        ++result.visited;if(count)++result.contributed;retiring[i]=complete;
    }
    std::array<uint32_t,1024> words;
    for(unsigned i=0;i<words.size();++i)if(!apu_mix_word(bins[i],words[i])) {
        device_fault("VP quantum non-finite mix input");return false;
    }
    // Every GP access, including descriptor DMA, resolves this same bank.
    // Publish the entire block before any DONE status or completion IRQ.
    std::copy(words.begin(),words.end(),gp_words_.begin()+0x1400);
    result.published=true;
    if(observer_) {
        const uint64_t sequence=voice_mix_quanta_+1;
        const uint32_t metadata[]={uint32_t(sequence),uint32_t(sequence>>32),result.visited,result.contributed};
        observer_({ApuObservationKind::Mix,0,metadata,4,words.data(),uint32_t(words.size()*sizeof(words[0]))});
    }
    set_reg32(kVoiceCurrent[0],kNullVoice);set_reg32(kVoiceNext[0],kNullVoice);
    for(unsigned i=0;i<total;++i)if(retiring[i]) {
        if(!retire_buffer(voices[i]) || faulted_)return false;
        ++result.completed;
    }
    result.sequence=++voice_mix_quanta_;return true;
}
} // namespace xlle
