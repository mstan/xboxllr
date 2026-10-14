#include <array>
#include <algorithm>
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
static void paired_bit9_inputs() {
    // Physical representatives were count4. Count32 is an offline extent
    // regression for the existing transport contract, not a silicon measurement.
    for(bool circular:{false,true})for(unsigned count:{4u,32u})
    for(unsigned bank:circular?std::vector<unsigned>{0x1c00}:std::vector<unsigned>{0x400,0x1c00,0x2c00}) {
        std::vector<uint32_t> reference;
        for(bool bit9:{false,true}) {
            Fixture f;const unsigned base=0x2000,offset=circular?28:0,size=32;
            const uint32_t source[]={0xee244400,0x99305500,0x88316600,0x00000087};
            for(unsigned i=0;i<(circular?8u:count);++i)
                for(unsigned b=0;b<4;++b)f.scratch(base+i*4+b)=uint8_t(source[i%4]>>(b*8));
            const uint32_t control=(circular?0x49d0:0x59e0)|(bit9?0x200:0);
            const uint32_t node[]={0x4020,control,count,bank,circular?offset:base,base,size-1};
            for(unsigned i=0;i<(circular?7u:5u);++i)f.word(0x20+i)=node[i];
            for(unsigned i=0;i<count;++i)f.word(bank+i)=0x6b0000+i;
            const auto before=f.ram;f.go(0x20);
            CHECK(f.error.empty() && f.apu.gp_dma_nodes()==1 && f.ram==before);
            uint32_t status=0;CHECK(f.apu.gp_read(0xffffc5,status) && status==0x80);
            CHECK(f.apu.gp_read(0xffffd6,status) && status==0);
            const unsigned cursor=circular?(offset+count*4)%size:base;
            CHECK(f.word(0x24)==cursor);
            for(unsigned i=0;i<count;++i) {
                const unsigned source_index=circular?((offset+i*4)%size)/4:i;
                const uint32_t expected=circular?source[source_index%4]>>8:source[i%4]&0xffffff;
                CHECK(f.word(bank+i)==expected);
            }
            const std::vector<uint32_t> actual(f.apu.gp_word(bank),f.apu.gp_word(bank)+count);
            if(bit9)CHECK(actual==reference);else reference=actual;
            CHECK(f.apu.gp_write(0xffffc5,0x80) && f.apu.gp_read(0xffffc5,status) && status==0);
        }
    }
}
static void paired_bit9_circular_outputs() {
    // Physical count4 evidence; count32 remains an offline transport regression.
    // This authored ring also crosses noncontiguous SGE pages to preserve the
    // existing transport ownership contract independently of bit9 admission.
    for(unsigned count:{4u,32u}) {
        std::vector<uint8_t> clear_ram;
        for(bool bit9:{false,true}) {
            Fixture f;const unsigned base=0x2ff0,offset=28,size=32,bank=0x1c00;
            const uint32_t source[]={0x123456,0xabcdef,0x654321,0xfedcba};
            const uint8_t literal_bytes[]={0,0x56,0x34,0x12,0,0xef,0xcd,0xab,
                                          0,0x21,0x43,0x65,0,0xba,0xdc,0xfe};
            for(unsigned i=0;i<count;++i)f.word(bank+i)=source[i%4];
            const std::vector<uint32_t> source_before(f.apu.gp_word(bank),f.apu.gp_word(bank)+count);
            f.node(0x20,0x4020,2,true,bank,count,offset,size,base);
            if(bit9)f.word(0x21)|=0x200;
            CHECK(f.word(0x21)==(bit9?0x4bd2u:0x49d2u));
            auto expected=f.ram;
            for(unsigned at=0;at<count*4;++at) {
                const unsigned logical=base+(offset+at)%size;
                // Literal independently authored destinations for SGE2/SGE3.
                const unsigned physical=logical<0x3000?0x6000+logical-0x2000:0x5000+logical-0x3000;
                expected[physical]=literal_bytes[at%16];
            }
            const auto descriptor_before=std::array<uint32_t,7>{f.word(0x20),f.word(0x21),f.word(0x22),
                f.word(0x23),f.word(0x24),f.word(0x25),f.word(0x26)};
            f.go(0x20);
            CHECK(f.error.empty() && f.apu.gp_dma_nodes()==1 && f.ram==expected);
            CHECK(std::equal(source_before.begin(),source_before.end(),f.apu.gp_word(bank)));
            for(unsigned i=0;i<7;++i)CHECK(f.word(0x20+i)==(i==4?(offset+count*4)%size:descriptor_before[i]));
            uint32_t status=0;CHECK(f.apu.gp_read(0xffffc5,status) && status==0x80);
            CHECK(f.apu.gp_read(0xffffd6,status) && status==0);
            if(bit9)CHECK(f.ram==clear_ram);else clear_ram=f.ram;
            CHECK(f.apu.gp_write(0xffffc5,0x80) && f.apu.gp_read(0xffffc5,status) && status==0);
        }
    }
}
static void observed_gp_idle_controls() {
    Fixture f;uint32_t status=0xffffffff;
    CHECK(f.apu.gp_read(0xffffd6,status) && status==0);
    CHECK(f.apu.gp_read(0xffffc5,status) && status==0);
    const auto ram=f.ram;
    CHECK(f.apu.gp_write(0xffffd6,2) && f.apu.gp_read(0xffffd6,status) && status==0);
    CHECK(f.apu.gp_write(0xffffd6,3) && f.apu.gp_read(0xffffd6,status) && status==8);
    CHECK(f.apu.gp_write(0xffffd6,4) && f.apu.gp_read(0xffffd6,status) && status==0);
    CHECK(!f.apu.dsp_dma_pending(xlle::ApuDspKind::Gp) && !f.apu.gp_dma_nodes() && f.ram==ram);
    CHECK(f.apu.gp_read(0xffffc5,status) && status==0);
}
static void bit9_profile_boundaries() {
    // Shared implementation must not widen EP, other output, FIFO or adjacent flags.
    for(bool ep:{false,true})for(uint32_t control:{0x5be0u,0x4bd0u,0x5be2u,0x4bd2u,
            0x5a00u,0x5a02u,0x47e0u,0x47d2u,0x4be0u,0x4be2u,0x5bd0u,0x5bd2u,0x5be1u,0x7be0u,0x15be0u}) {
        if(!ep && (control==0x5be0 || control==0x4bd0 || control==0x4bd2))continue;
        std::array<uint32_t,0x3800> memory{};std::string error;unsigned byte_calls=0,fifo_calls=0;
        xlle::ApuDspDma dma([&](uint32_t a)->uint32_t*{return a<memory.size()?&memory[a]:nullptr;},
            [&](uint32_t,void*,uint32_t,bool){++byte_calls;return true;},
            [&](unsigned,void*,uint32_t,bool){++fifo_calls;return true;},
            [&](const std::string& e){error=e;},{},{},ep?xlle::ApuDspKind::Ep:xlle::ApuDspKind::Gp);
        const uint32_t node[]={0x4020,control,4,0x400,28,0x2000,31};
        std::copy(std::begin(node),std::end(node),memory.begin()+0x20);const auto before=memory;
        CHECK(dma.write(0xffffd4,0x20) && dma.write(0xffffd6,1));
        CHECK(!dma.service(4096) && !error.empty() && !dma.nodes());
        CHECK(memory==before && byte_calls==0 && fifo_calls==0);
        CHECK(!dma.write(0xffffd6,1));
    }
    // Admitted P input still requires the executable publication owner's consent.
    std::array<uint32_t,0x3800> memory{};std::string error;unsigned reads=0,publications=0;
    xlle::ApuDspDma dma([&](uint32_t a)->uint32_t*{return a<memory.size()?&memory[a]:nullptr;},
        [&](uint32_t,void* out,uint32_t n,bool output){CHECK(!output);++reads;std::memset(out,0x12,n);return true;},
        [](unsigned,void*,uint32_t,bool){CHECK(false);return false;},
        [&](const std::string& e){error=e;},[&]{++publications;return false;});
    const uint32_t node[]={0x4020,0x5be0,4,0x2c00,0x2000};
    std::copy(std::begin(node),std::end(node),memory.begin()+0x20);const auto before=memory;
    CHECK(dma.write(0xffffd4,0x20) && dma.write(0xffffd6,1) && !dma.service(4096));
    CHECK(reads==1 && publications==1 && !dma.nodes() && memory==before);
    CHECK(error.find("executable DMA publication was rejected")!=std::string::npos);
    CHECK(!dma.read(0xffffc5,memory[0]) && !dma.write(0xffffd6,1));
    // Existing count/bank/physical-source guards remain active for exact profiles.
    for(unsigned failure=0;failure<4;++failure) {
        Fixture f;const uint32_t node[]={0x4020,0x5be0,failure==0?0u:failure==1?4097u:4u,
            failure==2?0xffeu:0x400u,failure==3?0x10000u:0x2000u};
        for(unsigned i=0;i<5;++i)f.word(0x20+i)=node[i];const auto before=f.ram;
        CHECK(f.apu.gp_write(0xffffd4,0x20) && f.apu.gp_write(0xffffd6,1));
        CHECK(!f.apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096) && f.apu.faulted());
        CHECK(!f.apu.gp_dma_nodes() && f.ram==before && f.word(0x400)==0);
    }
}
int main() {
    observed_gp_idle_controls();paired_bit9_inputs();paired_bit9_circular_outputs();bit9_profile_boundaries();
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
        CHECK(f.apu.gp_write(0xffffd6,2));CHECK(f.apu.gp_read(0xffffd6,value) && value==0);
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
        if(kind==1)f.word(0x21)|=0x201; // Unknown bit0 remains rejected with bit9.
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
