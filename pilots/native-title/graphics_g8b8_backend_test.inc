/* Authored unsigned endpoints and rectangular Morton layouts. Expected RGBA
 * is (low byte, high byte, low byte, high byte), independently documented by
 * xemu's GL/Vulkan SZ_G8B8 swizzles and Cxbx's G8B8 converter. */
static unsigned g8b8_test_address(unsigned x,unsigned y,unsigned width,unsigned height)
{
    unsigned result=0,shift=0;
    for(unsigned bit=1;bit<width || bit<height;bit<<=1) {
        if(bit<width){if(x&bit)result|=1u<<shift;++shift;}
        if(bit<height){if(y&bit)result|=1u<<shift;++shift;}
    }
    return result;
}
static int test_g8b8_texture(void)
{
    uint32_t texture=0,untouched=0x12345678;
    CHECK(pilot_gfx_texture_create_usage(8,4,1,0x28,1,&untouched)<0 && untouched==0x12345678);
    CHECK(pilot_gfx_texture_create(3,4,1,0x28,&untouched)<0 && untouched==0x12345678);
    CHECK(pilot_gfx_texture_create(8,4,1,0x29,&untouched)<0 && untouched==0x12345678);
    CHECK(!pilot_gfx_texture_create(8,4,4,0x28,&texture));
    unsigned char raw[64],before[64],decoded[128];
    static const unsigned char endpoints[]={0,127,128,255};
    for(unsigned level=0;level<4;++level) {
        unsigned width=8>>level,height=4>>level;if(!height)height=1;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            unsigned address=g8b8_test_address(x,y,width,height)*2;
            raw[address]=endpoints[(x+2*y+level)%4];
            raw[address+1]=endpoints[(2*x+y+level+1)%4];
        }
        memcpy(before,raw,width*height*2);
        pilot_gfx_texture_upload(texture,level,raw,width*height*2);
        CHECK(!memcmp(raw,before,width*height*2));
        CHECK(!pilot_gfx_texture_readback(texture,level,decoded,width*height*4));
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            unsigned address=g8b8_test_address(x,y,width,height)*2;
            const unsigned char *pixel=decoded+(y*width+x)*4;
            CHECK(pixel[0]==raw[address] && pixel[1]==raw[address+1]);
            CHECK(pixel[2]==raw[address] && pixel[3]==raw[address+1]);
        }
    }
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_LIGHTING,0);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
    pilot_gfx_set_state(D3DRS_FOGENABLE,0);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    pilot_gfx_texture_bind(0,texture);pilot_gfx_texture_sampling_format(0,0x28);
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
    unsigned char pixels[64*64*4];
    for(unsigned y=0;y<4;y+=3)for(unsigned x=0;x<8;++x) {
        for(unsigned i=0;i<3;++i){vertices[i].u=(x+.5f)/8;vertices[i].v=(y+.5f)/4;}
        CHECK(!pilot_gfx_clear(1,0,1,0));
        CHECK(!pilot_gfx_draw_up(4,1,vertices,sizeof(vertices[0]),0x144));
        CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
        const unsigned char *pixel=pixels+(24*64+32)*4;
        unsigned b=endpoints[(x+2*y)%4],g=endpoints[(2*x+y+1)%4];
        CHECK(pixel[0]==b && pixel[1]==g && pixel[2]==b && pixel[3]==g);
    }
    /* Repeat unchanged input, then check a changed pair on the GPU without
     * changing another texel or writing into source storage. */
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<8;++x) {
        unsigned address=g8b8_test_address(x,y,8,4)*2;
        raw[address]=endpoints[(x+2*y)%4];raw[address+1]=endpoints[(2*x+y+1)%4];
    }
    pilot_gfx_texture_upload(texture,0,raw,64);
    raw[0]=231;raw[1]=17;memcpy(before,raw,64);
    pilot_gfx_texture_upload(texture,0,raw,64);
    CHECK(!memcmp(raw,before,64));CHECK(!pilot_gfx_texture_readback(texture,0,decoded,128));
    CHECK(decoded[0]==231 && decoded[1]==17 && decoded[2]==231 && decoded[3]==17);
    CHECK(decoded[4]==127 && decoded[5]==255 && decoded[6]==127 && decoded[7]==255);
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);
    fprintf(stderr,"PASS: G8B8 unsigned BGBG GPU sampling, rectangular/full mip tails, changed uploads and guarded usage/format\n");
    return 0;
}
