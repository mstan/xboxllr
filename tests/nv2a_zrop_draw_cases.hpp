// Authored queued ZROP consumers after real LOAD. Pixel expectations below
// are independent of the compression codec. IMAGE_BLIT observes logical bytes;
// raw payload checks distinguish actual compression selection from plain writes.
struct LoadedZropFixture : LoadedDrawFixture {
    uint32_t deny_begin=0,deny_end=0;
    uint64_t depth_writes=0;
    LoadedZropFixture(bool batching=true) : LoadedDrawFixture(batching) {
        ram.resize(32768,0xa5);write(0x700304,32767);
        gpu.set_mem_access(
            [this](uint32_t a,void* p,size_t n) {
                if(uint64_t(a)+n>ram.size())return false;
                std::memcpy(p,ram.data()+a,n);return true;
            },
            [this](uint32_t a,const void* p,size_t n) {
                if(uint64_t(a)+n>ram.size() || (a<deny_end && uint64_t(a)+n>deny_begin))return false;
                std::memcpy(ram.data()+a,p,n);if(a>=0x4000)depth_writes+=n;return true;
            });
        write(0x100240,0x4001);write(0x100244,0x7fff);write(0x100248,512);
        write(0x100300,0x84000000);
        write(0x400900,0x4001);write(0x400904,0x7fff);write(0x400908,512);
        write(0x400980,0x84000000);
        for(const auto& s:std::vector<std::pair<uint32_t,uint32_t>>{
                {0x10,0x4001},{0x30,0x7fff},{0x50,512},{0x90,0x84000000}}) {
            write(0x400750,0xea0000|s.first);write(0x400754,s.second);
        }
        state();shader();color();emit(0x198,2);emit(0x214,0x4000);emit(0x20c,0x02000040);
    }
    void clear(uint32_t value,bool compressed=true) {
        emit(0x1d80,compressed);emit(0x1d8c,value);
        emit(0x1d98,7u<<16);emit(0x1d9c,7u<<16);emit(0x1d94,3);kick();
        CHECK(failure.empty() && gpu.zeta_clears()==1);
        // Only four blocks were initialized. Padding/neighbor tags stay unknown.
    }
    bool observe() {
        emit(0,3);emit(0x184,2);emit(0x188,2);emit(0x300,0xa);
        emit(0x304,512|(64u<<16));emit(0x308,0x4000);emit(0x30c,0x3000);
        emit(0,5);emit(0x19c,3);
        for(uint32_t m=0x184;m<=0x198;m+=4)emit(m,9);
        emit(0x2fc,3);emit(0x300,0);emit(0x304,0);emit(0x308,8|(8u<<16));kick();
        if(!failure.empty())std::printf("ZROP observation: %s\n",failure.c_str());
        const bool ready=failure.empty() && gpu.image_blit_copies()==1;
        CHECK(ready);return ready;
    }
};

static void test_loaded_zrop_draws() {
    for(bool batching:{false,true})for(bool compress:{false,true}) {
        LoadedZropFixture f(batching);f.clear(0x1234567f,!compress);
        const auto before=f.ram;
        f.emit(0x1d80,compress);f.emit(0x35c,1);f.triangle();f.kick();
        if(!f.failure.empty())std::printf("ZROP depth: %s\n",f.failure.c_str());
        CHECK(f.failure.empty() && f.read(0x800048)==0x1234);
        CHECK(f.gpu.generic_combiner_executed_batches()==1);
        CHECK(get32(f.ram,0x1044)==0xff204060);
        // Whole upper-left block becomes depth1, stencil7F. Its published
        // compressed header is the independently specified constant plane.
        CHECK(get32(f.ram,0x4000)==0x0000017f);
        CHECK(get32(f.ram,0x4004)==(compress?0x80000000u:0x0000017fu));
        // Uninitialized neighboring blocks must not be fetched/re-encoded.
        // For this pitch/bank, each x-block occupies four16-byte rows spaced64.
        for(unsigned i=0x4000;i<0x8000;++i) {
            const unsigned offset=i-0x4000;
            const bool touched=(offset<256 && (offset&63)<32) ||
                               (offset>=1024 && offset<1280 && (offset&63)>=32);
            if(!touched)CHECK(f.ram[i]==before[i]);
        }
        if(!f.observe())continue;
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            const bool covered=x+y<7; // pixel-center edge, top-left fill rule
            CHECK(get32(f.ram,0x3000+y*64+x*4)==(covered?0x0000017fu:0x1234567fu));
        }
    }
    // SFAIL, ZFAIL and ZPASS preserve depth and masked S8 lanes. A varying
    // stencil block cannot use gradient compression and must fall back to raw.
    for(unsigned operation=0;operation<3;++operation) {
        LoadedZropFixture f;f.clear(0x123456a5);
        f.emit(0x32c,1);f.emit(0x360,0x0f);f.emit(0x364,operation==0?0x200:0x207);
        f.emit(0x368,0x3c);f.emit(0x36c,0xff);
        for(unsigned i=0;i<3;++i)f.emit(0x370+i*4,i==operation?0x1e01:0x1e00);
        if(operation==1){f.emit(0x30c,1);f.emit(0x354,0x200);}
        f.triangle();f.kick();
        CHECK(f.failure.empty() && f.read(0x800048)==0x1234);
        CHECK(get32(f.ram,0x1044)==(operation==2?0xff204060u:0xa5a5a5a5u));
        if(!f.observe())continue;
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
            CHECK(get32(f.ram,0x3000+y*64+x*4)==(x+y<7?0x123456acu:0x123456a5u));
        CHECK(get32(f.ram,0x4010)==0x123456ac); // mixed boundary block, raw
    }
    {
        // A known selector cannot invent initial tag contents.
        LoadedZropFixture f;f.emit(0x1d80,1);f.emit(0x35c,1);f.triangle();f.kick();
        CHECK(!f.failure.empty() && !f.read(0x800048) && !f.depth_writes);
        CHECK(get32(f.ram,0x1044)==0xa5a5a5a5 && !f.gpu.generic_combiner_executed_batches());
    }
    for(bool partial:{false,true}) {
        // Fail at the first row, or after one physical row of raw publication.
        // Neither failed data nor the following semaphore may retire as a draw.
        LoadedZropFixture f;f.clear(0x1234567f);
        const auto before=f.depth_writes;
        f.deny_begin=partial?0x4040:0x4000;f.deny_end=f.deny_begin+16;
        f.emit(0x1d80,0);f.emit(0x35c,1);f.triangle();f.kick();
        CHECK(!f.failure.empty() && !f.read(0x800048) && !f.gpu.generic_combiner_executed_batches());
        CHECK(f.depth_writes==before+(partial?16:0));
        const auto methods=f.gpu.push_methods();f.deny_begin=f.deny_end=0;f.failure.clear();f.kick();
        CHECK(!f.failure.empty() && !f.read(0x800048) && f.gpu.push_methods()==methods);
    }
}
