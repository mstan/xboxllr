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
