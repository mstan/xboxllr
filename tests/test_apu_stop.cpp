// Static-buffer stop owns actual source state, status publication and IRQ order.
// No assertion about the still-unqualified notifier timestamp/envelope payload.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "machine/apu.hpp"
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"APU STOP FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    static constexpr unsigned voice=71,table=0x1000,v=table+voice*128,notify=0x8000+16*(2+voice*4),source=0x18000;
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x20000,0);
    std::string error;unsigned audio_reads=0,irq_edges=0,reject=0;
    std::vector<uint32_t> writes;
    xlle::McpxApu apu{[this](const std::string& e){error=e;}};
    void put(unsigned a,uint32_t value){std::memcpy(ram.data()+a,&value,4);}
    uint32_t word(unsigned a){uint32_t n;std::memcpy(&n,ram.data()+a,4);return n;}
    void command(unsigned a,uint32_t value){apu.mmio_write(0xfe800000+a,4,value);}
    Fixture() {
        apu.set_mem_access([this](uint32_t a,void* p,size_t n){
            if(uint64_t(a)+n>ram.size())return false;
            if(a>=source && a<source+4096)++audio_reads;
            std::memcpy(p,ram.data()+a,n);return true;
        },[this](uint32_t a,const void* p,size_t n){
            if(uint64_t(a)+n>ram.size() || a==reject)return false;
            writes.push_back(a);std::memcpy(ram.data()+a,p,n);return true;
        });
        command(0x202c,table);command(0x2030,0x17000);put(0x17000,source);
        command(0x115c,0x8000);command(0x202f8,voice);
        command(0x20304,0x52000000); // ordinary PCM16, mono, loop
        command(0x203dc,63);command(0x2054,0xffff);
        command(0x20120,0x1ffff);command(0x20124,voice);command(0x1004,0x61);
        std::memset(ram.data()+notify,0xa5,64);ram[notify+15]=0x80;
        apu.set_irq([this](int line,bool level){
            CHECK(line==5);++irq_edges;
            if(level)CHECK(!(word(v+0x54)&(1u<<21)) && ram[notify+15]==1);
        });
        for(unsigned i=0;i<64;++i){const int16_t sample=8192;std::memcpy(ram.data()+source+i*2,&sample,2);}
    }
};
int main() {
    // Buffered resampler history cannot survive a commanded stop and rebind.
    for(unsigned lifetime=0;lifetime<32;++lifetime) {
        Fixture f;float samples[64];uint32_t count=0;bool end=false;
        CHECK(f.apu.resample_source(f.voice,samples,32,count,end) && count==32 && !end);
        const auto before=f.ram;const unsigned reads=f.audio_reads;f.writes.clear();
        f.command(0x20128,f.voice);CHECK(f.error.empty() && f.apu.voice_off_methods()==1);
        CHECK(f.irq_edges==1 && f.writes==std::vector<uint32_t>({f.v+0x54,f.notify+15}));
        auto expected=before;uint32_t state=f.word(f.v+0x54);std::memcpy(expected.data()+f.v+0x54,&state,4);
        expected[f.notify+15]=1;CHECK(f.ram==expected);
        CHECK(f.apu.resample_source(f.voice,samples,32,count,end) && !count && f.audio_reads==reads);
        f.command(0x1000,0x60);CHECK(f.irq_edges==2 && f.apu.mmio_read(0xfe801000,4)==0);
        // Caller has retired the inactive list entry and supplies a new buffer.
        f.command(0x2054,0xffff);f.command(0x203d8,0);
        for(unsigned i=0;i<64;++i){const int16_t sample=-8192;std::memcpy(f.ram.data()+f.source+i*2,&sample,2);}
        f.ram[f.notify+15]=0x80;f.command(0x20124,f.voice);
        CHECK(f.apu.resample_source(f.voice,samples,32,count,end) && count==32);
        for(unsigned i=0;i<64;++i)CHECK(samples[i]<-0.20f); // no old positive tail
        f.command(0x20128,f.voice);CHECK(f.apu.voice_off_methods()==2 && f.irq_edges==3);
    }
    // No completion/IRQ on invalid ownership or a failed state/status DMA.
    for(unsigned kind=0;kind<8;++kind) {
        Fixture f;const auto before=f.ram;
        if(kind<3)f.command(0x20304,0x52000000|(1u<<(kind==0?24:kind==1?22:21)));
        if(kind==3)f.command(0x115c,0xfffffff0);
        if(kind==4)f.command(0x115c,0x30000);
        if(kind==5)f.reject=f.v+0x54;
        if(kind==6)f.reject=f.notify+15;
        const auto configured=f.ram;f.writes.clear();
        f.command(0x20128,kind==7?0x10000|f.voice:f.voice);
        CHECK(!f.error.empty() && !f.irq_edges && !f.apu.voice_off_methods());
        CHECK(f.ram[f.notify+15]==before[f.notify+15]);
        if(kind!=6)CHECK(f.ram==configured);
        else CHECK(!(f.word(f.v+0x54)&(1u<<21))); // real stop preceded failed status publication
    }
    // The direct device, independently of the strict reference wrapper, must
    // reject reads and every byte lane/crossing access to the unknown fence.
    // Qualified base-class calls below only inspect the stored bytes in tests.
    struct Access {int delta,size;};
    const Access accesses[]={{-3,4},{-2,4},{-1,2},{-1,4},
        {0,1},{0,2},{0,4},{1,1},{1,2},{1,4},
        {2,1},{2,2},{2,4},{3,1},{3,2},{3,4}};
    for(unsigned field:{0x1510u,0x1324u,0x1334u}) {
        for(const auto& access:accesses)for(bool write:{false,true}) {
            Fixture f;const auto before=f.ram;f.writes.clear();
            const unsigned start=field-4;
            std::array<uint32_t,3> registers{};
            for(unsigned i=0;i<registers.size();++i)
                registers[i]=f.apu.RegFileMmio::mmio_read(0xfe800000+start+4*i,4);
            const unsigned address=0xfe800000+field+access.delta;
            if(write)f.apu.mmio_write(address,access.size,0x12345678);
            else CHECK(f.apu.mmio_read(address,access.size)==0);
            CHECK(f.error=="mcpx-apu: FE notification fence/control remains unqualified");
            CHECK(f.ram==before && f.writes.empty() && !f.irq_edges);
            for(unsigned i=0;i<registers.size();++i)
                CHECK(f.apu.RegFileMmio::mmio_read(0xfe800000+start+4*i,4)==registers[i]);
            // A rejected fence transaction is sticky: no later voice stop can
            // turn it into an apparently successful completion/notification.
            f.command(0x20128,f.voice);
            CHECK(f.ram==before && f.writes.empty() && !f.irq_edges && !f.apu.voice_off_methods());
        }
        // Accesses ending exactly before or starting exactly after the field
        // retain ordinary register transport; no neighboring bytes are guarded.
        Fixture f;
        f.apu.mmio_write(0xfe800000+field-4,4,0xaabbccdd);
        f.apu.mmio_write(0xfe800000+field+4,4,0x55667788);
        CHECK(f.apu.mmio_read(0xfe800000+field-4,4)==0xaabbccdd);
        CHECK(f.apu.mmio_read(0xfe800000+field+4,4)==0x55667788 && f.error.empty());
    }
    std::puts("PASS: static VOICE_OFF ordered state/status/IRQ, 32 stop/rebind lifetimes, DMA/ownership guards and 96 direct FE read/write overlap guards");
}
