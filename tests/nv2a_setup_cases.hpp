// Authored hardware-control cases. nxdk supplies the independent RAM-bank
// offsets and original driver sequence; no successful context transfer is
// inferred from a register write or accepted before its implementation.
static void test_method_observer() {
    std::vector<xlle::Nv2a::PushMethod> baseline;
    for(bool bulk:{false,true}) for(bool held:{false,true}) {
        std::string failure;
        xlle::Nv2a gpu([&](const std::string& why){failure=why;});
        std::vector<uint8_t> ram(4096);
        std::vector<xlle::Nv2a::PushMethod> observed;
        CHECK(!gpu.on_method_recorded({}));
        CHECK(gpu.on_method_recorded([&](const xlle::Nv2a::PushMethod& push){observed.push_back(push);}));
        CHECK(!gpu.on_method_recorded([](const xlle::Nv2a::PushMethod&){}));
        gpu.set_pfifo_run_bulk(bulk);
        gpu.set_mem_access(
            [&](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [&](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);return true;},
            [&](uint32_t a,size_t n)->uint8_t*{return uint64_t(a)+n<=ram.size()?ram.data()+a:nullptr;});
        auto write=[&](uint32_t r,uint32_t v){gpu.mmio_write(0xfd000000u+r,4,v);};
        const uint32_t setup[][2]={{0x700008,1},{0x70000c,0x80010020},{0x700200,0x97},
            {0x700180,0x3d},{0x700184,4095},{0x700188,0},
            {0x002504,1},{0x003204,0x100},{0x00322c,0x18},{0x003244,0},
            {0x003220,1},{0x003250,1},{0x003200,1},{0x400720,held?0u:1u}};
        for(auto& r:setup)write(r[0],r[1]);
        uint32_t cursor=0;
        auto packet=[&](uint32_t method,uint32_t n,uint32_t first){
            put32(ram,cursor,0x40000000u|(n<<18)|method);cursor+=4;
            for(uint32_t i=0;i<n;++i){put32(ram,cursor,first+i);cursor+=4;}
        };
        packet(0,1,1);packet(0x17fc,1,1);
        for(uint32_t method:{0x1800u,0x1808u,0x1818u})packet(method,17,method);
        const auto original=ram;
        write(0x800040,cursor);
        if(held){CHECK(observed.empty() && !gpu.push_methods());write(0x400720,1);}
        CHECK(failure.empty() && gpu.mmio_read(0xfd800044,4)==cursor);
        CHECK(observed.size()==53 && gpu.push_methods()==53 && ram==original);
        if(!bulk)CHECK(gpu.pfifo_runs_bulk()==0);
        if(bulk && !held)CHECK(gpu.pfifo_runs_bulk()>=3);
        CHECK(!gpu.on_method_recorded([](const xlle::Nv2a::PushMethod&){}));
        // An empty resume cannot duplicate notifications.
        write(0x400720,1);write(0x800040,cursor);CHECK(observed.size()==53);
        if(baseline.empty())baseline=observed;
        CHECK(observed.size()==baseline.size());
        for(size_t i=0;i<observed.size() && i<baseline.size();++i) {
            const auto& a=observed[i];const auto& b=baseline[i];
            CHECK(a.seq==i && a.channel==b.channel && a.subchannel==b.subchannel &&
                  a.object_class==b.object_class && a.method==b.method && a.parameter==b.parameter &&
                  a.parser_get==b.parser_get && a.parser_put==b.parser_put);
        }
    }
}

