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
    std::string error;unsigned transfers=0;uint32_t fail_write=~0u;
    xlle::McpxApu apu{[this](const std::string& e){error=e;}};
    Fixture() {
        apu.set_mem_access(
            [this](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [this](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size() || a==fail_write)return false;std::memcpy(ram.data()+a,p,n);++transfers;return true;});
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
static void observed_ep_linear_inputs() {
    // Independent physical count24 snapshots, taken by the EP immediately
    // after EOL and before acknowledgement, for both X and P clear/set pairs.
    const uint32_t expected[]={0x400000,0x430405,0x46080a,0x490c0f,0x4c1014,0x4f1419,
        0x52181e,0x551c23,0x582028,0x5b242d,0x5e2832,0x612c37,0x64303c,0x673441,
        0x6a3846,0x6d3c4b,0x704050,0x734455,0x76485a,0x794c5f,0x7c5064,0x7f5469,0x82586e,0x855c73};
    for(unsigned target:{0xbc0u,0x2c00u})for(unsigned control:{0x59e0u,0x5be0u}) {
        Fixture f;f.apu.mmio_write(0xfe802048,4,0x1000);f.apu.mmio_write(0xfe8020dc,4,3);
        for(unsigned i=0;i<24;++i)for(unsigned b=0;b<4;++b) {
            f.scratch(0x2000+i*4+b)=uint8_t((0xbb400000+i*0x30405)>>(b*8));
            f.scratch(0x2100+i*4+b)=uint8_t(0x87u>>(b*8));
        }
        auto* words=f.apu.ep_word(0);CHECK(words);
        const auto ram=f.ram;
        const std::vector<uint32_t> gp(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
        const auto revision=f.apu.ep_program_revision();
        for(unsigned transfer=0;transfer<2;++transfer) {
            const uint32_t node[]={0x4200,control,24,target,0x2000+transfer*0x100};
            std::copy(std::begin(node),std::end(node),words+0x200);
            for(unsigned i=0;i<24;++i)words[target+i]=0x6b3000;
            const std::vector<uint32_t> before(words,words+0x3800);
            CHECK(f.apu.ep_write(0xffffd4,0x200) && f.apu.ep_write(0xffffd6,1));
            // Reads cannot perform the queued transfer or publish completion.
            unsigned value=0;for(unsigned i=0;i<16;++i) {
                CHECK(f.apu.ep_read(0xffffc5,value) && value==0);
                CHECK(std::equal(before.begin(),before.end(),words));
            }
            CHECK(f.apu.service_dsp_dma(xlle::ApuDspKind::Ep,2));
            CHECK(f.apu.ep_dma_nodes()==transfer+1 && !f.apu.gp_dma_nodes());
            CHECK(f.apu.ep_read(0xffffc5,value) && value==0x80);
            CHECK(f.apu.ep_read(0xffffd6,value) && value==0);
            for(unsigned i=0;i<0x3800;++i)CHECK(words[i]==
                (i>=target && i<target+24?(transfer?0x87:expected[i-target]):before[i]));
            CHECK(f.ram==ram && !f.transfers && std::equal(gp.begin(),gp.end(),f.apu.gp_word(0)));
            CHECK(f.apu.ep_program_revision()==revision+(target==0x2c00?transfer+1:0));
            CHECK(f.apu.ep_write(0xffffc5,0x80));
            for(unsigned i=0;i<100;++i)CHECK(f.apu.ep_read(0xffffc5,value) && value==0x80);
            CHECK(f.apu.dsp_work_pending(xlle::ApuDspKind::Ep) && !f.apu.dsp_dma_pending(xlle::ApuDspKind::Ep));
            CHECK(f.apu.service_dsp_acknowledgements(xlle::ApuDspKind::Ep) && f.apu.ep_read(0xffffc5,value) && value==0x80);
            CHECK(f.apu.service_dsp_acknowledgements(xlle::ApuDspKind::Ep) && f.apu.ep_read(0xffffc5,value) && value==0);
            CHECK(!f.apu.dsp_work_pending(xlle::ApuDspKind::Ep));
        }
    }
}
static void bit9_profile_boundaries() {
    // Shared implementation must not widen EP circular/Y/P output, FIFO or adjacent flags.
    for(bool ep:{false,true})for(uint32_t control:{0x5be0u,0x4bd0u,0x5be2u,0x4bd2u,
            0x5a00u,0x5a02u,0x47e0u,0x47d2u,0x4be0u,0x4be2u,0x5bd0u,0x5bd2u,0x5be1u,0x7be0u,0x15be0u}) {
        if(!ep && (control==0x5be0 || control==0x4bd0 || control==0x4bd2))continue;
        std::array<uint32_t,0x3800> memory{};std::string error;unsigned byte_calls=0,fifo_calls=0;
        xlle::ApuDspDma dma([&](uint32_t a)->uint32_t*{return a<memory.size()?&memory[a]:nullptr;},
            [&](uint32_t,void*,uint32_t,bool){++byte_calls;return true;},
            [&](unsigned,void*,uint32_t,bool){++fifo_calls;return true;},
            [&](const std::string& e){error=e;},{},{},ep?xlle::ApuDspKind::Ep:xlle::ApuDspKind::Gp);
        const uint32_t node[]={0x4020,control,4,ep && control==0x5be0?0x1840u:
            ep && control==0x5be2?0x2c00u:0x400u,28,0x2000,31};
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
static void cold_gp_circular_initial_cursor() {
    // Authored byte transport regression for the reached 4BD0 profile.
    // Distinguish reset-to-zero from modulo, include exact-size boundary,
    // multiple wraps, and the original 32-word / FF7D80 cursor shape.
    for(unsigned size:{11u,0x2b80u})for(unsigned initial:{size,size+3,0xff7d80u})
    for(unsigned count:{4u,32u}) {
        Fixture f;const unsigned base=0x100,bank=0x524;
        for(unsigned i=0;i<size;++i)f.scratch(base+i)=uint8_t(i*13+7);
        f.node(0x20,0x4020,2,false,bank,count,initial,size,base);
        f.word(0x21)=0x4bd0;
        for(unsigned i=0;i<count;++i)f.word(bank+i)=0xabcdef;
        const auto before=f.ram;f.go(0x20);
        CHECK(f.error.empty() && f.ram==before && f.transfers==0);
        for(unsigned i=0;i<count;++i) {
            uint32_t packed=0;
            for(unsigned b=0;b<4;++b)packed|=uint32_t(uint8_t(((i*4+b)%size)*13+7))<<(b*8);
            CHECK(f.word(bank+i)==packed>>8);
        }
        CHECK(f.word(0x24)==(count*4)%size && f.apu.gp_dma_nodes()==1);
        uint32_t status=0;CHECK(f.apu.gp_read(0xffffc5,status) && status==0x80);
    }
    // A failed physical read must not publish samples, cursor or completion.
    {
        Fixture f;f.node(0x20,0x4020,2,false,0x524,32,0xff7d80,0x2b80,0x10000);
        f.word(0x21)=0x4bd0;f.word(0x524)=0xabcdef;
        CHECK(f.apu.gp_write(0xffffd4,0x20) && f.apu.gp_write(0xffffd6,1));
        CHECK(!f.apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096));
        CHECK(f.word(0x24)==0xff7d80 && f.word(0x524)==0xabcdef && !f.apu.gp_dma_nodes());
    }
    // Normalization does not qualify output, adjacent flags, or EP circular.
    for(bool ep:{false,true})for(unsigned control:{0x4bd0u,0x49d0u,0x4bd2u,0x4bc0u}) {
        if(!ep && control==0x4bd0)continue;
        std::array<uint32_t,0x3800> memory{};unsigned reads=0;std::string error;
        xlle::ApuDspDma dma([&](uint32_t a)->uint32_t*{return a<memory.size()?&memory[a]:nullptr;},
            [&](uint32_t,void*,uint32_t,bool){++reads;return true;},
            [](unsigned,void*,uint32_t,bool){return false;},
            [&](const std::string& e){error=e;},{},{},ep?xlle::ApuDspKind::Ep:xlle::ApuDspKind::Gp);
        const uint32_t node[]={0x4020,control,32,0x524,0xff7d80,0x100,0x2b7f};
        std::copy(std::begin(node),std::end(node),memory.begin()+0x20);const auto before=memory;
        CHECK(dma.write(0xffffd4,0x20) && dma.write(0xffffd6,1) && !dma.service(4096));
        CHECK(!error.empty() && !reads && !dma.nodes() && memory==before);
    }
}
static void cold_gp_stereo_fifo() {
    // Literal PCM16 bytes from two distinct signed24 channels, truncating
    // the low8 bits. Cross the real FIFO ring end after three bytes.
    const uint32_t left[]={0x123456,0xabcdef,0x800001,0x7fffff};
    const uint32_t right[]={0xfedcba,0x010203,0x0000ff,0xff0000};
    const uint8_t expected_bytes[]={0x34,0x12,0xdc,0xfe,0xcd,0xab,0x02,0x01,
                                   0x00,0x80,0x00,0x00,0xff,0x7f,0x00,0xff};
    for(bool fail:{false,true}) {
        Fixture f;
        f.apu.mmio_write(0xfe802044,4,0x1000);f.apu.mmio_write(0xfe8020d8,4,3);
        f.apu.mmio_write(0xfe803024,4,0);f.apu.mmio_write(0xfe803028,4,0x100);
        f.apu.mmio_write(0xfe80302c,4,0xfd);
        for(unsigned i=0;i<32;++i){f.word(0xc00+i)=left[i%4];f.word(0xc20+i)=right[i%4];}
        // Only four descriptor words belong to FIFO; sentinels must survive.
        const uint32_t node[]={0x4000,0x080403,0x201,0xc00,0x6abcde,0x654321,0x612345};
        for(unsigned i=0;i<7;++i)f.word(0x20+i)=node[i];
        const std::vector<uint32_t> banks(f.apu.gp_word(0),f.apu.gp_word(0)+0x3800);
        auto expected=f.ram;
        for(unsigned i=0;i<(fail?3u:128u);++i)expected[0x8000+(0xfd+i)%256]=expected_bytes[i%16];
        if(fail)f.fail_write=0x8000;
        CHECK(f.apu.gp_write(0xffffd4,0x20) && f.apu.gp_write(0xffffd6,1));
        CHECK(f.apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096)==!fail);
        CHECK(f.ram==expected && std::equal(banks.begin(),banks.end(),f.apu.gp_word(0)));
        CHECK(f.apu.RegFileMmio::mmio_read(0xfe80302c,4)==(fail?0xfdu:0x7du));
        CHECK(f.apu.gp_dma_nodes()==(fail?0u:1u));
        if(fail)CHECK(f.apu.faulted());
        else {uint32_t v=0;CHECK(f.apu.gp_read(0xffffc5,v) && v==0x80);
            CHECK(f.apu.gp_read(0xffffd6,v) && v==0);}
    }
    for(unsigned bad=0;bad<8;++bad) {
        std::array<uint32_t,0x3800> memory{};unsigned calls=0;std::string error;
        const bool ep=bad==7;
        xlle::ApuDspDma dma([&](uint32_t a)->uint32_t*{return a<memory.size()?&memory[a]:nullptr;},
            [](uint32_t,void*,uint32_t,bool){return false;},
            [&](unsigned,void*,uint32_t,bool){++calls;return true;},
            [&](const std::string& e){error=e;},{},{},ep?xlle::ApuDspKind::Ep:xlle::ApuDspKind::Gp);
        uint32_t node[]={0x4000,0x080403,0x201,0xc00};
        if(bad==0)node[1]|=0x20; // FIFO1
        if(bad==1)node[1]&=~2u; // input
        if(bad==2)node[1]|=0x200; // adjacent bit9
        if(bad==3)node[1]+=0x4000; // stride33
        if(bad==4)node[2]=0x211; //33 frames
        if(bad==5)node[2]=0x202; //three channels
        if(bad==6)node[3]=0xc01; //adjacent source
        std::copy(std::begin(node),std::end(node),memory.begin()+0x20);const auto before=memory;
        CHECK(dma.write(0xffffd4,0x20) && dma.write(0xffffd6,1) && !dma.service(4096));
        CHECK(!calls && !dma.nodes() && !error.empty() && memory==before);
    }
}
int main() {
    cold_gp_stereo_fifo();cold_gp_circular_initial_cursor();observed_gp_idle_controls();paired_bit9_inputs();paired_bit9_circular_outputs();observed_ep_linear_inputs();bit9_profile_boundaries();
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
