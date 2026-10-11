#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "machine/apu.hpp"
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"GP DMA FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x10000,0xa5);
    std::string error;unsigned transfers=0;
    xlle::McpxApu apu{[this](const std::string& e){error=e;}};
    Fixture() {
        apu.set_mem_access(
            [this](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [this](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);++transfers;return true;});
        for(unsigned i=0;i<4;++i){const uint32_t page=0x8000-i*0x1000;std::memcpy(ram.data()+0x1000+i*8,&page,4);}
        apu.mmio_write(0xfe802040,4,0x1000);apu.mmio_write(0xfe8020d4,4,3);
    }
    uint8_t& scratch(unsigned a){return ram[(0x8000-(a>>12)*0x1000)+(a&4095)];}
    uint32_t& word(unsigned a){auto* p=apu.gp_word(a);CHECK(p);return *p;}
    void node(unsigned at,unsigned next,unsigned format,bool output,unsigned src,unsigned count,unsigned offset,unsigned size,unsigned base=4093) {
        const uint32_t n[]={next,0x4000|(format<<10)|(14<<5)|16|(output?2u:0u),count,src,offset,base,size-1};
        for(unsigned i=0;i<7;++i)word(at+i)=n[i];
    }
    void go(unsigned node){CHECK(apu.gp_write(0xffffd4,node));CHECK(apu.gp_write(0xffffd6,1));CHECK(apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096));}
};
int main() {
    // Published hardware byte examples, independent of native/xemu packing.
    for(unsigned format:{2u,6u})for(unsigned bank:{0x80u,0x1480u,0x1880u,0x2880u}) {
        Fixture f;f.word(bank)=0x123456;f.word(bank+1)=0xabcdef;
        f.node(0x20,0x4000,format,true,bank,2,6,11);f.go(0x20);
        const uint8_t msb[]={0,0x56,0x34,0x12,0,0xef,0xcd,0xab};
        const uint8_t lsb[]={0x56,0x34,0x12,0,0xef,0xcd,0xab,0};
        for(unsigned i=0;i<8;++i)CHECK(f.scratch(4093+(6+i)%11)==(format==2?msb:lsb)[i]);
        CHECK(f.word(0x24)==3 && f.apu.gp_dma_nodes()==1);
        uint32_t value;for(unsigned i=0;i<8;++i)CHECK(f.apu.gp_read(0xffffd6,value) && value==0);
        CHECK(f.apu.gp_read(0xffffc5,value) && value==0x80);
        CHECK(f.apu.gp_write(0xffffc5,0x80));CHECK(f.apu.gp_read(0xffffc5,value) && !value);
        CHECK(f.apu.gp_write(0xffffd6,2));CHECK(f.apu.gp_read(0xffffd6,value) && value==32);
        f.word(bank)=f.word(bank+1)=0;
        // Nonzero padding must be ignored on input in the correct byte lane.
        f.scratch(4093+(6+(format==2?0:3))%11)=0xff;
        f.node(0x20,0x4000,format,false,bank,2,6,11);f.go(0x20);
        CHECK(f.word(bank)==0x123456 && f.word(bank+1)==0xabcdef && f.word(0x24)==3);
        CHECK(f.apu.gp_dma_nodes()==2 && f.error.empty());
    }
    {
        Fixture f;f.word(0x80)=0x123456;f.node(0x20,0x40,2,true,0x80,1,0,4);
        f.node(0x40,0x4000,2,false,0x1880,1,0,4);
        CHECK(f.apu.gp_write(0xffffd6,3));const auto before=f.ram;
        f.go(0x20);CHECK(!f.apu.gp_dma_nodes() && f.ram==before);
        uint32_t value;CHECK(f.apu.gp_read(0xffffd6,value) && value==24);
        CHECK(f.apu.gp_read(0xffffc5,value) && !value);
        CHECK(f.apu.gp_write(0xffffd6,4) && f.apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096));CHECK(f.word(0x1880)==0x123456 && f.apu.gp_dma_nodes()==2);
    }
    // Host-visible GP apertures and DSP storage refer to the same 24-bit words.
    for(unsigned a:{0u,0xfffu,0x1400u,0x17ffu,0x1800u,0x1fffu,0x2800u,0x37ffu}) {
        Fixture f;f.apu.mmio_write(0xfe830000+a*4,4,0xff123456);
        CHECK(f.error.empty() && f.word(a)==0x123456);
        f.word(a)=0x987654;CHECK(f.apu.mmio_read(0xfe830000+a*4,4)==0x987654);
    }
    // Bad descriptors, banks and SGE pages cannot report a completed transfer.
    for(unsigned kind=0;kind<9;++kind) {
        Fixture f;f.node(0x20,0x4000,2,true,0x80,2,0,8);
        if(kind==0)f.word(0x21)|=1;
        if(kind==1)f.word(0x21)|=0x200;
        if(kind==2)f.word(0x21)&=~0x4000u;
        if(kind==3)f.word(0x22)=4097;
        if(kind==4)f.word(0x23)=0x1000;
        if(kind==5)f.word(0x23)=0x17ff;
        if(kind==6)f.word(0x24)=8;
        if(kind==7){const uint32_t bad=0x10000;std::memcpy(f.ram.data()+0x1000,&bad,4);}
        if(kind==8)f.apu.mmio_write(0xfe8020d4,4,0); // first word straddles absent next page
        const auto before=f.ram;
        CHECK(f.apu.gp_write(0xffffd4,0x20));CHECK(f.apu.gp_write(0xffffd6,1) && !f.apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096));
        CHECK(!f.error.empty() && !f.apu.gp_dma_nodes());
        if(kind!=8)CHECK(f.ram==before); // a later page fault may follow real partial bytes
    }
    for(unsigned lifetime=0;lifetime<64;++lifetime) {
        Fixture f;f.word(0x80)=lifetime;f.node(0x20,0x4000,6,true,0x80,1,0,4);f.go(0x20);
        CHECK(f.scratch(4093)==lifetime && f.apu.gp_dma_nodes()==1);
    }
    std::puts("PASS: GP byte-lane goldens, real SGE/page/ring transport, banks, linked/frozen work, status ownership and 64 lifetimes");
}
