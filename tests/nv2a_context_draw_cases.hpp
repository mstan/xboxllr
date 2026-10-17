// Authored queued draws through the real LOAD, decoder, vertex engine and CPU
// rasterizer. No original title bytes or direct private-state mutation.
struct LoadedDrawFixture : LoadedContextFixture {
    using LoadedContextFixture::LoadedContextFixture;
    static uint32_t bits(float v){uint32_t w;std::memcpy(&w,&v,4);return w;}
    void state(int omit=-1) {
        emit(0,1);emit(0x194,2);
        const uint32_t words[][2]={
            {0x208,0x128},{0x200,8u<<16},{0x204,8u<<16},{0x20c,0x00400040},
            {0x290,0x00100001},{0x2c0,7u<<16},{0x2e0,7u<<16},
            {0x300,0},{0x304,0},{0x308,0},{0x30c,0},{0x310,0},{0x318,0},
            {0x31c,0},{0x320,0},{0x324,0},{0x32c,0},{0x330,0},{0x334,0},{0x338,0},
            {0x17bc,0},{0x358,0x01010101},{0x35c,0},{0x37c,0x1d01},
            {0x38c,0x1b02},{0x390,0x1b02},{0x394,0},{0x398,0x4b7fffff},
            {0x1d78,1},{0x1e70,0},{0x1e60,1},
            {0xac0,0x04200000},{0x260,0x14200000},{0x1e40,0xc00},{0xaa0,0xc00},
            {0x288,0xc},{0x28c,0x1c80},
            {0x1e94,2},{0x1ea0,0},{0x17cc,0},{0x2b4,0},{0x147c,0},
            {0x1d7c,0xffff0000},{0x1e98,0},{0x9f8,4},{0x17c4,0},
        };
        for(const auto& w:words)if(int(w[0])!=omit)emit(w[0],w[1]);
        for(unsigned stage=0;stage<4;++stage)emit(0x1b0c+64*stage,0);
    }
    void shader(int missing_word=-1,bool missing_color=false,bool constant_color=false,unsigned constant_index=56) {
        emit(0x1e9c,0);
        // MOV oPos,v0; MOV oD0,v3 (or c56); MOV oD1,v4,FINAL.
        // Exact encoding follows the maintained Kelvin token decoder.
        const uint32_t program[]={0,0x0020001b,0x08000000,0x0000f800,
            0,constant_color?(0x0020001bu|(constant_index<<13)):0x0020061bu,
            constant_color?0x0c000000u:0x08000000u,0x0000f818,
            0,0x0020081b,0x08000000,0x0000f821};
        for(unsigned i=0;i<12;++i)if(int(i)!=missing_word && !(missing_color && i>=4 && i<8))
            emit(0xb00+i*4,program[i]);
    }
    void color(bool primary=true,bool secondary=true) {
        if(primary)emit(0x194c,0xff604020); // v3 RGBA=(20,40,60,FF)
        if(secondary)emit(0x1950,0);        // v4
    }
    void dependency_shader(bool indexed) {
        emit(0x1e9c,0);
        // Optional ARL a0,v5.w; MOV oPos,v0;
        // DP3 oD0,c56[+a0],c56[+a0]; MOV oD1,v4,FINAL.
        // Dot(1,0,0) with itself gives RGBA=1. The unread constant W comes
        // from LOAD; primary input v3 deliberately has no producer here.
        const uint32_t program[]={0,0x01a00aff,0x08000000,0,
            0,0x0020001b,0x08000000,0x0000f800,
            0,0x00a7001b,0x0c361800,indexed?0x0000f81au:0x0000f818u,
            0,0x0020081b,0x08000000,0x0000f821};
        for(unsigned i=indexed?0:4,j=0;i<16;++i,++j)emit(0xb00+j*4,program[i]);
    }
    void xyz_constant(unsigned index,unsigned lanes=3) {
        emit(0x1ea4,index);
        for(unsigned lane=0;lane<lanes;++lane)emit(0xb80+lane*4,bits(lane==0?1.f:0.f));
    }
    void triangle(bool missing_first_x=false) {
        emit(0x17fc,5);
        unsigned i=0;
        for(const auto& xy:{std::pair{0.f,0.f},std::pair{8.f,0.f},std::pair{0.f,8.f}}) {
            if(!missing_first_x || i)emit(0x1518,bits(xy.first));
            emit(0x151c,bits(xy.second));emit(0x1520,bits(1));emit(0x1524,bits(1));++i;
        }
        emit(0x17fc,0);emit(0x50,0x1234);
    }
};

