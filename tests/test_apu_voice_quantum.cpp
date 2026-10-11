// Device quantum -> real GP input -> real peripheral DMA. Waveform expectations
// are authored constants/tone fits; no native/donor output predicts the result.
#include "machine/apu.hpp"
#include "machine/apu_mix_word.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"VP quantum FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#include "apu_rate_fixture.hpp"

struct Fixture {
    static constexpr uint32_t table=0x1000,sge=0xa000,notify=0x70000;
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x90000,0xa5);
    std::string error;
    uint32_t fail_read=UINT32_MAX,fail_write=UINT32_MAX;
    unsigned source_reads=0,irqs=0;
    xlle::ApuVoiceQuantum* current=nullptr;
    xlle::McpxApu apu{[this](const std::string& why){error=why;}};
    Fixture() {
        apu.set_mem_access(
            [this](uint32_t a,void* p,size_t n){
                if(a==fail_read || uint64_t(a)+n>ram.size())return false;
                if(a>=0x10000 && a<0x60000)++source_reads;
                std::memcpy(p,ram.data()+a,n);return true;
            },
            [this](uint32_t a,const void* p,size_t n){
                if(a==fail_write || uint64_t(a)+n>ram.size())return false;
                std::memcpy(ram.data()+a,p,n);return true;
            });
        reg(0x202c,table);reg(0x2030,sge);reg(0x115c,notify);
        reg(0x2054,0xffff);reg(0x2060,0xffff);reg(0x206c,0xffff);
        reg(0x2000,8);reg(0x1100,0);reg(0x1504,1u<<15);reg(0x1004,0x61);
        for(unsigned page=0;page<64;++page)put(sge+page*8,0x10000+page*4096);
        apu.set_irq([this](int line,bool high){
            CHECK(line==5);
            if(high) {
                ++irqs;
                if(current) {
                    CHECK(current->published);
                    CHECK(*apu.gp_word(0x1400+31*32+31)==0);
                    CHECK(apu.mmio_read(0xfe835000+31*128+124,4)==0);
                }
            }
        });
    }
    static unsigned object(unsigned voice){return table+voice*128;}
    static unsigned status(unsigned voice){return notify+16*(2+voice*4)+15;}
    void put(unsigned a,uint32_t v){std::memcpy(ram.data()+a,&v,4);}
    uint32_t word(unsigned a)const{uint32_t v;std::memcpy(&v,ram.data()+a,4);return v;}
    void reg(unsigned o,uint32_t v){apu.mmio_write(0xfe800000+o,4,v);}
    void method(unsigned o,uint32_t v){reg(0x20000+o,v);}
    void add(unsigned voice,unsigned channels,unsigned frames,bool loop,unsigned bin,
             int16_t left,int16_t right=0,bool tone=false,unsigned amplitude=255) {
        const unsigned o=object(voice),base=(voice-64)*1024;
        std::fill(ram.begin()+o,ram.begin()+o+128,0);
        for(unsigned n=0;n<frames;++n)for(unsigned c=0;c<channels;++c) {
            const int16_t value=tone?int16_t(std::lround(left*std::sin(6.283185307179586*n/64))):(c?right:left);
            std::memcpy(ram.data()+0x10000+base+(n*channels+c)*2,&value,2);
        }
        method(0x2f8,voice);
        method(0x304,0x5000e000|(loop?1u<<25:0)|((channels-1)<<27)|((channels-1)<<16)|6|(7<<5));
        method(0x300,bin|(1<<5)|(2<<10)|(3<<16)|(4<<21)|(5<<26));
        method(0x360,0xffff000f);method(0x364,0xffffffff);method(0x368,0xffffffff);
        method(0x3a0,base);method(0x3dc,frames-1);
        method(0x30c,amplitude<<24);
        ram[status(voice)]=0x80;
        // Nonunity sustain is reached through the real legal HOLD -> DECAY
        // -> SUSTAIN phases; VOICE_ON only admits initial phases0..3.
        method(0x120,0x1ffff);method(0x124,voice|(amplitude==255?0u:3u<<28));
        if(!error.empty())std::fprintf(stderr,"voice %u setup: %s\n",voice,error.c_str());
        CHECK(error.empty());
    }
    bool quantum(xlle::ApuVoiceQuantum& out) {
        *apu.gp_word(0x1400+31*32+31)=0xabcdef;
        current=&out;const bool ok=apu.process_2d_quantum(out);current=nullptr;return ok;
    }
    float sample(unsigned bin,unsigned at) {
        const uint32_t raw=*apu.gp_word(0x1400+bin*32+at);
        CHECK(raw<=0xffffff);
        const int32_t pcm=int32_t(raw)-((raw&0x800000)?0x1000000:0);
        CHECK(apu.mmio_read(0xfe835000+bin*128+at*4,4)==raw);
        return pcm*(1.0f/8388608);
    }
    // Authored caller-side list maintenance models the already separately
    // qualified original unlink operation. This fixture does not claim to
    // execute that original ISR/consumer or synthesize completion on its behalf.
    void unlink(unsigned voice) {
        unsigned current=apu.mmio_read(0xfe802054,4),prior=0xffff;
        while(current!=voice){CHECK(current!=0xffff);prior=current;current=word(object(current)+0x7c)&0xffff;}
        const unsigned next=word(object(voice)+0x7c)&0xffff;
        if(prior==0xffff)reg(0x2054,next);
        else put(object(prior)+0x7c,(word(object(prior)+0x7c)&0xffff0000)|next);
        reg(0x1100,0);reg(0x1000,0x70);
    }
};

