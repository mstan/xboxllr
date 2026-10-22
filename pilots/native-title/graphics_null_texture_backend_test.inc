#ifdef PILOT_D3D_PHOBOS
/* Captured caller: constant RGB plus MODULATE(empty-sampler alpha, factor
 * alpha). Check RGB/alpha independently against ordinary byte-color blending;
 * the Cxbx fixed-function sampler reference also supplies one for SAMPLE_NONE. */
static int test_null_texture_alpha(void)
{
    uint32_t parameters[17]={64,64,7,1,0x11,1,0,0,1,0x2e,0,60,1};
    CHECK(!pilot_gfx_create(parameters));
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,0);
    pilot_gfx_set_state(D3DRS_LIGHTING,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,1);
    pilot_gfx_set_state(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    pilot_gfx_set_state(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    pilot_gfx_set_state(D3DRS_BLENDOP,1); /* ADD in the retained desktop ABI. */
    pilot_gfx_set_state(D3DRS_TEXTUREFACTOR,0x80042000);
    for(unsigned stage=0;stage<4;++stage) {
        pilot_gfx_texture_bind(stage,0);
        pilot_gfx_set_texture_state(stage,D3DTSS_COLOROP,D3DTOP_DISABLE);
    }
    pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_TFACTOR);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG2,D3DTA_TFACTOR);
    struct {float x,y,z,w;uint32_t color;} vertices[]={
        {8,8,.5f,1,0xffff0000},{56,8,.5f,1,0xffff0000},{32,56,.5f,1,0xffff0000}};
    unsigned char pixels[64*64*4];unsigned center=(24*64+32)*4;
    const unsigned expected[2][3]={{2,16,32},{128,0,32}};
    for(unsigned variant=0;variant<2;++variant) {
        pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,variant?D3DTA_DIFFUSE:D3DTA_TFACTOR);
        CHECK(!pilot_gfx_clear(1,0xff000040,1,0));
        CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x44));
        CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
        fprintf(stderr,"NULL ALPHA variant=%u RGB=%u,%u,%u\n",variant,pixels[center],pixels[center+1],pixels[center+2]);
        for(unsigned lane=0;lane<3;++lane)CHECK(abs((int)pixels[center+lane]-(int)expected[variant][lane])<=1);
    }
    /* Preserve the existing later empty-RGB-stage compatibility behavior used
     * by Blinx's opening movie, while correcting the independent alpha case. */
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    pilot_gfx_set_texture_state(1,D3DTSS_COLOROP,D3DTOP_MODULATE);
    pilot_gfx_set_texture_state(1,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    pilot_gfx_set_texture_state(1,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    CHECK(!pilot_gfx_clear(1,0xff000040,1,0));
    CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x44));
    CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
    CHECK(pixels[center]==255 && pixels[center+1]==0 && pixels[center+2]==0);
    /* A real texture mask must survive a later unbound stage whose RGB adds
     * an independent factor. This is the Blinx whisker/fur material shape;
     * both zero and full alpha are checked with authored BC2 bytes. */
    uint32_t texture=0;
    CHECK(!pilot_gfx_texture_create(4,4,1,0x0e,&texture));
    const unsigned char mask[16]={0xff,0,0xff,0,0xff,0,0xff,0,
                                 0xff,0xff,0,0,0,0,0,0};
    pilot_gfx_texture_upload(texture,0,mask,sizeof(mask));
    pilot_gfx_texture_bind(0,texture);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,1);
    pilot_gfx_set_state(D3DRS_TEXTUREFACTOR,0xff040810);
    pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_MODULATE);
    pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    pilot_gfx_set_texture_state(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    pilot_gfx_set_texture_state(1,D3DTSS_COLOROP,D3DTOP_ADD);
    pilot_gfx_set_texture_state(1,D3DTSS_COLORARG1,D3DTA_CURRENT);
    pilot_gfx_set_texture_state(1,D3DTSS_COLORARG2,D3DTA_TFACTOR);
    pilot_gfx_set_texture_state(1,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(1,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    struct {float x,y,z,w;uint32_t color;float u,v;} masked[]={
        {8,8,.5f,1,0xff804020,0,0.125f},
        {56,8,.5f,1,0xff804020,0,0.125f},
        {32,56,.5f,1,0xff804020,0,0.125f}};
    const unsigned mask_expected[2][3]={{0,0,64},{132,72,48}};
    for(unsigned opaque=0;opaque<2;++opaque) {
        for(unsigned vertex=0;vertex<3;++vertex)masked[vertex].u=opaque?0.125f:0.625f;
        CHECK(!pilot_gfx_clear(1,0xff000040,1,0));
        CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,masked,sizeof(masked[0]),0x144));
        CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
        fprintf(stderr,"LATER NULL ALPHA opaque=%u RGB=%u,%u,%u\n",opaque,pixels[center],pixels[center+1],pixels[center+2]);
        for(unsigned lane=0;lane<3;++lane)CHECK(abs((int)pixels[center+lane]-(int)mask_expected[opaque][lane])<=1);
    }
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);
    pilot_gfx_close();
    fprintf(stderr,"PASS: unbound alpha preserves factor/diffuse RGB and factor alpha; later empty RGB compatibility retained\n");
    return 0;
}
#endif
