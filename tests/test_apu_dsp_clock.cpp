#include "machine/apu_dsp_clock.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"DSP clock FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::string error;
    std::vector<uint8_t> ram=std::vector<uint8_t>(0x8000);
    xlle::McpxApu apu{[this](const std::string& s){error=s;}};
    std::unique_ptr<xlle::ApuGpProcessor> gp;
    std::unique_ptr<xlle::ApuEpProcessor> ep;
    unsigned reads=0,writes=0;
    Fixture() {
        CHECK(apu.initialize_kernel_audio_handoff());
        apu.set_mem_access([this](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;++reads;std::memcpy(p,ram.data()+a,n);return true;},
            [this](uint32_t,const void*,size_t){++writes;return false;});
        gp=std::make_unique<xlle::ApuGpProcessor>(apu,[this](const std::string& s){error=s;});
        ep=std::make_unique<xlle::ApuEpProcessor>(apu,[this](const std::string& s){error=s;});
        put(0x2040,0x1000);put(0x20d4,1);put(0x2048,0x2000);put(0x20dc,1);
        word(0x1000,0x4000);word(0x1008,0x5000);word(0x2000,0x6000);word(0x2008,0x7000);
        const uint32_t program[]={0x08f485,0xfff,0x08f484,1,0x0af080,4};
        for(uint32_t base:{0x4000u,0x6000u})std::memcpy(ram.data()+base,program,sizeof(program));
    }
    void word(unsigned at,uint32_t value){std::memcpy(ram.data()+at,&value,4);}
    void put(unsigned off,uint32_t value){apu.mmio_write(0xfe800000+off,4,value);}
    void release(){put(0x3fffc,1);put(0x3fffc,3);put(0x5fffc,1);put(0x5fffc,3);}
};
int main() {
    uint64_t ep_retired=0;uint32_t ep_pc=0;
    for(uint64_t step:{1ull,750ull,11250ull}) {
        Fixture f;const uint64_t epoch=60000;
        f.apu.set_time(epoch,1125000);
        xlle::ApuDspClock clock(f.apu,*f.gp,*f.ep,epoch,1125000,[&](const std::string& s){f.error=s;});
        CHECK(clock.advance(epoch+11250) && !clock.retired(false) && !clock.retired(true) && !f.reads);
        f.release();CHECK(f.error.empty() && f.reads==8);
        const auto before=f.ram;const uint64_t start=clock.ticks();
        CHECK(clock.advance(start) && !clock.retired(false) && !clock.retired(true));
        for(uint64_t elapsed=step;elapsed<=11250;elapsed+=step)CHECK(clock.advance(start+elapsed));
        CHECK(f.gp->phase()==xlle::ApuGpPhase::Yielded && clock.retired(false)==6);
        CHECK(f.ep->phase()==xlle::ApuGpPhase::Running && clock.last(true).pc==4);
        CHECK(clock.retired(true)>=1333125 && clock.retired(true)<=1333128);
        if(step==1){ep_retired=clock.retired(true);ep_pc=clock.last(true).pc;}
        else CHECK(clock.retired(true)==ep_retired && clock.last(true).pc==ep_pc);
        CHECK(!f.apu.ep_frame_events() && !f.apu.gp_frame_outstanding() && !f.apu.gp_dma_nodes() && !f.apu.ep_dma_nodes());
        CHECK(f.ram==before && f.reads==8 && !f.writes && f.error.empty());
        for(unsigned i=0;i<100;++i)CHECK(clock.advance(clock.ticks()));
        CHECK(clock.retired(false)==6 && clock.retired(true)==ep_retired && f.reads==8);
        CHECK(!clock.advance(clock.ticks()-1) && !f.error.empty());
        CHECK(!clock.advance(clock.ticks()+1) && clock.retired(true)==ep_retired);
    }
    puts("PASS shared stopped clock: no held credit, exact GP yield, real EP polling, partition invariance and no read/time-repeat progress");
}
