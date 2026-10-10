static void alias_test_draw(uint32_t expected)
{
    struct {float x,y,z,w;uint32_t color;float u,v;} quad[]={
        {16,16,.5f,1,0xffffffff,.5f,.5f},{48,16,.5f,1,0xffffffff,.5f,.5f},
        {16,48,.5f,1,0xffffffff,.5f,.5f},{48,48,.5f,1,0xffffffff,.5f,.5f}};
    CHECK(pilot_gfx_draw_up(5,2,quad,sizeof(quad[0]),0x144)==0);
    unsigned char pixels[64*64*4];CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
    unsigned n=(32*64+32)*4;
    CHECK(pixels[n]==((expected>>16)&255) && pixels[n+1]==((expected>>8)&255) && pixels[n+2]==(expected&255));
}
static void test_graphics_aliases(void)
{
    uint64_t resident=vm_committed(0,0x100000000ull);
    const uint32_t header=0x19800;
    pilot_gfx_pixel_fixed();
    uint32_t vp[]={0,0,64,64,0,0x3f800000};CHECK(pilot_gfx_viewport(vp)==0);
    pilot_gfx_set_state(7,0);pilot_gfx_set_state(27,0);pilot_gfx_set_state(15,0);
    pilot_gfx_set_state(22,1);pilot_gfx_set_state(168,15);pilot_gfx_set_state(29,0);
    for(unsigned i=0;i<4;++i){pilot_gfx_set_texture_state(i,1,1);pilot_gfx_set_texture_state(i,4,1);}
    pilot_gfx_set_texture_state(0,1,2);pilot_gfx_set_texture_state(0,2,2);
    pilot_gfx_set_texture_state(0,4,2);pilot_gfx_set_texture_state(0,5,2);
    CHECK(pilot_gfx_clear(1,0xffff0000,1,0)==0 && pilot_gfx_present()==0);
    CHECK(pilot_gfx_clear(1,0xff0000ff,1,0)==0);
    /* Original XGSetTextureHeader shape; the data belongs to the front surface. */
    MEM32(header)=0x40001;MEM32(header+4)=graphics_data[1];MEM32(header+8)=0;
    MEM32(header+12)=0x11229;MEM32(header+16)=MEM32(graphics_headers+32+16);
    uint32_t bind[]={0,header};call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);alias_test_draw(0xffff0000);
    CHECK((MEM32(header)&0xffff)==1 && (MEM32(header)&0x780000)==0x80000);
    unsigned native=graphics_textures[graphics_texture_slot(header)].native;
    uint32_t guest_pixels[64*64];CHECK(pilot_gfx_texture_readback(native,0,guest_pixels,sizeof(guest_pixels))==0 && guest_pixels[0]==0xffff0000);
    CHECK(pilot_gfx_clear(1,0xff00ff00,1,0)==0 && pilot_gfx_present()==0);
    CHECK(pilot_gfx_clear(1,0xff0000ff,1,0)==0);alias_test_draw(0xff00ff00); /* no rebind or upload */
    uint32_t create[]={32,24,1,1,1,0x12,3};
    uint32_t target=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create);CHECK(target);
    unsigned target_native=graphics_textures[graphics_texture_slot(target)].native;
    CHECK(pilot_gfx_texture_clear(target_native,0xff2468ac)==0);
    MEM32(header+4)=MEM32(target+4);MEM32(header+16)=MEM32(target+16);
    call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);alias_test_draw(0xff2468ac);
    CHECK(!pilot_gfx_targets(target_native,0) && pilot_gfx_feedback_mask()==1);
    CHECK(!pilot_gfx_targets(0,UINT32_MAX) && !pilot_gfx_feedback_mask());
    CHECK(pilot_gfx_texture_clear(target_native,0xff983412)==0);alias_test_draw(0xff983412);
    uint32_t mutable_header=header;
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&mutable_header)==0); /* binding retains view */
    bind[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
    CHECK(MEM32(header+4)==MEM32(target+4) && vm_find(header,20));
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&target)==0);
    /* Device teardown owns only the native view, never the caller's header. */
    MEM32(header)=0x40001;MEM32(header+4)=graphics_data[1];MEM32(header+8)=0;
    MEM32(header+12)=0x11229;MEM32(header+16)=MEM32(graphics_headers+32+16);
    bind[1]=header;call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
    bind[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
    CHECK(vm_committed(0,0x100000000ull)==resident);
    fprintf(stderr,"PASS: caller-owned front/render-target texture aliases share actual changing GPU pixels; header retarget, binding lifetime, format readback and shared-image hazards\n");
}

