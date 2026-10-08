/* Phobos's real YUV conversion and empty-stage rule, through actual GPU draws.
 * Color bars are independently authored BT.601 inputs, not title footage. */
#ifdef PILOT_D3D_PHOBOS
static int test_movie_textures(void)
{
    const unsigned width=34,height=18,pitch=128;
    unsigned char source[128*18],saved[128*18],pixels[64*64*4];
    uint32_t decoded[34*18];
    struct {float x,y,z,w;uint32_t color;float u,v;} quad[]={
        {0,0,0,1,0xffffffff,0,0},{64,0,0,1,0xffffffff,34,0},
        {64,64,0,1,0xffffffff,34,18},{0,64,0,1,0xffffffff,0,18}};
    pilot_gfx_pixel_fixed();
    uint32_t vp[]={0,0,64,64,0,0x3f800000};CHECK(pilot_gfx_viewport(vp)==0);
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
    pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    for(unsigned stage=0;stage<4;++stage) {
        pilot_gfx_texture_bind(stage,0);
        pilot_gfx_set_texture_state(stage,D3DTSS_COLOROP,D3DTOP_DISABLE);
        pilot_gfx_set_texture_state(stage,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    }
    pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    pilot_gfx_set_texture_state(0,D3DTSS_TEXCOORDINDEX,0);
    pilot_gfx_set_texture_state(0,D3DTSS_MINFILTER,D3DTEXF_POINT);
    pilot_gfx_set_texture_state(0,D3DTSS_MAGFILTER,D3DTEXF_POINT);
    pilot_gfx_set_texture_state(0,D3DTSS_MIPFILTER,D3DTEXF_NONE);
    pilot_gfx_set_texture_state(0,D3DTSS_MAXMIPLEVEL,0);
    CHECK(pilot_gfx_xbox_texture_state(0,28,0));
    // Blinx leaves this stage active with no bound texture after loading UI.
    pilot_gfx_set_texture_state(1,D3DTSS_COLOROP,D3DTOP_MODULATE);
    pilot_gfx_set_texture_state(1,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    pilot_gfx_set_texture_state(1,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    for(unsigned format=0x24;format<=0x25;++format) {
        uint32_t texture=0;CHECK(pilot_gfx_texture_create(width,height,1,format,&texture)==0);
        for(unsigned frame=0;frame<4;++frame) {
            memset(source,0xcd,sizeof(source));
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;x+=2) {
                int red=(x<width/2)^(frame&1);
                unsigned yy=y<height/2?(red?81:41):235,u=y<height/2?(red?90:240):128,v=y<height/2?(red?240:110):128;
                unsigned char *p=source+y*pitch+x*2;
                if(format==0x24){p[0]=p[2]=(unsigned char)yy;p[1]=(unsigned char)u;p[3]=(unsigned char)v;}
                else {p[0]=(unsigned char)u;p[2]=(unsigned char)v;p[1]=p[3]=(unsigned char)yy;}
            }
            memcpy(saved,source,sizeof(saved));pilot_gfx_texture_upload(texture,0,source,sizeof(source));
            CHECK(!memcmp(source,saved,sizeof(source)));
            unsigned char grid[6];CHECK(!pilot_gfx_movie_luma(texture,16,grid,sizeof(grid)));
            for(unsigned gy=0;gy<2;++gy)for(unsigned gx=0;gx<3;++gx) {
                unsigned x=gx*16+8,y=gy*16+8;if(x>=width)x=width-1;if(y>=height)y=height-1;
                CHECK(grid[gy*3+gx]==saved[y*pitch+x*2+(format==0x25)]);
            }
            /* Capture observes the uploaded owner, not subsequent caller edits. */
            memset(source,0,sizeof(source));unsigned char retained[6];
            CHECK(!pilot_gfx_movie_luma(texture,16,retained,sizeof(retained)) && !memcmp(grid,retained,sizeof(grid)));
            CHECK(pilot_gfx_movie_luma(texture,16,retained,5)<0);
            CHECK(pilot_gfx_texture_readback(texture,0,decoded,sizeof(decoded))==0);
            CHECK((decoded[0]&0xffffff)==(frame&1?0x0000ff:0xff0000));
            CHECK((decoded[width*height-1]&0xffffff)==0xffffff);
            pilot_gfx_texture_bind(0,texture);
            CHECK(pilot_gfx_fvf_prepare(0x144,.53125f,.53125f)==0);
            CHECK(pilot_gfx_clear(1,0xff00ff00,1,0)==0);
            CHECK(pilot_gfx_draw_memory(8,4,quad,sizeof(quad[0]))==0);
            CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
            unsigned left=(8*64+8)*4,right=(8*64+56)*4,bottom=(56*64+8)*4;
            CHECK(pixels[left+(frame&1?2:0)]>=254 && pixels[left+(frame&1?0:2)]<=1 && pixels[left+1]<=1);
            CHECK(pixels[right+(frame&1?0:2)]>=254 && pixels[right+(frame&1?2:0)]<=1);
            CHECK(pixels[bottom]>=254 && pixels[bottom+1]>=254 && pixels[bottom+2]>=254);
            CHECK(pilot_gfx_present()==0);
        }
        pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);
    }
    pilot_gfx_set_texture_state(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    fprintf(stderr,"PASS: real YUY2/UYVY conversion, padded rows, texel coordinates, empty stage and successive movie frames\n");
    return 0;
}
#endif
