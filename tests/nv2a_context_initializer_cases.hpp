// Queued initializer controls after LoadedContextFixture's real PRAMIN LOAD.
// Include after the context method/draw cases; no private device access.
static void context_initializer_emit(LoadedContextFixture& f, unsigned sub,
                                     uint32_t method, uint32_t value) {
    put32(f.ram, f.put, 0x40000u | (sub << 13) | method);
    put32(f.ram, f.put + 4, value);
    f.put += 8;
}

static uint32_t context_initializer_pramin(LoadedContextFixture& f, uint32_t offset, int size) {
    return f.gpu.mmio_read(0xfd700000u + offset, size);
}

static void context_initializer_objects(LoadedContextFixture& f) {
    // Authored equivalent of the captured graph/DMA/NULL owner classes. Keep
    // their handles familiar, but do not embed title RAM or program tokens.
    const uint32_t setup[][2] = {
        {0x700010,2}, {0x700014,0x80000030}, {0x700300,3},
        {0x700020,4}, {0x700024,0x80000050},
        {0x700500,3}, {0x700504,16383}, {0x700508,0},
        {0x700030,6}, {0x700034,0x80010070}, {0x700700,0x9f},
        {0x700038,7}, {0x70003c,0x800000a0},
        {0x700a00,0x3d}, {0x700a04,0x1f}, {0x700a08,0},
        {0x700068,0xd}, {0x70006c,0x80010020},
        {0x700070,0xe}, {0x700074,0x80010080}, {0x700800,0x39},
        {0x700080,0x10}, {0x700084,0x80010060},
        {0x7000c8,0x19}, {0x7000cc,0x80010090}, {0x700900,0x30},
        {0x700204,0xaabbccdd}, {0x700604,0x11223344}, {0x700704,0x55667788}
    };
    for (const auto& r : setup) f.write(r[0],r[1]);
    CHECK(f.failure.empty());
}

static void context_initializer_bumps(LoadedContextFixture& f, bool overwrite) {
    // Matrix boundaries, negative zero and denormals are legal encodings.
    // Scale/offset are uninterpreted32-bit commands while bump is inactive.
    const uint32_t words[3][6] = {
        {0x3f800000,0x80000000,0xbf800000,0x41000000,0x7fc12345,0xff800000},
        {0x40800000,0,0x40800000,0,0xdeadbeef,0x80000000},
        {0xc1000000,1,0x3f000000,0x80000001,0xffffffff,0x7f800000}
    };
    for (unsigned stage=1; stage<4; ++stage) for (unsigned word=0; word<6; ++word) {
        const uint32_t value=overwrite
            ? (stage==2 && (word==0 || word==2) ? 0x40800000u : 0u)
            : words[stage-1][word];
        f.emit(0x1b00+stage*0x40+0x28+word*4,value);
    }
}

