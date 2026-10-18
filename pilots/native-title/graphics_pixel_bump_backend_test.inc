#ifdef PILOT_D3D_PHOBOS
static int test_pixel_bump(void)
{
    uint32_t bump=0,environment=0;
    CHECK(!pilot_gfx_texture_create(2,2,1,0x28,&bump));
    CHECK(!pilot_gfx_texture_create(8,8,1,6,&environment));
    uint8_t source[8]={64,192,64,192,64,192,64,192};
    uint32_t map[64];
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
        unsigned m=0;for(unsigned bit=0;bit<3;++bit)m|=((x>>bit)&1)<<(bit*2)|((y>>bit)&1)<<(bit*2+1);
        map[m]=0xad000080|(x*32<<16)|(y*32<<8);
    }
    pilot_gfx_texture_upload(bump,0,source,sizeof(source));
    pilot_gfx_texture_upload(environment,0,map,sizeof(map));
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_LIGHTING,0);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
    pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    struct {float x,y,z,w;uint32_t color;float tc[4][4];} v[]={
        {8,8,.5f,1,0xffffffff},{56,8,.5f,1,0xffffffff},{32,56,.5f,1,0xffffffff}};
    for(unsigned i=0;i<3;++i)for(unsigned s=0;s<4;++s) {
        v[i].tc[s][0]=v[i].tc[s][1]=.5f;v[i].tc[s][3]=2;
    }
    CHECK(!pilot_gfx_fvf_prepare(0xaa0444,0,0));
    uint8_t pixels[64*64*4];unsigned center=(24*64+32)*4;
    uint32_t states[57]={0};
    CHECK(pilot_gfx_pixel_program(states,6)<0); /* Stage zero is not dependent. */
    CHECK(pilot_gfx_pixel_program(states,7<<5)<0); /* Luminance remains guarded. */
    for(unsigned stage=1;stage<4;++stage) {
        for(unsigned s=0;s<4;++s) {
            pilot_gfx_texture_bind(s,0);
            CHECK(pilot_gfx_xbox_texture_state(s,0,3) && pilot_gfx_xbox_texture_state(s,1,3));
            CHECK(pilot_gfx_xbox_texture_state(s,3,1) && pilot_gfx_xbox_texture_state(s,4,1));
            CHECK(pilot_gfx_xbox_texture_state(s,5,0) && pilot_gfx_xbox_texture_state(s,6,0) && pilot_gfx_xbox_texture_state(s,7,0));
            for(unsigned c=0;c<4;++c)CHECK(pilot_gfx_xbox_texture_state(s,22+c,0));
        }
        unsigned input=stage-1;
        pilot_gfx_texture_bind(input,bump);pilot_gfx_texture_sampling_format(input,0x28);
        pilot_gfx_texture_bind(stage,environment);pilot_gfx_texture_sampling_format(stage,6);
        memset(states,0,sizeof(states));states[8]=8+stage;states[9]=(0x18+stage)<<8;
        if(stage>1)states[56]=input<<(8+stage*4);
        uint32_t modes=(1u<<(5*input))|(6u<<(5*stage));
        CHECK(!pilot_gfx_pixel_program(states,modes));
        /* Columns are tested separately. Values cross the signed-byte seam;
         * the destination's w=2 must not divide its base coordinate. */
        static const unsigned expected[4][2]={{6,4},{4,6},{4,1},{1,4}};
        ID3D11PixelShader *first_shader=NULL;
        for(unsigned coefficient=0;coefficient<4;++coefficient) {
            for(unsigned c=0;c<4;++c)CHECK(pilot_gfx_xbox_texture_state(stage,22+c,0));
            CHECK(pilot_gfx_xbox_texture_state(stage,22+coefficient,bump_float(.5f)));
            CHECK(!pilot_gfx_clear(1,0,1,0));
            CHECK(!pilot_gfx_draw_memory(5,3,v,sizeof(v[0])) && !pilot_gfx_readback(pixels,sizeof(pixels)));
            fprintf(stderr,"PIXEL BUMP stage=%u coefficient=%u rgba=%u,%u,%u,%u\n",stage,coefficient,pixels[center],pixels[center+1],pixels[center+2],pixels[center+3]);
            CHECK(pixels[center]==expected[coefficient][0]*32 && pixels[center+1]==expected[coefficient][1]*32);
            CHECK(pixels[center+2]==128 && pixels[center+3]==173);
            ID3D11PixelShader *bound=NULL;
            ID3D11DeviceContext_PSGetShader(d3d8_GetD3D11Context(),&bound,NULL,NULL);
            CHECK(bound);
            if(!coefficient)first_shader=bound;
            else {CHECK(bound==first_shader);ID3D11PixelShader_Release(bound);}
        }
        ID3D11PixelShader_Release(first_shader);
        /* Matrix changes stay live without rebinding or compiling a variant. */
        for(unsigned c=0;c<4;++c)CHECK(pilot_gfx_xbox_texture_state(stage,22+c,0));
        CHECK(!pilot_gfx_draw_memory(5,3,v,sizeof(v[0])) && !pilot_gfx_readback(pixels,sizeof(pixels)));
        CHECK(pixels[center]==128 && pixels[center+1]==128);
        static const unsigned char endpoints[]={0,127,128,255};
        static const unsigned expected_x[]={4,6,1,3};
        CHECK(pilot_gfx_xbox_texture_state(stage,22,bump_float(.25f)));
        for(unsigned n=0;n<4;++n) {
            for(unsigned p=0;p<4;++p){source[p*2]=endpoints[n];source[p*2+1]=0;}
            pilot_gfx_texture_upload(bump,0,source,sizeof(source));
            CHECK(!pilot_gfx_draw_memory(5,3,v,sizeof(v[0])) && !pilot_gfx_readback(pixels,sizeof(pixels)));
            CHECK(pixels[center]==expected_x[n]*32 && pixels[center+1]==128);
        }
        for(unsigned p=0;p<4;++p){source[p*2]=64;source[p*2+1]=192;}
        pilot_gfx_texture_upload(bump,0,source,sizeof(source));
        if(stage>1) {
            states[56]=stage<<(8+stage*4);CHECK(pilot_gfx_pixel_program(states,modes)<0);
        }
    }
    /* Linear destination coordinates are in texels, including the offsets.
     * q=2 still does not divide them. The source's red differs from blue. */
    pilot_gfx_pixel_fixed();
    for(unsigned s=0;s<4;++s)pilot_gfx_texture_bind(s,0);
    uint32_t linear=0,argb=0,linear_map[128],argb_source[4];
    CHECK(!pilot_gfx_texture_create(8,8,1,0x12,&linear));
    CHECK(!pilot_gfx_texture_create(2,2,1,6,&argb));
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x)linear_map[y*16+x]=0xad000080|(x*32<<16)|(y*32<<8);
    for(unsigned p=0;p<4;++p)argb_source[p]=0xff17c040;
    pilot_gfx_texture_upload(linear,0,linear_map,sizeof(linear_map));
    pilot_gfx_texture_upload(argb,0,argb_source,sizeof(argb_source));
    pilot_gfx_texture_bind(0,argb);pilot_gfx_texture_sampling_format(0,6);
    pilot_gfx_texture_bind(1,linear);pilot_gfx_texture_sampling_format(1,0x12);
    for(unsigned i=0;i<3;++i)v[i].tc[1][0]=v[i].tc[1][1]=4;
    for(unsigned c=0;c<4;++c)CHECK(pilot_gfx_xbox_texture_state(1,22+c,0));
    CHECK(pilot_gfx_xbox_texture_state(1,22,bump_float(4)));
    memset(states,0,sizeof(states));states[8]=9;states[9]=0x1900;
    CHECK(!pilot_gfx_pixel_program(states,0xc1));
    CHECK(!pilot_gfx_draw_memory(5,3,v,sizeof(v[0])) && !pilot_gfx_readback(pixels,sizeof(pixels)));
    CHECK(pixels[center]==192 && pixels[center+1]==128 && pixels[center+2]==128 && pixels[center+3]==173);
    pilot_gfx_pixel_fixed();
    for(unsigned stage=0;stage<4;++stage)pilot_gfx_texture_bind(stage,0);
    pilot_gfx_texture_destroy(bump);pilot_gfx_texture_destroy(environment);
    pilot_gfx_texture_destroy(linear);pilot_gfx_texture_destroy(argb);
    fprintf(stderr,"PASS: native BUMPENVMAP signed G8B8, all matrix columns/stages, dependent source, nonprojective coordinates and alpha\n");
    return 0;
}
#endif
