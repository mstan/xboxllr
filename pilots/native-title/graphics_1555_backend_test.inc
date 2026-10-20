/* Independent authored Morton addresses and 5-bit UNORM expectations. */
static int test_1555_textures(void)
{
    static const unsigned edge[]={0,1,15,16,30,31};
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_LIGHTING,0);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
    pilot_gfx_set_state(D3DRS_FOGENABLE,0);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    for(unsigned stage=0;stage<4;++stage) {
        CHECK(pilot_gfx_xbox_texture_state(stage,12,stage?1:2));
        CHECK(pilot_gfx_xbox_texture_state(stage,16,2));
        CHECK(pilot_gfx_xbox_texture_state(stage,14,2));
        CHECK(pilot_gfx_xbox_texture_state(stage,18,2));
    }
    CHECK(pilot_gfx_xbox_texture_state(0,3,1) && pilot_gfx_xbox_texture_state(0,4,1));
    CHECK(pilot_gfx_xbox_texture_state(0,5,0) && pilot_gfx_xbox_texture_state(0,6,0) && pilot_gfx_xbox_texture_state(0,7,0));
    CHECK(pilot_gfx_xbox_texture_state(0,0,3) && pilot_gfx_xbox_texture_state(0,1,3));
    uint32_t untouched=0x12345678;
    CHECK(pilot_gfx_texture_create_usage(8,4,1,3,1,&untouched)<0 && untouched==0x12345678);
    for(unsigned format=2;format<=3;++format) {
        uint32_t texture=0;CHECK(!pilot_gfx_texture_create(8,4,4,format,&texture));
        uint16_t raw[32],before[32],decoded[32];
        for(unsigned level=0;level<4;++level) {
            unsigned width=8>>level,height=4>>level;if(!height)height=1;
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                unsigned address=g8b8_test_address(x,y,width,height);
                raw[address]=(uint16_t)((edge[(x+y+level)%6]<<10)|(edge[(2*x+y+1)%6]<<5)|
                    edge[(x+3*y+2)%6]|(((x+y)&1)<<15));
            }
            memcpy(before,raw,width*height*2);
            pilot_gfx_texture_upload(texture,level,raw,width*height*2);
            pilot_gfx_texture_upload(texture,level,raw,width*height*2);
            CHECK(!memcmp(raw,before,width*height*2));
            CHECK(!pilot_gfx_texture_readback(texture,level,decoded,width*height*2));
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
                CHECK(decoded[y*width+x]==(uint16_t)(raw[g8b8_test_address(x,y,width,height)]|(format==3?0x8000:0)));
        }
        pilot_gfx_texture_bind(0,texture);pilot_gfx_texture_sampling_format(0,format);
        struct {float x,y,z,w;uint32_t color;float u,v;} vertices[]={
            {8,8,.5f,1,0xffffffff,0,0},{56,8,.5f,1,0xffffffff,0,0},{32,56,.5f,1,0xffffffff,0,0}};
        unsigned char pixels[64*64*4];
        /* Include both ignored-X values, alpha endpoints and distinct RGB. */
        for(unsigned y=0;y<4;y+=3)for(unsigned x=0;x<8;++x) {
            for(unsigned i=0;i<3;++i){vertices[i].u=(x+.5f)/8;vertices[i].v=(y+.5f)/4;}
            CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x144));
            CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
            const unsigned char *p=pixels+(24*64+32)*4;
            unsigned r=edge[(x+y)%6],g=edge[(2*x+y+1)%6],b=edge[(x+3*y+2)%6];
            CHECK(abs((int)p[0]-(int)((r*255+15)/31))<=1);
            CHECK(abs((int)p[1]-(int)((g*255+15)/31))<=1);
            CHECK(abs((int)p[2]-(int)((b*255+15)/31))<=1);
            CHECK(p[3]==(format==3 || ((x+y)&1)?255:0));
        }
        /* Changed CPU texels reach the GPU; ignored X never mutates input. */
        for(unsigned i=0;i<32;++i)raw[i]=0x001f;
        pilot_gfx_texture_upload(texture,0,raw,sizeof(raw));
        CHECK(!pilot_gfx_texture_readback(texture,0,decoded,sizeof(decoded)));
        CHECK(raw[0]==0x001f && decoded[0]==(format==3?0x801f:0x001f));
        pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);
    }
    fprintf(stderr,"PASS 1555: rectangular Morton/full mip tails, CPU source preservation, changed uploads, actual RGB5/alpha1 GPU pixels and ignored X\n");
    return 0;
}
