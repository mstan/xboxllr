// Authored control-plane suite, shared across both renderer configurations.
#include "machine/nv2a.hpp"
#include "machine/nv2a_context_image.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#define CHECK(c) do {if(!(c)){std::fprintf(stderr,"context-control FAIL %d: %s\n",__LINE__,#c);std::exit(1);}}while(0)
static void put32(std::vector<uint8_t>& b,uint32_t a,uint32_t v) {
    for(unsigned i=0;i<4;++i)b[a+i]=uint8_t(v>>(i*8));
}
static uint32_t get32(const std::vector<uint8_t>& b,uint32_t a) {
    uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(b[a+i])<<(i*8);return v;
}
#include "nv2a_setup_cases.hpp"
#include "nv2a_context_method_cases.hpp"
#include "nv2a_context_draw_cases.hpp"
#include "nv2a_phase_ownership_cases.hpp"
#include "nv2a_context_transport_cases.hpp"
static void test_loaded_object_header_switch() {
    for(uint32_t mode:{0x28c3ffu,0x8cfffu})for(bool batching:{false,true}) {
        auto fixture=std::make_unique<LoadedContextFixture>(batching,nullptr,mode);auto& f=*fixture;
        // Three independent object handles, including reached handleE/sub2.
        f.write(0x700070,14);f.write(0x700074,0x800100a0);f.write(0x700a00,0x97);
        f.emit(0,3);f.kick();CHECK(f.failure.empty() && f.read(0x40014c)==0x62);
        f.emit(2u<<13,14); // queued SET_OBJECT on subchannel2
        f.write(0x400720,0);f.kick();
        CHECK(f.failure.empty() && f.read(0x40014c)==0x62); // prefetch cannot switch
        f.write(0x400720,1);CHECK(f.failure.empty() && f.read(0x40014c)==0x97);
        f.write(0x700a00,0x44);CHECK(f.read(0x40014c)==0x97); // captured, no alias
        f.emit(0,3);f.kick();CHECK(f.failure.empty() && f.read(0x40014c)==0x62);
        f.emit((2u<<13)|0x100,0);f.kick();
        CHECK(f.failure.empty() && f.read(0x40014c)==0x97); // sub2 retained its cache
        f.emit(2u<<13,14);f.kick();
        CHECK(f.failure.empty() && f.read(0x40014c)==0x44); // explicit rebind captures new bytes
    }
    // Separate fixture: actual subchannel switching and a trapped software NOP.
    for(uint32_t mode:{0x28c3ffu,0x8cfffu}) {
        auto fixture=std::make_unique<LoadedContextFixture>(true,nullptr,mode);auto& f=*fixture;
        f.emit(0,1);f.kick();CHECK(f.failure.empty() && f.read(0x40014c)==0x97);
        f.write(0x700200,0x62);CHECK(f.read(0x40014c)==0x97);
        f.emit(0x100,0x31c);f.kick();
        CHECK(f.failure.empty() && f.read(0x40014c)==0x97 && f.read(0x400108)==2);
        CHECK(f.read(0x400100)&(1u<<20));CHECK(f.read(0x400720)==0);
        CHECK(f.read(0x400704)==0x100 && f.read(0x400708)==0x31c);
        const auto methods=f.gpu.push_methods();
        f.write(0x400100,1u<<20);CHECK(f.read(0x40014c)==0x97 && f.gpu.push_methods()==methods);
        f.read(0x400150);CHECK(f.failure.find("qualified context mapping")!=std::string::npos);
    }
}
static void test_single_sample_clear_raster_independence() {
    for(uint32_t mode:{0x28c3ffu,0x8cfffu})
    for(int control:{-1,0,1,0x111,int(0xffff0001u),int(0xffff0000u)}) {
        auto fixture=std::make_unique<LoadedContextFixture>(true,nullptr,mode);auto& f=*fixture;
        f.emit(0,1);f.emit(0x194,2);f.emit(0x208,0x128);
        f.emit(0x1d90,0x12345678);f.emit(0x1d98,1u<<16);f.emit(0x1d9c,1u<<16);
        if(control!=-1)f.emit(0x1d7c,uint32_t(control));
        f.emit(0x1d94,0xf0);f.emit(0x50,0x7788);f.kick();
        CHECK(f.failure.empty() && f.gpu.color_clears()==1 && f.read(0x800048)==0x7788);
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
            CHECK(get32(f.ram,0x1000+y*64+x*4)==0x12345678);
        CHECK(get32(f.ram,0x1008)==0xa5a5a5a5 && get32(f.ram,0x1100)==0xa5a5a5a5);
    }
    for(unsigned failure=0;failure<3;++failure) {
        auto fixture=std::make_unique<LoadedContextFixture>();auto& f=*fixture;
        f.emit(0,1);f.emit(0x194,2);
        if(failure!=0)f.emit(0x208,failure==1?0x1128:0x128);
        f.emit(0x1d90,0x12345678);f.emit(0x1d98,1u<<16);f.emit(0x1d9c,1u<<16);
        f.emit(0x1d7c,0xffff0000);f.emit(0x1d94,failure==2?0x100:0xf0);f.emit(0x50,1);f.kick();
        CHECK(f.failure.find("single-sample profile")!=std::string::npos && !f.gpu.color_clears());
        CHECK(get32(f.ram,0x1000)==0xa5a5a5a5 && !f.read(0x800048));
    }
}
static void test_tiled_clear_phase_uses_cpu_codec() {
    struct Renderer final:xlle::Nv2aRenderer {
        unsigned calls=0;
        const char* name()const override{return "passive-rejection";}
        bool supports_generic_combiner(const xlle::Nv2aGenericCombinerPacket&)const override{return false;}
        bool submit_generic_combiner(const xlle::Nv2aGenericCombinerPacket&,xlle::Nv2aGenericCombinerResult&,std::string&)override{++calls;return false;}
    } renderer;
    for(bool overlap:{false,true}) {
        auto fixture=std::make_unique<LoadedContextFixture>();auto& f=*fixture;
        const uint32_t base=overlap?0:0x4000;
        const uint32_t words[]={base|1,base+0x3fff,0x200};
        for(unsigned lane:{1u,2u,0u}) {
            f.write(0x100240+lane*4,words[lane]);f.write(0x400900+lane*4,words[lane]);
            f.write(0x400750,0xea0010+lane*0x20);f.write(0x400754,words[lane]);
        }
        f.gpu.set_renderer(&renderer);
        f.color_inputs();f.emit(0x1d94,0xf0);f.emit(0x50,0x7777);
        const auto before=f.ram;f.kick();
        CHECK(f.failure.empty() && f.ram!=before && f.gpu.color_clears()==1);
        CHECK(f.read(0x800048)==0x7777 && !renderer.calls);
    }
}
int main() {
    test_opaque09f8_consumer_policy();
    test_hle_context_transport();
    test_gpu_phase_ownership();
    test_tiled_clear_phase_uses_cpu_codec();test_single_sample_clear_raster_independence();test_loaded_object_header_switch();test_pgraph_setup();test_pgraph_context_transfer();test_loaded_context_methods();
    test_pgraph_boot_configuration();test_pgraph_context_admission();
    std::puts("PASS: context transfer, real image fields, cold boot profile, preparation guards, retained command quarantine");
}