static void test_pgraph_setup() {
    std::string failure;
    xlle::Nv2a gpu([&](const std::string& why){failure=why;});
    auto read=[&](uint32_t reg){return gpu.mmio_read(0xfd000000u+reg,4);};
    auto write=[&](uint32_t reg,uint32_t value){gpu.mmio_write(0xfd000000u+reg,4,value);};
    const uint32_t setup[][2]={
        {0x400080,0},{0x400084,0x118700},{0x400880,0x28c3ff},{0x40008c,0xf3de0479},
        {0x400090,0},{0x400094,4},{0x400b80,0x45ead10e},{0x400b84,0},
        {0x400098,0x78},{0x40009c,0x40},{0x400b88,0}};
    for(auto& r:setup){write(r[0],r[1]);CHECK(read(r[0])==r[1]);}
    CHECK(read(0x400700)==0);
    write(0x400084,0x1118700);CHECK(read(0x400084)==0x118700);
    for(uint32_t tile=0;tile<8;++tile) {
        write(0x100244+tile*16,0x1000+tile*0x1000);
        write(0x100248+tile*16,0x200+tile*0x100);
        for(uint32_t word=0;word<3;++word) {
            uint32_t value=read(0x100240+tile*16+word*4);
            write(0x400900+tile*16+word*4,value);
            CHECK(read(0x400900+tile*16+word*4)==value);
            uint32_t address=0xea0010+tile*4+word*0x20;
            write(0x400750,address);write(0x400754,value);
            CHECK(read(0x400750)==address+4);
            write(0x400750,address);CHECK(read(0x400754)==value);
        }
        CHECK(read(0x100300+tile*4)==0);
        write(0x400980+tile*4,0);write(0x400750,0xea0090+tile*4);write(0x400754,0);
    }
    write(0x4009a0,read(0x100324));write(0x4009a4,read(0x100200));write(0x4009a8,read(0x100204));
    CHECK(read(0x4009a4)==0x03070003 && !read(0x4009a0) && !read(0x4009a8));
    write(0x400750,0xea0000);write(0x400754,0x03070003);write(0x400754,0);
    write(0x400750,0xea0000);CHECK(read(0x400754)==0x03070003 && !read(0x400754));
    for(uint32_t r=0x40014c;r<=0x400158;r+=4){write(r,0);CHECK(read(r)==0);}
    write(0x400144,0x10000100);write(0x400148,0x03000000);write(0x400764,0x08000000);
    CHECK(read(0x400144)==0x10000100 && read(0x400148)==0x03000000);
    read(0x400784);CHECK(failure=="NV2A context pointer has not been programmed");failure.clear();
    for(uint32_t instance:{0u,0x111du,0xffffu}) {
        write(0x400784,instance);CHECK(read(0x400784)==instance && failure.empty());
    }
    write(0x400784,0x10000);CHECK(failure=="NV2A unsupported context pointer bits");failure.clear();
    CHECK(read(0x400784)==0xffff);
    for(uint32_t command:{1u,2u,0u,3u}) {
        write(0x400788,command);
        if(command==2) {
#if XLLE_RENDERER_HLE
            CHECK(failure.find("NV2A HLE context SAVE requires an owned idle context")==0);
#else
            CHECK(failure=="NV2A PGRAPH context save is not implemented");
#endif
        } else CHECK(command==1?failure.find("NV2A context load configuration is unsupported:")==0:
            failure=="NV2A unsupported PGRAPH context transfer command");
        failure.clear();
        CHECK(read(0x400784)==0xffff && read(0x400144)==0x10000100 &&
              read(0x400148)==0x03000000 && read(0x400700)==0 && !gpu.push_methods());
    }
    write(0x400080,0x70000);CHECK(read(0x400080)==0x70000);write(0x400080,0);
    // Independent bank map: fifteen FD words at3D:00..38. The reached
    // original context-switch clear must neither execute queued methods nor
    // claim that unknown initial or nonzero active patch state is supported.
    write(0x400750,0x3d0000);read(0x400754);
    CHECK(failure=="NV2A FD state read has no initialized word");failure.clear();
    CHECK(read(0x400750)==0x3d0000);
    for(unsigned i=0;i<15;++i)write(0x400754,0);
    CHECK(failure.empty() && read(0x400750)==0x3d003c && gpu.push_methods()==0);
    write(0x400750,0x3d0000);
    for(unsigned i=0;i<15;++i)CHECK(read(0x400754)==0);
    CHECK(failure.empty() && read(0x400750)==0x3d003c);
    write(0x400754,0);CHECK(!failure.empty() && read(0x400750)==0x3d003c);failure.clear();
    write(0x400750,0x3d0038);write(0x400754,1);
    CHECK(!failure.empty() && read(0x400750)==0x3d0038);failure.clear();
    CHECK(read(0x400754)==0);
    write(0x400080,0x70000);write(0x400080,0);
    write(0x400750,0x3d0038);read(0x400754);
    CHECK(failure=="NV2A FD state read has no initialized word");failure.clear();
    write(0x400754,0);write(0x400750,0x3d0034);read(0x400754);
    CHECK(failure=="NV2A FD state read has no initialized word");failure.clear();
    for(uint32_t address : {0xe00050u,0xdf0008u}) {
        write(0x400750,address);write(0x400754,1);
        CHECK(read(0x400750)==address+4);
        write(0x400750,address);CHECK(read(0x400754)==1);
        write(0x400750,address);write(0x400754,2);
        CHECK(failure=="NV2A unsupported DXT1 noise control transition");failure.clear();
        CHECK(read(0x400750)==address && read(0x400754)==1);
    }
    write(0x7110a0,0x12345678);write(0x200,0xffffefff);
    CHECK(read(0x400084)==0 && read(0x400144)==0 && read(0x400750)==0);
    read(0x400784);CHECK(failure=="NV2A context pointer has not been programmed");failure.clear();
    CHECK(read(0x400904)==0 && read(0x100244)==0x1000 && read(0x7110a0)==0x12345678);
    write(0x400750,0x3d0000);read(0x400754);
    CHECK(failure=="NV2A FD state read has no initialized word");failure.clear();
    for(uint32_t address : {0xe00050u,0xdf0008u}) {
        write(0x400750,address);CHECK(read(0x400754)==0);
    }
    write(0x200,0xffffffff);
    CHECK(failure.empty());
    for(uint32_t flags : {4u,8u,0x04000000u}) {
        write(0x100240,0x10000|flags);
        CHECK(!failure.empty() && read(0x100240)==0);failure.clear();
        write(0x400900,0x10000|flags);
        CHECK(!failure.empty() && read(0x400900)==0);failure.clear();
        write(0x400750,0xea0010);write(0x400754,0x10000|flags);
        CHECK(!failure.empty() && read(0x400750)==0xea0010);failure.clear();
        CHECK(read(0x400754)==0);
    }
    // Base/limit/pitch are programmable state. Actual clients validate the
    // finished bank configuration and consume the layout (command tests).
    for(uint32_t flags : {1u,2u,3u}) {
        write(0x100240,0x10000|flags);CHECK(read(0x100240)==(0x10000|flags));
        write(0x400900,0x10000|flags);CHECK(read(0x400900)==(0x10000|flags));
        write(0x400750,0xea0010);write(0x400754,0x10000|flags);
        CHECK(read(0x400750)==0xea0014);
        write(0x400750,0xea0010);CHECK(read(0x400754)==(0x10000|flags));
    }
    CHECK(failure.empty());
    write(0x40008c,0);CHECK(!failure.empty() && !read(0x40008c));failure.clear();
    write(0x400750,0xea0008);read(0x400754);CHECK(!failure.empty());
}

