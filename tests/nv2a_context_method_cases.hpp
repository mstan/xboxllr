// Actual queued-method tests after a real PRAMIN LOAD, not direct handler calls.
struct LoadedContextFixture {
    std::string failure;
    std::vector<uint8_t> ram=std::vector<uint8_t>(16384,0xa5);
    xlle::Nv2a gpu{[this](const std::string& why){failure=why;}};
    uint32_t put=0;
    uint32_t read(uint32_t r){return gpu.mmio_read(0xfd000000u+r,4);}
    void write(uint32_t r,uint32_t v){gpu.mmio_write(0xfd000000u+r,4,v);}
    void emit(uint32_t method,uint32_t value) {
        put32(ram,put,0x40000|method);put32(ram,put+4,value);put+=8;
    }
    void kick(){write(0x800040,put);}
    LoadedContextFixture(bool batching=true,const std::array<std::array<uint32_t,4>,192>* constants=nullptr,
                         uint32_t debug7=0x0028c3ffu,bool cold_board=false) {
        if(cold_board)CHECK(gpu.initialize_board_vga_reset());
        gpu.set_pfifo_run_batching(batching);
        gpu.set_mem_access(
            [this](uint32_t a,void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(p,ram.data()+a,n);return true;},
            [this](uint32_t a,const void* p,size_t n){if(uint64_t(a)+n>ram.size())return false;std::memcpy(ram.data()+a,p,n);return true;},
            [this](uint32_t a,size_t n)->uint8_t*{return uint64_t(a)+n<=ram.size()?ram.data()+a:nullptr;});
        const uint32_t modes[][2]={{0x400084,0x118700},{0x40008c,0xf3de0479},{0x400090,0},
            {0x400094,4},{0x400098,0x78},{0x40009c,0x40},{0x400880,0x28c3ff},
            {0x400b80,0x45ead10e},{0x400b84,0},{0x400b88,0}};
        for(auto& r:modes)write(r[0],r[0]==0x400880?debug7:r[1]);
        const uint32_t setup[][2]={{0x700008,1},{0x70000c,0x80010020},{0x700200,0x97},
            {0x700010,2},{0x700014,0x80010030},{0x700300,0x3d},{0x700304,16383},{0x700308,0},
            {0x700018,3},{0x70001c,0x80010040},{0x700400,0x62},
            {0x700028,5},{0x70002c,0x80010060},{0x700600,0x9f},
            {0x700048,9},{0x70004c,0x80010090},{0x700900,0x30},
            {0x700180,0x3d},{0x700184,16383},{0x700188,0},{0x002504,1},{0x003204,0x100},
            {0x00322c,0x18},{0x003244,0},{0x003220,1},{0x003250,1},{0x003200,1},
            {0x400720,0},{0x400144,0x10000000},{0x400148,0},{0x400764,0x18000000},
            {0x400080,0x70000},{0x400080,0},{0x400084,0x1118700},{0x400784,0x1800}};
        for(auto& r:setup)write(r[0],r[1]);
        write(0x400750,0x3d0000);for(unsigned i=0;i<15;++i)write(0x400754,0);
        std::vector<uint8_t> image(xlle::Nv2aContextImage::kSourceBytes);
        put32(image,0,1);put32(image,0xe8,0x1000);put32(image,0xec,0x1100);
        put32(image,0xe0,0x1300);put32(image,0xe4,0x1200);
        put32(image,0x110,64);put32(image,0x114,64);
        put32(image,0x118,64);put32(image,0x11c,64);
        if(constants)for(unsigned slot=0;slot<192;++slot)for(unsigned lane=0;lane<4;++lane)
            put32(image,0x231c+slot*16+(3-lane)*4,(*constants)[slot][lane]);
        for(uint32_t i=0;i<image.size();i+=4)write(0x718000+i,get32(image,i));
        write(0x400788,1);
        write(0x400148,0x11);write(0x400144,0x10010100);write(0x400764,0x08000000);
        write(0x400720,1);CHECK(failure.empty());
    }
    void color_inputs(int omit=-1) {
        emit(0,1);
        if(omit!=0)emit(0x194,2);
        if(omit!=1)emit(0x208,0x128);
        emit(0x1d7c,0xffff0000);
        if(omit!=2)emit(0x1d90,0x12345678);
        if(omit!=3)emit(0x1d98,1u<<16);
        if(omit!=4)emit(0x1d9c,1u<<16);
    }
};