static void mixed_waveforms_and_dma() {
    for(unsigned life=0;life<3;++life) {
        Fixture f;
        f.add(71,1,2048,true,0,4096,0,false,128);
        f.add(96,2,2048,true,0,8192,-16384);
        // A second left send to bin0 at -6dB, and a right send to bin1.
        f.method(0x2f8,96);f.method(0x300,0|(1<<5)|(0<<10)|(3<<16)|(4<<21)|(5<<26));
        f.method(0x360,0x000f000f);f.method(0x364,0xffff180f);
        f.add(129,1,2048,true,3,8192,0,true);f.reg(0x20200+3*4,1);
        *f.apu.gp_word(0x200)=0x123456;*f.apu.gp_word(0x2800)=0x654321;
        std::vector<float> tone;
        const double steady=0.125*128/255.0+0.25+0.25*std::pow(10.0,-6.0/20);
        for(unsigned q=0;q<24;++q) {
            xlle::ApuVoiceQuantum result;CHECK(f.quantum(result));
            CHECK(result.visited==3 && result.contributed==3 && !result.completed && result.published);
            CHECK(result.sequence==q+1 && f.apu.voice_mix_quanta()==q+1);
            if(q>=8)for(unsigned n=0;n<32;++n) {
                CHECK(std::fabs(f.sample(0,n)-steady)<0.000002);
                CHECK(std::fabs(f.sample(1,n)+0.5)<0.000002);
                tone.push_back(f.sample(3,n));
            }
            for(unsigned bin:{2u,4u,8u,31u})for(unsigned n=0;n<32;++n)CHECK(f.sample(bin,n)==0);
        }
        const auto fit=apu_tone_fit(tone.data(),unsigned(tone.size()),1,750);
        CHECK(std::fabs(fit.amplitude-0.125)<0.0001 && fit.error<0.00001);
        CHECK(!f.irqs && f.error.empty() && *f.apu.gp_word(0x200)==0x123456 && *f.apu.gp_word(0x2800)==0x654321);
        // The generated GP bank goes through real known-bit descriptor DMA.
        f.put(0xc000,0x60000);f.put(0xc008,0x64000);
        f.reg(0x2040,0xc000);f.reg(0x20d4,1);
        const uint32_t descriptor[]={0x4000,0x4000|(6<<10)|(14<<5)|16|2,32,0x1400,0,4093,127};
        for(unsigned i=0;i<7;++i)*f.apu.gp_word(0x20+i)=descriptor[i];
        CHECK(f.apu.gp_write(0xffffd4,0x20) && f.apu.gp_write(0xffffd6,1) && f.apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096));
        for(unsigned n=0;n<32;++n)for(unsigned b=0;b<4;++b) {
            const unsigned linear=4093+n*4+b,physical=(linear<4096?0x60000:0x64000)+(linear&4095);
            const uint8_t expected=b==3?0:uint8_t(*f.apu.gp_word(0x1400+n)>>(b*8));
            CHECK(f.ram[physical]==expected);
        }
        CHECK(f.apu.gp_dma_nodes()==1 && f.error.empty());
    }
}

static void finite_and_idle_ownership() {
    for(unsigned life=0;life<3;++life) {
        Fixture f;f.add(71,1,65,false,0,16384);f.add(96,1,129,false,1,8192);
        unsigned retired=0,quanta=0;double energy[2]{};
        for(unsigned attempt=0;attempt<20 && retired<2;++attempt) {
            xlle::ApuVoiceQuantum result;CHECK(f.quantum(result));
            if(result.published) {
                ++quanta;CHECK(result.sequence==quanta);
                for(unsigned n=0;n<32;++n)for(unsigned bin=0;bin<2;++bin)energy[bin]+=std::fabs(f.sample(bin,n));
                retired+=result.completed;
            } else {
                CHECK(!result.sequence && !result.visited && !result.contributed && !result.completed);
                const unsigned idle=f.apu.mmio_read(0xfe801110,4);
                CHECK(idle==71 || idle==96);
                CHECK(!(f.word(Fixture::object(idle)+0x54)&(1u<<21)) && f.ram[Fixture::status(idle)]==1);
                const unsigned reads=f.source_reads;auto before=f.ram;
                xlle::ApuVoiceQuantum blocked;CHECK(f.quantum(blocked) && !blocked.published);
                CHECK(f.ram==before && f.source_reads==reads);
                f.unlink(idle);
            }
        }
        CHECK(retired==2 && energy[0]>20 && energy[1]>20 && f.irqs>=1 && f.error.empty());
        // A retired but still linked voice causes the actual idle trap, with
        // no stale sample replay; only the guest's list owner removes it.
        xlle::ApuVoiceQuantum idle;CHECK(f.quantum(idle) && !idle.published);
        const unsigned last=f.apu.mmio_read(0xfe801110,4);f.unlink(last);
        const auto before=f.ram;const unsigned reads=f.source_reads;
        for(unsigned i=0;i<3;++i) {
            xlle::ApuVoiceQuantum quiet;CHECK(f.quantum(quiet));
            CHECK(quiet.published && !quiet.visited && !quiet.contributed && !quiet.completed);
            for(unsigned n=0;n<1024;++n)CHECK(*f.apu.gp_word(0x1400+n)==0);
        }
        CHECK(f.source_reads==reads && f.ram==before);
    }
}

