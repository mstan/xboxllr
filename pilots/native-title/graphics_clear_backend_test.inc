static int test_viewport_clear(void)
{
    uint32_t color=0,depth=0;
    CHECK(!pilot_gfx_texture_create_usage(32,16,1,0x12,1,&color));
    CHECK(!pilot_gfx_texture_create_usage(32,16,1,0x2e,2,&depth));
    CHECK(!pilot_gfx_targets(color,depth));
    uint32_t whole[]={0,0,32,16,0,0x3f800000},part[]={7,3,17,9,0,0x3f800000};
    uint32_t colors[32*16],depths[32*16],shape[3],actual_view[6];
    for(unsigned flags=1;flags<8;++flags) {
        CHECK(!pilot_gfx_viewport(whole));CHECK(!pilot_gfx_clear(7,0xffff0000,.25f,0xa5));
        CHECK(!pilot_gfx_viewport(part));
        /* A clear must ignore guest masks/tests, retain every untouched pixel
         * and channel, and contribute no samples to a guest visibility query. */
        pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,0);pilot_gfx_set_state(D3DRS_ZFUNC,D3DCMP_NEVER);
        pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,1);pilot_gfx_set_state(D3DRS_ALPHAFUNC,D3DCMP_NEVER);
        CHECK(!pilot_gfx_visibility_begin());CHECK(!pilot_gfx_clear(flags,0xff00ff00,.75f,0x5a));
        CHECK(!pilot_gfx_visibility_end(8000+flags));
#ifdef PILOT_D3D_PHOBOS
        CHECK(!visibility_expect(8000+flags,0,NULL));
#endif
        CHECK(!pilot_gfx_get_viewport(actual_view) && !memcmp(part,actual_view,sizeof(part)));
        CHECK(d3d8_GetRenderStates()[D3DRS_COLORWRITEENABLE]==0 && d3d8_GetRenderStates()[D3DRS_ZFUNC]==D3DCMP_NEVER);
        CHECK(!pilot_gfx_texture_readback(color,0,colors,sizeof(colors)));
        CHECK(!pilot_gfx_reference_target_readback(1,depths,sizeof(depths),shape));
        CHECK(shape[0]==32 && shape[1]==16);
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<32;++x) {
            unsigned inside=x>=7 && x<24 && y>=3 && y<12,i=y*32+x;
            CHECK(colors[i]==(inside && (flags&1)?0xff00ff00:0xffff0000));
            CHECK((depths[i]>>24)==(inside && (flags&4)?0x5a:0xa5));
            CHECK(abs((int)(depths[i]&0xffffff)-(inside && (flags&2)?0xbfffff:0x400000))<=1);
        }
    }
    CHECK(!pilot_gfx_targets(0,UINT32_MAX));uint32_t full[]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(full));
    pilot_gfx_texture_destroy(color);pilot_gfx_texture_destroy(depth);
    fprintf(stderr,"PASS viewport clear: seven color/depth/stencil masks, outside contents, ignored guest tests, restored viewport and zero clear visibility samples\n");
    return 0;
}
