// Actual queued draws through a real LOAD and public MMIO, with an authored
// host renderer that owns dirty linear surfaces. Tests the transport/ownership
// contract rather than shader math (the shader/renderer have separate goldens).
#include "machine/nv2a_hle_draw.hpp"
#include "machine/nv2a_tile_layout.hpp"
struct PhaseRenderer final:xlle::Nv2aRenderer {
    xlle::Nv2aHleSurface color,depth;
    bool color_valid=false,depth_valid=false,color_dirty=false,depth_dirty=false;
    unsigned draws=0,color_resolves=0,depth_resolves=0,invalidations=0;
    std::function<void(const xlle::Nv2aHleDraw&)> observe;
    const char* name()const override{return "authored-phase-renderer";}
    static bool overlap(uint32_t a,size_t n,const xlle::Nv2aHleSurface& s) {
        return n && !s.bytes.empty() && a<uint64_t(s.address)+s.bytes.size() && uint64_t(a)+n>s.address;
    }
    bool supports_generic_combiner(const xlle::Nv2aGenericCombinerPacket&)const override{return false;}
    bool submit_generic_combiner(const xlle::Nv2aGenericCombinerPacket&,xlle::Nv2aGenericCombinerResult&,std::string&)override{return false;}
    bool has_resident_color_surface(uint32_t a,size_t n,uint32_t pitch,uint32_t h,uint32_t fmt)const override {
        return color_valid && color.address==a && color.bytes.size()==n && color.pitch==pitch && color.height==h && (!fmt || color.format==fmt);
    }
    bool has_resident_depth_surface(uint32_t a,size_t n,uint32_t pitch,uint32_t h,uint32_t fmt)const override {
        return depth_valid && depth.address==a && depth.bytes.size()==n && depth.pitch==pitch && depth.height==h && (!fmt || depth.format==fmt);
    }
    bool submit_hle_draw(const xlle::Nv2aHleDraw& p,std::string&)override {
        if(observe)observe(p);
        auto update=[](xlle::Nv2aHleSurface& owned,bool& valid,bool& dirty,const xlle::Nv2aHleSurface& input) {
            if(!input.address)return;
            if(!input.bytes.empty())owned=input;
            else CHECK(valid && owned.address==input.address);
            CHECK(owned.bytes.size()>=4);
            put32(owned.bytes,0,get32(owned.bytes,0)+1);
            valid=dirty=true;
        };
        update(color,color_valid,color_dirty,p.color);update(depth,depth_valid,depth_dirty,p.depth);
        ++draws;return true;
    }
    bool resolve_resident_color_surface(uint32_t a,size_t n,xlle::Nv2aResidentColorResolve& out,std::string&)override {
        out.data.clear();if(color_valid && color_dirty && overlap(a,n,color)) {
            out.address=color.address;out.data=color.bytes;color_dirty=false;++color_resolves;
        }return true;
    }
    bool resolve_resident_depth_surface(uint32_t a,size_t n,xlle::Nv2aResidentDepthResolve& out,std::string&)override {
        out.data.clear();if(depth_valid && depth_dirty && overlap(a,n,depth)) {
            out.address=depth.address;out.data=depth.bytes;depth_dirty=false;++depth_resolves;
        }return true;
    }
    void invalidate_resident_color_surface(uint32_t a,size_t n,uint32_t)override {
        if(color_valid && overlap(a,n,color)){CHECK(!color_dirty);color_valid=false;++invalidations;}
    }
    void invalidate_resident_depth_surface(uint32_t a,size_t n,uint32_t)override {
        if(depth_valid && overlap(a,n,depth)){CHECK(!depth_dirty);depth_valid=false;++invalidations;}
    }
};
static void phase_tile(LoadedContextFixture& f,unsigned slot,uint32_t base,uint32_t bank,bool compressed=false) {
    const uint32_t words[]={base|1u|(bank<<1),base+0x3fff,0x200};
    for(unsigned lane:{1u,2u,0u}) {
        f.write(0x100240+slot*16+lane*4,words[lane]);f.write(0x400900+slot*16+lane*4,words[lane]);
        f.write(0x400750,0xea0010+slot*4+lane*0x20);f.write(0x400754,words[lane]);
    }
    if(compressed) {
        f.write(0x100300+slot*4,0x84000000);f.write(0x400980+slot*4,0x84000000);
        f.write(0x400750,0xea0090+slot*4);f.write(0x400754,0x84000000);
    }
}
static void test_gpu_phase_ownership() {
#if XLLE_RENDERER_HLE
    for(bool tiled:{false,true}) {
        PhaseRenderer renderer;auto fixture=std::make_unique<LoadedDrawFixture>();auto& f=*fixture;
        f.ram.resize(0x10000,0xa5);if(tiled)phase_tile(f,0,0,0);
        f.gpu.set_renderer(&renderer);f.state();f.shader();f.color();
        if(tiled)f.emit(0x20c,0x02000200);
        f.triangle();f.triangle();f.kick();
        CHECK(f.failure.empty() && renderer.draws==2 && renderer.color_resolves==1);
        uint32_t physical=0x1000;
        if(tiled){xlle::Nv2aTileLayout layout;CHECK(layout.configure(512,0) && layout.translate(0x1000,physical));CHECK(physical!=0x1000);}
        CHECK(get32(f.ram,physical)==0xa5a5a5a7);
        // CPU changes physical RAM without a hook. No clean GPU carrier may
        // override that store when the next FIFO burst starts.
        put32(f.ram,physical,0x11223344);
        renderer.observe=[&](const xlle::Nv2aHleDraw& p){CHECK(!p.color.bytes.empty() && get32(p.color.bytes,0)==0x11223344);};
        f.triangle();f.kick();
        CHECK(f.failure.empty() && renderer.draws==3 && renderer.color_resolves==2);
        CHECK(get32(f.ram,physical)==0x11223345);
    }
    // HLE keeps compressed GPU sampling under the documented approximation;
    // the paired raw controls are retained and disagreement is rejected.
    for(unsigned noise=0;noise<3;++noise) {
        PhaseRenderer renderer;auto fixture=std::make_unique<LoadedDrawFixture>();auto& f=*fixture;
        f.gpu.set_renderer(&renderer);
        f.write(0x400750,0xe00050);f.write(0x400754,noise?1:0);
        f.write(0x400750,0xdf0008);f.write(0x400754,noise==1?1:0);
        f.state();f.shader();f.color();f.triangle();f.kick();
        CHECK(noise==2 ? (!f.failure.empty() && !renderer.draws) : (f.failure.empty() && renderer.draws==1));

    }
    // Unknown stipple is an explicit HLE cosmetic omission, not known zero;
    // a command enabling that unsupported feature remains a real rejection.
    for(bool enabled:{false,true}) {
        PhaseRenderer renderer;auto fixture=std::make_unique<LoadedDrawFixture>();auto& f=*fixture;
        f.gpu.set_renderer(&renderer);f.state(0x147c);
        if(enabled)f.emit(0x147c,1);
        f.shader();f.color();f.triangle();f.kick();
        CHECK(enabled ? (!f.failure.empty() && !renderer.draws) : (f.failure.empty() && renderer.draws==1));
    }
    // Single-sample HLE storage accepts the reached enable bit only with a
    // full mask; coverage features and genuinely multisampled layouts reject.
    for(unsigned profile=0;profile<5;++profile) {
        PhaseRenderer renderer;auto fixture=std::make_unique<LoadedDrawFixture>();auto& f=*fixture;
        f.gpu.set_renderer(&renderer);f.state();
        f.emit(0x1d7c,profile==0?0xffff0000u:profile==2?0xfffe0001u:profile==3?0xffff0011u:0xffff0001u);
        if(profile==4)f.emit(0x208,0x1128);
        f.shader();f.color();f.triangle();f.kick();
        CHECK(profile>=2 ? (!f.failure.empty() && !renderer.draws) : (f.failure.empty() && renderer.draws==1 && get32(f.ram,0x1000)==0xa5a5a5a6));
    }
    // Unselected color outputs must not invent input dependencies merely
    // because the software reference interpolates both colors unconditionally.
    {
        PhaseRenderer renderer;auto fixture=std::make_unique<LoadedDrawFixture>();auto& f=*fixture;
        f.gpu.set_renderer(&renderer);f.state();f.shader();
        // Final combiner selects only zero registers; no vertex color input.
        f.emit(0xac0,0);f.emit(0x260,0);
        f.triangle();f.kick();CHECK(f.failure.empty() && renderer.draws==1);
    }
    // Final G selects alpha, not RGB. Only its actual source lane is needed;
    // writing an unrelated lane cannot satisfy that semantic input dependency.
    for(bool alpha:{false,true}) {
        PhaseRenderer renderer;auto fixture=std::make_unique<LoadedDrawFixture>();auto& f=*fixture;
        f.gpu.set_renderer(&renderer);f.state();f.shader();
        f.emit(0xac0,0);f.emit(0x260,0x00001400);
        f.emit(alpha?0x1a3c:0x1a30,LoadedDrawFixture::bits(1));
        f.triangle();f.kick();
        CHECK(alpha ? (f.failure.empty() && renderer.draws==1) : (!f.failure.empty() && !renderer.draws));
    }
    // An explicit HLE API default, tied to cold board -> first LOAD only.
    // Raw receipts stay unknown; only untouched disabled W is eligible.
    for(unsigned scenario=0;scenario<6;++scenario) {
        PhaseRenderer renderer;
        auto fixture=std::make_unique<LoadedDrawFixture>(true,nullptr,0x0028c3ffu,scenario!=1);auto& f=*fixture;
        f.gpu.set_renderer(&renderer);f.state();f.shader();
        f.emit(0xac0,0);f.emit(0x260,scenario==2?0x00000400:0x00001400);
        if(scenario==3)f.emit(0x1a3c,LoadedDrawFixture::bits(.25f));
        if(scenario==4 || scenario==5) {
            // An explicit producer followed by a retained LOAD cannot renew
            // fresh-default eligibility, even when W numerically returns1.
            f.emit(0x1a3c,LoadedDrawFixture::bits(scenario==4?0.f:1.f));f.kick();
            f.write(0x400720,0);f.write(0x400148,0);f.write(0x400144,0x10000000);f.write(0x400764,0x18000000);
            f.write(0x400788,1);
            CHECK(f.failure.find("replacement context load")!=std::string::npos);
            CHECK(!renderer.draws && !f.gpu.hle_default_w_uses());continue;
        }
        if(scenario==0 || scenario==3)renderer.observe=[scenario](const xlle::Nv2aHleDraw& p){
            CHECK(p.vertices.size()==3 && p.vertices[0][3][3]==(scenario==0?1.f:.25f));
        };
        f.triangle();f.kick();
        const bool accepted=scenario==0 || scenario==3;
        CHECK(accepted ? (f.failure.empty() && renderer.draws==1) : (!f.failure.empty() && !renderer.draws));
        CHECK(scenario==0 ? f.gpu.hle_default_w_uses()==3 : f.gpu.hle_default_w_uses()==0);
    }
    // Reached PROJECT3D depth sampler is already an HLE shader consumer. Its
    // descriptor bytes and compare function must have real producers.
    for(unsigned missing=0;missing<4;++missing) {
        PhaseRenderer renderer;auto fixture=std::make_unique<LoadedDrawFixture>();auto& f=*fixture;
        f.gpu.set_renderer(&renderer);f.state();f.shader();f.color();
        f.emit(0x184,2);f.emit(0x188,2);f.emit(0x1e70,2);f.emit(0x1b00,0x3000);
        f.emit(0x1b04,0x0001302a);f.emit(0x1b08,0x303);f.emit(0x1b0c,0x4003ffc0);
        if(missing!=1)f.emit(0x1b10,8u<<16);
        f.emit(0x1b14,0x01010000);f.emit(0x1b1c,0x00040001);
        if(missing!=2)f.emit(0x1e6c,7);
        if(missing==3)f.emit(0x1e70,5); // clip-plane side effects stay guarded
        renderer.observe=[](const xlle::Nv2aHleDraw& p){CHECK(p.textures[0].present && p.textures[0].format==0x30 && p.textures[0].bytes.size()==8);};
        f.triangle();f.kick();
        CHECK(missing ? (!f.failure.empty() && !renderer.draws) : (f.failure.empty() && renderer.draws==1));
    }
    // All existing loaded dependencies remain consumers even with a renderer.
    for(unsigned missing=0;missing<3;++missing) {
        PhaseRenderer renderer;auto fixture=std::make_unique<LoadedDrawFixture>();auto& f=*fixture;
        f.gpu.set_renderer(&renderer);f.state(missing==0?0x35c:-1);
        f.shader(missing==1?0:-1);f.color(missing!=2,true);f.triangle();const auto before=f.ram;f.kick();
        CHECK(!f.failure.empty() && !renderer.draws && f.ram==before);
    }
    // An actual cold full-block clear initializes compressed depth tags; GPU
    // upload decodes them and its dirty writeback uses the real ZROP owner.
    {
        PhaseRenderer renderer;auto fixture=std::make_unique<LoadedDrawFixture>();auto& f=*fixture;
        f.ram.resize(0x10000,0xa5);f.write(0x700304,0xffff);phase_tile(f,0,0,0);phase_tile(f,1,0x4000,1,true);
        f.gpu.set_renderer(&renderer);f.state();f.shader();f.color();
        f.emit(0x198,2);f.emit(0x214,0x4000);f.emit(0x20c,0x02000200);
        f.emit(0x1d80,0);f.emit(0x1d8c,0x12345678);f.emit(0x1d98,127u<<16);f.emit(0x1d9c,7u<<16);f.emit(0x1d94,3);
        f.emit(0x30c,1);f.emit(0x354,0x207);f.emit(0x35c,1);
        f.triangle();f.emit(0x1d80,1);f.triangle();f.kick();
        if(!f.failure.empty())std::fprintf(stderr,"compressed phase: %s draws=%u resolves=%u\n",f.failure.c_str(),renderer.draws,renderer.depth_resolves); CHECK(f.failure.empty() && renderer.draws==2 && renderer.depth_resolves==2);
        // Next burst must decode the genuinely re-encoded physical depth.
        renderer.observe=[&](const xlle::Nv2aHleDraw& p){CHECK(!p.depth.bytes.empty() && get32(p.depth.bytes,0)==0x1234567a);};
        f.triangle();f.kick();CHECK(f.failure.empty() && renderer.depth_resolves==3);
        // Resolve old storage before its first bank mutation; no old dirty or
        // clean host plane survives the deliberately incomplete new config.
        f.write(0x100248,0x400);CHECK(f.failure.empty() && !renderer.color_valid && !renderer.depth_valid);
    }
    // A real partial physical write remains real, but the failed publication
    // stops the phase; another call cannot replay the admitted host draw.
    {
        PhaseRenderer renderer;auto fixture=std::make_unique<LoadedDrawFixture>();auto& f=*fixture;
        phase_tile(f,0,0,0);f.gpu.set_renderer(&renderer);unsigned writes=0;
        f.gpu.set_mem_access([&](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>f.ram.size())return false;std::memcpy(p,f.ram.data()+a,n);return true;},
            [&](uint32_t a,const void* p,size_t n){if(++writes==3 || uint64_t(a)+n>f.ram.size())return false;std::memcpy(f.ram.data()+a,p,n);return true;});
        f.state();f.shader();f.color();f.emit(0x20c,0x02000200);f.triangle();f.kick();
        CHECK(!f.failure.empty() && renderer.draws==1 && writes==3);
        const auto after=f.ram;f.failure.clear();f.kick();
        CHECK(renderer.draws==1 && writes==3 && f.ram==after);
    }
#endif
}