static void admission_and_failures() {
    for(unsigned scenario=0;scenario<8;++scenario) {
        Fixture f;f.add(71,1,128,true,0,4096);f.add(96,1,128,true,1,8192);
        const unsigned object=Fixture::object(71);
        switch(scenario) {
        case 0:f.put(object+0x7c,96);break; // cycle in the complete list
        case 1:f.reg(0x2060,7);break;
        case 2:f.reg(0x2054,7);break;
        case 3:f.reg(0x202c,0xfffff000);break;
        case 4:f.put(object+0x18,1u<<16);break;
        case 5:f.put(object+4,f.word(object+4)&~0xe000u);break;
        case 6:f.put(object+4,f.word(object+4)|(1u<<24));break;
        case 7:f.put(object,f.word(object)|0x8000);break;
        }
        auto before=f.ram;std::vector<uint32_t> gp(f.apu.gp_word(0x1400),f.apu.gp_word(0x1400)+1024);
        xlle::ApuVoiceQuantum result;
        CHECK(!f.apu.process_2d_quantum(result) && !f.error.empty());
        CHECK(!result.published && !result.visited && !result.completed && !f.apu.voice_mix_quanta());
        CHECK(f.ram==before && !f.source_reads && !f.irqs);
        CHECK(std::equal(gp.begin(),gp.end(),f.apu.gp_word(0x1400)));
    }
    Fixture partial;partial.add(71,1,128,true,0,4096);partial.add(96,1,128,true,1,8192);
    // The top voice completes real source work before the lower voice faults.
    partial.fail_read=0x10000+(71-64)*1024;
    const auto gp=std::vector<uint32_t>(partial.apu.gp_word(0x1400),partial.apu.gp_word(0x1400)+1024);
    xlle::ApuVoiceQuantum failed;CHECK(!partial.apu.process_2d_quantum(failed));
    CHECK(failed.visited==1 && failed.contributed==1 && !failed.published && !failed.completed);
    CHECK((partial.word(Fixture::object(96)+0x58)&0xffffff)>0 && !partial.irqs);
    CHECK(std::equal(gp.begin(),gp.end(),partial.apu.gp_word(0x1400)));
    xlle::ApuVoiceQuantum repeat;CHECK(!partial.apu.process_2d_quantum(repeat) && !repeat.published);
    // A failed final notifier store is a terminal partial operation: actual
    // GP publication is reported, but no successful retirement/sequence/IRQ.
    Fixture notify_failure;notify_failure.add(71,1,1,false,0,16384);
    notify_failure.fail_write=Fixture::status(71);
    xlle::ApuVoiceQuantum published;
    CHECK(!notify_failure.apu.process_2d_quantum(published));
    CHECK(published.published && !published.completed && !published.sequence);
    CHECK(!notify_failure.apu.voice_mix_quanta() && !notify_failure.irqs);
    CHECK(notify_failure.ram[Fixture::status(71)]==0x80);
    CHECK(!(notify_failure.word(Fixture::object(71)+0x54)&(1u<<21)));
    // Disabled, paused and locked processing must not invent source progress.
    for(unsigned control=0;control<4;++control) {
        Fixture f;f.add(71,1,128,true,0,4096);
        if(control==0)f.reg(0x2000,0);
        if(control==1)f.reg(0x1100,0x80);
        if(control==2)f.method(0x140,71|(1u<<18));
        if(control==3)f.method(0x2f8,71),f.method(0x2fc,1);
        const auto before=f.ram;xlle::ApuVoiceQuantum result;CHECK(f.apu.process_2d_quantum(result));
        CHECK(!result.contributed && !result.completed && !f.source_reads && f.ram==before);
        CHECK(result.published==(control>=2));
    }
    const float levels[]={-2,-1,-0.5f,0,0.5f,1,2};
    const uint32_t words[]={0x800000,0x800000,0xc00000,0,0x400000,0x7fffff,0x7fffff};
    for(unsigned i=0;i<7;++i){uint32_t value=123;CHECK(xlle::apu_mix_word(levels[i],value) && value==words[i]);}
}

int main() {
    mixed_waveforms_and_dma();finite_and_idle_ownership();admission_and_failures();
    std::puts("PASS: device-owned ordinary VP/GP quanta; 3 mixed-waveform lifetimes, 3 finite/idle lifetimes, shared GP DMA, publication before IRQ, preflight and terminal partial failures; no bootstrap/EP/clock qualification");
}
