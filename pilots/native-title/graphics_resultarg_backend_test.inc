/* Independent constant-colour cases: TEMP writes must preserve CURRENT,
 * and subsequent stages must read the register selected by the caller. */
static int test_fixed_resultarg(void)
{
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_LIGHTING,0);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
    pilot_gfx_set_state(D3DRS_FOGENABLE,0);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    pilot_gfx_set_state(D3DRS_TEXTUREFACTOR,0x80402010);
    struct {float x,y,z,w;uint32_t color;} vertices[]={
        {8,8,.5f,1,0xff204060},{56,8,.5f,1,0xff204060},{32,56,.5f,1,0xff204060}};
    for(unsigned i=0;i<4;++i) {
        pilot_gfx_texture_bind(i,0);
        CHECK(pilot_gfx_xbox_texture_state(i,20,1));
        CHECK(pilot_gfx_xbox_texture_state(i,12,i<3?2:1));
        CHECK(pilot_gfx_xbox_texture_state(i,16,2));
        CHECK(pilot_gfx_xbox_texture_state(i,14,i==1?3:1));
        CHECK(pilot_gfx_xbox_texture_state(i,18,i==1?3:1));
        CHECK(pilot_gfx_xbox_texture_state(i,15,1));
        CHECK(pilot_gfx_xbox_texture_state(i,19,1));
    }
    CHECK(pilot_gfx_xbox_texture_state(1,20,5));
    CHECK(!pilot_gfx_xbox_texture_state(1,20,0));
    CHECK(!pilot_gfx_xbox_texture_state(1,20,2));
    CHECK(!pilot_gfx_xbox_texture_state(1,20,0x15));
    CHECK(d3d8_GetTSS(1)[D3DTSS_RESULTARG]==5);
    CHECK(!pilot_gfx_xbox_texture_state(2,14,6));
    CHECK(!pilot_gfx_xbox_texture_state(2,14,0x45));
    static const struct {uint32_t colorop,color1,color2,alpha1;unsigned expected[4];} cases[]={
        {2,1,1,1,{32,64,96,255}},
        {2,5,1,5,{64,32,16,128}},
        {7,1,5,1,{96,96,112,255}},
        {2,0x35,1,0x15,{127,127,127,127}},
    };
    for(unsigned c=0;c<sizeof(cases)/sizeof(*cases);++c) {
        CHECK(pilot_gfx_xbox_texture_state(2,12,cases[c].colorop));
        CHECK(pilot_gfx_xbox_texture_state(2,14,cases[c].color1));
        CHECK(pilot_gfx_xbox_texture_state(2,15,cases[c].color2));
        CHECK(pilot_gfx_xbox_texture_state(2,18,cases[c].alpha1));
        CHECK(pilot_gfx_clear(1,0xff000000,1,0)==0);
        CHECK(pilot_gfx_draw_up(4,1,vertices,sizeof(vertices[0]),0x44)==0);
        unsigned char pixels[64*64*4];CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
        unsigned char *p=pixels+(24*64+32)*4;
        fprintf(stderr,"RESULTARG case=%u actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n",c,
            p[0],p[1],p[2],p[3],cases[c].expected[0],cases[c].expected[1],cases[c].expected[2],cases[c].expected[3]);
        for(unsigned channel=0;channel<4;++channel)
            CHECK(abs((int)p[channel]-(int)cases[c].expected[channel])<=1);
    }
    /* Same compiled shader, changed constant: TEMP cannot remain stale. */
    pilot_gfx_set_state(D3DRS_TEXTUREFACTOR,0xc0102030);
    CHECK(pilot_gfx_xbox_texture_state(2,12,2) && pilot_gfx_xbox_texture_state(2,14,5) && pilot_gfx_xbox_texture_state(2,18,5));
    CHECK(pilot_gfx_draw_up(4,1,vertices,sizeof(vertices[0]),0x44)==0);
    unsigned char pixels[64*64*4];CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
    unsigned char *p=pixels+(24*64+32)*4;
    CHECK(p[0]==16 && p[1]==32 && p[2]==48 && p[3]==192);
    fprintf(stderr,"PASS: real GPU CURRENT/TEMP preservation, mixed reads, modifiers, live constants and invalid destinations\n");
    return 0;
}
