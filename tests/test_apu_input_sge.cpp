// Runtime source-table ownership, independently authored PCM and failure paths.
#include "machine/apu.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"input SGE FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x30000,0xa5);
    std::string error;
    bool reject=false,reenter=false,source_live=true;
    unsigned writes=0,source_reads=0;
    xlle::McpxApu apu{[this](const std::string& why){if(error.empty())error=why;}};
    explicit Fixture(bool configured=true,bool released=true) {
        CHECK(apu.initialize_kernel_audio_handoff());
        apu.set_mem_access([this](uint32_t a,void* p,size_t n){
            if(uint64_t(a)+n>ram.size())return false;
            if(a>=0x20000){if(!source_live)return false;++source_reads;}
            std::memcpy(p,ram.data()+a,n);return true;
        },[this](uint32_t a,const void* p,size_t n){
            if(reject || uint64_t(a)+n>ram.size())return false;
            if(reenter){reg(0x20804,1);return false;}
            std::memcpy(ram.data()+a,p,n);++writes;return true;
        });
        reg(0x1100,0x88);
        if(configured)reg(0x1138,0x7ff);
        reg(0x2030,0x10000);reg(0x202c,0x1000);
        if(released){reg(0x1510,0);reg(0x1100,0x100f);}
        reg(0x2000,15);
        CHECK(error.empty());
    }
    void reg(unsigned off,uint32_t value){apu.mmio_write(0xfe800000+off,4,value);}
    void word(unsigned at,uint32_t value){std::memcpy(ram.data()+at,&value,4);}
    void map(unsigned slot,unsigned page){reg(0x20804,slot);reg(0x20808,page);}
};
int main() {
    for(unsigned slot:{0u,17u,2046u}) {
        Fixture f;auto expected=f.ram;
        f.map(slot,0x20000);f.map(slot+1,0x22000);
        const uint32_t first=0x20000,second=0x22000;
        std::memcpy(expected.data()+0x10000+slot*8,&first,4);
        std::memcpy(expected.data()+0x10000+(slot+1)*8,&second,4);
        CHECK(f.error.empty() && f.ram==expected && f.writes==2);
        // Actual mono PCM16 source pull crosses two non-contiguous pages.
        const unsigned voice=0x1000+64*128;
        f.word(voice+4,0x50000000);f.word(voice+0x20,slot*4096);
        f.word(voice+0x54,1u<<21);f.word(voice+0x58,2047);f.word(voice+0x5c,4095);
        const int16_t a=8192,b=-16384,c=24576;
        std::memcpy(f.ram.data()+0x20ffe,&a,2);std::memcpy(f.ram.data()+0x22000,&b,2);
        std::memcpy(f.ram.data()+0x24002,&c,2);
        float samples[4]={};uint32_t count=0;bool ended=false;
        CHECK(f.apu.source_frames(64,samples,2,count,ended) && count==2 && !ended);
        CHECK(samples[0]==0.25f && samples[1]==0.25f && samples[2]==-0.5f && samples[3]==-0.5f);
        // A later publication changes the next actual fetch, without touching
        // the already produced samples or resetting the source cursor.
        f.map(slot+1,0x24000);
        CHECK(f.apu.source_frames(64,samples,1,count,ended) && count==1 && samples[0]==0.75f);
        f.word(voice+0x54,0);f.source_live=false;
        const auto reads=f.source_reads;
        CHECK(f.apu.source_frames(64,samples,1,count,ended) && count==0 && f.source_reads==reads);
        f.word(voice+0x54,1u<<21);
        CHECK(!f.apu.source_frames(64,samples,1,count,ended) && count==0 && f.source_reads==reads);
    }
    for(unsigned bad=0;bad<12;++bad) {
        Fixture f(bad!=0,bad!=1);const auto before=f.ram;
        if(bad==0 || bad==1)f.map(0,0x20000);
        if(bad==2)f.reg(0x20804,0x800);
        if(bad==3)f.reg(0x20808,0x20000);
        if(bad==4)f.map(0,0x20001);
        if(bad==5)f.map(0,0x4000000);
        if(bad==6){f.reject=true;f.map(0,0x20000);}
        if(bad==7){f.reenter=true;f.map(0,0x20000);}
        if(bad==8)f.apu.mmio_write(0xfe820805,2,0);
        if(bad==9){f.reg(0x2030,0x10004);f.map(0,0x20000);}
        if(bad==10)f.reg(0x21000,0); // Runtime input admission must not admit output methods.
        if(bad==11)(void)f.apu.mmio_read(0xfe820804,4);
        CHECK(!f.error.empty() && f.ram==before && !f.writes);
        const auto error=f.error;f.map(0,0x20000);
        CHECK(f.ram==before && !f.writes && f.error==error);
    }
    puts("PASS runtime input SGE isolation, real cross-page/remapped source, retirement and guarded failures");
}
