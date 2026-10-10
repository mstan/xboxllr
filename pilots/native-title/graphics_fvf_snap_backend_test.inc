/* Authored rational screen edges and ideal bilinear samples distinguish
 * rounded input+bias, signed truncation and unsnapped native coordinates. */
static int test_fvf_screen_snap(void)
{
    static const struct {float input,bias;double left,right;} cases[]={
        {-.5f,.53125f,0,64},{.125f,.03125f,.125,64.125},
        {-.125f,-.03125f,-.125,63.8125},{-.46875f,0,-.4375,63.5},
        {.4375f,.09375f,.5,64.5}};
    uint32_t texture;CHECK(!pilot_gfx_texture_create(8,8,1,0x12,&texture));
    uint32_t source[8*16];
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x)
        source[y*16+x]=0xff000000u|((x+y)&1?0x00ffffffu:0);
    pilot_gfx_texture_upload(texture,0,source,sizeof(source));pilot_gfx_texture_bind(0,texture);
    uint32_t ps[57]={0};ps[0]=0x18200000;ps[26]=0xc00;ps[34]=0x08200000;
    ps[45]=0xc00;ps[8]=0xc;ps[9]=0x1c00;ps[53]=1;ps[54]=1;
    CHECK(!pilot_gfx_pixel_program(ps,1));
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);
    for(unsigned stage=0;stage<4;++stage) {
        CHECK(pilot_gfx_xbox_texture_state(stage,0,3) && pilot_gfx_xbox_texture_state(stage,1,3));
        CHECK(pilot_gfx_xbox_texture_state(stage,3,2) && pilot_gfx_xbox_texture_state(stage,4,2));
        CHECK(pilot_gfx_xbox_texture_state(stage,5,0) && pilot_gfx_xbox_texture_state(stage,28,0));
    }
    uint32_t viewport[]={0,0,64,64,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
    for(unsigned n=0;n<sizeof(cases)/sizeof(*cases);++n)for(unsigned zero_w=0;zero_w<2;++zero_w) {
        const float a=cases[n].input,b=a+64;
        struct {float x,y,z,w,u,v;} quad[]={
            {a,a,0,1,0,0},{a,b,0,1,0,8},{b,a,0,1,8,0},{b,b,0,1,8,8}};
        for(unsigned i=0;i<4;++i)quad[i].w=zero_w?0:1;
        /* Negative edges exercise clipping/coverage with a constant texel.
         * Native clip/filter precision is a separate interpolation contract. */
        const int clipped=n==2 || n==3;
        if(clipped)for(unsigned i=0;i<4;++i)quad[i].u=quad[i].v=3.5f;
        CHECK(!pilot_gfx_clear(1,0xff123456,1,0));
        CHECK(!pilot_gfx_fvf_prepare(0x104,cases[n].bias,cases[n].bias));
        CHECK(!pilot_gfx_draw_memory(6,4,quad,sizeof(quad[0])));
        uint8_t actual[64*64*4];CHECK(!pilot_gfx_readback(actual,sizeof(actual)));
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            unsigned i=(y*64+x)*4;
            if(x+.5<cases[n].left || x+.5>=cases[n].right ||
               y+.5<cases[n].left || y+.5>=cases[n].right) {
                CHECK(actual[i]==0x12 && actual[i+1]==0x34 && actual[i+2]==0x56 && actual[i+3]==255);
                continue;
            }
            if(clipped) {CHECK(!actual[i] && !actual[i+1] && !actual[i+2] && actual[i+3]==255);continue;}
            double sx=(x+.5-cases[n].left)/(cases[n].right-cases[n].left)*8-.5;
            double sy=(y+.5-cases[n].left)/(cases[n].right-cases[n].left)*8-.5;
            int ix=(int)floor(sx),iy=(int)floor(sy);double fx=sx-ix,fy=sy-iy,value=0;
            for(int dx=0;dx<2;++dx)for(int dy=0;dy<2;++dy) {
                int xx=ix+dx,yy=iy+dy;if(xx<0)xx=0;if(xx>7)xx=7;if(yy<0)yy=0;if(yy>7)yy=7;
                value+=((xx+yy)&1?255:0)*(dx?fx:1-fx)*(dy?fy:1-fy);
            }
            int expected=(int)floor(value+.5);
            for(unsigned channel=0;channel<3;++channel) {
                if(abs((int)actual[i+channel]-expected)>1)
                    fprintf(stderr,"[FVF SNAP] case=%u RHWzero=%u xy=%u,%u channel=%u actual=%u expected=%d source=%g,%g\n",
                        n,zero_w,x,y,channel,actual[i+channel],expected,sx,sy);
                CHECK(abs((int)actual[i+channel]-expected)<=1);
            }
            CHECK(actual[i+3]==255);
        }
    }
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(texture);pilot_gfx_pixel_fixed();
    fprintf(stderr,"PASS: authored FVF signed subpixel screen+bias cases, ideal bilinear checker samples and RHW0/1\n");return 0;
}
