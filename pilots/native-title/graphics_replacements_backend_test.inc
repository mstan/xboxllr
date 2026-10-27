/* Authored BC2 red native texture; its 4x companion has one-pixel alternating
 * blue/green detail which cannot exist in the original 4x4 resource. */
static int test_texture_replacement(void)
{
  for(unsigned linear=0;linear<3;++linear) {
    uint32_t texture=0;unsigned char raw[4096]={255,255,255,255,255,255,255,255,0,248,0,0,0,0,0,0};
    unsigned bytes=linear==2?4096:linear?256:16;
    if(linear==1)for(unsigned i=0;i<128;++i){raw[i*2]=0;raw[i*2+1]=248;}
    if(linear==2) {
        for(unsigned i=0;i<1024;++i){raw[i*4]=raw[i*4+1]=0;raw[i*4+2]=raw[i*4+3]=255;}
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){unsigned i=y*4+x,at=((y+4)*32+x+8)*4;raw[at]=i*7;raw[at+1]=i*11;raw[at+2]=i*13;raw[at+3]=255;}
    }
    CHECK(!pilot_gfx_texture_create(linear?32:4,linear==2?32:4,1,linear==2?0x12:linear?0x11:0xe,&texture));
    pilot_gfx_texture_upload(texture,0,raw,bytes);pilot_gfx_texture_bind(0,texture);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
    pilot_gfx_set_state(D3DRS_LIGHTING,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    for(unsigned stage=0;stage<4;++stage) {
        CHECK(pilot_gfx_xbox_texture_state(stage,12,stage?1:2));
        CHECK(pilot_gfx_xbox_texture_state(stage,16,2));CHECK(pilot_gfx_xbox_texture_state(stage,14,2));
        CHECK(pilot_gfx_xbox_texture_state(stage,18,2));CHECK(pilot_gfx_xbox_texture_state(stage,20,1));
    }
    CHECK(pilot_gfx_xbox_texture_state(0,3,1) && pilot_gfx_xbox_texture_state(0,4,1));
    CHECK(pilot_gfx_xbox_texture_state(0,5,0) && pilot_gfx_xbox_texture_state(0,6,0) && pilot_gfx_xbox_texture_state(0,7,0));
    struct {float x,y,z,w;uint32_t color;float u,v;} vertices[]={
        {8,8,.5f,1,0xffffffff,0,0},{56,8,.5f,1,0xffffffff,0,0},{32,56,.5f,1,0xffffffff,0,0}};
    unsigned char pixels[64*64*4],native[4096];
    for(unsigned edited=0;edited<2;++edited) {
        if(edited){
            if(linear==2)for(unsigned i=0;i<1024;++i){raw[i*4]=255;raw[i*4+1]=raw[i*4+2]=0;raw[i*4+3]=255;}
            else if(linear)for(unsigned i=0;i<128;++i){raw[i*2]=31;raw[i*2+1]=0;}
            else{raw[8]=31;raw[9]=0;}
            pilot_gfx_texture_upload(texture,0,raw,bytes);
        }
        for(unsigned x=0;x<2;++x) {
            for(unsigned i=0;i<3;++i){vertices[i].u=(x+.5f+(linear==2?32:0))/(linear?128:16);vertices[i].v=(.5f+(linear==2?16:0))/(linear==2?128:16);}
            CHECK(!pilot_gfx_clear(1,0,1,0));CHECK(!pilot_gfx_draw_up(4,1,vertices,sizeof(vertices[0]),0x144));
            CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
            const unsigned char *p=pixels+(24*64+32)*4;
            CHECK(p[0]==0 && p[1]==(!edited && x?255:0) && p[2]==(edited || !x?255:0));
        }
        CHECK(!pilot_gfx_texture_readback(texture,0,native,bytes));CHECK(!memcmp(raw,native,bytes));
    }
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);
  }
    fprintf(stderr,"PASS: 4x texture GPU detail at original UVs, unchanged native storage/readback, edited-texture fallback\n");return 0;
}
