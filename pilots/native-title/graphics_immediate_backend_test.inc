#ifdef PILOT_D3D_PHOBOS
static int test_immediate_pixels(void)
{
    float vertices[4][16][4]={0};
    const float positions[4][4]={{8,8,.5f,.5f},{8,56,.5f,.5f},{56,8,.5f,.5f},{56,56,.5f,.5f}};
    for(unsigned i=0;i<4;++i) {
        memcpy(vertices[i][0],positions[i],16);
        vertices[i][3][1]=1;vertices[i][3][3]=1;
        vertices[i][9][0]=(float)(i/2);vertices[i][9][1]=(float)(i%2);vertices[i][9][3]=1;
    }
    pilot_gfx_pixel_fixed();
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
    pilot_gfx_set_state(D3DRS_FOGENABLE,0);pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);
    for(unsigned stage=0;stage<4;++stage) {
        pilot_gfx_texture_bind(stage,0);
        CHECK(pilot_gfx_xbox_texture_state(stage,12,stage?1:2));
    }
    CHECK(pilot_gfx_xbox_texture_state(0,14,0));
    uint8_t pixels[64*64*4];
    const uint32_t formats[]={4,0x44,0xc4,0x1c4};
    for(unsigned n=0;n<4;++n)for(unsigned shade=1;shade<=2;++shade) {
        uint32_t fvf=formats[n];pilot_gfx_set_state(D3DRS_SHADEMODE,shade);
        CHECK(!pilot_gfx_clear(1,0xff123456,1,0));
        CHECK(!pilot_gfx_fvf_prepare(fvf,0,0) && !pilot_gfx_immediate_fvf(fvf));
        CHECK(!pilot_gfx_draw_memory(6,4,vertices,sizeof(vertices[0])) && !pilot_gfx_readback(pixels,sizeof(pixels)));
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            unsigned pixel=(y*64+x)*4;int inside=x>=8 && x<56 && y>=8 && y<56;
            CHECK(pixels[pixel]==(inside?(fvf&0x40?0:255):0x12));
            CHECK(pixels[pixel+1]==(inside?255:0x34));
            CHECK(pixels[pixel+2]==(inside?(fvf&0x40?0:255):0x56));
        }
    }
    fprintf(stderr,"PASS immediate float-attribute decoding: eight full screen-quad images, RHW.5, flat/Gouraud, optional colors and UVs\n");
    return 0;
}
#endif
