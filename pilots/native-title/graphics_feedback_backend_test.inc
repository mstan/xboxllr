#ifdef PILOT_D3D_PHOBOS
static int test_feedback(void)
{
    uint32_t copies,allocations;pilot_donor_feedback_stats(&copies,&allocations);
    CHECK(!copies && !allocations); /* previous device's scratch must be gone */
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);
    pilot_gfx_set_state(D3DRS_SHADEMODE,2);
    for(unsigned stage=0;stage<4;++stage)pilot_gfx_texture_bind(stage,0);
    CHECK(pilot_gfx_xbox_texture_state(0,0,3) && pilot_gfx_xbox_texture_state(0,1,3));
    CHECK(pilot_gfx_xbox_texture_state(0,3,2) && pilot_gfx_xbox_texture_state(0,4,2));
    CHECK(pilot_gfx_xbox_texture_state(0,5,0) && pilot_gfx_xbox_texture_state(0,7,0));
    CHECK(pilot_gfx_xbox_texture_state(0,6,0) && pilot_gfx_xbox_texture_state(0,28,0));
    uint32_t ps[57]={0};ps[0]=0x18200000;ps[26]=0xc0;ps[34]=0x08010000;
    ps[45]=0xc0;ps[10]=0x80808080;ps[8]=0xc;ps[9]=0x1c00;ps[53]=1;ps[54]=1;
    CHECK(!pilot_gfx_pixel_program(ps,1));
    for(unsigned size=64;size>=32;size/=2) {
        uint32_t target;CHECK(!pilot_gfx_texture_create_usage(size,size,1,0x12,1,&target));
        uint32_t expected[64*64],observed[64*64];
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x)
            expected[y*size+x]=0x80000000u|((100+x)<<16)|((80+y)<<8)|200;
        pilot_gfx_texture_upload(target,0,expected,size*size*4);
        /* Texture-first binding and target-first binding both retain input. */
        pilot_gfx_texture_bind(0,target);CHECK(!pilot_gfx_targets(target,0));
        pilot_gfx_texture_bind(3,target);CHECK(pilot_gfx_feedback_mask()==9);
        uint32_t viewport[]={0,0,size,size,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
        CHECK(!pilot_gfx_fvf_prepare(0x104,.53125f,.53125f));
        struct {float x,y,z,w,u,v;} quad[]={
            {-.53125f,-.53125f,0,1,0,0},{size-.53125f,-.53125f,0,1,(float)size,0},
            {size-.53125f,size-.53125f,0,1,(float)size,(float)size},{-.53125f,size-.53125f,0,1,0,(float)size}};
        for(unsigned repeat=0;repeat<2;++repeat) {
            CHECK(!pilot_gfx_draw_memory(8,4,quad,sizeof(quad[0])));
            CHECK(!pilot_gfx_texture_readback(target,0,observed,size*size*4));
            for(unsigned i=0;i<size*size;++i) {
                uint32_t value=expected[i]&0xff000000u;
                for(unsigned shift=0;shift<24;shift+=8)value|=((((expected[i]>>shift)&255)*128+127)/255)<<shift;
                expected[i]=value;CHECK(observed[i]==value);
            }
        }
        pilot_donor_feedback_stats(&copies,&allocations);
        CHECK(copies==(size==64?2:4) && allocations==(size==64?1:2));
        CHECK(!pilot_gfx_targets(0,UINT32_MAX) && !pilot_gfx_feedback_mask());
        ID3D11ShaderResourceView *view=NULL;
        ID3D11DeviceContext_PSGetShaderResources(d3d8_GetD3D11Context(),0,1,&view);CHECK(view);
        ID3D11ShaderResourceView_Release(view);
        pilot_gfx_texture_bind(0,0);pilot_gfx_texture_bind(3,0);pilot_gfx_texture_destroy(target);
    }
    uint32_t viewport[]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
    /* A separately wrapped RHI image of the default surface must compare by
     * underlying GPU storage, not by wrapper address. Exercise the donor's
     * COM UP draw as well as the adapter's direct draw above. */
    uint32_t alias;CHECK(!pilot_gfx_texture_alias(PILOT_GFX_SURFACE,0,0x12,&alias));
    CHECK(!pilot_gfx_clear(1,0xff204060,1,0));pilot_gfx_texture_bind(0,alias);
    CHECK(pilot_gfx_feedback_mask()==1);
    struct {float x,y,z,w,u,v;} strip[]={
        {0,0,0,1,0,0},{64,0,0,1,1,0},{0,64,0,1,0,1},{64,64,0,1,1,1}};
    CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLESTRIP,2,strip,sizeof(strip[0]),0x104));
    uint8_t pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
    unsigned center=(32*64+32)*4;
    CHECK(pixels[center]==16 && pixels[center+1]==32 && pixels[center+2]==48 && pixels[center+3]==255);
    pilot_donor_feedback_stats(&copies,&allocations);CHECK(copies==5 && allocations==3);
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(alias);
    pilot_gfx_pixel_fixed();
    fprintf(stderr,"PASS: donor GPU feedback pixels, repeat draws, bilinear texel centers, shared-stage copy, resize cache and binding order\n");
    return 0;
}
#endif
