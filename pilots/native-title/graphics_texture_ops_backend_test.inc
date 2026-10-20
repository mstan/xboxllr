/* Independent GPU expectations for Xbox fixed-function color operations.
 * The values below come from the documented component equations, not from
 * the backend combiner implementation. */
#ifdef PILOT_D3D_PHOBOS
static int test_texture_color_ops(void)
{
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_LIGHTING,0);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
    pilot_gfx_set_state(D3DRS_FOGENABLE,0);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    const uint32_t diffuse=0xa06080c0u,texel=0x60204060u;
    pilot_gfx_set_state(D3DRS_TEXTUREFACTOR,0x40302010u);
    uint32_t texture=0;CHECK(!pilot_gfx_texture_create(1,1,1,6,&texture));
    pilot_gfx_texture_upload(texture,0,&texel,sizeof(texel));
    for(unsigned stage=0;stage<4;++stage) {
        pilot_gfx_texture_bind(stage,texture);
        CHECK(pilot_gfx_xbox_texture_state(stage,7,0));
        CHECK(pilot_gfx_xbox_texture_state(stage,21,0));
        CHECK(pilot_gfx_xbox_texture_state(stage,28,0));
    }
    struct {float x,y,z,w;uint32_t color;float u,v;} vertices[]={
        {8,8,.5f,1,diffuse,.5f,.5f},{56,8,.5f,1,diffuse,.5f,.5f},{32,56,.5f,1,diffuse,.5f,.5f}};
    static const struct {uint32_t op,arg1,arg2;unsigned rgb[3];const char *name;} cases[]={
        {8,1,2,{0,64,160},"ADDSIGNED"},
        {9,1,2,{0,128,255},"ADDSIGNED2X"},
        {10,1,2,{64,64,96},"SUBTRACT"},
        {11,1,2,{116,160,216},"ADDSMOOTH"},
        {12,1,2,{72,104,156},"BLENDDIFFUSEALPHA"},
        {13,1,2,{56,88,132},"BLENDTEXTUREALPHA"},
        {14,1,3,{60,56,60},"BLENDFACTORALPHA"},
        {8,1,0x22,{64,96,160},"ADDSIGNED_TEXTURE_ALPHA_REPLICATE"},
        {10,1,0x12,{0,0,33},"SUBTRACT_COMPLEMENT_TEXTURE"},
    };
    unsigned char pixels[64*64*4];const unsigned center=(24*64+32)*4;
    for(unsigned target=0;target<4;++target)for(unsigned c=0;c<sizeof(cases)/sizeof(*cases);++c) {
        for(unsigned stage=0;stage<4;++stage) {
            uint32_t colorop=stage<target?2:stage==target?cases[c].op:1;
            uint32_t alphaop=stage<=target?2:1;
            CHECK(pilot_gfx_xbox_texture_state(stage,12,colorop));
            CHECK(pilot_gfx_xbox_texture_state(stage,20,1));
            CHECK(pilot_gfx_xbox_texture_state(stage,14,stage==target?cases[c].arg1:1));
            CHECK(pilot_gfx_xbox_texture_state(stage,15,stage==target?cases[c].arg2:1));
            CHECK(pilot_gfx_xbox_texture_state(stage,16,alphaop));
            /* Prefix CURRENT alpha differs from DIFFUSE and TEXTURE. The
             * target restores DIFFUSE alpha, which is independent of COLOROP. */
            CHECK(pilot_gfx_xbox_texture_state(stage,18,stage<target?3:0));
            CHECK(pilot_gfx_xbox_texture_state(stage,19,1));
        }
        CHECK(!pilot_gfx_clear(1,0xff000000,1,0));
        CHECK(!pilot_gfx_draw_up(4,1,vertices,sizeof(vertices[0]),0x144));
        CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
        const unsigned char *p=pixels+center;
        if(abs((int)p[0]-(int)cases[c].rgb[0])>1 ||
           abs((int)p[1]-(int)cases[c].rgb[1])>1 ||
           abs((int)p[2]-(int)cases[c].rgb[2])>1 || abs((int)p[3]-160)>1)
            fprintf(stderr,"texture-op %s actual=%u,%u,%u,%u expected=%u,%u,%u,160\n",
                cases[c].name,p[0],p[1],p[2],p[3],cases[c].rgb[0],cases[c].rgb[1],cases[c].rgb[2]);
        CHECK(abs((int)p[0]-(int)cases[c].rgb[0])<=1);
        CHECK(abs((int)p[1]-(int)cases[c].rgb[1])<=1);
        CHECK(abs((int)p[2]-(int)cases[c].rgb[2])<=1);
        CHECK(abs((int)p[3]-160)<=1);
    }
    for(unsigned stage=0;stage<4;++stage) {
        uint32_t prior=d3d8_GetTSS(stage)[D3DTSS_COLOROP];
        CHECK(!pilot_gfx_xbox_texture_state(stage,12,0));
        CHECK(!pilot_gfx_xbox_texture_state(stage,12,15));
        CHECK(!pilot_gfx_xbox_texture_state(stage,12,16));
        CHECK(!pilot_gfx_xbox_texture_state(stage,12,17));
        CHECK(!pilot_gfx_xbox_texture_state(stage,12,24));
        CHECK(!pilot_gfx_xbox_texture_state(stage,12,UINT32_MAX));
        CHECK(d3d8_GetTSS(stage)[D3DTSS_COLOROP]==prior);
        uint32_t alpha_prior=d3d8_GetTSS(stage)[D3DTSS_ALPHAOP];
        CHECK(!pilot_gfx_xbox_texture_state(stage,16,14));
        CHECK(d3d8_GetTSS(stage)[D3DTSS_ALPHAOP]==alpha_prior);
    }
    for(unsigned stage=0;stage<4;++stage) {
        pilot_gfx_texture_bind(stage,0);
        CHECK(pilot_gfx_xbox_texture_state(stage,12,1));
        CHECK(pilot_gfx_xbox_texture_state(stage,16,1));
    }
    pilot_gfx_texture_destroy(texture);
    fprintf(stderr,"PASS seven color ops (8..14) on each stage, complement/alpha-replicate arguments, independent RGBA expectations and invalid color/alpha op rejection\n");
    return 0;
}
#endif