static void test_loaded_context_methods() {
    // Same already-qualified method contracts under both exact configurations.
    // Invalid sources/draw inputs retain their queued consumer in either mode.
    for(uint32_t debug7:{0x0028c3ffu,0x0008cfffu}) {
    for(unsigned copy_case=0;copy_case<3;++copy_case) {
        const bool operation_known=copy_case!=0, selectors_known=copy_case!=2;
        auto fixture=std::make_unique<LoadedContextFixture>(true,nullptr,debug7);auto& f=*fixture;
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
            put32(f.ram,0x1200+y*64+x*4,0x11220000+y*2+x);
        f.emit(0,3);f.emit(0x184,2);f.emit(0x188,2);f.emit(0x300,0xa);
        f.emit(0,5);f.emit(0x19c,3);
        if(selectors_known)for(uint32_t m=0x184;m<=0x198;m+=4)f.emit(m,9);
        if(operation_known)f.emit(0x2fc,3);
        f.emit(0x300,0);f.emit(0x304,0);f.emit(0x308,0x00020002);
        f.emit(0x50,0x9876);f.kick();
        if(operation_known && selectors_known) {
            CHECK(f.failure.empty() && f.gpu.image_blit_copies()==1 && f.read(0x800048)==0x9876);
            for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
                CHECK(get32(f.ram,0x1300+y*64+x*4)==0x11220000+y*2+x);
        } else {
            CHECK(f.failure.find(selectors_known?"2D copy input is unknown":"context selectors are unknown")!=std::string::npos);
            CHECK(!f.gpu.image_blit_copies() && get32(f.ram,0x1300)==0xa5a5a5a5 && !f.read(0x800048));
        }
    }
    for(bool batching:{false,true}) {
        auto fixture=std::make_unique<LoadedContextFixture>(batching,nullptr,debug7);auto& f=*fixture;
        f.color_inputs();f.emit(0x1d94,0xf0);
        f.emit(0x1a4,2);f.emit(0x1d6c,0x1200);f.emit(0x1d70,0x11223344);
        f.emit(0x50,0x87654321);f.kick();
        CHECK(f.failure.empty() && f.gpu.color_clears()==1 && f.gpu.semaphore_writes()==1);
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
            CHECK(get32(f.ram,0x1000+y*64+x*4)==0x12345678);
        CHECK(get32(f.ram,0x1008)==0xa5a5a5a5 && get32(f.ram,0x1100)==0xa5a5a5a5);
        CHECK(get32(f.ram,0x1200)==0x11223344 && f.read(0x800048)==0x87654321);
        CHECK(f.read(0x40186c)==0x12345678 && f.read(0x400828)==0x1000);
        // Base/limit readback cannot return stale LOAD values after a new DMA
        // binding. The independent raw read contract stays explicitly guarded.
        f.read(0x400840);CHECK(!f.failure.empty());f.failure.clear();
        f.emit(0x20c,0x00800080);f.emit(0x210,0x1040);f.emit(0x214,0x1140);f.kick();
        CHECK(f.failure.empty() && f.read(0x400858)==128 && f.read(0x40085c)==128);
        CHECK(f.read(0x400828)==0x1040 && f.read(0x40082c)==0x1140);
        // One enable setter must not overwrite any other bit of the word.
        f.emit(0x300,1);f.emit(0x304,1);f.emit(0x300,0);f.kick();
        CHECK(f.failure.empty() && (f.gpu.debug_raster_state_word(xlle::Nv2a::kRasterEnables)&3)==2);
    }
    for(int omit=0;omit<6;++omit) {
        auto fixture=std::make_unique<LoadedContextFixture>(true,nullptr,debug7);auto& f=*fixture;
        f.color_inputs(omit);
        // Case5 requests depth too, without its binding/value. Color must not
        // be cleared before discovering the missing second-plane inputs.
        const uint32_t clear_get=f.put+8;
        f.emit(0x1d94,omit==5?0xf3:0xf0);f.emit(0x50,0x1234);f.kick();
        CHECK(f.failure.find("unknown")!=std::string::npos);
        CHECK(!f.gpu.color_clears() && get32(f.ram,0x1000)==0xa5a5a5a5);
        CHECK(f.read(0x800044)==clear_get && !f.read(0x800048));
        const auto count=f.gpu.push_methods();f.failure.clear();f.kick();
        CHECK(!f.failure.empty() && f.gpu.push_methods()==count && !f.read(0x800048));
    }
    {
        auto fixture=std::make_unique<LoadedContextFixture>(true,nullptr,debug7);auto& f=*fixture;
        f.emit(0,1);f.emit(0x194,0xdeadbeef);f.emit(0x50,0x1234);f.kick();
        CHECK(f.failure.find("RAMHT lookup failed")!=std::string::npos);
        CHECK(!f.read(0x800048));const auto count=f.gpu.push_methods();
        f.failure.clear();f.kick();
        CHECK(f.failure.find("previous method failed")!=std::string::npos);
        CHECK(f.gpu.push_methods()==count && !f.read(0x800048));
    }
    {
        auto fixture=std::make_unique<LoadedContextFixture>(true,nullptr,debug7);auto& f=*fixture;f.color_inputs();
        // Known handle and binding, but the depth descriptor itself has never
        // been supplied. Catch this before even the independently valid color.
        f.write(0x700020,4);f.write(0x700024,0x80010050);
        f.emit(0x198,4);f.emit(0x1d8c,0x76543210);f.emit(0x1d94,0xf3);f.kick();
        CHECK(f.failure.find("depth clear input is unknown")!=std::string::npos);
        CHECK(!f.gpu.color_clears() && get32(f.ram,0x1000)==0xa5a5a5a5);
    }
    for(bool missing_offset:{false,true}) {
        auto fixture=std::make_unique<LoadedContextFixture>(true,nullptr,debug7);auto& f=*fixture;f.emit(0,1);f.emit(0x1a4,2);
        if(!missing_offset)f.emit(0x1d6c,16383); // full DWORD would cross limit
        f.emit(0x1d70,0x11223344);f.emit(0x50,1);f.kick();
        CHECK(!f.failure.empty() && !f.gpu.semaphore_writes() && !f.read(0x800048));
    }
    {
        auto fixture=std::make_unique<LoadedContextFixture>(true,nullptr,debug7);auto& f=*fixture;
        // Active compressed RAM starts at4000; color remains in linear RAM.
        // No draw is pending while the matching three client banks are set.
        f.write(0x100240,0x4001);f.write(0x100244,0x7fff);f.write(0x100248,512);
        f.write(0x100300,0x84000000);
        f.write(0x400900,0x4001);f.write(0x400904,0x7fff);f.write(0x400908,512);
        f.write(0x400980,0x84000000);
        for(const auto& setting:std::vector<std::pair<uint32_t,uint32_t>>{
                {0x10,0x4001},{0x30,0x7fff},{0x50,512},{0x90,0x84000000}}) {
            f.write(0x400750,0xea0000|setting.first);f.write(0x400754,setting.second);
        }
        // Extend the explicitly initialized DMA descriptor/host backing too.
        f.ram.resize(32768,0xa5);f.write(0x700304,32767);
        f.color_inputs();f.emit(0x198,2);f.emit(0x214,0x4000);
        f.emit(0x20c,0x02000040);f.emit(0x1d8c,0x76543210);f.emit(0x1d94,0xf3);
        f.kick();
        CHECK(f.failure.find("compression selector is unknown")!=std::string::npos);
        CHECK(!f.gpu.color_clears() && get32(f.ram,0x1000)==0xa5a5a5a5 &&
              get32(f.ram,0x4000)==0xa5a5a5a5);
    }
    {
        auto fixture=std::make_unique<LoadedContextFixture>(true,nullptr,debug7);auto& f=*fixture;
        f.write(0x40071c,2);CHECK(f.failure.find("unknown")!=std::string::npos);f.failure.clear();
        f.emit(0,1);f.emit(0x120,0);f.emit(0x124,1);f.emit(0x128,3);f.kick();
        CHECK(f.failure.empty());f.write(0x40071c,2);
        CHECK(f.failure.empty() && f.read(0x400710)==0x31100000);
    }
    {
        auto fixture=std::make_unique<LoadedContextFixture>(true,nullptr,debug7);auto& f=*fixture;
        f.emit(0,1);f.emit(0xb00,0x12345678);f.emit(0x50,1);f.kick();
        CHECK(f.failure.find("cursor is unknown")!=std::string::npos && !f.read(0x800048));
    }
    {
        auto fixture=std::make_unique<LoadedContextFixture>(true,nullptr,debug7);auto& f=*fixture;f.emit(0,1);f.emit(0x1808,0);f.emit(0x50,1);f.kick();
        CHECK(f.failure.find("outside BEGIN/END")!=std::string::npos && !f.read(0x800048));
    }
}
}
