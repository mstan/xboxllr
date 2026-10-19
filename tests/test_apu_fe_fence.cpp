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
    std::vector<uint32_t> written_addresses;
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
            written_addresses.push_back(a);
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
    for(uint64_t epoch:{uint64_t(0),uint64_t(1234)}) {
        Fixture f;CHECK(f.apu.initialize_board_audio_reset());
        int gp,ep;CHECK(f.apu.bind_gp_program_owner(&gp,[]{return true;}));
        CHECK(f.apu.bind_ep_program_owner(&ep,[]{return true;}));
        CHECK(f.apu.prime_frame_clock(epoch) && f.error.empty());
        CHECK(!f.writes && !f.apu.fe_fences_published());
        CHECK(!f.apu.prime_frame_clock(epoch));
    }
    for(bool read:{false,true}) {
        Fixture f;CHECK(f.apu.initialize_board_audio_reset());
        if(read)f.read(0x1510);else f.write(0x5fffc,1);
        CHECK(!f.apu.prime_frame_clock(1));
    }
    // Explicit board reset supplies cold DWORD register transport, without
    // borrowing a warm application's retained state or completing any work.
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        Fixture f;CHECK(f.apu.initialize_board_audio_reset());
        CHECK(f.read(0x1510)==0 && f.read(0x1100)==0 && f.read(0x2000)==0);
        CHECK(f.read(0x3fffc)==0 && f.read(0x5fffc)==0);
        for(uint32_t control:{1u,0u,1u}) {
            f.write(0x1510,control);CHECK(f.read(0x1510)==control);
            CHECK(f.apu.service_fe_fence() && !f.writes && !f.irq_edges);
        }
        for(unsigned repeat=0;repeat<3;++repeat) {
            f.write(0x3fffc,0);f.write(0x5fffc,1);
            CHECK(f.read(0x3fffc)==0 && f.read(0x5fffc)==1);
        }
        f.write(0x3ff00,0);f.write(0x3ff04,0);
        f.write(0x3fffc,1);CHECK(f.read(0x3fffc)==1 && f.error.empty());
        CHECK(f.error.empty() && !f.writes && !f.apu.fe_fences_published());
        f.write(0x3fffc,3); // execution still requires the real CPU owner
        CHECK(!f.error.empty() && !f.writes && f.apu.RegFileMmio::mmio_read(0xfe83fffc,4)==1);
    }
    {Fixture f;f.read(0x1004);CHECK(!f.apu.initialize_board_audio_reset());}
    {
        Fixture cold,warm;CHECK(cold.apu.initialize_board_audio_reset());
        CHECK(warm.apu.initialize_kernel_audio_handoff());cold.write(0x5fffc,1);
        for(auto* fixture:{&cold,&warm}) {
            fixture->write(0x1100,0x88);fixture->write(0x2000,7);
            CHECK(fixture->read(0x1100)==0x70088);
            fixture->write(0x1100,8);CHECK(fixture->read(0x1100)==0x70008);
            CHECK(fixture->error.empty());
        }
    }
    for(unsigned field:{0x201cu,0x2038u,0x203cu}) {
        Fixture f;CHECK(f.apu.initialize_board_audio_reset());f.write(0x5fffc,1);
        CHECK(f.read(field)==0 && f.error.empty());
        CHECK(f.apu.mmio_read(0xfe800000+field+1,4)==0 && !f.error.empty());
    }
    for(unsigned field:{0x1324u,0x1334u}) {
        Fixture f;CHECK(f.apu.initialize_board_audio_reset());f.write(field,4096);
        CHECK(!f.error.empty() && !f.writes && !f.apu.fe_fence_pending());
    }
    // Real cold-driver initialization must feed the existing serialized fence
    // owner. Its token becomes visible only at independent device service.
    {
        Fixture f;CHECK(f.apu.initialize_board_audio_reset());
        f.write(0x1510,1);f.write(0x3fffc,0);f.write(0x5fffc,1);
        f.write(0x1100,0x88);f.write(0x2000,7);f.write(0x1100,8);
        f.write(0x1324,4096);f.write(0x1334,0x87654321);
        CHECK(f.error.empty() && f.apu.fe_fence_pending());
        CHECK(f.word(4096)==0xa5a5a5a5 && !f.writes);
        for(unsigned poll=0;poll<8;++poll)CHECK(f.read(0x1510)==1);
        CHECK(f.apu.service_fe_fence() && f.word(4096)==0x87654321);
        CHECK(f.writes==1 && f.apu.fe_fences_published()==1 && !f.irq_edges);
    }
    // Control access does not itself submit or complete a memory token.
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
        CHECK((f.error.empty()==(output && field==0x1324)) && f.ram==before && !f.writes && !f.irq_edges);
        CHECK(!f.apu.fe_fence_pending() && !f.apu.fe_fences_published());
    }
    // Prior use alone is not pending work. Explicit service must still publish
    // the token; register writes and repeated reads cannot complete it.
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
        CHECK(f.error.empty());const auto before=f.ram;
        f.write(0x1324,4096);f.write(0x1334,0x12345678);
        if(kind==7) {
            // Restoring the mode does not acknowledge a trapped interrupt.
            CHECK(!f.error.empty() && !f.writes && f.ram==before && !f.apu.fe_fence_pending());
            continue;
        }
        if(!f.error.empty())std::fprintf(stderr,"history case %u: %s\n",kind,f.error.c_str());
        CHECK(f.error.empty() && !f.writes && f.ram==before && f.apu.fe_fence_pending());
        for(unsigned n=0;n<8;++n)CHECK(f.read(0x1510)==1 && !f.writes);
        CHECK(f.apu.service_fe_fence() && f.word(4096)==0x12345678 && f.writes==1);
    }
    // A real method's RAM publication precedes the queued token, with SE
    // running and the original table owner retained. Device failures must not
    // publish a token over incomplete work or retry the failed transfer.
    for(bool reject:{false,true}) {
        Fixture f;CHECK(f.apu.initialize_kernel_audio_handoff());
        f.write(0x1138,0x7ff);f.write(0x2030,0x4000);
        f.write(0x1100,0x100f);f.write(0x1510,0);f.write(0x2000,15);
        f.write(0x20804,0x7ff);f.reject=reject;f.write(0x20808,0xc000);
        f.write(0x1510,1);f.write(0x1324,4096);f.write(0x1334,0x11223344);
        if(reject) {
            CHECK(!f.apu.service_fe_fence() && f.word(4096)==0xa5a5a5a5);
            CHECK(!f.apu.fe_fences_published() && f.written_addresses.empty() && f.writes==1);
        } else {
            CHECK(f.word(0x7ff8)==0xc000 && f.word(4096)==0xa5a5a5a5);
            CHECK(f.apu.service_fe_fence() && f.word(4096)==0x11223344 && f.error.empty());
            CHECK(f.written_addresses==std::vector<uint32_t>({0x7ff8,4096}));
        }
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
    // Binding a DSP does not turn an imported idle-only snapshot into the
    // distinct initialized-kernel profile used by actual original startup.
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
    // A caller cannot insert a fence from within an unfinished voice fetch,
    // including the older standalone processing interface.
    for(bool captured:{false,true}) {
        Fixture f;if(captured)f.capture();else CHECK(f.apu.initialize_kernel_audio_handoff());
        f.apu.set_mem_access([&](uint32_t,void*,size_t){f.write(0x1324,4096);return false;},{});
        float bins[1024]{};uint32_t count=0;bool completed=false;
        CHECK(!f.apu.process_2d_voice(71,bins,count,completed));
        CHECK(!f.error.empty() && !f.apu.fe_fence_pending() && !f.apu.fe_fences_published() && !f.writes);
    }
    for(bool reenter:{false,true}) {
        Fixture f;f.capture();f.write(0x1324,4096);f.write(0x1334,7);
        f.reject=!reenter;f.reenter=reenter;const auto before=f.ram;
        CHECK(!f.apu.service_fe_fence() && !f.error.empty() && f.writes==1);
        CHECK(f.ram==before && !f.irq_edges && !f.apu.fe_fences_published());
        CHECK(!f.apu.service_fe_fence() && f.writes==1); // sticky: no hidden retry
    }
    std::puts("PASS: serialized FE method/token order and failure retention; idle publications, overlap guards, unknown-context/DMA/reentry negatives");
}
