// Queued initializer controls after LoadedContextFixture's real PRAMIN LOAD.
// Include after nv2a_context_method_cases.hpp; no private device access.
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

static void test_loaded_context_initializers() {
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
        f.emit(0,1); f.emit(0x194,2); f.emit(0x208,0x128);
        f.emit(0x1d90,0x12345678); f.emit(0x1d98,1u<<16); f.emit(0x1d9c,1u<<16);
        if (unsupported) f.emit(0x1d7c,0xffff0001); // legal setter, unsupported consumer
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
