// SPDX-License-Identifier: GPL-3.0-or-later
#include "boot_audio_owner.h"
#include "boot_audio_apu.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
constexpr uint32_t voice_table=0x1000,sge_table=0x9000,notify_table=0xa000,sample_memory=0x10000;
constexpr uint32_t null_voice=0xffff,active_flag=1u<<21;

// The bounded native equivalent of the original ordinary-mono idle path:
// 4983 80066E99 -> 8006650D -> 80065E26 -> 80064D7D -> 800630BE.
// It consumes the actual FE trap, preserves the predecessor's pitch halfword,
// unlinks the actual hardware list and only then acknowledges the library.
// No callback is inferred merely from an inactive voice or an elapsed beat.
class Owner {
    std::vector<uint8_t> ram_=std::vector<uint8_t>(0x80000);
    std::array<uint16_t,XB_AUDIO_CHANNELS> handles_{};
    std::array<BootApuVoiceAdapter::Source,XB_TABLES> sources_{};
    std::string fault_;
    bool irq_=false;
    uint32_t bound_=0,pending_ack_=0,ack_count_=0;
    xlle::McpxApu apu_;
    std::unique_ptr<BootApuVoiceAdapter> adapter_;

    void check(bool ok,const char *why) const {
        if(!ok)throw std::runtime_error(fault_.empty()?why:fault_);
    }
    uint32_t word(uint32_t at) const {
        check(uint64_t(at)+4<=ram_.size(),"Native boot audio RAM read outside owner");
        return uint32_t(ram_[at])|(uint32_t(ram_[at+1])<<8)|
            (uint32_t(ram_[at+2])<<16)|(uint32_t(ram_[at+3])<<24);
    }
    void put(uint32_t at,uint32_t value) {
        check(uint64_t(at)+4<=ram_.size(),"Native boot audio RAM write outside owner");
        for(unsigned i=0;i<4;++i)ram_[at+i]=uint8_t(value>>(8*i));
    }
    void reg(uint32_t at,uint32_t value) {
        apu_.mmio_write(xlle::McpxApu::kBase+at,4,value);
        check(!apu_.faulted(),"Native boot audio register declined");
    }
    uint32_t reg(uint32_t at) {
        auto value=apu_.mmio_read(xlle::McpxApu::kBase+at,4);
        check(!apu_.faulted(),"Native boot audio register read declined");
        return value;
    }
    static uint32_t object(uint16_t voice) { return voice_table+128u*voice; }
    static uint32_t notification(uint16_t voice) { return notify_table+16u*(2+4u*voice)+15; }
    unsigned channel(uint16_t voice) const {
        for(unsigned i=0;i<handles_.size();++i)if(handles_[i]==voice && (bound_>>i&1u))return i;
        throw std::runtime_error("Native boot idle event has no ordinary voice owner");
    }
    void idle() {
        const uint32_t control=reg(0x1100);
        check(irq_ && (control&0xfe0)==0xfe0 && reg(0x1300)==0x8000,
              "Native boot audio needs an actual idle trap");
        const uint32_t parameter=reg(0x1304);
        check(parameter<256,"Native boot idle handle outside device");
        const auto voice=uint16_t(parameter);
        const unsigned c=channel(voice);
        const uint32_t o=object(voice),format=word(o+4);
        check(!(word(o+0x54)&active_flag) && ram_[notification(voice)]==1,
              "Native boot idle consumer requires real device completion");
        check(!(format&((1u<<21)|(1u<<22)|(1u<<23)|(1u<<24)|(1u<<27))),
              "Native boot idle consumer only owns ordinary mono buffers");
        uint16_t current=uint16_t(reg(0x2054)),previous=null_voice;
        std::array<bool,256> seen{};
        while(current!=voice) {
            check(current>=64 && current<256 && !seen[current],"Native boot idle list missing/cyclic");
            channel(current);seen[current]=true;
            previous=current;current=uint16_t(word(object(current)+0x7c));
        }
        const uint16_t next=uint16_t(word(o+0x7c));
        check(next==null_voice || (next>=64 && next<256 && next!=voice && !seen[next]),
              "Native boot idle successor invalid");
        if(next!=null_voice)channel(next);
        if(previous==null_voice)reg(0x2054,next);
        else {
            const uint32_t link=object(previous)+0x7c;
            put(link,(word(link)&0xffff0000u)|next);
        }
        // Idle preflight has not begun a voice quantum: CURRENT/NEXT are null.
        // Retain that ownership rather than inventing a running cursor.
        check(uint16_t(reg(0x2058))==null_voice && uint16_t(reg(0x205c))==null_voice,
              "Native boot idle consumer encountered an in-flight quantum");
        reg(0x202f8,voice);
        reg(0x20304,format); // Original ordinary-buffer library format publication.
        // Original control helper80063E9A(0), then (1), consumes trap reason.
        reg(0x1100,(control&0xffffff9fu)|0x80u);
        reg(0x1100,reg(0x1100)&0xffffff1fu);
        reg(0x1000,0x70);
        check(!apu_.faulted(),"Native boot idle retirement failed");
        pending_ack_|=1u<<c;
        ++ack_count_;
    }
  public:
    explicit Owner(xb_state *state)
        :apu_([this](const std::string &why){if(fault_.empty())fault_=why;},
              xlle::ApuSignalModel::XemuStateVariableV1) {
        for(unsigned i=0;i<handles_.size();++i) {
            handles_[i]=uint16_t(64+i);
            put(object(handles_[i])+0x7c,null_voice);
        }
        uint32_t offset=0;
        for(unsigned table=0;table<XB_TABLES;++table) {
            const int frames=xb_audio_table(state,table,nullptr,0);
            check(frames>0,"Native boot sample table unavailable");
            offset=(offset+4095u)&~4095u;
            check(uint64_t(sample_memory)+offset+uint64_t(frames)*2<=ram_.size(),
                  "Native boot samples exceed private RAM");
            std::vector<int16_t> values(size_t(frames),0);
            check(xb_audio_table(state,table,values.data(),uint32_t(frames))==frames,
                  "Native boot sample generation failed");
            for(unsigned i=0;i<unsigned(frames);++i) {
                const auto value=uint16_t(values[i]);
                ram_[sample_memory+offset+i*2]=uint8_t(value);
                ram_[sample_memory+offset+i*2+1]=uint8_t(value>>8);
            }
            sources_[table]={offset,uint32_t(frames)};
            offset+=uint32_t(frames)*2;
        }
        for(unsigned i=0;i<(offset+4095)/4096;++i)put(sge_table+i*8,sample_memory+i*4096);
        apu_.set_mem_access(
            [this](uint32_t at,void *data,size_t n){
                if(uint64_t(at)+n>ram_.size())return false;
                std::memcpy(data,ram_.data()+at,n);return true;
            },[this](uint32_t at,const void *data,size_t n){
                if(uint64_t(at)+n>ram_.size())return false;
                std::memcpy(ram_.data()+at,data,n);return true;
            });
        apu_.set_irq([this](int,bool high){irq_=high;});
        reg(0x202c,voice_table);reg(0x2030,sge_table);reg(0x115c,notify_table);
        for(auto at:{0x2054u,0x2058u,0x205cu,0x2060u,0x2064u,0x2068u,0x206cu,0x2070u,0x2074u})
            reg(at,null_voice);
        reg(0x2000,8);reg(0x1100,0);reg(0x1504,1u<<15);reg(0x1004,0x71);
        // Original boot submix headroom1, applied by the shared final send stage.
        for(unsigned bin=0;bin<32;++bin)reg(0x20200+bin*4,1);
        adapter_=std::make_unique<BootApuVoiceAdapter>(apu_,handles_,
            [this](uint32_t table,uint32_t frames,BootApuVoiceAdapter::Source &out){
                if(table>=sources_.size() || !frames || frames>sources_[table].frames)return false;
                out=sources_[table];return true;
            },[this](uint16_t voice,uint32_t field,uint32_t &out){
                if(voice>=256 || field>124 || field%4)return false;
                out=word(object(voice)+field);return true;
            });
    }
    BootNativeAudioResult render(xb_state *state,uint32_t frames) {
        BootNativeAudioResult result;
        result.pcm.resize(uint64_t(frames)*2);
        for(uint32_t at=0;at<frames;at+=XB_AUDIO_BLOCK) {
            uint32_t active=0,ack=0;
            if(!adapter_->feedback(pending_ack_,active,ack))check(false,adapter_->error().c_str());
            pending_ack_=0;
            xb_audio_native_out commands{};
            check(xb_audio_native_block(state,active,ack,&commands)==XB_OK,
                  "Native boot sequence rejected actual voice feedback");
            for(unsigned i=0;i<commands.n_events;++i) {
                const auto &event=commands.event[i];
                check(event.call.channel<handles_.size(),"Native boot event channel outside owner");
                if(event.call.fn==XB_CALL_NOTE && event.call.effect) {
                    ram_[notification(handles_[event.call.channel])]=0x80;
                    ++result.note_starts;
                }
                if(!adapter_->submit(event))check(false,adapter_->error().c_str());
                if(event.call.fn==XB_CALL_SAMPLE && event.voice.alloc)bound_|=1u<<event.call.channel;
            }
            BootApuVoiceAdapter::Bins bins{};
            bool published=false;
            for(unsigned attempt=0;attempt<=XB_AUDIO_CHANNELS;++attempt) {
                const auto status=adapter_->quantum(bins);
                if(status==BootApuVoiceAdapter::Status::Failed)
                    check(false,adapter_->error().c_str());
                if(status==BootApuVoiceAdapter::Status::Produced){published=true;break;}
                idle();
            }
            check(published,"Native boot audio idle service did not make progress");
            std::array<int32_t,XB_AUDIO_BLOCK> left{},right{};
            check(xb_audio_gp(state,bins[0].data(),bins[1].data(),bins[2].data(),left.data(),right.data())==XB_OK,
                  "Native boot GP processing failed");
            for(unsigned i=0;i<XB_AUDIO_BLOCK;++i) {
                const auto pcm=[](int32_t word){
                    const int32_t value=word>=0?word/256:-int32_t((-int64_t(word)+255)/256);
                    return int16_t(std::clamp(value,int32_t(-32768),int32_t(32767)));
                };
                result.pcm[(at+i)*2]=pcm(left[i]);result.pcm[(at+i)*2+1]=pcm(right[i]);
            }
            ++result.blocks;
        }
        result.idle_acknowledgements=ack_count_;
        result.cutoff_modulation_omitted_frames=apu_.dls2_modulation_omitted_frames();
        result.last_omitted_cutoff_scale=apu_.dls2_modulation_last_omitted_scale();
        return result;
    }
};
}

BootNativeAudioResult boot_native_audio(xb_state *state,uint32_t frames) {
    if(!state || !frames || frames>384000 || frames%XB_AUDIO_BLOCK)
        throw std::runtime_error("Native boot audio requires bounded whole 32-sample blocks");
    auto owner=std::make_unique<Owner>(state);
    return owner->render(state,frames);
}