static void test_graphics_depth_sampling_header(void)
{
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t previous=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER),fvf=0x144,fixed=0;
    call_graphics(PILOT_D3D_D3DDevice_SetPixelShader,&fixed);
    call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&fvf);
    graphics_render_state(143,0,1);graphics_render_state(144,0,1);graphics_render_state(147,0,1);
    graphics_render_state(60,0,1);graphics_render_state(59,0,1);graphics_render_state(92,0,1);
    graphics_render_state(67,0x01010101,1);
    for(unsigned i=0;i<4;++i)graphics_texture_state(i,12,1,1);
    graphics_texture_state(0,12,2,1);graphics_texture_state(0,14,2,1);
    graphics_texture_state(0,16,2,1);graphics_texture_state(0,18,2,1);
    graphics_texture_state(0,0,3,1);graphics_texture_state(0,1,3,1);
    graphics_texture_state(0,3,1,1);graphics_texture_state(0,4,1,1);graphics_texture_state(0,5,0,1);
    uint32_t create[]={4,4,1,1,2,0x2e,3};
    uint32_t texture=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create);CHECK(texture);
    unsigned slot=graphics_texture_slot(texture);uint32_t native=graphics_textures[slot].native;
    uint32_t view_args[]={texture,0},view=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,view_args);
    uint32_t lock[]={view,0x19540,0,0};call_graphics(PILOT_D3D_D3DSurface_LockRect,lock);
    CHECK(MEM32(0x19540)==64);
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)MEM32(MEM32(0x19544)+y*64+x*4)=0x123456a1;
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&view));
    uint32_t original=MEM32(texture+12);MEM32(texture+12)=0x11229;
    uint32_t bind[]={0,texture};call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
    CHECK(graphics_textures[slot].format==0x2e && graphics_textures[slot].native==native);
    struct {float x,y,z,w;uint32_t color;float u,v;} quad[]={
        {16,16,0,1,0xffffffff,.5f,.5f},{16,48,0,1,0xffffffff,.5f,.5f},
        {48,16,0,1,0xffffffff,.5f,.5f},{48,48,0,1,0xffffffff,.5f,.5f}};
    memcpy(memory+0x19c00,quad,sizeof(quad));uint32_t draw[]={6,4,0x19c00,sizeof(quad[0])};
    uint8_t pixels[64*64*4];unsigned center=(32*64+32)*4;
    struct PilotDepthTextureStats before,after;pilot_gfx_depth_texture_stats(&before);
    for(unsigned i=0;i<3;++i) {
        if(i==1)MEM32(texture+12)=original;
        if(i==2)MEM32(texture+12)=0x11229;
        call_graphics(PILOT_D3D_D3DDevice_DrawVerticesUP,draw);
        CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
        if(i==1)CHECK(pixels[center]==0x12 && !pixels[center+1] && !pixels[center+2] && pixels[center+3]==255);
        else CHECK(pixels[center]==0x34 && pixels[center+1]==0x56 && pixels[center+2]==0xa1 && pixels[center+3]==0x12);
        CHECK(graphics_textures[slot].format==0x2e && graphics_textures[slot].native==native);
    }
    pilot_gfx_depth_texture_stats(&after);
    CHECK(after.allocations==before.allocations+1 && after.conversions==before.conversions+1);
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&texture));
    bind[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
    if(previous!=2)call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&previous);
    CHECK(vm_committed(0,0x100000000ull)==resident);
    fprintf(stderr,"PASS: guest depth header ARGB reinterpretation, bound-header mutation without rebind, unchanged allocation/ownership and cached GPU sampling\n");
}