static void test_pgraph_context_transfer() {
    using Image=xlle::Nv2aContextImage;
    std::vector<uint8_t> image(Image::kSourceBytes);
    for(size_t i=0;i<image.size();++i)image[i]=uint8_t((i*37)^(i>>7)^0xa5);
    put32(image,0,1);
    // Authored distinguishable legal values, in the physical tag map's order.
    // Independent primary slot order is DST,SRC,COLOR,ZETA,DVD,SWIZZLED; pitch
    // has only five slots. Inputs retain both observed limit low-bit forms.
    const uint32_t surface[]={
        0x1100,0x2200,0x3300,0x4400,0x5500,0x6600,
        0x7700,0x8800,0x9900,0xaa00,0xbb00,0xcc00,
        0x140,0x280,0x3c0,0x500,0x640,
        0,0x103f,0x2000,0x303f,0x80004000,0x8000503f};
    for(size_t i=0;i<23;++i)put32(image,0xe0+uint32_t(i)*4,surface[i]);
    const auto original=image;
    const uint32_t modes[][2]={{0x400084,0x118700},{0x40008c,0xf3de0479},{0x400090,0},
        {0x400094,4},{0x400098,0x78},{0x40009c,0x40},{0x400880,0x28c3ff},
        {0x400b80,0x45ead10e},{0x400b84,0},{0x400b88,0}};
    Image decoded;
    const char* error=nullptr;
    CHECK(decoded.load(image.data(),image.size(),error) && !error);
    CHECK(std::equal(image.begin(),image.end(),decoded.source().begin()));
    std::fill(image.begin(),image.end(),0xde);
    CHECK(std::equal(original.begin(),original.end(),decoded.source().begin()));
    for(size_t i=0;i<23;++i)CHECK(decoded.surface_word(i)==(surface[i]|(i>=17?0x3fu:0u)));
    for(size_t slot=0;slot<192;++slot)for(size_t lane=0;lane<4;++lane)
        CHECK(decoded.constant_word(slot,lane)==get32(original,0x231c+uint32_t(slot)*16+uint32_t(3-lane)*4));
    for(const auto& bad:std::vector<std::pair<uint32_t,uint32_t>>{
            {0,0},{0,0x01000001},{0xe0,1},{0x10c,0x40000000},
            {0x110,0x20},{0x120,0x10000},{0x124,1},{0x138,0x4000003f}}) {
        image=original;put32(image,bad.first,bad.second);
        CHECK(!decoded.load(image.data(),image.size(),error) && error);
        CHECK(std::equal(original.begin(),original.end(),decoded.source().begin()));
        CHECK(decoded.constant_word(191,0)==get32(original,0x2f18));
    }
    CHECK(!decoded.load(original.data(),Image::kMeasuredSavePrefixBytes,error));
    CHECK(!decoded.load(nullptr,Image::kSourceBytes,error));
    image=original;put32(image,0,0x11);
    CHECK(decoded.load(image.data(),image.size(),error));

    // Exercise actual PRAMIN ownership and the original control path. A hole
    // in an opaque word or the producer tail is still unknown input, even if
    // its host backing happens to contain zero. Rejected LOAD is atomic.
    enum Rejection { None, FifoEnabled, NoReset, NoFd, NoInvalidate, NoPointer,
                     BadExtent, PrefixHole, TailHole, BadHeader, BadLastField,
                     ChannelValid, WrongChannel, NoInstanceProcessing, MissingZeroMode };
    // Both reached configurations share actual data transfer; no extra
    // provenance/idle/preparation guard is waived for kernel boot.
    for(uint32_t debug7:{0x0028c3ffu,0x0008cfffu})
    for(int reject=None;reject<=MissingZeroMode;++reject) {
        std::string failure;
        xlle::Nv2a gpu([&](const std::string& why){failure=why;});
        auto read=[&](uint32_t r){return gpu.mmio_read(0xfd000000u+r,4);};
        auto write=[&](uint32_t r,uint32_t v){gpu.mmio_write(0xfd000000u+r,4,v);};
        constexpr uint32_t base=0x18000;
        image=original;
        if(reject==BadHeader)put32(image,0,0x1000001);
        if(reject==BadLastField)put32(image,0x138,0x4000003f);
        const uint32_t hole=reject==PrefixHole?0x2345:
                            reject==TailHole?uint32_t(image.size()-1):UINT32_MAX;
        for(uint32_t i=0;i<image.size();i+=4) {
            if((i&~3u)==(hole&~3u)) {
                for(uint32_t j=0;j<4;++j)if(i+j!=hole)
                    gpu.mmio_write(0xfd700000u+base+i+j,1,image[i+j]);
            } else write(0x700000+base+i,get32(image,i));
        }
        if(reject==PrefixHole) {
            const uint32_t address=0xfd700000+base+(hole&~3u);
            const uint32_t kept=gpu.mmio_read(address,4);
            for(int width:{-1,0,3,8}) {
                gpu.mmio_write(address,width,0xffffffff);
                CHECK(!failure.empty());failure.clear();CHECK(gpu.mmio_read(address,4)==kept);
            }
        }
        write(0x700000+base+uint32_t(image.size()),0xcafef00d);
        for(auto& r:modes) {
            if(reject==NoInstanceProcessing && r[0]==0x400084)continue;
            if(reject==MissingZeroMode && r[0]==0x400090)continue;
            write(r[0],r[0]==0x400880?debug7:r[1]);
        }
        write(0x400720,reject==FifoEnabled?1:0);
        write(0x400144,reject==ChannelValid?0x10010100:0x10000100);
        write(0x400148,reject==WrongChannel?0x01000000:0);
        if(reject!=NoReset){write(0x400080,0x70000);write(0x400080,0);}
        if(reject!=NoFd){write(0x400750,0x3d0000);for(unsigned i=0;i<15;++i)write(0x400754,0);}
        if(reject!=NoInstanceProcessing)
            write(0x400084,reject==NoInvalidate?0x118700:0x1118700);
        if(reject!=NoPointer)write(0x400784,reject==BadExtent?0xffff:base>>4);
        CHECK(failure.empty());
        const auto before=gpu.first_context_diagnostic_snapshot();
        write(0x400788,1);
        if(reject!=None) {
            CHECK(!failure.empty());failure.clear();
            const auto after=gpu.first_context_diagnostic_snapshot();
            CHECK(before.pramin==after.pramin && before.context_user==after.context_user &&
                  before.context_control==after.context_control && before.ffintfc==after.ffintfc &&
                  before.fifo_access==after.fifo_access && before.status==after.status &&
                  before.intr==after.intr && after.pending.empty() && !gpu.push_methods());
            CHECK(gpu.debug_raster_state_word(xlle::Nv2a::kRasterSurfacePitch)==0);
            continue;
        }
        CHECK(failure.empty() && read(0x400148)==0x11 && !read(0x400700) && !read(0x400720));
        CHECK(read(0x400880)==debug7); // LOAD cannot silently switch execution mode
        const auto after=gpu.first_context_diagnostic_snapshot();
        CHECK(before.pramin==after.pramin && after.context_control==0x10000100 &&
              after.pending.empty() && !gpu.push_methods() && !gpu.semaphore_writes());
        for(size_t i=0;i<23;++i)CHECK(read(0x400820+uint32_t(i)*4)==(surface[i]|(i>=17?0x3fu:0u)));
        CHECK(gpu.debug_raster_state_word(xlle::Nv2a::kRasterSurfaceColorOffset)==0x3300);
        CHECK(gpu.debug_raster_state_word(xlle::Nv2a::kRasterSurfaceZetaOffset)==0x4400);
        CHECK(gpu.debug_raster_state_word(xlle::Nv2a::kRasterSurfacePitch)==0x050003c0);
        // The active field storage owns its bytes; CPU edits do not alias it.
        write(0x700000+base+0xe8,0xdeadbeef);CHECK(read(0x400828)==0x3300);
        CHECK(read(0x700000+base+uint32_t(image.size()))==0xcafef00d);
        write(0x400148,0x11);CHECK(failure.empty() && read(0x400148)==0x11);
        for(uint32_t value:{0u,1u,0x01000011u}) {
            write(0x400148,value);CHECK(!failure.empty());failure.clear();CHECK(read(0x400148)==0x11);
        }
        // Unknown values do not acquire the preceding frontend's defaults.
        for(uint32_t reg:{0x400144u,0x40014cu,0x400710u,0x400b10u,0x40186cu,0x401a88u,0x400800u}) {
            read(reg);CHECK(!failure.empty());failure.clear();
        }
        write(0x400750,0x3d0000);read(0x400754);CHECK(!failure.empty());failure.clear();
        gpu.mmio_read(0xfd400828,1);CHECK(!failure.empty());failure.clear();
        write(0x400828,0x123400);CHECK(!failure.empty());failure.clear();CHECK(read(0x400828)==0x3300);
        for(uint32_t command:{1u,2u,3u}) {
#if XLLE_RENDERER_HLE
            if(command==2)continue; // admitted semantic SAVE is tested independently
#endif
            const auto saved=gpu.first_context_diagnostic_snapshot();
            write(0x400788,command);CHECK(!failure.empty());failure.clear();
            CHECK(gpu.first_context_diagnostic_snapshot().pramin==saved.pramin && read(0x400148)==0x11);
        }
        // Empty-queue reset must not drop the quarantine while leaving loaded
        // frontend state behind, even though no methods have executed yet.
#if XLLE_RENDERER_HLE
        write(0x400720,1); // explicit discard still requires disabled FIFO
#endif
        write(0x200,0xffffefff);CHECK(!failure.empty());failure.clear();
#if XLLE_RENDERER_HLE
        write(0x400720,0);
#endif
        CHECK(read(0x200)==0xffffffff && read(0x400148)==0x11 && read(0x400828)==0x3300);
        write(0x400080,0x70000);CHECK(!failure.empty());
    }

    for(bool batching:{false,true}) for(bool prefetched:{false,true}) {
        std::string failure;
        xlle::Nv2a gpu([&](const std::string& why){failure=why;});
        std::vector<uint8_t> ram(8192);
        gpu.set_pfifo_run_batching(batching);
        gpu.set_mem_access(
            [&](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [&](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);return true;},
            [&](uint32_t a,size_t n)->uint8_t*{return uint64_t(a)+n<=ram.size()?ram.data()+a:nullptr;});
        auto read=[&](uint32_t r){return gpu.mmio_read(0xfd000000u+r,4);};
        auto write=[&](uint32_t r,uint32_t v){gpu.mmio_write(0xfd000000u+r,4,v);};
        for(auto& r:modes)write(r[0],r[1]);
        const uint32_t packet[]={0x40100,1,0x401a4,2,0x81d6c,4096,0x12345678};
        std::memcpy(ram.data(),packet,sizeof(packet));put32(ram,4096,0xabcdef01);
        const uint32_t setup[][2]={{0x700008,1},{0x70000c,0x80010020},{0x700200,0x97},
            {0x700010,2},{0x700014,0x80010030},{0x700300,0x3d},{0x700304,8191},{0x700308,0},
            {0x700180,0x3d},{0x700184,8191},{0x700188,0},{0x002504,1},{0x003204,0x100},
            {0x00322c,0x18},{0x003244,0},{0x003220,1},{0x003250,1},{0x003200,1},
            {0x400720,0},{0x400144,0x10000000},{0x400148,0},{0x400764,0x18000000},
            {0x400080,0x70000},{0x400080,0},{0x400084,0x1118700},{0x400784,0x1800}};
        for(auto& r:setup)write(r[0],r[1]);
        write(0x400750,0x3d0000);for(unsigned i=0;i<15;++i)write(0x400754,0);
        for(uint32_t i=0;i<original.size();i+=4)write(0x718000+i,get32(original,i));
        if(prefetched)write(0x800040,sizeof(packet));
        const auto before=gpu.first_context_diagnostic_snapshot();
        CHECK(before.pending.size()==(prefetched?4u:0u) && !gpu.push_methods() && failure.empty());
        write(0x400788,1);CHECK(failure.empty());
        auto after=gpu.first_context_diagnostic_snapshot();
        CHECK(after.pending.size()==before.pending.size() && after.context_control==0x10000000 &&
              after.ffintfc==before.ffintfc && !after.fifo_access && !gpu.push_methods() &&
              get32(ram,4096)==0xabcdef01);
        // Original USER rewrite, explicit channel-valid write, then FIFO hold
        // release. The first command has no established object binding. Both
        // parser modes preserve exactly the pending queue at that consumer.
        write(0x400148,0x11);write(0x400144,0x10010100);write(0x400764,0x08000000);
        CHECK(failure.empty());write(0x400720,1);
        if(!prefetched) {
            CHECK(failure.empty());write(0x800040,sizeof(packet));
        }
        CHECK(failure.find("object binding is unknown")!=std::string::npos);
        after=gpu.first_context_diagnostic_snapshot();
        CHECK(after.pending.size()==(prefetched?4u:1u) && !gpu.push_methods() &&
              get32(ram,4096)==0xabcdef01 && !gpu.semaphore_writes() && !gpu.pfifo_runs_batched());
        CHECK(read(0x800044)==(prefetched?uint32_t(sizeof(packet)):8u));
        failure.clear();write(0x800040,sizeof(packet));
        CHECK(failure.find("object binding is unknown")!=std::string::npos);
        CHECK(read(0x800044)==(prefetched?uint32_t(sizeof(packet)):8u));
        for(size_t i=0;i<before.pending.size();++i) {
            CHECK(after.pending[i].method==before.pending[i].method &&
                  after.pending[i].parameter==before.pending[i].parameter &&
                  after.pending[i].parser_get==before.pending[i].parser_get);
        }
        CHECK(read(0x800048)==0);
    }
}

