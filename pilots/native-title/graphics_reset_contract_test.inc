static void test_graphics_reset(void)
{
#ifdef PILOT_D3D_PHOBOS
    GRAPHICS_TEST_REQUIRE("reset retained front/back resource fixture",PILOT_D3D_D3DDevice_GetBackBuffer2);
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t saved_parameters[17];memcpy(saved_parameters,graphics_parameters,sizeof(saved_parameters));
    uint32_t headers=graphics_headers,create[]={4,4,1,1,0,6,3},length=64;
    uint32_t texture=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create);
    struct GraphicsTextureObject *t=&graphics_textures[graphics_texture_slot(texture)];
    for(unsigned i=0;i<16;++i)MEM32(t->data+i*4)=0xff123456;
    uint32_t bind[]={0,texture};call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
    uint32_t vertex=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length);
    for(unsigned i=0;i<length;++i)memory[graphics_vertex(vertex)->data+i]=(unsigned char)(i*19);
    uint32_t stream[]={15,vertex,16};call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    /* This fixture verifies already-populated GPU storage survives Reset.
     * Binding alone need not snapshot data before a draw consumes it. */
    pilot_gfx_vertex_upload(graphics_vertex(vertex)->native,
        memory+graphics_vertex(vertex)->data,length);
    uint32_t rs[166];memcpy(rs,memory+PILOT_D3D_RENDER_STATES,sizeof(rs));
#if PILOT_D3D_STATEBLOCK_AVAILABLE
    uint32_t block_args[]={1,0x19984};
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreateStateBlock,block_args)==0);
    uint32_t block=MEM32(block_args[1]),block_header[15];memcpy(block_header,memory+block,sizeof(block_header));
#endif
    unsigned texture_refs=MEM32(texture),vertex_refs=MEM32(vertex);
    uint64_t populated=vm_committed(0,0x100000000ull);
    uint32_t pointer=0x19000,*p=(uint32_t*)(memory+pointer);
    p[4]=0x1021;CHECK(call_graphics(PILOT_D3D_D3DDevice_Reset,&pointer)==0x80004001u);
    CHECK(graphics_parameters[4]==0x11 && pilot_gfx_width()==64);p[4]=0x11;
    for(unsigned repeat=0;repeat<4;++repeat) {
        uint32_t back_index=0;
        CHECK(call_graphics(PILOT_D3D_D3DDevice_GetBackBuffer2,&back_index)==headers);
        CHECK(graphics_surface_refs[0]==1);
        p[0]=repeat&1?64:96;p[1]=64;p[12]=repeat&1?1:0x80000000u;
        CHECK(call_graphics(PILOT_D3D_D3DDevice_Reset,&pointer)==0);
        CHECK(graphics_headers==headers && pilot_gfx_width()==p[0] && pilot_gfx_height()==64);
        CHECK(!graphics_surface_refs[0] && MEM32(headers)==0x010d0000);
        CHECK(MEM32(PILOT_D3D_PRESENT_INTERVAL)==p[12]);
        CHECK(graphics_target==headers && graphics_depth==headers+64 && !av_saved_data);
        rs[(PILOT_D3D_PRESENT_INTERVAL-PILOT_D3D_RENDER_STATES)/4]=p[12];
        CHECK(!memcmp(rs,memory+PILOT_D3D_RENDER_STATES,sizeof(rs)));
#if PILOT_D3D_STATEBLOCK_AVAILABLE
        CHECK(!memcmp(block_header,memory+block,sizeof(block_header)));
#endif
        CHECK(MEM32(texture)==texture_refs && MEM32(vertex)==vertex_refs);
        CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES)==texture);
        CHECK(MEM32(PILOT_D3D_STREAMS+15*12+8)==vertex);
        graphics_sync_deferred();
        uint32_t vp[6];CHECK(pilot_gfx_get_viewport(vp)==0 && vp[0]==0 && vp[1]==0 && vp[2]==p[0] && vp[3]==64 && vp[4]==0 && vp[5]==0x3f800000);
        uint32_t texels[16];CHECK(pilot_gfx_texture_readback(t->native,0,texels,sizeof(texels))==0);
        for(unsigned i=0;i<16;++i)CHECK(texels[i]==0xff123456);
        unsigned char bytes[64];CHECK(pilot_gfx_vertex_readback(graphics_vertex(vertex)->native,bytes,sizeof(bytes))==0);
        CHECK(!memcmp(bytes,memory+graphics_vertex(vertex)->data,sizeof(bytes)));
        uint32_t clear[]={0,0,0xf0,0xff224466,0x3f800000,0},zero=0;
        CHECK(call_graphics(PILOT_D3D_D3DDevice_Clear,clear)==0);
        if(p[12]==0x80000000u) {
            /* A stale paced deadline must not delay an immediate swap. A
             * regression blocks past the test timeout, not a noisy FPS bound. */
            AcquireSRWLockExclusive(&graphics_lock);graphics_swap_deadline=dispatch_monotonic()+600000000;
            ReleaseSRWLockExclusive(&graphics_lock);
        }
        uint32_t swap=call_graphics(PILOT_D3D_D3DDevice_Swap,&zero);
        AcquireSRWLockExclusive(&graphics_lock);
        if(p[12]==0x80000000u)CHECK(!graphics_swap_deadline && (!graphics_present_count || !graphics_presents[0].target));
        graphics_present_drain();CHECK(graphics_swap_completed==swap);
        ReleaseSRWLockExclusive(&graphics_lock);
        unsigned char pixels[96*64*4];CHECK(pilot_gfx_surface_readback(1,pixels,sizeof(pixels))==0);
        CHECK(pixels[0]==0x22 && pixels[1]==0x44 && pixels[2]==0x66);
        CHECK(vm_committed(0,0x100000000ull)==populated);
    }
    /* State-block resource references survive all resets and release normally. */
#if PILOT_D3D_STATEBLOCK_AVAILABLE
    unsigned slot=0;while(slot<128 && graphics_blocks[slot].header!=block)++slot;
    CHECK(slot<128);graphics_block_release(slot);
#endif
    bind[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
    stream[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&texture)==0);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&vertex)==0);
    CHECK(vm_committed(0,0x100000000ull)==resident);
    memcpy(p,saved_parameters,sizeof(saved_parameters));
    fprintf(stderr,"PASS: repeated real display reset/resize, retained texture/vertex ownership, pixels, ABI and immediate swaps\n");
#if PILOT_D3D_STATEBLOCK_AVAILABLE
    fprintf(stderr,"PASS: state-block references/bytes survive display reset\n");
#else
    fprintf(stderr,"SKIP: state-block ownership across reset is unavailable in the selected graphics profile\n");
#endif
#endif
}
