/* The prepared XRGB upload normalizes only native alpha. Guest storage and
 * surface ownership retain every authored byte. This fixture compares both
 * representations exactly, including the full rectangular mip tails. */
static void x8_texture_readback(uint32_t native,unsigned level,unsigned format,
    const unsigned char *linear,unsigned bytes)
{
    unsigned char actual[128],expected[128];CHECK(bytes<=sizeof(actual));
    memcpy(expected,linear,bytes);
    if(format==7)for(unsigned i=3;i<bytes;i+=4)expected[i]=255;
    CHECK(!pilot_gfx_texture_readback(native,level,actual,bytes));
    if(memcmp(actual,expected,bytes)) {
        for(unsigned i=0;i<bytes;++i)if(actual[i]!=expected[i]) {
            fprintf(stderr,"[X8 READBACK] format=%02x level=%u byte=%u actual=%02x expected=%02x guest=%02x\n",
                format,level,i,actual[i],expected[i],linear[i]);break;
        }
        CHECK(!memcmp(actual,expected,bytes));
    }
}
static void x8_texture_sample(uint32_t texture,unsigned width,unsigned height,
    unsigned format,const unsigned char *linear)
{
    uint32_t zero=0,fvf=0x144,bind[]={0,texture};
    call_graphics(PILOT_D3D_D3DDevice_SetPixelShader,&zero);
    call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&fvf);
    graphics_render_state(143,0,1);graphics_render_state(144,0,1);graphics_render_state(147,0,1);
    graphics_render_state(60,0,1);graphics_render_state(59,0,1);graphics_render_state(92,0,1);
    graphics_render_state(67,0x01010101,1);
    for(unsigned i=0;i<4;++i)graphics_texture_state(i,12,1,1);
    graphics_texture_state(0,12,2,1);graphics_texture_state(0,14,2,1);
    graphics_texture_state(0,16,2,1);graphics_texture_state(0,18,2,1);
    graphics_texture_state(0,0,3,1);graphics_texture_state(0,1,3,1);
    graphics_texture_state(0,3,1,1);graphics_texture_state(0,4,1,1);graphics_texture_state(0,5,0,1);
    struct {float x,y,z,w;uint32_t color;float u,v;} quad[]={
        {16,16,0,1,0xffffffff,0,0},{16,48,0,1,0xffffffff,0,0},
        {48,16,0,1,0xffffffff,0,0},{48,48,0,1,0xffffffff,0,0}};
    for(unsigned i=0;i<4;++i){quad[i].u=.5f/width;quad[i].v=.5f/height;}
    memcpy(memory+0x19800,quad,sizeof(quad));uint32_t draw[]={6,4,0x19800,sizeof(quad[0])};
    call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
    call_graphics(PILOT_D3D_D3DDevice_DrawVerticesUP,draw);
    unsigned char pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
    const unsigned n=(32*64+32)*4;
    CHECK(pixels[n]==linear[2] && pixels[n+1]==linear[1] && pixels[n+2]==linear[0]);
    CHECK(pixels[n+3]==(format==7?255:linear[3]));
    bind[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
}
static void test_graphics_x8_texture_lifetime(void)
{
    const uint64_t resident=vm_committed(0,0x100000000ull);
    for(unsigned format=6;format<=7;++format)for(unsigned shape=0;shape<2;++shape) {
        unsigned width=shape?8:4,height=shape?4:8;
        uint32_t create[]={width,height,1,0,0,format,3};
        uint32_t texture=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create);CHECK(texture);
        CHECK(call_graphics(PILOT_D3D_D3DBaseTexture_GetLevelCount,&texture)==4);
        CHECK(((MEM32(texture+12)>>8)&255)==format);
        const unsigned native=graphics_textures[graphics_texture_slot(texture)].native;
        unsigned offset=0;unsigned char first_linear[128];
        for(unsigned level=0;level<4;++level) {
            unsigned w=width>>level,h=height>>level;if(!w)w=1;if(!h)h=1;
            const unsigned bytes=w*h*4;unsigned char linear[128],swizzled[128];
            for(unsigned i=0;i<bytes;++i)linear[i]=(unsigned char)(i*37+level*53+format*19+shape*7);
            unsigned cursor=0;texture_test_swizzle(swizzled,linear,w,4,0,0,w,h,&cursor);CHECK(cursor==bytes);
            uint32_t view_args[]={texture,level};
            uint32_t surface=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,view_args);
            uint32_t alias=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,view_args);
            CHECK(surface && alias && surface!=alias && MEM32(texture)==0x01040003u);
            CHECK(MEM32(surface+4)==MEM32(texture+4)+offset && MEM32(alias+4)==MEM32(surface+4));
            uint32_t desc[]={surface,0x19504},expected_desc[]={format,1,1,bytes,0x11,w,h};
            call_graphics(PILOT_D3D_D3DSurface_GetDesc,desc);
            CHECK(!memcmp(memory+0x19504,expected_desc,sizeof(expected_desc)));
            uint32_t lock[]={surface,0x19540,0,0x40};call_graphics(PILOT_D3D_D3DSurface_LockRect,lock);
            const uint32_t data=MEM32(0x19544);CHECK(MEM32(0x19540)==w*4 && data==MEM32(surface+4));
            memcpy(memory+data,swizzled,bytes);
            lock[0]=alias;lock[3]=0xc0;call_graphics(PILOT_D3D_D3DSurface_LockRect,lock);
            CHECK(MEM32(0x19544)==data);
            CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&surface));
            CHECK(MEM32(texture)==0x01040002u && !memcmp(memory+data,swizzled,bytes));
            x8_texture_readback(native,level,format,linear,bytes);
            /* Both an RGB byte and the ignored guest X/alpha byte change. */
            linear[0]^=0x5a;linear[3]^=0xa5;cursor=0;
            texture_test_swizzle(swizzled,linear,w,4,0,0,w,h,&cursor);
            memcpy(memory+data,swizzled,bytes);
            CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&alias));
            CHECK(MEM32(texture)==0x01040001u && !memcmp(memory+data,swizzled,bytes));
            x8_texture_readback(native,level,format,linear,bytes);
            if(!level)memcpy(first_linear,linear,bytes);
            CHECK(!test_pool_owned(surface) && !test_pool_owned(alias) && test_pool_owned(texture));
            offset+=bytes;
        }
        x8_texture_sample(texture,width,height,format,first_linear);
        uint32_t args[]={texture,0},view=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,args);
        const uint32_t data=MEM32(texture+4);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&texture)==1);
            CHECK(physical_find(data,offset) && MEM32(view+20)==texture && test_pool_owned(texture));
        CHECK(call_graphics(PILOT_D3D_D3DResource_AddRef,&view)==2);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&view)==1);
        CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&view));
        CHECK(!test_pool_owned(texture) && !test_pool_owned(view) && !physical_find(data,offset));
        CHECK(vm_committed(0,0x100000000ull)==resident);
    }
    fprintf(stderr,"PASS: XRGB normalized native alpha, exact ARGB controls/RGB, both rectangular full mip chains, unchanged guest bytes, changed-data uploads, sampled alpha and shared surface/parent lifetimes\n");
}
