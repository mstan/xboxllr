static uint32_t bump_float(float value)
{uint32_t bits;memcpy(&bits,&value,4);return bits;}
static int test_bump_pipeline(void)
{
    uint32_t bump=0,environment=0;
    CHECK(pilot_gfx_texture_create(2,2,1,0x12,&bump)==0);
    CHECK(pilot_gfx_texture_create(8,8,1,0x12,&environment)==0);
    uint32_t bumps[32],map[128];
    for(unsigned i=0;i<32;++i)bumps[i]=0x804080c0;
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x)map[y*16+x]=0xc0000000|(x*32<<16)|(y*32<<8)|0x80;
    pilot_gfx_texture_prepare_sample(bump,0,bumps,sizeof(bumps),1);
    pilot_gfx_texture_prepare_sample(environment,0,map,sizeof(map),1);
    struct {float x,y,z,w;uint32_t color;float uv[4][2];} vertices[]={
        {8,8,.5f,1,0x80ffffff},{56,8,.5f,1,0x80ffffff},{32,56,.5f,1,0x80ffffff}};
    for(unsigned vertex=0;vertex<3;++vertex)for(unsigned slot=0;slot<4;++slot)
        vertices[vertex].uv[slot][0]=vertices[vertex].uv[slot][1]=.125f;
    unsigned char pixels[64*64*4];unsigned center=(24*64+32)*4;
    for(unsigned stage=0;stage<3;++stage) {
        for(unsigned i=0;i<4;++i) {
            CHECK(pilot_gfx_xbox_texture_state(i,12,2));
            CHECK(pilot_gfx_xbox_texture_state(i,14,0)); /* preceding stages preserve diffuse */
            CHECK(pilot_gfx_xbox_texture_state(i,16,1));
            CHECK(pilot_gfx_xbox_texture_state(i,28,0));
            CHECK(pilot_gfx_xbox_texture_state(i,0,3) && pilot_gfx_xbox_texture_state(i,1,3));
            CHECK(pilot_gfx_xbox_texture_state(i,3,1) && pilot_gfx_xbox_texture_state(i,4,1));
            CHECK(pilot_gfx_xbox_texture_state(i,5,0) && pilot_gfx_xbox_texture_state(i,6,0) && pilot_gfx_xbox_texture_state(i,7,0));
        }
        pilot_gfx_texture_bind(stage,bump);pilot_gfx_texture_bind(stage+1,environment);
        CHECK(pilot_gfx_xbox_texture_state(stage,12,25));
        CHECK(pilot_gfx_xbox_texture_state(stage+1,14,2));
        if(stage<2)CHECK(pilot_gfx_xbox_texture_state(stage+2,12,1));
        /* Each coefficient moves a different sampled axis/component. Xbox
         * order is 00,01,11,10; a desktop-order pass-through fails here. */
        static const unsigned expected[4][2]={{2,1},{1,2},{1,3},{3,1}};
        for(unsigned coefficient=0;coefficient<4;++coefficient) {
            for(unsigned n=0;n<6;++n)CHECK(pilot_gfx_xbox_texture_state(stage,22+n,0));
            CHECK(pilot_gfx_xbox_texture_state(stage,22+coefficient,bump_float(.5f)));
            CHECK(pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x444)==0);
            CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
            fprintf(stderr,"BUMP stage=%u coefficient=%u rgba=%u,%u,%u,%u\n",stage,coefficient,pixels[center],pixels[center+1],pixels[center+2],pixels[center+3]);
            CHECK(pixels[center]==expected[coefficient][0]*32 && pixels[center+1]==expected[coefficient][1]*32);
            CHECK(pixels[center+2]==128 && pixels[center+3]==128); /* Bump alpha/color do not replace current. */
        }
        for(unsigned n=0;n<4;++n)CHECK(pilot_gfx_xbox_texture_state(stage,22+n,0));
        CHECK(pilot_gfx_xbox_texture_state(stage,12,26));
        CHECK(pilot_gfx_xbox_texture_state(stage,26,bump_float(.5f)));
        CHECK(pilot_gfx_xbox_texture_state(stage,27,bump_float(.25f)));
        CHECK(pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x444)==0);
        CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
        CHECK(abs((int)pixels[center]-20)<=1 && abs((int)pixels[center+1]-20)<=1);
        CHECK(abs((int)pixels[center+2]-80)<=1 && pixels[center+3]==128);
        CHECK(pilot_gfx_xbox_texture_state(stage,27,bump_float(-2)));
        CHECK(pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x444)==0);
        CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
        CHECK(!pixels[center] && !pixels[center+1] && !pixels[center+2] && pixels[center+3]==128);
        pilot_gfx_texture_bind(stage,0);pilot_gfx_texture_bind(stage+1,0);
    }
    CHECK(!pilot_gfx_xbox_texture_state(3,12,25) && !pilot_gfx_xbox_texture_state(0,16,25));
    for(unsigned stage=0;stage<4;++stage)CHECK(pilot_gfx_xbox_texture_state(stage,12,1));
    pilot_gfx_texture_destroy(bump);pilot_gfx_texture_destroy(environment);
    fprintf(stderr,"PASS: real bump-dependent GPU sampling for all matrix coefficients/stages, luminance scale/offset/clamp and preserved alpha\n");return 0;
}
