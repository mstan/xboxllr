// Published FIFO ring/byte contract exercised through real DSP descriptors and
// the maintained physical SGE owner. No guest completion or PCM is substituted.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "machine/apu.hpp"
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"GP FIFO FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x20000,0xa5);
    std::string error;
    xlle::McpxApu apu{[this](const std::string& e){error=e;}};
    static constexpr std::array<uint32_t,3> pages={0x13000,0x11000,0x15000};
    Fixture() {
        apu.set_mem_access(
            [this](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [this](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);return true;});
        for(unsigned i=0;i<pages.size();++i)std::memcpy(ram.data()+0x1800+i*8,&pages[i],4);
        const uint32_t scratch=0x8000;std::memcpy(ram.data()+0x1000,&scratch,4);
        reg(0x2040,0x1000);reg(0x20d4,0); // different scratch address space
        reg(0x2044,0x1800);reg(0x20d8,2);
    }
    static unsigned physical(unsigned a){CHECK((a>>12)<pages.size());return pages[a>>12]+(a&4095);}
    void reg(unsigned a,unsigned v){apu.mmio_write(0xfe800000+a,4,v);}
    unsigned reg(unsigned a){return apu.mmio_read(0xfe800000+a,4);}
    uint32_t& word(unsigned a){auto* p=apu.gp_word(a);CHECK(p);return *p;}
    static unsigned registers(unsigned index,bool output){return (output?0x3024:0x3064)+index*16;}
    void ring(unsigned index,bool output,unsigned base,unsigned end,unsigned cur) {
        const auto r=registers(index,output);reg(r,base);reg(r+4,end);reg(r+8,cur);
    }
    void node(unsigned index,bool output,unsigned format,unsigned count) {
        // Scratch-only descriptor fields deliberately unsuitable for a ring;
        // FIFO must use its register owner and must not write these fields back.
        const uint32_t fields[]={0x4000,0x4000|(format<<10)|(index<<5)|16|(output?2u:0u),count,0x1880,0xabcdef,0xffffff,0};
        for(unsigned i=0;i<7;++i)word(0x20+i)=fields[i];
    }
    bool go(){CHECK(apu.gp_write(0xffffd4,0x20));return apu.gp_write(0xffffd6,1) && apu.service_dsp_dma(xlle::ApuDspKind::Gp,4096);}
};
int main() {
    unsigned cases=0;
    for(bool output:{false,true})for(unsigned index=0;index<(output?4u:2u);++index)
    for(unsigned format:{1u,2u,6u})for(unsigned scenario=0;scenario<6;++scenario) {
        Fixture f;
        const unsigned base=0x100,end=scenario==5?0x200:0x2100;
        const unsigned cur=scenario==0?base:scenario==1?4093:scenario==2?end-1:
                           scenario==3?end-8:scenario==4?base-1:base+127;
        const unsigned count=scenario==5?300:4,width=format==1?2:4;
        f.ring(index,output,base,end,cur);f.node(index,output,format,count);
        // Literal independent lane goldens from the published DSP contract.
        const uint8_t pcm16[]={0x34,0x12,0xcd,0xab};
        const uint8_t msb[]={0,0x56,0x34,0x12,0,0xef,0xcd,0xab};
        const uint8_t lsb[]={0x56,0x34,0x12,0,0xef,0xcd,0xab,0};
        const uint8_t* golden=format==1?pcm16:format==2?msb:lsb;
        if(output)for(unsigned i=0;i<count;++i)f.word(0x1880+i)=(i&1)?0xabcdef:0x123456;
        else for(unsigned a=base;a<end;++a)f.ram[f.physical(a)]=uint8_t(a*13+7);
        const auto before=f.ram;auto expected=before;std::vector<uint32_t> input(count);
        const unsigned start=cur<base?base:cur;
        for(unsigned b=0;b<count*width;++b) {
            const unsigned a=base+(start-base+b)%(end-base),physical=f.physical(a);
            if(output)expected[physical]=golden[b%(width*2)];
            else input[b/width]|=uint32_t(before[physical])<<((b%width)*8);
        }
        CHECK(f.go() && f.error.empty() && f.apu.gp_dma_nodes()==1);
        CHECK(f.ram==expected); // verifies all non-FIFO RAM and scratch unchanged
        if(!output)for(unsigned i=0;i<count;++i)
            CHECK(f.word(0x1880+i)==(format==1?input[i]<<8:format==2?input[i]>>8:input[i]&0xffffff));
        const unsigned final=base+(start-base+count*width)%(end-base);
        CHECK(f.reg(f.registers(index,output)+8)==final);
        for(bool other_output:{false,true})for(unsigned other=0;other<(other_output?4u:2u);++other)
            if(other!=index || other_output!=output)CHECK(!f.reg(f.registers(other,other_output)+8));
        CHECK(f.word(0x24)==0xabcdef && f.word(0x25)==0xffffff && !f.word(0x26));
        uint32_t status;
        for(unsigned i=0;i<8;++i)CHECK(f.apu.gp_read(0xffffd6,status) && status==0);
        CHECK(f.apu.gp_read(0xffffc5,status) && status==0x80);
        CHECK(f.reg(f.registers(index,output)+8)==final && f.ram==expected);
        ++cases;
    }
    // Bad rings, unknown directions/formats and physical failures do not earn
    // node completion or CUR progress. A late page fault may retain real bytes.
    for(unsigned kind=0;kind<12;++kind) {
        Fixture f;f.ring(0,true,0x100,0x2100,4093);f.node(0,true,2,4);
        for(unsigned i=0;i<4;++i)f.word(0x1880+i)=0x123456;
        if(kind==0)f.reg(0x302c,0x2100);
        if(kind==1)f.reg(0x3028,0x100);
        if(kind==2)f.reg(0x3024,0x101);
        if(kind==3)f.reg(0x3028,0x1002100);
        if(kind==4)f.node(2,false,2,4);
        if(kind==5)f.node(4,true,2,4);
        if(kind==6)f.node(0,true,1,3);
        if(kind==7)f.word(0x21)|=0x200;
        if(kind==8)f.reg(0x20d8,0); // first page copied, next page rejected
        if(kind==9){const uint32_t bad=0x20000;std::memcpy(f.ram.data()+0x1800,&bad,4);}
        if(kind==10){const uint32_t bad=0x13001;std::memcpy(f.ram.data()+0x1800,&bad,4);}
        if(kind==11)f.reg(0x2044,0xfffffffdu);
        // Invalid base programming can fault before GO. Inspect retained
        // owner state here without asking a faulted device for guest readback.
        const auto before=f.ram;const auto cursor=f.apu.xlle::RegFileMmio::mmio_read(0xfe80302c,4);
        if(kind==2)CHECK(f.apu.faulted() && !f.error.empty() && !f.apu.gp_dma_nodes());
        else CHECK(!f.go() && !f.error.empty() && !f.apu.gp_dma_nodes());
        // Device reads fail closed after a fault; inspect owner via the base
        // register storage only in this negative fixture, not a production API.
        CHECK(f.apu.xlle::RegFileMmio::mmio_read(0xfe80302c,4)==cursor);
        CHECK(f.word(0x24)==0xabcdef);
        if(kind==8){CHECK(f.ram!=before);CHECK(f.ram[f.physical(4093)]==0);}
        else CHECK(f.ram==before);
    }
    // Repeated real transfers retain their cursor, consume no scratch storage,
    // and require real W1C/STOP ownership between commands.
    for(unsigned lifetime=0;lifetime<16;++lifetime) {
        Fixture f;f.ring(0,true,0x100,0x200,0x100);
        for(unsigned transfer=0;transfer<129;++transfer) {
            f.node(0,true,6,2);f.word(0x1880)=transfer;f.word(0x1881)=lifetime;
            CHECK(f.go());CHECK(f.apu.gp_write(0xffffc5,0x80));CHECK(f.apu.gp_write(0xffffd6,2));
            CHECK(f.reg(0x302c)==0x100+((transfer+1)*8)%256);
        }
        CHECK(f.apu.gp_dma_nodes()==129 && f.error.empty());
    }
    std::printf("PASS: %u FIFO cases, 12 guards, 16 lifetimes/2064 real transfers; physical bytes, lane packing, independent CUR and completion ownership\n",cases);
}