static void test_loaded_context_initializers() {
    for (bool batching : {false,true}) {
        LoadedContextFixture f(batching);
        uint32_t observed=0xaabbccdd;
        for(unsigned lane=0;lane<4;++lane)
            CHECK(!f.gpu.debug_context_eye_vector(lane,observed) && observed==0xaabbccdd);
        f.emit(0,1);
        // One actual incrementing packet covers all three methods. The first
        // word is the reached Blinx command; signed zero/NaN are raw receipts.
        const uint32_t input[]={0x3e808081,0x80000000,0x7fc12345};
        put32(f.ram,f.put,(3u<<18)|0x181c);
        for(unsigned lane=0;lane<3;++lane)put32(f.ram,f.put+4+lane*4,input[lane]);
        f.put+=16;f.emit(0x50,0x1111);f.kick();
        CHECK(f.failure.empty() && f.read(0x800048)==0x1111);
        if(!f.failure.empty()) {std::printf("EYE VECTOR GUARD: %s\n",f.failure.c_str());continue;}
        for(unsigned lane=0;lane<3;++lane)
            CHECK(f.gpu.debug_context_eye_vector(lane,observed) && observed==input[lane]);
        f.emit(0x1820,0x3f800000);f.emit(0x50,0x2222);
        CHECK(f.gpu.debug_context_eye_vector(1,observed) && observed==input[1]);
        f.kick();CHECK(f.failure.empty() && f.read(0x800048)==0x2222);
        CHECK(f.gpu.debug_context_eye_vector(0,observed) && observed==input[0]);
        CHECK(f.gpu.debug_context_eye_vector(1,observed) && observed==0x3f800000);
        CHECK(f.gpu.debug_context_eye_vector(2,observed) && observed==input[2]);
        observed=0xaabbccdd;
        CHECK(!f.gpu.debug_context_eye_vector(3,observed) && observed==0xaabbccdd);
        // Replacement LOAD stays unqualified and must not erase live state.
        f.write(0x400720,0);f.write(0x400788,1);
        CHECK(f.failure.find("replacement context load")!=std::string::npos);
        CHECK(f.gpu.debug_context_eye_vector(0,observed) && observed==input[0]);
        CHECK(f.gpu.debug_context_eye_vector(1,observed) && observed==0x3f800000);
        CHECK(f.gpu.debug_context_eye_vector(2,observed) && observed==input[2]);
    }
    for (bool batching : {false,true}) {
        LoadedContextFixture f(batching);
        // A component command does not fabricate the other two producers.
        f.emit(0,1);f.emit(0x1820,0xff800000);f.emit(0x50,0x1111);f.kick();
        uint32_t observed=0xaabbccdd;
        CHECK(f.failure.empty() && f.read(0x800048)==0x1111);
        CHECK(!f.gpu.debug_context_eye_vector(0,observed) && observed==0xaabbccdd);
        CHECK(f.gpu.debug_context_eye_vector(1,observed) && observed==0xff800000);
        observed=0xaabbccdd;
        CHECK(!f.gpu.debug_context_eye_vector(2,observed) && observed==0xaabbccdd);
        // A raw command receipt does not authorize physical PGRAPH readback.
        f.read(0x401aac);
        CHECK(f.failure.find("no qualified context value")!=std::string::npos);
    }
    for (bool batching : {false,true}) {
        LoadedContextFixture f(batching);
        f.emit(0,1);f.emit(0x181c,0x3e808081);f.emit(0x1824,0);
        const uint32_t stopped_get=f.put+8;
        // End of the three-word family must not admit the adjacent method.
        f.emit(0x1828,0);f.emit(0x50,0x3333);f.kick();
        CHECK(f.failure.find("method 1828")!=std::string::npos);
        CHECK(f.read(0x800044)==stopped_get && !f.read(0x800048));
        CHECK(get32(f.ram,0x1000)==0xa5a5a5a5);
        uint32_t observed=0;
        CHECK(f.gpu.debug_context_eye_vector(0,observed) && observed==0x3e808081);
        f.failure.clear();f.kick();
        CHECK(!f.failure.empty() && !f.read(0x800048));
    }
    for (bool batching : {false,true}) {
        LoadedDrawFixture f;f.gpu.set_pfifo_run_batching(batching);
        f.state();f.shader();f.color();
        f.emit(0x181c,0x3e808081);f.emit(0x1820,0);f.emit(0x1824,0x3f800000);
        // The stored setter cannot authorize constant-eye reflection draws.
        f.emit(0x1e70,0x12);f.triangle();f.emit(0x50,0x3333);
        const auto before=f.ram;f.kick();
        CHECK(f.failure.find("texture shader side effects")!=std::string::npos);
        CHECK(f.gpu.generic_combiner_executed_batches()==0 && !f.read(0x800048));
        CHECK(f.ram==before);
    }
    for (bool batching : {false,true}) {
        LoadedContextFixture f(batching);
        context_initializer_objects(f);
        context_initializer_emit(f,1,0,0xe); // M2MF
        context_initializer_emit(f,2,0,0x10); // blit
        context_initializer_emit(f,0,0,0xd); // Kelvin
        context_initializer_emit(f,1,0x180,7);
        for (uint32_t m=0x184; m<=0x198; m+=4)
            context_initializer_emit(f,2,m,0x19);
        f.emit(0x180,2); f.emit(0x190,4);
        const uint32_t setters[][2] = {
            {0xa50,0}, {0xa54,0}, {0xa58,0}, {0xa5c,0x3f800000},
            {0x16bc,1}, {0x1e68,0x7f800000},
            {0x9d0,0}, {0x9d4,0}, {0x9d8,0x3f800000}, {0x9dc,0},
            {0x380,0}, {0x380,8}, {0x2b4,0}, {0x9f8,4}, {0x147c,0},
            {0x1d84,0}, {0x1d84,1}, {0x1d84,3},
            {0x1d7c,0}, {0x1d7c,0xffff0000},
            {0xae0,0}, {0xae4,0}, {0xae8,0}, {0xaec,0},
            {0x1e98,0}, {0x43c,8}, {0x17c4,0}
        };
        for (const auto& r : setters) f.emit(r[0],r[1]);
        context_initializer_bumps(f,false);
        // Actual memory consequence, not a counter-only accepted setter batch.
        // Opaque 1D84=3 is retained without inventing a blanket clear blocker.
        f.emit(0x194,2); f.emit(0x198,2); f.emit(0x208,0x128);
        f.emit(0x1d90,0x12345678); f.emit(0x1d8c,0x87654321);
        f.emit(0x1d98,1u<<16); f.emit(0x1d9c,1u<<16);
        f.emit(0x1d94,0xf3); f.emit(0x50,0x31415926); f.kick();
        CHECK(f.failure.empty() && f.gpu.color_clears()==1 && f.gpu.zeta_clears()==1);
        CHECK(f.read(0x800048)==0x31415926);
        for (unsigned y=0; y<2; ++y) for (unsigned px=0; px<2; ++px) {
            CHECK(get32(f.ram,0x1000+y*64+px*4)==0x12345678);
            CHECK(get32(f.ram,0x1100+y*64+px*4)==0x87654321);
        }
        CHECK(get32(f.ram,0x1008)==0xa5a5a5a5 && get32(f.ram,0x1108)==0xa5a5a5a5);
        // Successful notifier selection changes only the owner projection,
        // never the selected DMA descriptor's readable backing bytes.
        CHECK(f.read(0x700a00)==0x3d && f.read(0x700a04)==0x1f && f.read(0x700a08)==0);
    }

    for (bool batching : {false,true}) {
        LoadedDrawFixture f;
        f.gpu.set_pfifo_run_batching(batching);
        f.state();f.shader();f.color();
        // Append MOV oT0,v9 to the authored program. Stage0 is PROJECT2D;
        // stages1..3 are NONE, so none consumes the bump command receipts.
        f.emit(0x1e9c,2);
        f.emit(0xb00,0);f.emit(0xb04,0x0020081b);f.emit(0xb08,0x08000000);f.emit(0xb0c,0x0000f820);
        f.emit(0xb10,0);f.emit(0xb14,0x0020121b);f.emit(0xb18,0x08000000);f.emit(0xb1c,0x0000f849);
        for (unsigned lane=0; lane<4; ++lane)
            f.emit(0x1a90+lane*4,LoadedDrawFixture::bits(lane==3?1:lane==0?.625f:0));
        f.emit(0x184,2);f.emit(0x1e70,1);f.emit(0x1b00,0x3000);
        f.emit(0x1b04,0x00210629);f.emit(0x1b08,0x303);f.emit(0x1b0c,0x40000000);
        f.emit(0x1b14,0x01010000);
        f.emit(0xac0,0x08200000);f.emit(0x260,0x18200000);
        put32(f.ram,0x3000,0xff0000ff);put32(f.ram,0x3004,0xff00ff00);
        put32(f.ram,0x3008,0xffff0000);put32(f.ram,0x300c,0xffffffff);
        context_initializer_bumps(f,false);
        f.emit(0x181c,0x3e808081);f.emit(0x1820,0x80000000);f.emit(0x1824,0x7fc12345);
        f.triangle();f.kick();
        CHECK(f.failure.empty() && f.gpu.generic_combiner_executed_batches()==1);
        CHECK(get32(f.ram,0x1044)==0xffff0000 && f.read(0x800048)==0x1234);
        const std::vector<uint8_t> before(f.ram.begin()+0x1000,f.ram.begin()+0x1200);
        // Reprogram every receipt, including the reached stage2 matrix4.0.
        // Inactive bump state must not affect the entire rendered target.
        context_initializer_bumps(f,true);
        f.emit(0x181c,0xff800000);f.emit(0x1820,0xffffffff);f.emit(0x1824,0x3f800000);
        f.triangle();f.emit(0x50,0x5678);f.kick();
        CHECK(f.failure.empty() && f.gpu.generic_combiner_executed_batches()==2);
        CHECK(f.read(0x800048)==0x5678);
        CHECK(std::equal(before.begin(),before.end(),f.ram.begin()+0x1000));
        f.read(0x40181c);
        CHECK(f.failure.find("no qualified context value")!=std::string::npos);
        f.failure.clear();
        // Receipts confer no authority to execute either bump shader mode.
        f.emit(0x1e70,1u|((batching?7u:6u)<<5));f.triangle();f.kick();
        CHECK(f.failure.find("texture shader side effects")!=std::string::npos);
        CHECK(f.gpu.generic_combiner_executed_batches()==2 && f.read(0x800048)==0x5678);
        CHECK(std::equal(before.begin(),before.end(),f.ram.begin()+0x1000));
    }
    for (unsigned word=0; word<6; ++word) {
        LoadedContextFixture f;
        f.emit(0,1);const uint32_t stopped_get=f.put+8;
        f.emit(0x1b28+word*4,0);f.emit(0x50,0x1234);f.kick();
        CHECK(f.failure.find("stage0 bump commands")!=std::string::npos);
        CHECK(f.read(0x800044)==stopped_get && f.gpu.push_methods()==1);
        CHECK(!f.read(0x800048) && get32(f.ram,0x1000)==0xa5a5a5a5);
    }
    const uint32_t invalid_bumps[][2] = {
        {0x1b68,0x41000001},{0x1bac,0xc1000001},
        {0x1bf0,0x7f800000},{0x1bf4,0x7fc00000}
    };
    for (const auto& command : invalid_bumps) {
        LoadedContextFixture f;
        f.emit(0,1);const uint32_t stopped_get=f.put+8;
        f.emit(command[0],command[1]);f.emit(0x50,0x1234);f.kick();
        CHECK(f.failure.find("bump matrix command")!=std::string::npos);
        CHECK(f.read(0x800044)==stopped_get && f.gpu.push_methods()==1);
        CHECK(!f.read(0x800048) && get32(f.ram,0x1000)==0xa5a5a5a5);
        f.failure.clear();f.kick();
        CHECK(!f.failure.empty() && f.gpu.push_methods()==1 && !f.read(0x800048));
    }

    for (bool batching : {false,true}) {
        LoadedDrawFixture f;f.gpu.set_pfifo_run_batching(batching);
        f.state();f.shader();f.color(false,true);
        for (unsigned slot=0; slot<16; ++slot) f.emit(0x1760+slot*4,0);
        f.emit(0x1760,0x1042);f.emit(0x176c,0x0444);
        f.emit(0x19c,2);f.emit(0x1720,0x3000);f.emit(0x172c,0x3100);
        auto backing=[&](float extent,uint32_t color) {
            unsigned index=0;
            for (const auto& xy : {std::pair{0.f,0.f},std::pair{extent,0.f},std::pair{0.f,extent}}) {
                const uint32_t v[]={LoadedDrawFixture::bits(xy.first),LoadedDrawFixture::bits(xy.second),
                                    LoadedDrawFixture::bits(1),LoadedDrawFixture::bits(1)};
                for (unsigned lane=0; lane<4; ++lane) put32(f.ram,0x3000+index*16+lane*4,v[lane]);
                put32(f.ram,0x3100+index*4,color);++index;
            }
        };
        auto indexed=[&](uint32_t ref) {
            f.emit(0x17fc,5);for (unsigned i=0;i<3;++i)f.emit(0x1808,i);
            f.emit(0x17fc,0);f.emit(0x50,ref);f.kick();
        };
        backing(8,0xff0000ff);indexed(0x1111);
        CHECK(f.failure.empty() && get32(f.ram,0x1044)==0xffff0000);
        CHECK(get32(f.ram,0x1050)==0xffff0000);
        backing(4,0xff00ff00);f.emit(0x1710,0);indexed(0x2222);
        CHECK(f.failure.empty() && f.read(0x800048)==0x2222);
        // Same indices must fetch changed positions/colors, not cached output.
        CHECK(get32(f.ram,0x1044)==0xff00ff00 && get32(f.ram,0x1050)==0xffff0000);
        f.emit(0x176c,0);f.emit(0x1710,0);f.triangle();f.emit(0x50,0x3333);f.kick();
        // Breaking the buffer cache must preserve the last actual current color.
        CHECK(f.failure.empty() && f.read(0x800048)==0x3333);
        CHECK(get32(f.ram,0x1050)==0xff00ff00 && f.gpu.generic_combiner_executed_batches()==3);
    }
    for (bool active : {false,true}) {
        LoadedContextFixture f;f.emit(0,1);if(active)f.emit(0x17fc,5);
        const uint32_t stopped_get=f.put+8;
        f.emit(0x1710,active?0:1);f.emit(0x50,0x1234);f.kick();
        CHECK(f.failure.find(active?"active assembly":"zero command")!=std::string::npos);
        CHECK(f.read(0x800044)==stopped_get && f.gpu.push_methods()==(active?2u:1u));
        CHECK(!f.read(0x800048) && get32(f.ram,0x1000)==0xa5a5a5a5);
    }

    for (unsigned bad=0; bad<4; ++bad) {
        LoadedContextFixture f;
        f.write(0x700204,0xaabbccdd);
        f.write(0x700038,7); f.write(0x70003c,bad==2?0x800100a0:0x800000a0);
        f.write(0x700a00,bad==1?0x62:0x3d);
        f.write(0x700a04,bad==0?0x1e:0x1f);
        if (bad!=3) f.write(0x700a08,0); // case3: frame bytes are not produced
        f.emit(0,1);
        const uint32_t stopped_get=f.put+8;
        f.emit(0x180,7); f.emit(0x50,0x1234); f.kick();
        CHECK(!f.failure.empty() && !f.read(0x800048));
        CHECK(f.read(0x800044)==stopped_get && f.gpu.push_methods()==1);
        // Failed preflight must not create an owner receipt/readback guard.
        f.failure.clear();
        CHECK(f.read(0x700204)==0xaabbccdd && f.failure.empty());
        f.kick();
        CHECK(!f.failure.empty() && f.gpu.push_methods()==1 && !f.read(0x800048));
    }

    {
        LoadedContextFixture f;
        context_initializer_objects(f);
        f.emit(0,1); f.emit(0x190,4); f.emit(0x50,1); f.kick();
        CHECK(f.failure.empty() && f.read(0x800048)==1);
        // STATE has no notifier/cache owner-PRAMIN mutation in this contract.
        CHECK(f.read(0x700200)==0x97 && f.read(0x700204)==0xaabbccdd);
        CHECK(f.failure.empty());
    }
    {
        LoadedContextFixture f;
        context_initializer_objects(f);
        // Two Kelvin instances bound on the same subchannel must not share
        // notifier projection state merely because their classes match.
        f.write(0x700700,0x97);
        f.emit(0,1); f.emit(0x180,7); f.emit(0,6); f.emit(0x50,2); f.kick();
        CHECK(f.failure.empty() && f.read(0x800048)==2);
        CHECK(f.read(0x700700)==0x97 && f.read(0x700704)==0x55667788);
        CHECK(context_initializer_pramin(f,0x200,1)==0x97);
        CHECK(context_initializer_pramin(f,0x204,2)==0xccdd);
        CHECK(context_initializer_pramin(f,0x205,1)==0xcc);
        CHECK(f.failure.empty());
        context_initializer_pramin(f,0x206,1);
        CHECK(f.failure.find("projection readback")!=std::string::npos); f.failure.clear();
        context_initializer_pramin(f,0x207,1);
        CHECK(f.failure.find("projection readback")!=std::string::npos); f.failure.clear();
        f.gpu.mmio_write(0xfd700206,1,0x55);
        CHECK(context_initializer_pramin(f,0x206,1)==0x55 && f.failure.empty());
        context_initializer_pramin(f,0x207,1);
        CHECK(f.failure.find("projection readback")!=std::string::npos); f.failure.clear();
        f.gpu.mmio_write(0xfd700207,1,0x66);
        CHECK(f.read(0x700204)==0x6655ccdd && f.failure.empty());
        // A later real selector makes that owner projection unknown again.
        f.emit(0,1); f.emit(0x180,7); f.emit(0x50,3); f.kick();
        CHECK(f.failure.empty() && f.read(0x800048)==3);
        f.read(0x700204);
        CHECK(f.failure.find("projection readback")!=std::string::npos);
    }
    {
        LoadedContextFixture f;
        context_initializer_objects(f);
        f.emit(0,5); f.emit(0x184,0x19); f.emit(0,6); f.emit(0x50,4); f.kick();
        CHECK(f.failure.empty() && f.read(0x800048)==4);
        CHECK(f.read(0x700700)==0x9f && f.read(0x700704)==0x55667788);
        CHECK(context_initializer_pramin(f,0x600,1)==0x9f);
        CHECK(f.read(0x700604)==0x11223344 && f.read(0x700900)==0x30);
        CHECK(f.failure.empty());
        for (uint32_t byte=1; byte<4; ++byte) {
            context_initializer_pramin(f,0x600+byte,1);
            CHECK(f.failure.find("projection readback")!=std::string::npos);
            f.failure.clear();
        }
        f.gpu.mmio_write(0xfd700601,3,0); // rejected write cannot clear knowledge guards
        CHECK(!f.failure.empty()); f.failure.clear();
        context_initializer_pramin(f,0x601,1);
        CHECK(f.failure.find("projection readback")!=std::string::npos); f.failure.clear();
        f.gpu.mmio_write(0xfd700601,1,0x10);
        CHECK(context_initializer_pramin(f,0x601,1)==0x10 && f.failure.empty());
        context_initializer_pramin(f,0x602,1);
        CHECK(f.failure.find("projection readback")!=std::string::npos); f.failure.clear();
        // Replacing the CPU-visible header establishes exactly those bytes;
        // it does not invent a saved GPU projection or change class byte9F.
        f.write(0x700600,0x0102009f);
        CHECK(f.read(0x700600)==0x0102009f && f.failure.empty());
        f.emit(0,5); f.emit(0x198,0x19); f.emit(0x50,5); f.kick();
        CHECK(f.failure.empty() && f.read(0x800048)==5);
        CHECK(context_initializer_pramin(f,0x600,1)==0x9f);
        context_initializer_pramin(f,0x602,1);
        CHECK(f.failure.find("projection readback")!=std::string::npos);
    }
    {
        LoadedContextFixture f;
        context_initializer_objects(f);
        f.emit(0,5); f.emit(0x184,2); f.emit(0x50,6); f.kick();
        CHECK(f.failure.find("only explicit NULL")!=std::string::npos && !f.read(0x800048));
        f.failure.clear();
        CHECK(f.read(0x700600)==0x9f && f.failure.empty());
    }

    for (bool unsupported : {false,true}) {
        LoadedContextFixture f;
        f.emit(0,1); f.emit(0x194,2);
        // Actual sample layout must be known and single-sample. Raster MSAA
        // enable/mask is not a clear dependency, unlike ordinary drawing.
        if(unsupported)f.emit(0x208,0x1128);
        f.emit(0x1d90,0x12345678); f.emit(0x1d98,1u<<16); f.emit(0x1d9c,1u<<16);
        f.emit(0x1d7c,0xffff0000);
        const auto before=f.gpu.push_methods();
        const uint32_t stopped_get=f.put+8;
        f.emit(0x1d94,0xf0); f.emit(0x50,7); f.kick();
        CHECK(f.failure.find("single-sample profile")!=std::string::npos);
        CHECK(!f.gpu.color_clears() && get32(f.ram,0x1000)==0xa5a5a5a5 && !f.read(0x800048));
        CHECK(f.read(0x800044)==stopped_get && f.gpu.push_methods()>before);
        const auto retired=f.gpu.push_methods(); f.failure.clear(); f.kick();
        CHECK(!f.failure.empty() && f.gpu.push_methods()==retired && !f.read(0x800048));
    }
    {
        LoadedContextFixture f;
        f.write(0x700204,0xaabbccdd);
        // Read widths need the same portable bounds as writes, even when no
        // initializer mask overlaps. A wide read must not overwrite its host
        // uint32 result buffer, and negative widths must not wrap a bound.
        for (int width : {0,3,8,-1}) {
            CHECK(context_initializer_pramin(f,0x204,width)==0);
            CHECK(!f.failure.empty()); f.failure.clear();
        }
        CHECK(f.read(0x700204)==0xaabbccdd && f.failure.empty());
    }
}
