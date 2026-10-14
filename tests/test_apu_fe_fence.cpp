#include "machine/apu.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
namespace {
struct Fixture {
    std::string error;
    std::vector<uint8_t> ram=std::vector<uint8_t>(65536,0xa5);
    unsigned writes=0,irq_edges=0;
    bool reject=false,reenter=false;
    std::unique_ptr<xlle::McpxApu> device=std::make_unique<xlle::McpxApu>(
        [this](const std::string& why){error=why;});
    xlle::McpxApu& apu=*device;
    Fixture() {
        apu.set_mem_access([this](uint32_t a,void* p,size_t n){
            if(uint64_t(a)+n>ram.size())return false;
            std::memcpy(p,ram.data()+a,n);return true;
        },[this](uint32_t a,const void* p,size_t n){
            ++writes;
            if(reenter){CHECK(!apu.service_fe_fence());return false;}
            if(reject || uint64_t(a)+n>ram.size())return false;
            CHECK(n==4 && !(a&3));
            std::memcpy(ram.data()+a,p,n);return true;
        });
        apu.set_irq([this](int line,bool){CHECK(line==5);++irq_edges;});
    }
    void capture(){CHECK(apu.capture_fe_idle_context(0x71f8f,7,1,0));}
    void write(uint32_t off,uint32_t value){apu.mmio_write(0xfe800000+off,4,value);}
    uint32_t read(uint32_t off){return apu.mmio_read(0xfe800000+off,4);}
    uint32_t word(uint32_t a){uint32_t v;std::memcpy(&v,ram.data()+a,4);return v;}
};
}
int main() {
    // Initialized-kernel controls remain distinct from notification admission.
    // Merely binding two unused CPU owners does not fabricate active work.
    for(unsigned life=0;life<3;++life) {
        Fixture f;CHECK(f.apu.initialize_kernel_audio_handoff());
        CHECK(f.read(0x2000)==7 && f.read(0x1004)==0xafa && f.read(0x1510)==1);
        int gp,ep;CHECK(f.apu.bind_gp_program_owner(&gp,[]{return true;}));
        CHECK(f.apu.bind_ep_program_owner(&ep,[]{return true;}));
        for(uint32_t value:{1u,0u,1u,0u}) {
            f.write(0x1510,value);
            for(unsigned poll=0;poll<8;++poll)CHECK(f.read(0x1510)==value);
            CHECK(f.apu.service_fe_fence());
            CHECK(!f.writes && !f.irq_edges && !f.apu.fe_fences_published());
        }
        CHECK(f.error.empty());
    }
    for(uint32_t field:{0x1324u,0x1334u})for(bool output:{false,true}) {
        Fixture f;CHECK(f.apu.initialize_kernel_audio_handoff());const auto before=f.ram;
        if(output)f.write(field,4096);else CHECK(f.read(field)==0);
        CHECK(!f.error.empty() && f.ram==before && !f.writes && !f.irq_edges);
        CHECK(!f.apu.fe_fence_pending() && !f.apu.fe_fences_published());
    }
    // Restoring register values cannot launder processing/transport history.
    for(unsigned kind=0;kind<8;++kind) {
        Fixture f;CHECK(f.apu.initialize_kernel_audio_handoff());
        switch(kind) {
        case 0:f.write(0x2000,15);f.write(0x2000,7);break;
        case 1:CHECK(f.apu.gp_write(0xffffd4,0));break;
        case 2:CHECK(f.apu.ep_write(0xffffd4,0));break;
        case 3:CHECK(f.apu.gp_scratch(0,nullptr,0,false));break;
        case 4:CHECK(f.apu.ep_scratch(0,nullptr,0,false));break;
        case 5:f.apu.set_frame(1);break;
        case 6:f.apu.mmio_write(0xfe801fff,2,0x1800);f.write(0x2000,7);break;
        case 7:f.write(0x1100,0xe0);f.write(0x1100,0x88);break;
        }
        CHECK(f.error.empty());CHECK(f.read(0x1510)==0 && !f.error.empty() && !f.writes);
    }
    {Fixture f;CHECK(f.apu.initialize_kernel_audio_handoff());f.write(0x1510,0);
     CHECK(!f.apu.initialize_kernel_audio_handoff());
     CHECK(f.apu.RegFileMmio::mmio_read(0xfe801510,4)==0);}
    {Fixture f;f.write(0x1004,0);CHECK(!f.apu.initialize_kernel_audio_handoff());}
    {Fixture f;CHECK(f.apu.initialize_kernel_audio_handoff());
     CHECK(!f.apu.capture_fe_idle_context(0x71f8f,7,1,0));}
    // Both observed orderings: publication with control held at 1, and after
    // release. CPU/MMIO reads never stand in for independent device service.
    for(bool held:{true,false})for(unsigned lifetime=0;lifetime<3;++lifetime) {
        Fixture f;f.capture();
        for(unsigned n=0;n<16;++n) {
            const uint32_t address=4096+8*n,token=0xfffffff8u+n;
            f.write(0x1510,1);f.write(0x1324,address);f.write(0x1334,token);
            CHECK(f.apu.fe_fence_pending() && f.word(address)==0xa5a5a5a5);
            for(unsigned poll=0;poll<16;++poll)CHECK(f.read(0x1510)==1);
            CHECK(f.writes==n && f.word(address)==0xa5a5a5a5);
            if(!held)f.write(0x1510,0);
            CHECK(f.apu.service_fe_fence());
            CHECK(f.word(address)==token && !f.apu.fe_fence_pending());
            CHECK(f.apu.fe_fences_published()==n+1 && f.writes==n+1 && !f.irq_edges);
            CHECK(f.read(0x1510)==unsigned(held));
            CHECK(f.ram[address-1]==0xa5 && f.ram[address+4]==0xa5);
            if(held)f.write(0x1510,0);
            f.write(0x1510,1);CHECK(f.apu.service_fe_fence() && f.writes==n+1);
        }
        CHECK(f.error.empty());
    }
    // Warm observations are not fabricated construction or an active-DPC floor.
    {Fixture f;CHECK(f.read(0x1510)==0 && !f.error.empty() && !f.writes);}
    for(unsigned changed=0;changed<4;++changed) {
        Fixture f;
        CHECK(!f.apu.capture_fe_idle_context(changed==0?0:0x71f8f,changed==1?8:7,
                                           changed==2?0:1,changed==3?3:0));
        CHECK(!f.error.empty() && !f.writes && !f.irq_edges);
    }
    {Fixture f;f.write(0x2000,8);CHECK(!f.apu.capture_fe_idle_context(0x71f8f,7,1,0));}
    {Fixture f;f.capture();CHECK(!f.apu.capture_fe_idle_context(0x71f8f,7,1,0));}
    {Fixture f;f.capture();f.write(0x1100,0);CHECK(f.read(0x1510)==0 && !f.error.empty());}
    {Fixture f;f.capture();f.write(0x1100,0);f.write(0x1100,0x71f8f);
     CHECK(f.read(0x1510)==0 && !f.error.empty());} // no laundering busy history
    {Fixture f;f.capture();f.write(0x2000,8);f.write(0x1324,4096);CHECK(!f.error.empty() && !f.writes);}
    // Every unsupported partial/crossing transaction still stops before any
    // register, RAM or interrupt mutation, including in the captured context.
    struct Access {int delta,size;};
    const Access accesses[]={{-3,4},{-2,4},{-1,2},{-1,4},{0,1},{0,2},
        {1,1},{1,2},{1,4},{2,1},{2,2},{2,4},{3,1},{3,2},{3,4}};
    for(const auto& a:accesses)for(bool output:{false,true}) {
        Fixture f;CHECK(f.apu.initialize_kernel_audio_handoff());
        if(output)f.apu.mmio_write(0xfe801510+a.delta,a.size,0);
        else CHECK(f.apu.mmio_read(0xfe801510+a.delta,a.size)==0);
        CHECK(!f.error.empty() && !f.writes && !f.irq_edges);
        CHECK(f.apu.RegFileMmio::mmio_read(0xfe801510,4)==1);
    }
    for(unsigned field:{0x1510u,0x1324u,0x1334u})for(const auto& a:accesses)for(bool output:{false,true}) {
        Fixture f;f.capture();const auto before=f.ram;
        std::array<uint32_t,3> regs{};
        for(unsigned i=0;i<3;++i)regs[i]=f.apu.RegFileMmio::mmio_read(0xfe800000+field-4+4*i,4);
        if(output)f.apu.mmio_write(0xfe800000+field+a.delta,a.size,0x12345678);
        else CHECK(f.apu.mmio_read(0xfe800000+field+a.delta,a.size)==0);
        CHECK(!f.error.empty() && f.ram==before && !f.writes && !f.irq_edges);
        for(unsigned i=0;i<3;++i)CHECK(f.apu.RegFileMmio::mmio_read(0xfe800000+field-4+4*i,4)==regs[i]);
    }
    for(unsigned field:{0x1324u,0x1334u}) {
        Fixture f;f.capture();CHECK(f.read(field)==0 && !f.error.empty());
    }
    for(uint32_t address:{4097u,0xfffffffcu,0x04000000u}) {
        Fixture f;f.capture();f.write(0x1324,address);CHECK(!f.error.empty() && !f.writes);
    }
    {Fixture f;f.capture();f.write(0x1510,2);CHECK(!f.error.empty());}
    {Fixture f;f.capture();f.write(0x1334,7);CHECK(!f.error.empty() && !f.writes);}
    {Fixture f;f.capture();f.write(0x1324,4096);f.write(0x1510,0);f.write(0x1334,7);CHECK(!f.error.empty() && !f.writes);}
    for(uint32_t changed:{0x1324u,0x1334u,0x1100u,0x2000u,0x20128u}) {
        Fixture f;f.capture();f.write(0x1324,4096);f.write(0x1334,7);const auto before=f.ram;
        f.write(changed,8);CHECK(!f.error.empty() && !f.apu.service_fe_fence());
        CHECK(f.ram==before && !f.writes && !f.irq_edges && !f.apu.fe_fences_published());
    }
    // A real DSP owner is not equivalent to completed work just because FE/SE
    // register values happen to match the idle observation.
    for(bool ep:{false,true}) {
        Fixture f;f.capture();int owner;
        if(ep)CHECK(f.apu.bind_ep_program_owner(&owner,[]{return true;}));
        else CHECK(f.apu.bind_gp_program_owner(&owner,[]{return true;}));
        f.write(0x1324,4096);CHECK(!f.error.empty() && !f.writes);
    }
    for(bool ep:{false,true}) {
        Fixture f;f.capture();int owner;
        if(ep){CHECK(f.apu.bind_ep_program_owner(&owner,[]{return true;}));f.apu.unbind_ep_program_owner(&owner);}
        else {CHECK(f.apu.bind_gp_program_owner(&owner,[]{return true;}));f.apu.unbind_gp_program_owner(&owner);}
        CHECK(!f.apu.capture_fe_idle_context(0x71f8f,7,1,0) && !f.error.empty() && !f.writes);
    }
    {Fixture f;CHECK(f.apu.gp_write(0xffffd4,0));CHECK(!f.apu.capture_fe_idle_context(0x71f8f,7,1,0));}
    {Fixture f;CHECK(f.apu.ep_write(0xffffd4,0));CHECK(!f.apu.capture_fe_idle_context(0x71f8f,7,1,0));}
    for(bool reenter:{false,true}) {
        Fixture f;f.capture();f.write(0x1324,4096);f.write(0x1334,7);
        f.reject=!reenter;f.reenter=reenter;const auto before=f.ram;
        CHECK(!f.apu.service_fe_fence() && !f.error.empty() && f.writes==1);
        CHECK(f.ram==before && !f.irq_edges && !f.apu.fe_fences_published());
        CHECK(!f.apu.service_fe_fence() && f.writes==1); // sticky: no hidden retry
    }
    std::puts("PASS: startup control/notification separation and history guards; 96 idle FE publications, 120 overlap guards, cold/busy/ownership/DMA/reentry negatives");
}