static void test_pgraph_fd_patch_guard() {
    for(bool batching:{false,true}) for(uint32_t method=0x1de0;method<0x1e20;method+=4) {
        std::string failure;
        xlle::Nv2a gpu([&](const std::string& why){failure=why;});
        std::vector<uint8_t> ram(4096);
        gpu.set_pfifo_run_batching(batching);
        gpu.set_mem_access([&](uint32_t a,void* p,size_t n){
            if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [&](uint32_t a,const void* p,size_t n){
            if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);return true;},
            [&](uint32_t a,size_t n)->uint8_t*{return uint64_t(a)+n<=ram.size()?ram.data()+a:nullptr;});
        auto write=[&](uint32_t reg,uint32_t value){gpu.mmio_write(0xfd000000u+reg,4,value);};
        write(0x400750,0x3d0000);for(unsigned i=0;i<15;++i)write(0x400754,0);
        const uint32_t packet[]={0x40000,1,0x40000|method,0};
        std::memcpy(ram.data(),packet,sizeof(packet));
        const uint32_t setup[][2]={{0x700008,1},{0x70000c,0x80010020},{0x700200,0x97},
            {0x700180,0x3d},{0x700184,4095},{0x700188,0},
            {0x002504,1},{0x003204,0x100},{0x00322c,0x18},{0x003244,0},
            {0x003220,1},{0x003250,1},{0x003200,1},{0x400720,1}};
        for(auto& r:setup)write(r[0],r[1]);
        CHECK(failure.empty());write(0x800040,sizeof(packet));
        CHECK(failure=="NV2A FD patch processing is not implemented");
        CHECK(gpu.semaphore_writes()==0);
        CHECK(batching ? gpu.pfifo_runs_batched()>0 : gpu.pfifo_runs_batched()==0);
    }
}

