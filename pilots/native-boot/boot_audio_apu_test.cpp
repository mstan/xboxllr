// SPDX-License-Identifier: GPL-3.0-or-later
// Actual shared native APU with authored signals. No original ROM/sample bytes.
#include "boot_audio_apu.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok,const char *why) {if(!ok)throw std::runtime_error(why);}
struct Fixture {
    static constexpr unsigned handle=71,table=0x1000,sge=0x9000,source=0x10000;
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x40000);
    std::string fault;
    xlle::McpxApu apu;
    std::array<uint16_t,16> handles{};
    std::unique_ptr<BootApuVoiceAdapter> adapter;
    xb_audio_native_event event{};
    void put(unsigned at,uint32_t word) {for(unsigned b=0;b<4;++b)ram.at(at+b)=uint8_t(word>>(b*8));}
    uint32_t get(unsigned at) {uint32_t v=0;for(unsigned b=0;b<4;++b)v|=uint32_t(ram.at(at+b))<<(b*8);return v;}
    void reg(unsigned offset,uint32_t value) {apu.mmio_write(xlle::McpxApu::kBase+offset,4,value);require(!apu.faulted(),"fixture register");}
    explicit Fixture(bool tone=false,xlle::ApuSignalModel model=xlle::ApuSignalModel::Strict)
        :apu([this](const std::string &why){fault=why;},model) {
        apu.set_mem_access([this](uint32_t a,void *p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [this](uint32_t a,const void *p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);return true;});
        reg(0x202c,table);reg(0x2030,sge);reg(0x115c,0x30000);
        reg(0x2054,0xffff);reg(0x2060,0xffff);reg(0x206c,0xffff);
        reg(0x2000,8);reg(0x1100,0);reg(0x1504,1u<<15);reg(0x1004,0x61);
        put(sge,source);
        for(unsigned bin=0;bin<31;++bin)reg(0x20200+bin*4,1);
        for(unsigned n=0;n<128;++n) {
            const int16_t sample=tone?int16_t(std::lround(4096*std::sin(6.283185307179586*n/64))):4096;
            ram[source+n*2]=uint8_t(sample);ram[source+n*2+1]=uint8_t(uint16_t(sample)>>8);
        }
        handles.fill(0xffff);handles[0]=handle;
        adapter=std::make_unique<BootApuVoiceAdapter>(apu,handles,
            [](uint32_t t,uint32_t frames,BootApuVoiceAdapter::Source &out){
                if(t!=XB_TABLE_SINE || frames>128)return false;out={0,128};return true;},
            [this](uint16_t h,uint32_t field,uint32_t &out){
                unsigned at=table+unsigned(h)*128+field;if(at+4>ram.size())return false;out=get(at);return true;});
        event.call.channel=0;event.call.fn=XB_CALL_SAMPLE;
        event.voice.alloc=1;event.voice.fmt=0x5000e0e6;event.voice.vbin=0x14830820;
        event.voice.vola=0x000f000f;event.voice.volb=event.voice.volc=0xffffffff;
        event.voice.ebo=127;event.frames=128;event.table=XB_TABLE_SINE;
        require(adapter->submit(event),"original sample methods");
        event.call.fn=XB_CALL_NOTE;event.call.effect=1;event.voice.on=1;event.voice.fmt|=1u<<25;
        require(adapter->submit(event),"original start methods");
    }
    BootApuVoiceAdapter::Bins block() {
        BootApuVoiceAdapter::Bins out{};
        require(adapter->quantum(out)==BootApuVoiceAdapter::Status::Produced,
                fault.empty()?adapter->error().c_str():fault.c_str());
        return out;
    }
};
}
int main() {
  try {
    Fixture constant;
    BootApuVoiceAdapter::Bins last{};
    for(unsigned q=0;q<16;++q)last=constant.block();
    for(unsigned i=0;i<32;++i) {
        require(std::abs(last[0][i]-524288)<256,"actual constant source/headroom");
        require(last[0][i]==last[1][i] && !last[2][i],"original planar routing");
    }
    uint32_t active=0,idle=0;
    require(constant.adapter->feedback(0,active,idle) && active==1 && !idle,"actual active feedback");
    require(!constant.adapter->feedback(1,active,idle),"active voice cannot acquire idle acknowledgement");

    Fixture repeated(true),control(true);
    for(unsigned q=0;q<5;++q)require(repeated.block()==control.block(),"identical pre-note signal");
    repeated.event.call.effect=0; // Original running-library note: pitch only, no VOICE_ON.
    require(repeated.adapter->submit(repeated.event),"repeated original NOTE methods");
    for(unsigned q=0;q<6;++q)
        require(repeated.block()==control.block(),"unchanged loop/EBO NOTE must preserve buffered source phase");

    Fixture guarded;
    guarded.event.call.fn=XB_CALL_FILTER;guarded.event.voice.misc=0x00810000;
    require(guarded.adapter->submit(guarded.event),"original filter methods");
    BootApuVoiceAdapter::Bins untouched;
    for(auto &bin:untouched)bin.fill(0x123456);
    require(guarded.adapter->quantum(untouched)==BootApuVoiceAdapter::Status::Failed,
            "strict shared policy must not silently substitute an unqualified filter");
    for(auto &bin:untouched)for(auto sample:bin)require(sample==0x123456,"declined output remains untouched");

    // Independent steady-state invariant: DC output equals normalized input.
    // The old linear policy retains unity DC gain. Q2600 exercises the large
    // audible normalization difference; Q26000 exercises the other boot value.
    for(unsigned resonance:{2600u,26000u}) {
        Fixture reference(false,xlle::ApuSignalModel::XemuStateVariableV1);
        reference.event.call.fn=XB_CALL_FILTER;reference.event.voice.misc=0x00810000;
        reference.event.voice.fca=(resonance<<16)|uint16_t(-8192);
        require(reference.adapter->submit(reference.event),"reference filter program");
        for(unsigned q=0;q<256;++q)last=reference.block();
        const double damping=std::max(resonance/32768.0,.079407);
        const int32_t expected=int32_t(std::llround(524288*std::sqrt(damping*.5+.01)));
        for(auto value:last[0])require(std::abs(value-expected)<256,"reference DC normalization");
    }
    // The pinned software reference explicitly omits EF_FCSCALE. Preserve the
    // signed original field and count affected actual frames, without inventing
    // a coefficient mapping. Both signs must equal a zero-scale control while
    // the actual filter envelope remains nonzero and steps normally.
    for(int scale:{37,-91}) {
        Fixture omitted(true,xlle::ApuSignalModel::XemuStateVariableV1);
        Fixture zero(true,xlle::ApuSignalModel::XemuStateVariableV1);
        for(auto *fixture:{&omitted,&zero}) {
            fixture->event.call.fn=XB_CALL_FILTER;
            fixture->event.voice.misc=0x00810000;
            fixture->event.voice.fca=(2600u<<16)|uint16_t(-8192);
            require(fixture->adapter->submit(fixture->event),"omitted-scale filter program");
        }
        const unsigned env1=Fixture::table+Fixture::handle*128+0x10;
        omitted.put(env1,uint32_t(uint8_t(scale))<<24);
        for(unsigned q=0;q<12;++q) {
            require(omitted.block()==zero.block(),"software reference must not infer cutoff modulation");
            require(omitted.get(env1)==uint32_t(uint8_t(scale))<<24,"signed cutoff register retained");
            require((omitted.get(Fixture::table+Fixture::handle*128+0x5c)>>24)!=0,
                    "actual filter envelope remains nonzero");
            require(omitted.apu.dls2_modulation_omitted_frames()==(q+1)*32,
                    "omission counter tracks actual rendered frames");
            require(omitted.apu.dls2_modulation_last_omitted_scale()==scale,
                    "omission diagnostic retains signed cutoff scale");
            require(!zero.apu.dls2_modulation_omitted_frames(),"zero-scale control has no omission");
        }
    }
    {
        const uint32_t packed=(2600u<<16)|uint16_t(-8192);
        auto c=xlle::ApuDls2Coefficients::decode(packed,true);
        std::vector<float> whole(4096*2),partitioned;
        for(unsigned i=0;i<4096;++i)whole[2*i]=whole[2*i+1]=float(.02*std::sin(i*.25));
        partitioned=whole;
        xlle::ApuDls2Lpf all,chunks;
        require(all.process(c,whole.data(),4096),"reference whole history");
        for(unsigned start=0;start<4096;start+=32)
            require(chunks.process(c,partitioned.data()+start*2,32),"reference chunk history");
        require(whole==partitioned,"reference history survives actual block boundaries");
        auto linear=xlle::ApuDls2Coefficients::decode(packed);
        std::vector<float> dc(4096*2,.125f);xlle::ApuDls2Lpf old;
        require(old.process(linear,dc.data(),4096),"legacy linear filter");
        require(std::abs(dc.back()-.125)<.00001,"existing nominal DC gain unchanged");
    }
    std::cout << "native source/routing/feedback, repeated-note phase and strict filter guard passed\n";
  } catch(const std::exception &error) {
    std::cerr << "Native boot APU fixture failed: " << error.what() << '\n';
    return 1;
  }
}
