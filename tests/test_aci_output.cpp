// Authored actual-RAM PCM consumer and descriptor/cursor/lifetime contract.
#include "machine/aci.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(c) do {if(!(c)){std::fprintf(stderr,"ACI output failure %d: %s\n",__LINE__,#c);std::exit(1);}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x10000,0xa5),output;
    std::string error;unsigned reads=0,payload_reads=0;
    uint32_t failed_address=UINT32_MAX;
    bool reject=false,mutate=false,reenter=false,rebind=false;
    std::vector<bool> irq;
    xlle::McpxAci aci{[this](const std::string& why){error=why;}};
    Fixture(bool observe=true) {
        CHECK(aci.initialize_kernel_audio_handoff());
        aci.set_time(0,1125000);
        aci.set_mem_access([this](uint32_t a,void* out,size_t n){
            ++reads;if(a>=0x2000)++payload_reads;
            if(a==failed_address || uint64_t(a)+n>ram.size())return false;
            std::memcpy(out,ram.data()+a,n);return true;
        });
        if(observe)aci.set_pcm_output([this](const uint8_t* p,size_t n){
            // The complete chunk is still uncommitted at the consumer boundary.
            CHECK(aci.consumed_pcm_words()*2==output.size());
            if(mutate)reg(0x11b,1,0);
            if(reenter)aci.set_frame(99);
            if(rebind)aci.set_mem_access({});
            if(reject || mutate || reenter || rebind)return false;
            output.insert(output.end(),p,p+n);return true;
        });
        aci.set_irq([this,observe](int line,bool level){CHECK(line==6);irq.push_back(level);
            if(level && observe)CHECK(aci.consumed_pcm_words()*2==output.size());},6);
    }
    void put(unsigned a,unsigned v){std::memcpy(ram.data()+a,&v,4);}
    void descriptor(unsigned index,unsigned address,unsigned words,unsigned flags=0x80000000) {
        put(0x1000+index*8,address);put(0x1004+index*8,words|flags);
    }
    std::vector<uint8_t> samples(unsigned address,unsigned words,unsigned seed) {
        std::vector<uint8_t> data(words*2);
        for(unsigned i=0;i<words;++i){int16_t sample=int16_t((i*113+seed*997)^0x80f3);
            data[i*2]=uint8_t(sample);data[i*2+1]=uint8_t(uint16_t(sample)>>8);}
        std::copy(data.begin(),data.end(),ram.begin()+address);return data;
    }
    void reg(unsigned off,unsigned width,unsigned value){aci.mmio_write(aci.kBase+off,width,value);}
    unsigned read(unsigned off,unsigned width=1){return aci.mmio_read(aci.kBase+off,width);}
    void run(unsigned lvi){reg(0x110,4,0x1000);reg(0x115,1,lvi);reg(0x11b,1,0x1d);}
};
static void append(std::vector<uint8_t>& a,const std::vector<uint8_t>& b){a.insert(a.end(),b.begin(),b.end());}
static void mixed_lifetimes() {
    for(unsigned life=0;life<3;++life) {
        Fixture f;auto a=f.samples(0x2ffc,768,life),b=f.samples(0x6000,256,life+4),c=f.samples(0x8000,1024,life+9);
        f.descriptor(0,0x2ffc,768);f.descriptor(1,0,0,0);f.descriptor(2,0x6000,256);f.descriptor(3,0x8000,1024,0xc0000000);
        const auto ram=f.ram;f.run(3);const unsigned initial_reads=f.reads;
        for(unsigned n=0;n<32;++n){CHECK(f.read(0x118,2)==768);CHECK(!f.read(0x114));}
        CHECK(f.reads==initial_reads && f.output.empty());f.aci.set_frame(1);
        CHECK(f.output==std::vector<uint8_t>(a.begin(),a.begin()+1024));CHECK(f.read(0x118,2)==256 && !f.aci.completed_descriptors());
        f.reg(0x11b,1,0x1c);f.aci.set_frame(2);CHECK(f.output.size()==1024 && f.read(0x118,2)==256);
        const unsigned paused_reads=f.reads;f.reg(0x11b,1,0x1d);f.reg(0x11b,1,0x19);
        CHECK(f.reads==paused_reads && !f.read(0x114) && f.read(0x118,2)==256);
        f.aci.set_frame(3);std::vector<uint8_t> expected=a;append(expected,b);
        CHECK(f.output==expected && f.aci.completed_descriptors()==3 && f.read(0x114)==3 && f.read(0x118,2)==1024);
        CHECK(f.irq==std::vector<bool>({true}));f.reg(0x116,2,0x0c);CHECK(f.irq==std::vector<bool>({true,false}));
        f.aci.set_frame(5);append(expected,c);CHECK(f.output==expected && f.aci.completed_descriptors()==4);
        CHECK((f.read(0x116,2)&15)==15 && f.read(0x114)==3 && !f.read(0x118,2));
        const unsigned halted_reads=f.reads;f.reg(0x115,1,3);f.aci.set_frame(6);
        CHECK(f.reads==halted_reads && f.output==expected);CHECK(f.ram==ram);
        auto d=f.samples(0xa000,512,life+18);f.descriptor(4,0xa000,512);f.reg(0x115,1,4);f.aci.set_frame(7);append(expected,d);
        CHECK(f.output==expected && f.aci.completed_descriptors()==5 && f.aci.consumed_pcm_words()==2560 && f.error.empty());
        f.reg(0x11b,1,0x1c);f.reg(0x11b,1,2);CHECK(!f.read(0x114) && !f.read(0x11a) && !f.read(0x118,2));
        CHECK((f.read(0x116,2)&15)==1 && f.read(0x11b)==0x1c);const auto count=f.output.size();f.aci.set_frame(10);CHECK(f.output.size()==count);
    }
}
static void ring_wrap_and_headless() {
    Fixture f;std::vector<uint8_t> expected;
    for(unsigned i=0;i<32;++i){auto bytes=f.samples(0x2000+i*64,16,i);append(expected,bytes);f.descriptor(i,0x2000+i*64,16);}
    f.run(31);f.aci.set_frame(1);CHECK(f.output==expected && f.aci.completed_descriptors()==32 && f.read(0x114)==31 && !f.read(0x11a));
    // New descriptor backing at the same ring index, after actual last-buffer halt.
    auto bytes=f.samples(0x8000,512,77);f.put(0x1800,0x8000);f.put(0x1804,0xc0000200);
    f.reg(0x110,4,0x1800);f.reg(0x115,1,0);f.aci.set_frame(2);append(expected,bytes);
    CHECK(f.output==expected && f.aci.completed_descriptors()==33 && !f.read(0x114) && f.read(0x11a)==1 && f.error.empty());
    Fixture headless(false);headless.samples(0x4000,512,19);headless.descriptor(0,0x4000,512,0);headless.run(0);headless.aci.set_frame(1);
    CHECK(headless.payload_reads==1 && headless.aci.consumed_pcm_words()==512 && headless.aci.completed_descriptors()==1 && headless.error.empty());
}
static void failure_ownership() {
    for(unsigned which=0;which<5;++which) {
        Fixture f;f.samples(0x4000,512,27);f.descriptor(0,0x4000,512);f.run(0);
        if(which==0)f.failed_address=0x4000;
        if(which==1)f.reject=true;if(which==2)f.mutate=true;if(which==3)f.reenter=true;if(which==4)f.rebind=true;
        f.aci.set_frame(1);CHECK(!f.error.empty() && f.output.empty() && !f.aci.consumed_pcm_words() && !f.aci.completed_descriptors());
        CHECK(f.read(0x118,2)==512 && !f.read(0x114) && f.read(0x11a)==1 && !(f.read(0x130,4)&64));
        const auto reads=f.reads;f.reg(0x11b,1,0x1d);f.reg(0x115,1,1);f.aci.set_frame(4);CHECK(f.reads==reads && f.output.empty());
    }
    for(unsigned which=0;which<4;++which) {
        Fixture f;f.descriptor(0,which==3?0xfffffffcu:0x4000,which==2?3:4,which==1?0x80010000u:0x80000000u);
        if(which==0)f.failed_address=0x1000;f.run(0);CHECK(!f.error.empty() && !f.read(0x118,2) && !f.aci.completed_descriptors() && f.output.empty());
    }
    Fixture next;auto bytes=next.samples(0x4000,4,99);next.descriptor(0,0x4000,4);next.descriptor(1,0x6000,4);next.failed_address=0x1008;
    next.run(1);next.aci.set_frame(1);CHECK(!next.error.empty() && next.output==bytes && next.aci.completed_descriptors()==1);
    CHECK(next.aci.consumed_pcm_words()==4 && !next.read(0x114) && next.read(0x11a)==1 && !next.read(0x118,2));
    CHECK((next.read(0x116,2)&8) && next.irq==std::vector<bool>({true}));
    Fixture wrap;wrap.reg(0x110,4,0xfffffffcu);wrap.reg(0x11b,1,1);CHECK(!wrap.error.empty() && !wrap.reads && !wrap.aci.completed_descriptors());
}
static void completion_reentry() {
    Fixture f;auto expected=f.samples(0x4000,512,51);append(expected,f.samples(0x8000,512,71));
    f.descriptor(0,0x4000,512);f.descriptor(1,0x8000,512);unsigned deliveries=0;
    f.aci.set_irq([&](int line,bool level){CHECK(line==6);if(!level)return;
        CHECK(f.aci.consumed_pcm_words()*2==f.output.size());
        ++deliveries;f.reg(0x116,2,0x0c);if(deliveries==1)f.reg(0x115,1,1);
    },6);
    f.run(0);f.aci.set_frame(1);CHECK(f.read(0x114)==1 && f.read(0x11a)==2 && f.read(0x118,2)==512);
    f.aci.set_frame(2);CHECK(f.output==expected && f.aci.completed_descriptors()==2 && deliveries==2 && f.error.empty());
}
static void other_stream_registers() {
    Fixture f;f.descriptor(0,0x4000,3,0);f.reg(0x120,4,0x1000);f.reg(0x12b,1,1);
    CHECK(f.read(0x128,2)==3 && f.read(0x12a)==1 && f.error.empty());
    f.aci.set_frame(2);CHECK(f.read(0x128,2)==3 && f.output.empty() && !f.aci.consumed_pcm_words());
}
int main(){mixed_lifetimes();ring_wrap_and_headless();failure_ownership();completion_reentry();other_stream_registers();
    std::puts("ACI output: real PCM bytes, partial/pause/resume, zero node, ring wrap/remap, headless reads and terminal ownership failures passed");}
