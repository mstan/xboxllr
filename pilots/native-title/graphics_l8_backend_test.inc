/* Original one-byte Morton storage, real donor luminance expansion and GPU
 * sampling. The CPU source bytes and expected texels are authored here. */
static int test_l8_texture(void)
{
    uint32_t texture=0;CHECK(pilot_gfx_texture_create(8,4,4,0,&texture)==0);
    unsigned char raw[32],before[32],linear[32],pixels[64*64*4];
    static const unsigned char endpoints[]={0,85,170,255};
    for(unsigned level=0;level<4;++level) {
        unsigned width=8>>level,height=4>>level;if(!height)height=1;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
            raw[g8b8_test_address(x,y,width,height)]=endpoints[(x+y+level)%4];
        memcpy(before,raw,width*height);pilot_gfx_texture_upload(texture,level,raw,width*height);
        CHECK(!memcmp(raw,before,width*height));
        CHECK(pilot_gfx_texture_readback(texture,level,linear,width*height)==0);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
            CHECK(linear[y*width+x]==endpoints[(x+y+level)%4]);
    }
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
    pilot_gfx_set_state(D3DRS_LIGHTING,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    pilot_gfx_texture_bind(0,texture);pilot_gfx_texture_sampling_format(0,0);
    for(unsigned stage=0;stage<4;++stage) {
        CHECK(pilot_gfx_xbox_texture_state(stage,12,stage?1:2));
        CHECK(pilot_gfx_xbox_texture_state(stage,16,2));
        CHECK(pilot_gfx_xbox_texture_state(stage,14,2));
        CHECK(pilot_gfx_xbox_texture_state(stage,18,2));
        CHECK(pilot_gfx_xbox_texture_state(stage,20,1));
    }
    CHECK(pilot_gfx_xbox_texture_state(0,3,1) && pilot_gfx_xbox_texture_state(0,4,1));
    CHECK(pilot_gfx_xbox_texture_state(0,5,0) && pilot_gfx_xbox_texture_state(0,6,0) && pilot_gfx_xbox_texture_state(0,7,0));
    CHECK(pilot_gfx_xbox_texture_state(0,0,3) && pilot_gfx_xbox_texture_state(0,1,3));
    struct {float x,y,z,w;uint32_t color;float u,v;} vertices[]={
        {8,8,.5f,1,0xffffffff,0,0},{56,8,.5f,1,0xffffffff,0,0},{32,56,.5f,1,0xffffffff,0,0}};
    for(unsigned y=0;y<4;y+=3)for(unsigned x=0;x<8;++x) {
        for(unsigned i=0;i<3;++i){vertices[i].u=(x+.5f)/8;vertices[i].v=(y+.5f)/4;}
        CHECK(pilot_gfx_clear(1,0,1,0)==0 && pilot_gfx_draw_up(4,1,vertices,sizeof(vertices[0]),0x144)==0);
        CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
        const unsigned char *pixel=pixels+(24*64+32)*4;unsigned value=endpoints[(x+y)%4];
        CHECK(pixel[0]==value && pixel[1]==value && pixel[2]==value && pixel[3]==255);
    }
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);
    fprintf(stderr,"PASS L8: original one-byte mip storage, immutable uploads, linear readback and real luminance/alpha GPU pixels\n");return 0;
}
static int test_linear_565_texture(void)
{
    uint32_t texture=0;CHECK(pilot_gfx_texture_create_usage(32,16,1,0x11,1,&texture)==0);
    uint16_t source[32*16],readback[32*16];
    static const uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff};
    for(unsigned i=0;i<32*16;++i)source[i]=colors[i%4];
    pilot_gfx_texture_upload(texture,0,source,sizeof(source));
    CHECK(pilot_gfx_texture_readback(texture,0,readback,sizeof(readback))==0 && !memcmp(source,readback,sizeof(source)));
    CHECK(pilot_gfx_targets(texture,0)==0);
    CHECK(pilot_gfx_clear(1,0xffff0000,1,0)==0);
    CHECK(pilot_gfx_texture_readback(texture,0,readback,sizeof(readback))==0);
    for(unsigned i=0;i<32*16;++i)CHECK(readback[i]==0xf800);
    CHECK(pilot_gfx_targets(0,UINT32_MAX)==0);pilot_gfx_texture_destroy(texture);
    fprintf(stderr,"PASS linear RGB565: original two-byte uploads/readback and actual sixteen-bit render-target pixels\n");return 0;
}