static void test_pgraph_boot_configuration() {
    for(bool batching : {false,true}) for(bool held : {false,true}) {
        std::string failure;
        xlle::Nv2a gpu([&](const std::string& why){failure=why;});
        std::vector<uint8_t> ram(8192);
        gpu.set_pfifo_run_batching(batching);
        gpu.set_mem_access(
            [&](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [&](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);return true;},
            [&](uint32_t a,size_t n)->uint8_t*{return uint64_t(a)+n<=ram.size()?ram.data()+a:nullptr;});
        auto read=[&](uint32_t reg){return gpu.mmio_read(0xfd000000u+reg,4);};
        auto write=[&](uint32_t reg,uint32_t value){gpu.mmio_write(0xfd000000u+reg,4,value);};
        write(0x400880,0x0008cfff);CHECK(failure.empty() && read(0x400880)==0x0008cfff);
        write(0x400880,0x0008cffe);CHECK(!failure.empty() && read(0x400880)==0x0008cfff);
        failure.clear();
        write(0x200,0xffffefff);CHECK(failure.empty() && read(0x400880)==0);
        write(0x200,0xffffffff);write(0x400880,0x0008cfff);
        const uint32_t packet[]={0x40000,1,0x401a4,2,0x81d6c,4096,0x12345678};
        std::memcpy(ram.data(),packet,sizeof(packet));put32(ram,4096,0xabcdef01);
        const uint32_t setup[][2]={
            {0x700008,1},{0x70000c,0x80010020},{0x700200,0x97},
            {0x700010,2},{0x700014,0x80010030},
            {0x700300,0x3d},{0x700304,8191},{0x700308,0},
            {0x700180,0x3d},{0x700184,8191},{0x700188,0},
            {0x002504,1},{0x003204,0x100},{0x00322c,0x18},{0x003244,0},
            {0x003220,1},{0x003250,1},{0x003200,1},{0x400720,held?0u:1u}};
        for(auto& r:setup)write(r[0],r[1]);
        write(0x800040,sizeof(packet));
        if(held) {CHECK(failure.empty());write(0x400720,1);}
        CHECK(failure.find("NV2A boot DEBUG_7 command execution remains unqualified:")==0);
        CHECK(get32(ram,4096)==0xabcdef01 && gpu.push_methods()==0 && gpu.semaphore_writes()==0);
        // A rejected command was retained, not silently completed or lost.
        // Clearing the test's error string is not a production fallback.
        failure.clear();write(0x400880,0x0028c3ff);write(0x400720,1);
        CHECK(failure.empty() && read(0x400880)==0x0028c3ff);
        CHECK(get32(ram,4096)==0x12345678 && gpu.push_methods()==4 && gpu.semaphore_writes()==1);
        // Live reset remains outside the qualified contract; it cannot erase
        // state and appear successful after commands have executed.
        write(0x200,0xffffefff);CHECK(!failure.empty() && read(0x400880)==0x0028c3ff);
    }
}

