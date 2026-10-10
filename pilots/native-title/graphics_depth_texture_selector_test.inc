#ifdef PILOT_D3D_PHOBOS
static int test_depth_argb_selector(void)
{
    uint32_t texture;CHECK(!pilot_gfx_texture_create_usage(16,16,1,0x2e,2,&texture));
    uint32_t viewport[]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);
    pilot_gfx_set_state(D3DRS_SHADEMODE,2);
    for(unsigned i=0;i<4;++i)pilot_gfx_texture_bind(i,0);
    CHECK(pilot_gfx_xbox_texture_state(0,0,3) && pilot_gfx_xbox_texture_state(0,1,3));
    CHECK(pilot_gfx_xbox_texture_state(0,3,2) && pilot_gfx_xbox_texture_state(0,4,2));
    CHECK(pilot_gfx_xbox_texture_state(0,5,0) && pilot_gfx_xbox_texture_state(0,7,0));
    CHECK(pilot_gfx_xbox_texture_state(0,6,0) && pilot_gfx_xbox_texture_state(0,28,0));
    /* Independently authored five-stage alpha program from NV input/output
     * fields. s=8*(T0.a-229/255)+(8/255)*T0.b-57/255. MSB mux returns s>=.5.
     * All texture coordinates are constant; the four weights are exactly1/4.
     * At blue143, s=32509/65025, below .5 by7/130050. A nearest-even16-bit
     * UNORM filter result can cross the threshold. Blue142/144 bracket it.
     * No retained title program or measured result supplies these words. */
    uint32_t ps[57]={0};
    ps[0]=0x1820f120;ps[26]=0x20d00;ps[10]=229u<<24; /* 4*(T.a-C0.a) -> R1.a */
    ps[1]=0xdd200000;ps[27]=0x10d00;                /* 2*R1.a -> R1.a */
    ps[2]=0xdd200802;ps[28]=0x00d00;ps[20]=8;      /* R1.a+T.b*C1.b -> R1.a */
    ps[3]=0xdd20f120;ps[29]=0x00c00;ps[13]=57u<<24;/* R1.a-C0.a -> R0.a */
    ps[4]=0x00002020;ps[30]=0x04d00;               /* MSB mux(0,1) -> R1.a */
    ps[8]=1;ps[9]=0x1d00;ps[43]=0x00ffffff;      /* RGB=white, alpha=R1.a */
    ps[53]=0x11105;ps[54]=1;
    CHECK(!pilot_gfx_pixel_program(ps,1) && !pilot_gfx_fvf_prepare(0x144,0,0));
    struct PilotDepthTextureStats before,after;pilot_gfx_depth_texture_stats(&before);
    uint8_t pixels[64*64*4];
    for(unsigned blue=142;blue<=144;++blue) {
        uint32_t source[256];for(unsigned i=0;i<256;++i)source[i]=0xfb030500|blue;
        source[1]=0xfb172900|blue;source[16]=0xfc113100|blue;source[17]=0xfc254300|blue;
        pilot_gfx_texture_upload(texture,0,source,sizeof(source));
        pilot_gfx_texture_bind(0,texture);pilot_gfx_texture_sampling_format(0,0x12);
        CHECK(!depth_argb_draw(1,1,pixels));
        const uint8_t *center=pixels+(32*64+32)*4;
        fprintf(stderr,"SELECTOR blue=%u RGBA=%u,%u,%u,%u\n",blue,center[0],center[1],center[2],center[3]);
        for(unsigned y=18;y<46;++y)for(unsigned x=18;x<46;++x) {
            const uint8_t *p=pixels+(y*64+x)*4;
            CHECK(p[0]==255 && p[1]==255 && p[2]==255 && p[3]==(blue==144?255:0));
        }
    }
    pilot_gfx_depth_texture_stats(&after);
    CHECK(after.allocations==before.allocations+1 && after.conversions==before.conversions+3);
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);pilot_gfx_pixel_fixed();
    fprintf(stderr,"PASS: authored rational depth-as-ARGB bilinear MSB selector and both threshold neighbors\n");
    return 0;
}
#endif
