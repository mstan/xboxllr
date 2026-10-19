// Authored context ownership fixtures; no title images or silicon SAVE claim.
static void test_hle_context_transport() {
    {
        auto fixture=std::make_unique<LoadedContextFixture>();auto& f=*fixture;f.color_inputs();f.kick();CHECK(f.failure.empty());
        f.write(0x400784,0x1800);f.write(0x400788,2);
        CHECK(!f.failure.empty() && f.gpu.hle_saved_context_count()==0); // FIFO enabled
    }
    {
        auto fixture=std::make_unique<LoadedContextFixture>();auto& f=*fixture;f.emit(0,1);f.emit(0x17fc,5);f.kick();CHECK(f.failure.empty());
        f.write(0x400720,0);f.write(0x400784,0x1800);f.write(0x400788,2);
        CHECK(!f.failure.empty() && !f.gpu.hle_saved_context_count()); // open primitive
    }
#if XLLE_RENDERER_HLE
    {
        auto fixture=std::make_unique<LoadedContextFixture>();auto& f=*fixture;f.color_inputs();f.kick();CHECK(f.failure.empty());
        f.write(0x400720,0);f.write(0x400784,0x1800);f.write(0x400788,2);
        CHECK(f.failure.empty() && f.gpu.hle_saved_context_count()==1);
        CHECK(f.read(0x7180e8)==0x1000); // independently mapped surface bytes
        f.write(0x400720,1);f.emit(0x210,0x1400);f.emit(0x1d90,0xfedcba98);f.kick();
        f.write(0x400720,0);f.write(0x400784,0x1c00);f.write(0x400788,2);
        CHECK(f.failure.empty() && f.gpu.hle_saved_context_count()==2);
        f.write(0x700f00,0x11223344);CHECK(f.gpu.hle_saved_context_count()==2);
        f.write(0x400784,0x1800);f.write(0x400788,1);CHECK(f.failure.empty());
        CHECK(f.read(0x400828)==0x1000); // LOAD image A was not mutated through B
        f.write(0x400720,1);f.emit(0x1d94,0xf0);f.kick();
        CHECK(f.failure.empty() && get32(f.ram,0x1000)==0x12345678 && f.gpu.color_clears()==1);
        f.write(0x400720,0);f.write(0x400784,0x1c00);f.write(0x400788,1);
        CHECK(f.failure.empty() && f.read(0x400828)==0x1400);
        f.write(0x400720,1);f.emit(0x1d94,0xf0);f.kick();
        CHECK(f.failure.empty() && get32(f.ram,0x1400)==0xfedcba98 && f.gpu.color_clears()==2);
    }
    {
        auto fixture=std::make_unique<LoadedContextFixture>();auto& f=*fixture;f.color_inputs();f.kick();f.write(0x400720,0);
        for(unsigned i=0;i<16;++i) {
            f.write(0x400784,0x2000+i*0x400);f.write(0x400788,2);
            CHECK(f.failure.empty() && f.gpu.hle_saved_context_count()==i+1);
        }
        f.write(0x400784,0x2000);f.write(0x400788,2);
        CHECK(f.failure.empty() && f.gpu.hle_saved_context_count()==16);
        f.write(0x7a0000,0x12345678);f.write(0x400784,0xa000);f.write(0x400788,2);
        CHECK(f.failure.find("capacity exhausted")!=std::string::npos && f.gpu.hle_saved_context_count()==16);
        f.failure.clear();CHECK(f.read(0x7a0000)==0x12345678 && f.failure.empty());
    }
    for(bool reset:{false,true}) {
        auto fixture=std::make_unique<LoadedContextFixture>();auto& f=*fixture;f.color_inputs();f.kick();
        f.write(0x400720,0);f.write(0x400784,0x20);f.write(0x400788,2);
        CHECK(f.failure.empty() && f.gpu.hle_saved_context_count()==1);
        if(reset){f.write(0x400080,0x70000);f.write(0x400080,0);CHECK(f.failure.empty());}
        // The SAVE prefix overlaps the existing graph-object descriptors.
        // A GPU bind must reject their now-unknown bytes, not reuse old RAM.
        f.write(0x400720,1);f.emit(0,1);f.emit(0x50,0xabcd);f.kick();
        if(f.failure.empty())std::fprintf(stderr,"descriptor negative reset=%u missing fault GET=%x PUT=%x status=%x\n",unsigned(reset),f.read(0x800044),f.read(0x800040),f.read(0x400700));
        CHECK(!f.failure.empty() && f.read(0x800048)==0 && !f.gpu.image_blit_copies());
    }
    {
        auto fixture=std::make_unique<LoadedContextFixture>(true,nullptr,0x28c3ff,true);auto& f=*fixture;
        CHECK(f.gpu.debug_hle_default_w_eligibility()!=0);
        f.color_inputs();f.kick();f.write(0x400720,0);
        f.write(0x400784,0x1800);f.write(0x400788,2);f.write(0x400788,1);
        CHECK(f.failure.empty() && f.gpu.debug_hle_default_w_eligibility()!=0);
        f.write(0x400788,2);f.write(0x400080,0x70000);f.write(0x400080,0);
        CHECK(f.failure.empty());
        f.write(0x400750,0x3d0000);for(unsigned i=0;i<15;++i)f.write(0x400754,0);
        f.write(0x400084,0x1118700);f.write(0x400144,0x10000000);f.write(0x400148,0);
        std::vector<uint8_t> image(xlle::Nv2aContextImage::kSourceBytes);
        put32(image,0,1);put32(image,0xe8,0x1800);
        for(uint32_t i=0;i<image.size();i+=4)f.write(0x724000+i,get32(image,i));
        f.write(0x400784,0x2400);f.write(0x400788,1);
        CHECK(f.failure.empty() && f.read(0x400828)==0x1800);
        CHECK(!f.gpu.debug_hle_default_w_eligibility());
        CHECK(f.gpu.debug_raster_state_word(xlle::Nv2a::kRasterSurfaceFormat)==0);
        f.write(0x400144,0x10010100);f.write(0x400720,1);
        f.emit(0,1);f.emit(0x1d94,0xf0);f.kick();
        CHECK(!f.failure.empty() && !f.gpu.color_clears()); // no old bindings/known masks
    }
    {
        PhaseRenderer renderer;unsigned expected_constant=56,observations=0;
        renderer.observe=[&](const xlle::Nv2aHleDraw& draw) {
            CHECK(draw.program.size()>=2 && draw.program[1][1]==(0x0020001bu|(expected_constant<<13)));
            for(unsigned lane=0;lane<4;++lane)
                CHECK(draw.constants[expected_constant][lane]==float(lane==3 || lane==(expected_constant==56?0:1)));
            ++observations;
        };
        auto fixture=std::make_unique<LoadedDrawFixture>();auto& f=*fixture;f.gpu.set_renderer(&renderer);f.state();f.shader(-1,false,true,56);f.color(false,true);
        f.emit(0x1ea4,56);for(unsigned lane=0;lane<4;++lane)
            f.emit(0xb80+lane*4,LoadedDrawFixture::bits(lane==0 || lane==3?1.f:0.f));
        f.triangle();f.kick();if(!f.failure.empty())std::fprintf(stderr,"transport draw: %s\n",f.failure.c_str());CHECK(f.failure.empty());
        f.write(0x400720,0);f.write(0x400784,0x1800);f.write(0x400788,2);
        f.write(0x400720,1);expected_constant=57;f.shader(-1,false,true,57);
        f.emit(0x1ea4,57);for(unsigned lane=0;lane<4;++lane)
            f.emit(0xb80+lane*4,LoadedDrawFixture::bits(lane==1 || lane==3?1.f:0.f));
        f.triangle();f.kick();if(!f.failure.empty())std::fprintf(stderr,"transport draw: %s\n",f.failure.c_str());CHECK(f.failure.empty());
        f.write(0x400720,0);f.write(0x400784,0x1c00);f.write(0x400788,2);
        f.write(0x400784,0x1800);f.write(0x400788,1);f.write(0x400720,1);expected_constant=56;
        f.triangle();f.kick();if(!f.failure.empty())std::fprintf(stderr,"transport draw: %s\n",f.failure.c_str());CHECK(f.failure.empty());
        CHECK(observations==3);
        f.write(0x400720,0);f.write(0x400784,0x1c00);f.write(0x400788,1);f.write(0x400720,1);expected_constant=57;
        f.triangle();f.kick();if(!f.failure.empty())std::fprintf(stderr,"transport draw: %s\n",f.failure.c_str());CHECK(f.failure.empty());
        CHECK(observations==4 && renderer.draws==4 && f.gpu.vertex_batches()==4); // no global rewind
    }
    {
        auto fixture=std::make_unique<LoadedContextFixture>();auto& f=*fixture;f.color_inputs();f.kick();f.write(0x400720,0);
        f.write(0x400784,0x1800);f.write(0x400788,2);
        f.write(0x400080,0x70000);f.write(0x400080,0);
        CHECK(f.failure.empty() && f.gpu.hle_saved_context_count()==1);
        f.write(0x400788,1);CHECK(f.failure.empty() && f.read(0x400828)==0x1000);
        f.write(0x400720,1);f.emit(0x1d94,0xf0);f.kick();
        CHECK(f.failure.empty() && get32(f.ram,0x1000)==0x12345678);
    }
    for(int width:{1,2,4}) {
        auto fixture=std::make_unique<LoadedContextFixture>();auto& f=*fixture;f.color_inputs();f.kick();
        f.write(0x400720,0);f.write(0x400784,0x1800);f.write(0x400788,2);
        CHECK(f.failure.empty() && f.gpu.hle_saved_context_count()==1);
        f.gpu.mmio_read(0xfd718020,1);
        CHECK(f.failure.find("raw readback projection is unknown")!=std::string::npos);
        f.failure.clear();
        f.gpu.mmio_write(0xfd718020,width,0xaabbccdd);
        CHECK(f.gpu.hle_saved_context_count()==0 && f.failure.empty());
        CHECK(f.gpu.mmio_read(0xfd718020,width)==(width==1?0xdd:width==2?0xccdd:0xaabbccdd));
        f.gpu.mmio_read(0xfd718024,1);CHECK(!f.failure.empty()); // adjacent remains unknown
    }
#else
    auto strict_fixture=std::make_unique<LoadedContextFixture>();auto& strict=*strict_fixture;strict.color_inputs();strict.kick();
    strict.write(0x400720,0);strict.write(0x400784,0x1800);strict.write(0x400788,2);
    CHECK(strict.failure.find("context save is not implemented")!=std::string::npos);
    CHECK(!strict.gpu.hle_saved_context_count());
#endif
}