static void test_pgraph_context_admission() {
    // Exercise both parser selections with directly addressable memory: the
    // run/bulk optimization must not bypass an original-driver context hold.
    for(bool batching : {false,true}) {
        std::string failure;
        xlle::Nv2a gpu([&](const std::string& why){failure=why;});
        std::vector<uint8_t> ram(8192);
        gpu.set_pfifo_run_batching(batching);
        gpu.set_mem_access(
            [&](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [&](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);return true;},
            [&](uint32_t a,size_t n)->uint8_t*{return uint64_t(a)+n<=ram.size()?ram.data()+a:nullptr;});
        auto read=[&](uint32_t reg){return gpu.mmio_read(0xfd000000u+reg,4);};
        auto write=[&](uint32_t reg,uint32_t value){gpu.mmio_write(0xfd000000u+reg,4,value);};
        bool irq=false;
        gpu.set_irq([&](int line,bool level){CHECK(line==3);irq=level;},3);
        const uint32_t packet[]={0x40000,1,0x401a4,2,0x81d6c,4096,0x12345678};
        std::memcpy(ram.data(),packet,sizeof(packet));put32(ram,4096,0xabcdef01);
        const uint32_t setup[][2]={
            {0x700008,1},{0x70000c,0x80010020},{0x700200,0x97},
            {0x700010,2},{0x700014,0x80010030},
            {0x700300,0x3d},{0x700304,8191},{0x700308,0},
            {0x700180,0x3d},{0x700184,8191},{0x700188,0},
            {0x002504,1},{0x003204,0x100},{0x00322c,0x18},{0x003244,0},
            {0x003220,1},{0x003250,1},{0x003200,1},{0x400720,1},
            {0x140,1},{0x400140,0x1000},
            {0x400144,0x10000100},{0x400148,0x03000000},{0x400764,0x08000000}};
        for(auto& r:setup)write(r[0],r[1]);
        write(0x800040,sizeof(packet));
        CHECK(irq && read(0x400100)==0x1000 && read(0x100)==0x1000);
        CHECK(!(read(0x400704)&0x01f00000)); // trapped channel is 0
        CHECK(read(0x400764)&0x10000000);
        CHECK(get32(ram,4096)==0xabcdef01 && gpu.push_methods()==0);
        // FIFO may prefetch while PGRAPH is idle. Neither GET nor STATUS=0
        // can stand in for the semaphore completion in guest memory.
        CHECK(read(0x800044)==sizeof(packet) && read(0x400700)==0);
        write(0x400100,0x1000);
        CHECK(!irq && get32(ram,4096)==0xabcdef01);
        write(0x400144,0x10010100); // valid, but still the wrong channel
        write(0x400764,0x08000000);
        CHECK(irq && get32(ram,4096)==0xabcdef01 && gpu.push_methods()==0);
        write(0x400100,0x1000);write(0x400148,0);
        write(0x400144,0x00010100); // device disabled, even with valid channel
        write(0x400764,0x08000000);
        CHECK(!irq && get32(ram,4096)==0xabcdef01);
        write(0x400144,0x10010100);write(0x400720,1);
        CHECK(get32(ram,4096)==0x12345678 && gpu.semaphore_writes()==1);
        CHECK(gpu.push_methods()==4 && read(0x400700)==0);
        write(0x400764,0x28000000); // explicit FIFO hold
        put32(ram,sizeof(packet),0x41d70);put32(ram,sizeof(packet)+4,0x98765432);
        write(0x800040,sizeof(packet)+8);
        CHECK(get32(ram,4096)==0x12345678 && gpu.semaphore_writes()==1 && !irq);
        write(0x400764,0x08000000);
        CHECK(get32(ram,4096)==0x98765432 && gpu.semaphore_writes()==2 && !irq);
        CHECK(read(0x003280)==1 && read(0x003254)==1);
        // REF_CNT is a real ordered completion counter, mirrored to USER.
        // Neither publication of PUT nor a CPU read may advance it early.
        write(0x400720,0);
        put32(ram,sizeof(packet)+8,0x40050);put32(ram,sizeof(packet)+12,0x11223344);
        write(0x800040,sizeof(packet)+16);
        CHECK(read(0x003248)==0 && read(0x800048)==0);
        write(0x400720,1);
        CHECK(read(0x003248)==0x11223344 && read(0x800048)==0x11223344);
        // Restore a partially parsed non-incrementing method, then resume on
        // its data word. A detached DMA_STATE shadow would parse data as a
        // header or dispatch to the wrong method/subchannel.
        write(0x003200,0);
        write(0x003228,0x48051); // count=1, subchannel=4, method=0x50, noninc
        write(0x00324c,0x101);
        CHECK(read(0x003228)==0x48051 && read(0x00324c)==0x101);
        put32(ram,sizeof(packet)+16,0x55667788);
        write(0x800040,sizeof(packet)+20);
        write(0x003200,1);
        CHECK(read(0x003248)==0x55667788 && read(0x800048)==0x55667788);
        CHECK(read(0x003228)==0x8051 && read(0x00324c)==0x101);
        write(0x003200,0);
        write(0x003204,0x101);write(0x003248,0xaabbccdd);
        CHECK(read(0x003248)==0xaabbccdd && read(0x810048)==0xaabbccdd);
        CHECK(read(0x800048)==0x55667788 && read(0x003228)==0 && read(0x00324c)==0);
        write(0x003204,0x100);
        CHECK(read(0x003248)==0x55667788 && read(0x003228)==0x8051);
        CHECK(read(0x003260)==0 && read(0x003264)==0 && read(0x003268)==0 && read(0x00326c)==0);
        CHECK(failure.empty());
        write(0x003228,0x20000000);CHECK(!failure.empty() && read(0x003228)==0x8051);failure.clear();
        write(0x00326c,1);CHECK(!failure.empty() && read(0x00326c)==0);
    }
}