static void test_loaded_context_draws() {
    // Distinguishable loaded constants, including both bank ends and c122.
    // Actual queued shaders/pixels check raw WZYX to logical RGBA conversion,
    // producer knowledge and retained live ownership after PRAMIN is changed.
    std::array<std::array<uint32_t,4>,192> loaded{};
    const float values[]={0.f,.25f,.5f,1.f};
    const uint32_t colors[]={0,64,128,255};
    for(unsigned slot=0;slot<192;++slot)for(unsigned lane=0;lane<4;++lane)
        loaded[slot][lane]=LoadedDrawFixture::bits(values[(slot>>(2*lane))&3]);
    auto expected_pixel=[&](unsigned slot) {
        return (colors[(slot>>6)&3]<<24)|(colors[slot&3]<<16)|
               (colors[(slot>>2)&3]<<8)|colors[(slot>>4)&3];
    };
    for(bool compiled:{false,true})for(bool batching:{false,true})for(unsigned group=0;group<12;++group) {
        LoadedDrawFixture f(batching,&loaded);f.gpu.set_vsh_compile_enabled(compiled);
        for(unsigned i=0;i<768;++i)f.write(0x718000+0x231c+i*4,0);
        f.state();f.color(false,true);
        for(unsigned slot=group*16;slot<(group+1)*16;++slot) {
            f.shader(-1,false,true,slot);f.triangle();f.kick();
            CHECK(f.failure.empty() && f.read(0x800048)==0x1234);
            CHECK(get32(f.ram,0x1044)==expected_pixel(slot));
        }
        CHECK(f.gpu.transform_constant_words()==0);
        if(group==7) {
            // Ordinary partial uploads replace X/Y only. Z/W retain LOAD,
            // and the original source is not reconstructed from CPU memory.
            f.emit(0x1ea4,122);f.emit(0xb80,LoadedDrawFixture::bits(.75f));
            f.emit(0xb84,LoadedDrawFixture::bits(.5f));
            f.shader(-1,false,true,122);f.triangle();f.kick();
            CHECK(f.failure.empty() && f.gpu.transform_constant_words()==2);
            CHECK(get32(f.ram,0x1044)==((expected_pixel(122)&0xff0000ffu)|0x00bf8000u));
        }
    }
    for(bool compiled:{false,true}) {
        // The original c122 source is zero: an equal CPU color-cache request
        // can skip its upload, while the loaded GPU value remains produced.
        LoadedDrawFixture f;f.gpu.set_vsh_compile_enabled(compiled);
        f.state();f.shader(-1,false,true,122);f.color(false,true);f.triangle();f.kick();
        CHECK(f.failure.empty() && get32(f.ram,0x1044)==0 && !f.gpu.transform_constant_words());
    }
    for(bool compiled:{false,true})for(bool complete:{false,true}) {
        LoadedDrawFixture f;f.gpu.set_vsh_compile_enabled(compiled);
        f.state();f.dependency_shader(false);f.color(false,true);
        f.xyz_constant(56,complete?3:2);f.triangle();f.kick();
        // The unuploaded Z in the partial case is a known zero from LOAD.
        CHECK(f.failure.empty() && get32(f.ram,0x1044)==0xffffffff && f.read(0x800048)==0x1234);
    }
    for(bool compiled:{false,true})for(unsigned scenario=0;scenario<5;++scenario) {
        LoadedDrawFixture f;f.gpu.set_vsh_compile_enabled(compiled);
        f.state();f.dependency_shader(true);f.color(false,true);
        f.xyz_constant(57,scenario==2?2:3);
        if(scenario!=1)f.emit(0x1a5c,LoadedDrawFixture::bits(scenario==3?136.f:0.9995f));
        if(scenario!=4)f.triangle();
        else {
            // The third vertex selects untouched c58, whose zero value comes
            // from LOAD. Its actual different color must survive interpolation.
            f.emit(0x17fc,5);unsigned i=0;
            for(const auto& xy:{std::pair{0.f,0.f},std::pair{8.f,0.f},std::pair{0.f,8.f}}) {
                if(i++==2)f.emit(0x1a5c,LoadedDrawFixture::bits(1.9995f));
                f.emit(0x1518,LoadedDrawFixture::bits(xy.first));f.emit(0x151c,LoadedDrawFixture::bits(xy.second));
                f.emit(0x1520,LoadedDrawFixture::bits(1));f.emit(0x1524,LoadedDrawFixture::bits(1));
            }
            f.emit(0x17fc,0);f.emit(0x50,0x1234);
        }
        f.kick();
        if(scenario==0 || scenario==2 || scenario==4) {
            CHECK(f.failure.empty() && f.read(0x800048)==0x1234);
            if(scenario!=4)CHECK(get32(f.ram,0x1044)==0xffffffff);
            else CHECK(get32(f.ram,0x1044)==0xcfcfcfcf);
            CHECK(f.gpu.generic_combiner_executed_batches()==1);
        } else {
            const char* expected[]={"","ARL source lane","c57","relative context index","c58"};
            CHECK(f.failure.find(expected[scenario])!=std::string::npos);
            CHECK(get32(f.ram,0x1044)==0xa5a5a5a5 && !f.read(0x800048));
            CHECK(!f.gpu.generic_combiner_executed_batches());
        }
    }
    // TX_SHADER_CULL_MODE carries compare polarity, not a clip enable. The
    // source-defined low16 bits are inert for NONE/PROJECT2D/CUBEMAP, whereas
    // CLIPPLANE remains unsupported even if no combiner references its color.
    for(bool batching:{false,true})for(uint32_t polarity:{0u,0xa53cu,0xffffu}) {
        LoadedDrawFixture f;f.gpu.set_pfifo_run_batching(batching);
        f.state();f.emit(0x17f8,polarity);f.shader();f.color();f.triangle();f.kick();
        CHECK(f.failure.empty() && f.read(0x800048)==0x1234);
        CHECK(f.gpu.debug_raster_state_word(xlle::Nv2a::kRasterShaderClipPlaneMode)==polarity);
        CHECK(get32(f.ram,0x1044)==0xff204060);
        // An illegal high bit must stop before changing the stored word or
        // publishing the following queued completion marker.
        f.emit(0x17f8,0x10000u|polarity);f.emit(0x50,0x5678);f.kick();
        CHECK(f.failure.find("clip-plane comparison encoding")!=std::string::npos);
        CHECK(f.gpu.debug_raster_state_word(xlle::Nv2a::kRasterShaderClipPlaneMode)==polarity);
        CHECK(f.read(0x800048)==0x1234 && get32(f.ram,0x1044)==0xff204060);
    }
    for(unsigned stage=0;stage<4;++stage)for(uint32_t polarity:{0u,0xffffu}) {
        LoadedDrawFixture f;f.state();f.emit(0x17f8,polarity);
        f.emit(0x1e70,5u<<(stage*5));f.shader();f.color();f.triangle();f.kick();
        CHECK(f.failure.find("texture shader side effects are unqualified")!=std::string::npos);
        CHECK(!f.read(0x800048) && !f.gpu.generic_combiner_executed_batches());
        CHECK(get32(f.ram,0x1044)==0xa5a5a5a5);
    }
    for(bool compiled:{false,true}) {
        LoadedDrawFixture f;f.gpu.set_vsh_compile_enabled(compiled);
        f.state();f.shader();f.color();f.triangle();f.kick();
        if(!f.failure.empty())std::fprintf(stderr,"loaded draw: %s\n",f.failure.c_str());
        CHECK(f.failure.empty() && f.read(0x800048)==0x1234);
        CHECK(f.gpu.generic_combiner_executed_batches()==1);
        CHECK(get32(f.ram,0x1000+64+4)==0xff204060);
        CHECK(get32(f.ram,0x1000+7*64+7*4)==0xa5a5a5a5);
    }
    for(int missing:{0x290,0x2c0,0x300,0x35c,0x1ea0,0x1e98,0x1d7c,0x17c4}) {
        LoadedDrawFixture f;f.state(missing);f.shader();f.color();f.triangle();f.kick();
        CHECK(!f.failure.empty() && !f.read(0x800048));
        CHECK(!f.gpu.generic_combiner_executed_batches() && get32(f.ram,0x1044)==0xa5a5a5a5);
    }
    {
        LoadedDrawFixture f;f.state();f.emit(0x17c4,1);f.shader();f.color();f.triangle();f.kick();
        CHECK(f.failure.find("two-sided")!=std::string::npos && !f.read(0x800048));
        CHECK(!f.gpu.generic_combiner_executed_batches() && get32(f.ram,0x1044)==0xa5a5a5a5);
    }
    for(unsigned failure=0;failure<4;++failure) {
        LoadedDrawFixture f;f.state();f.shader(failure==0?1:-1);
        f.color(failure!=1,failure!=2);f.triangle(failure==3);f.kick();
        CHECK(!f.failure.empty() && !f.read(0x800048));
        CHECK(!f.gpu.generic_combiner_executed_batches() && get32(f.ram,0x1044)==0xa5a5a5a5);
        const auto count=f.gpu.push_methods();f.failure.clear();f.kick();
        CHECK(!f.failure.empty() && f.gpu.push_methods()==count && !f.read(0x800048));
    }
    // Draw8 in the retained original queue enables LESS while depth writes
    // stay disabled. Check the loaded owner with independent passing/failing
    // stored depths, and require both comparison and descriptor producers.
    for(unsigned test=0;test<4;++test) {
        LoadedDrawFixture f;f.state();f.shader();f.color();
        f.emit(0x30c,1);if(test!=2)f.emit(0x354,0x201);
        if(test!=3)f.emit(0x198,2);f.emit(0x214,0x2000);
        const uint32_t depth=test==1?0:0xffffff00;
        for(unsigned i=0;i<128;++i)put32(f.ram,0x2000+i*4,depth);
        f.triangle();f.kick();
        if(test<2) {
            CHECK(f.failure.empty() && f.read(0x800048)==0x1234);
            CHECK(get32(f.ram,0x1044)==(test==0?0xff204060u:0xa5a5a5a5u));
        } else {
            CHECK(!f.failure.empty() && !f.read(0x800048));
            CHECK(get32(f.ram,0x1044)==0xa5a5a5a5);
        }
        for(unsigned i=0;i<128;++i)CHECK(get32(f.ram,0x2000+i*4)==depth);
    }
    // Original startup never writes17CC. It cannot block unrelated pixels,
    // but neither its stale host carrier nor a later setter supplies count
    // history. Check both inherited-enable and inherited-count cases.
    for(bool unknown_enable:{false,true})for(bool clear_after:{false,true}) {
        LoadedDrawFixture f;f.state(0x17cc);f.shader();f.color();
        if(unknown_enable)f.emit(0x17c8,1);
        else f.emit(0x17cc,1);
        f.triangle();f.kick();
        CHECK(f.failure.empty() && get32(f.ram,0x1044)==0xff204060);
        f.emit(0x17cc,0); // does not repair prior unknown accumulation
        if(clear_after)f.emit(0x17c8,1);
        f.emit(0x1a8,2);f.emit(0x17d0,0x01003200);f.emit(0x50,0x5678);f.kick();
        if(clear_after) {
            CHECK(f.failure.empty() && f.read(0x800048)==0x5678);
            CHECK(get32(f.ram,0x3208)==0 && get32(f.ram,0x320c)==0);
        } else {
            CHECK(f.failure.find("report input is unknown")!=std::string::npos);
            CHECK(f.read(0x800048)==0x1234 && get32(f.ram,0x320c)==0xa5a5a5a5);
        }
    }
    {
        LoadedDrawFixture f;f.state(0x17cc);
        f.emit(0x17c8,1);f.emit(0x1a8,2);f.emit(0x17d0,0x01003200);f.kick();
        // No intervening draw: explicit count clear is sufficient even when
        // the enable is unknown. This does not assume disabled counting.
        CHECK(f.failure.empty() && get32(f.ram,0x3208)==0 && get32(f.ram,0x320c)==0);
    }
    {
        LoadedDrawFixture f;f.state();f.emit(0x17cc,2);f.emit(0x50,0x5678);f.kick();
        CHECK(f.failure.find("visibility enable must be boolean")!=std::string::npos);
        CHECK(!f.read(0x800048));
    }
    {
        LoadedDrawFixture f;f.ram.resize(32768,0xa5);f.write(0x700304,32767);
        f.write(0x100240,0x4001);f.write(0x100244,0x7fff);f.write(0x100248,512);
        f.write(0x100300,0x84000000);
        f.write(0x400900,0x4001);f.write(0x400904,0x7fff);f.write(0x400908,512);
        f.write(0x400980,0x84000000);
        for(const auto& setting:std::vector<std::pair<uint32_t,uint32_t>>{
                {0x10,0x4001},{0x30,0x7fff},{0x50,512},{0x90,0x84000000}}) {
            f.write(0x400750,0xea0000|setting.first);f.write(0x400754,setting.second);
        }
        f.state();f.shader();f.color();f.emit(0x198,2);f.emit(0x214,0x4000);
        f.emit(0x20c,0x02000040);f.emit(0x35c,1);f.triangle();f.kick();
        CHECK(f.failure.find("qualified ZROP writer")!=std::string::npos && !f.read(0x800048));
        CHECK(get32(f.ram,0x1044)==0xa5a5a5a5 && get32(f.ram,0x4000)==0xa5a5a5a5);
    }
    // Actual alias c56 is consumed by the shader. A partial alias write keeps
    // LOAD's W=0; unrelated c57/c137 writes must not supply that alpha lane.
    for(bool complete:{false,true}) {
        LoadedDrawFixture f;f.state();f.shader(-1,false,true);f.color(false,true);
        for(unsigned lane=0;lane<(complete?4u:3u);++lane)f.emit(0xa50+lane*4,LoadedDrawFixture::bits(1));
        for(unsigned lane=0;lane<4;++lane)f.emit(0x9d0+lane*4,LoadedDrawFixture::bits(1));
        f.triangle();f.kick();
        CHECK(f.failure.empty() && get32(f.ram,0x1044)==(complete?0xffffffffu:0x00ffffffu) && f.read(0x800048)==0x1234);
    }
    for(bool use_inline:{false,true}) {
        LoadedDrawFixture f;f.state();f.shader();f.color(false,true);
        for(unsigned slot=0;slot<16;++slot)f.emit(0x1760+slot*4,0);
        f.emit(0x1760,0x1042); // v0 float4, stride16
        f.emit(0x176c,0x0444); // v3 RGBA bytes, stride4
        if(!use_inline) {
            f.emit(0x19c,2);f.emit(0x1720,0x3000);f.emit(0x172c,0x3100);
        }
        f.emit(0x17fc,5);
        unsigned index=0;
        for(const auto& xy:{std::pair{0.f,0.f},std::pair{8.f,0.f},std::pair{0.f,8.f}}) {
            const uint32_t color=index==2?0xff00ff00u:0xff0000ffu;
            const uint32_t vertex[]={LoadedDrawFixture::bits(xy.first),LoadedDrawFixture::bits(xy.second),
                                     LoadedDrawFixture::bits(1),LoadedDrawFixture::bits(1)};
            if(use_inline) {
                for(auto w:vertex)f.emit(0x1818,w);
                f.emit(0x1818,color);
            } else {
                for(unsigned lane=0;lane<4;++lane)put32(f.ram,0x3000+index*16+lane*4,vertex[lane]);
                put32(f.ram,0x3100+index*4,color);f.emit(0x1808,index);
            }
            ++index;
        }
        f.emit(0x17fc,0);f.kick();
        CHECK(f.failure.empty() && f.gpu.generic_combiner_executed_batches()==1);
        // Disabled v3 retains the last actual fetched/decoded vertex color.
        // No immediate current-color setter supplies the second draw.
        f.emit(0x176c,0);f.triangle();f.kick();
        CHECK(f.failure.empty() && f.gpu.generic_combiner_executed_batches()==2);
        CHECK(get32(f.ram,0x1044)==0xff00ff00 && f.read(0x800048)==0x1234);
    }
    // Direct pitch-linear colors never consume palette state: exercise an
    // unknown palette, explicit zero and an inaccessible stale address, with
    // no tile, an unrelated tile and an overlapping logical texture tile.
    // The physical tile transfer itself is covered by tiled_textures().
    for(unsigned color:{0x12u,0x1eu,0xbu})for(unsigned tile=0;tile<3;++tile)
    for(unsigned palette=0;palette<3;++palette) {
        LoadedDrawFixture f;f.ram.resize(0x8000,0xa5);f.write(0x700304,0x7fff);
        if(tile) {
            for(uint32_t base:{0x100240u,0x400900u}) {
                f.write(base,0x4001);f.write(base+4,0x7fff);f.write(base+8,512);
            }
            for(const auto& setting:std::vector<std::pair<uint32_t,uint32_t>>{
                    {0x10,0x4001},{0x30,0x7fff},{0x50,512}}) {
                f.write(0x400750,0xea0000|setting.first);f.write(0x400754,setting.second);
            }
        }
        f.state();f.shader();f.color();f.emit(0x1e9c,2);
        f.emit(0xb00,0);f.emit(0xb04,0x0020081b);f.emit(0xb08,0x08000000);f.emit(0xb0c,0x0000f820);
        f.emit(0xb10,0);f.emit(0xb14,0x0020121b);f.emit(0xb18,0x08000000);f.emit(0xb1c,0x0000f849);
        const float uv[]={2.5f,.5f,0,1};
        for(unsigned lane=0;lane<4;++lane)f.emit(0x1a90+lane*4,LoadedDrawFixture::bits(uv[lane]));
        const uint32_t texture=tile==2?0x4000:0x3000;
        f.emit(0x184,2);f.emit(0x1e70,1);f.emit(0x1b00,texture);
        f.emit(0x1b04,0x00010029|(color<<8));f.emit(0x1b08,0x303);
        f.emit(0x1b0c,0x40000000);f.emit(0x1b10,512u<<16);
        f.emit(0x1b14,0x01010000);f.emit(0x1b1c,0x00040001);
        if(palette)f.emit(0x1b20,palette==1?0:0x03ffffc1);
        f.emit(0xac0,0x08200000);f.emit(0x260,0x18200000);
        put32(f.ram,texture,0x120000ff);put32(f.ram,texture+4,0x3400ff00);
        put32(f.ram,texture+8,0x56ff0000);put32(f.ram,texture+12,0x78ffffff);
        f.triangle();f.kick();
        if(color==0xb) {
            CHECK(f.failure.find("indexed texture sampling is unqualified")!=std::string::npos);
            CHECK(!f.read(0x800048) && get32(f.ram,0x1044)==0xa5a5a5a5);
            CHECK(!f.gpu.generic_combiner_executed_batches());
        } else {
            if(!f.failure.empty())std::fprintf(stderr,"loaded linear texture: %s\n",f.failure.c_str());
            CHECK(f.failure.empty() && f.read(0x800048)==0x1234);
            CHECK(f.gpu.generic_combiner_executed_batches()==1);
            CHECK(get32(f.ram,0x1044)==(color==0x12?0x56ff0000u:0xffff0000u));
        }
    }
    for(bool compiled:{false,true})for(unsigned mode:{0u,1u})for(bool write_q:{false,true})
    for(bool unrelated_tile:{false,true}) {
        LoadedDrawFixture f;f.gpu.set_vsh_compile_enabled(compiled);
        if(unrelated_tile) {
            // The active tile is outside this direct-color texture and target.
            // It cannot make this nonindexed fetch consume unknown palette state.
            f.ram.resize(0x8000,0xa5);
            for(uint32_t base:{0x100240u,0x400900u}) {
                f.write(base,0x4001);f.write(base+4,0x7fff);f.write(base+8,512);
            }
            for(const auto& setting:std::vector<std::pair<uint32_t,uint32_t>>{
                    {0x10,0x4001},{0x30,0x7fff},{0x50,512}}) {
                f.write(0x400750,0xea0000|setting.first);f.write(0x400754,setting.second);
            }
        }
        f.state();f.shader();f.color();
        f.emit(0x1e9c,2); // reopen final instruction, then append MOV oT0,v9
        f.emit(0xb00,0);f.emit(0xb04,0x0020081b);f.emit(0xb08,0x08000000);f.emit(0xb0c,0x0000f820);
        f.emit(0xb10,0);f.emit(0xb14,0x0020121b);f.emit(0xb18,0x08000000);
        f.emit(0xb1c,write_q?0x0000f849:0x0000c849);
        for(unsigned lane=0;lane<4;++lane)f.emit(0x1a90+lane*4,LoadedDrawFixture::bits(lane==3?1:lane==0?.625f:0));
        f.emit(0x184,2);f.emit(0x1e70,mode);f.emit(0x1b00,0x3000);
        f.emit(0x1b04,0x00210629);f.emit(0x1b08,0x303);f.emit(0x1b0c,0x40000000);
        f.emit(0x1b14,0x01010000); // explicit nearest minification/magnification
        f.emit(0xac0,0x08200000);f.emit(0x260,0x18200000);
        put32(f.ram,0x3000,0xff0000ff);put32(f.ram,0x3004,0xff00ff00);
        put32(f.ram,0x3008,0xffff0000);put32(f.ram,0x300c,0xffffffff);
        f.triangle();f.kick();
        if(!f.failure.empty())std::fprintf(stderr,"loaded texture: %s\n",f.failure.c_str());
        CHECK(f.failure.empty() && f.gpu.generic_combiner_executed_batches()==1);
        CHECK(get32(f.ram,0x1044)==0xffff0000 && f.read(0x800048)==0x1234);
        if(mode && write_q) {
            // An observable full-Q=2 control samples a different texel. After
            // that invocation, partial XY must regain Q=1, not retain Q=2.
            f.emit(0x1a9c,LoadedDrawFixture::bits(2));f.triangle();f.kick();
            CHECK(f.failure.empty() && get32(f.ram,0x1044)==0xff00ff00);
            f.emit(0x1e9c,3);f.emit(0xb0c,0x0000c849);f.triangle();f.kick();
            CHECK(f.failure.empty() && get32(f.ram,0x1044)==0xffff0000);
            CHECK(f.gpu.generic_combiner_executed_batches()==3 && f.read(0x800048)==0x1234);
        }
    }
}
