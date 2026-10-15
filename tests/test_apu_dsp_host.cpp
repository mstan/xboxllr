// Physical held-register observations through the actual APU and sole CPUs.
// Source: probe docs/DSP-HOST-CONTROLS.md. No playback or inferred DSP aliases.
#include "machine/apu_gp_processor.hpp"
#include "machine/apu_ep_processor.hpp"
#include <cstdio>
#include <cstdlib>
#include <memory>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"DSP host FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct Fixture {
    std::string error;
    unsigned reads=0,writes=0;
    xlle::McpxApu apu{[this](const std::string& s){error=s;}};
    std::unique_ptr<xlle::ApuGpProcessor> gp;
    std::unique_ptr<xlle::ApuEpProcessor> ep;
    explicit Fixture(bool capture=true) {
        CHECK(apu.initialize_kernel_audio_handoff());
        if(capture)CHECK(apu.capture_ep_host_status(0x100));
        gp=std::make_unique<xlle::ApuGpProcessor>(apu,[this](const std::string& s){error=s;});
        ep=std::make_unique<xlle::ApuEpProcessor>(apu,[this](const std::string& s){error=s;});
        apu.set_mem_access([this](uint32_t,void*,size_t){++reads;return false;},
                           [this](uint32_t,const void*,size_t){++writes;return false;});
    }
    void put(uint32_t offset,uint32_t value){apu.mmio_write(0xfe800000u+offset,4,value);}
    uint32_t get(uint32_t offset){return apu.mmio_read(0xfe800000u+offset,4);}
};
static void observed_lifetimes() {
    for(unsigned life=0;life<3;++life) {
        Fixture f;
        CHECK(f.get(0x3ff10)==0 && f.get(0x5ff10)==0 && f.get(0x3ff14)==0 && f.get(0x5ff14)==0x100);
        uint32_t dsp_status=99;CHECK(f.apu.ep_read(0xffffc5,dsp_status) && dsp_status==0);
        for(uint32_t offset:{0x3ff00u,0x3ff04u,0x5ff00u,0x5ff04u,0x5ff5cu}) {
            f.put(offset,0);CHECK(f.get(offset)==0);
        }
        f.put(0x3fffc,1);
        for(uint32_t input:{0x30000001u,0xfffffff1u,0u}) {
            f.put(0x3ff10,input);CHECK(f.get(0x3ff10)==1);
        }
        f.put(0x3ff14,0xff);CHECK(f.get(0x3ff14)==0);
        f.put(0x3fffc,0);CHECK(f.get(0x3ff10)==0 && f.get(0x3ff14)==0);
        f.put(0x5ff10,1);f.put(0x5ff10,0);CHECK(f.get(0x5ff10)==1);
        f.put(0x5ff14,0xffffffff);CHECK(f.get(0x5ff14)==0);
        for(unsigned poll=0;poll<1000;++poll)CHECK(f.get(0x5ff10)==1 && f.get(0x5ff14)==0);
        CHECK(f.apu.ep_read(0xffffc5,dsp_status) && dsp_status==0);
        CHECK(f.error.empty() && !f.reads && !f.writes);
        CHECK(f.gp->phase()==xlle::ApuGpPhase::Held && f.ep->phase()==xlle::ApuGpPhase::Held);
        CHECK(!f.gp->bootstraps() && !f.ep->bootstraps() && !f.apu.ep_frame_events());
    }
}
static void guards() {
    {Fixture f(false);f.get(0x5ff14);CHECK(f.apu.faulted());}
    {Fixture f(false);f.put(0x5ff14,0xffffffff);CHECK(f.get(0x5ff14)==0 && f.error.empty());}
    {Fixture f;f.put(0x5ff14,0xffffffff);f.apu.set_time(0,1125000);CHECK(f.get(0x5ff14)==0);
     f.apu.set_time(1,1125000);f.get(0x5ff14);CHECK(f.apu.faulted());}
    {Fixture f;f.apu.set_time(0,1125000);f.put(0x5ff10,1);f.apu.set_time(1,1125000);
     CHECK(f.get(0x5ff10)==1 && !f.apu.ep_frame_events() && f.error.empty());}
    {Fixture f;f.put(0x5ff10,1);f.ep.reset();
     f.ep=std::make_unique<xlle::ApuEpProcessor>(f.apu,[&](const std::string& s){f.error=s;});
     CHECK(!f.error.empty() && f.ep->phase()==xlle::ApuEpPhase::Failed);}
    {Fixture f;CHECK(!f.apu.capture_ep_host_status(0));CHECK(f.apu.faulted());}
    for(unsigned variant=0;variant<9;++variant) {
        Fixture f;
        if(variant==0)f.put(0x3ff00,1);
        if(variant==1)f.put(0x3ff10,9);
        if(variant==2)f.put(0x5ff10,0x30000001);
        if(variant==3)f.put(0x5ff14,0x100);
        if(variant==4)f.apu.mmio_write(0xfe83ff0f,4,1);
        if(variant==5)f.apu.mmio_read(0xfe85ff11,1);
        if(variant==6)f.apu.mmio_write(0xfe85ff5c,2,0);
        if(variant==7){f.put(0x200c,0);f.put(0x2000,15);f.put(0x3ff00,0);}
        if(variant==8){f.put(0x5ff14,0xffffffff);f.apu.set_time(100,1125000);f.get(0x5ff14);}
        CHECK(f.apu.faulted() && !f.reads && !f.writes);
    }
    std::string failure;xlle::McpxApu cold([&](const std::string& s){failure=s;});
    cold.mmio_write(0xfe83ff00,4,0);CHECK(cold.faulted());
}
int main(){observed_lifetimes();guards();puts("DSP host: physical held profiles, shared requests, status expiry and guards pass");}
