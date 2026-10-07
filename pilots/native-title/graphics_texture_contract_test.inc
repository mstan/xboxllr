/* Independent recursive Morton fixture: visit the high coordinate halves,
 * then compare the resulting real GPU texture with authored linear pixels. */
static void texture_test_swizzle(unsigned char *destination,const unsigned char *source,
    unsigned stride,unsigned bpp,unsigned left,unsigned top,unsigned width,unsigned height,unsigned *cursor)
{
    if(width==1 && height==1) {
        memcpy(destination+*cursor,source+(top*stride+left)*bpp,bpp); *cursor+=bpp; return;
    }
    if(height>=width) {
        texture_test_swizzle(destination,source,stride,bpp,left,top,width,height/2,cursor);
        texture_test_swizzle(destination,source,stride,bpp,left,top+height/2,width,height/2,cursor);
    } else {
        texture_test_swizzle(destination,source,stride,bpp,left,top,width/2,height,cursor);
        texture_test_swizzle(destination,source,stride,bpp,left+width/2,top,width/2,height,cursor);
    }
}
static void test_graphics_target_binding(void)
{
    uint64_t resident=vm_committed(0,0x100000000ull);
    unsigned back_refs=graphics_surface_refs[0],depth_refs=graphics_surface_refs[2];
    uint32_t back=call_graphics(PILOT_D3D_D3DDevice_GetRenderTarget2,NULL);
    uint32_t depth=call_graphics(PILOT_D3D_D3DDevice_GetDepthStencilSurface2,NULL);CHECK(back==graphics_headers && depth==graphics_headers+64);
    uint32_t create[]={32,24,1,1,1,0x12,3},textures[2],views[2];
    for(unsigned i=0;i<2;++i){create[4]=i?2:1;create[5]=i?0x2e:0x12;textures[i]=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create);CHECK(textures[i]);uint32_t a[]={textures[i],0};views[i]=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,a);CHECK(views[i]);}
    call_graphics(PILOT_D3D_D3DDevice_SetRenderTarget,views);
    CHECK(MEM32(views[0])==0x01050002 && MEM32(views[1])==0x01050002);
    uint32_t vp[6];CHECK(pilot_gfx_get_viewport(vp)==0 && vp[0]==0 && vp[1]==0 && vp[2]==32 && vp[3]==24 && vp[4]==0 && vp[5]==0x3f800000);
    uint32_t query=call_graphics(PILOT_D3D_D3DDevice_GetRenderTarget2,NULL);CHECK(query==views[0]);CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&query)==2);
    for(unsigned i=0;i<2;++i)CHECK(call_graphics(PILOT_D3D_D3DResource_Release,views+i)==1); /* binding owns both */
    call_graphics(PILOT_D3D_D3DDevice_SetRenderTarget,views); /* safe self-rebind */
    uint32_t clear[]={0,0,0xf3,0xff2468ac,0x3f800000,0x39};CHECK(call_graphics(PILOT_D3D_D3DDevice_Clear,clear)==0);
    uint32_t pixels[32*24];
    for(unsigned i=0;i<2;++i){unsigned native=graphics_textures[graphics_texture_slot(textures[i])].native;CHECK(pilot_gfx_texture_readback(native,0,pixels,sizeof(pixels))==0);for(unsigned p=0;p<32*24;++p)CHECK(pixels[p]==(i?0xffffff39:0xff2468ac));}
    /* CPU edits through a different alias must precede a partial GPU clear;
     * releasing that alias afterwards must not re-upload stale pre-clear data. */
    uint32_t view_args[]={textures[1],0},alias=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,view_args);
    uint32_t lock[]={alias,0x19540,0,0};call_graphics(PILOT_D3D_D3DSurface_LockRect,lock);MEM32(MEM32(0x19544))=0x123456a1;
    clear[2]=2;clear[5]=0x57;CHECK(call_graphics(PILOT_D3D_D3DDevice_Clear,clear)==0);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&alias)==0);
    CHECK(pilot_gfx_texture_readback(graphics_textures[graphics_texture_slot(textures[1])].native,0,pixels,sizeof(pixels))==0 && pixels[0]==0x12345657 && pixels[1]==0xffffff57);
    uint32_t disabled[]={0,0};call_graphics(PILOT_D3D_D3DDevice_SetRenderTarget,disabled);
    CHECK(graphics_target==views[0] && !call_graphics(PILOT_D3D_D3DDevice_GetDepthStencilSurface2,NULL));
    CHECK(!vm_find(views[1],24));
    uint32_t restored[]={back,depth};call_graphics(PILOT_D3D_D3DDevice_SetRenderTarget,restored);CHECK(!vm_find(views[0],24));
    for(unsigned i=0;i<2;++i)CHECK(call_graphics(PILOT_D3D_D3DResource_Release,textures+i)==0);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&back)==back_refs && call_graphics(PILOT_D3D_D3DResource_Release,&depth)==depth_refs);
    CHECK(vm_committed(0,0x100000000ull)==resident);
    fprintf(stderr,"PASS: real color/depth binding and clears, NULL depth, retained target, safe self-rebind and binding ownership\n");
}
static void test_graphics_targets(void)
{
    uint64_t resident=vm_committed(0,0x100000000ull);
    for(unsigned pass=0;pass<4;++pass) {
        unsigned width=pass?37:640,height=pass?19:480,format=pass==3?0x2e:pass==2?0x1e:0x12;
        unsigned depth=format==0x2e;
        unsigned pitch=(width*4+63)&~63u,bytes=pitch*height;
        uint32_t mask=format==0x1e?0x00ffffff:0xffffffff; /* XRGB has no alpha contract. */
        uint32_t args[]={width,height,1,1,depth?2:1,format,3};
        uint32_t texture=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,args);
        CHECK(texture && MEM32(texture+12)==(0x10029|(format<<8)));
        CHECK(MEM32(texture+16)==(pass?0x02012024:0x271df27f)); /* original XDK oracle */
        struct GraphicsTextureObject *t=&graphics_textures[graphics_texture_slot(texture)];CHECK(t->bytes==bytes && t->usage==(depth?2:1));
        uint32_t view_args[]={texture,0},view=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,view_args);
        uint32_t desc[]={view,0x19504},expected[]={format,1,depth?2:1,bytes,0x11,width,height};
        call_graphics(PILOT_D3D_D3DSurface_GetDesc,desc);CHECK(!memcmp(memory+desc[1],expected,sizeof(expected)));
        uint32_t original=depth?0xffffff5a:0xff123456;
        if(depth)CHECK(pilot_gfx_texture_clear_depth(t->native,3,1.0f,0x5a)==0);
        else CHECK(pilot_gfx_texture_clear(t->native,original)==0);
        uint32_t *pixels=malloc(width*height*4);CHECK(pixels);
        CHECK(pilot_gfx_texture_readback(t->native,0,pixels,width*height*4)==0);
        for(unsigned i=0;i<width*height;++i)CHECK((pixels[i]&mask)==(original&mask));
        /* Releasing an untouched view must not upload stale CPU zeroes. */
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&view)==0);
        CHECK(pilot_gfx_texture_readback(t->native,0,pixels,width*height*4)==0 && (pixels[0]&mask)==(original&mask));
        view=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,view_args);
        uint32_t lock[]={view,0x19540,0,0};call_graphics(PILOT_D3D_D3DSurface_LockRect,lock);
        CHECK(MEM32(0x19540)==pitch && MEM32(0x19544)==t->data);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)CHECK((MEM32(t->data+y*pitch+x*4)&mask)==(original&mask));
        MEM32(t->data)=0xffa1b2c3;
        uint32_t alias=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,view_args);lock[0]=alias;
        call_graphics(PILOT_D3D_D3DSurface_LockRect,lock);CHECK(MEM32(t->data)==0xffa1b2c3);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&view)==0);
        CHECK(pilot_gfx_texture_readback(t->native,0,pixels,width*height*4)==0 && (pixels[0]&mask)==(0xffa1b2c3&mask) && (pixels[1]&mask)==(original&mask));
        if(depth) {
            CHECK(pilot_gfx_texture_clear_depth(t->native,2,0,0x27)==0);
            CHECK(pilot_gfx_texture_readback(t->native,0,pixels,width*height*4)==0 && pixels[0]==0xffa1b227 && pixels[1]==0xffffff27);
            CHECK(pilot_gfx_texture_clear_depth(t->native,1,0,0)==0);
            CHECK(pilot_gfx_texture_readback(t->native,0,pixels,width*height*4)==0 && pixels[0]==0x27 && pixels[1]==0x27);
            lock[0]=alias;call_graphics(PILOT_D3D_D3DSurface_LockRect,lock);CHECK(MEM32(t->data)==0x27);
        }
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&texture)==1);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&alias)==0);
        CHECK(vm_committed(0,0x100000000ull)==resident);free(pixels);
    }
    fprintf(stderr,"PASS: actual linear render-target GPU writes, original header/pitch/size, readback/CPU edits, aliases and lifetime\n");
    test_graphics_target_binding();
}
static void test_graphics_textures(void)
{
    test_graphics_targets();
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t create[]={4096,4096,1,1,0,6,3};
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create)==0); /* guest RAM admission fails */
    CHECK(vm_committed(0,0x100000000ull)==resident);
    const unsigned formats[]={4,5,6,7,4,0xc,0xe,0xf};
    for(unsigned test=0;test<8;++test) {
        unsigned format=formats[test],width=test==4?128:(test&1?4:8),height=test==4?256:(test&1?8:4);
        unsigned bpp=format<=5?2:4,levels=test==4?9:4;
        unsigned block=test>=5?(format==0xc?8:16):0;
        create[0]=width;create[1]=height;create[3]=0;create[5]=format;
        uint32_t texture=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create);
        CHECK(texture && MEM32(texture)==0x01040001u && MEM32(texture+16)==0);
        CHECK(call_graphics(PILOT_D3D_D3DBaseTexture_GetLevelCount,&texture)==levels);
        unsigned native=graphics_textures[graphics_texture_slot(texture)].native;
        unsigned char *linear=malloc(width*height*bpp),*pixels=malloc(width*height*bpp);
        CHECK(linear && pixels);
        unsigned offset=0;
        for(unsigned level=0;level<levels;++level) {
            unsigned w=width>>level,h=height>>level;if(!w)w=1;if(!h)h=1;
            unsigned bytes=block?((w+3)/4)*((h+3)/4)*block:w*h*bpp;
            for(unsigned i=0;i<bytes;++i)linear[i]=(unsigned char)(i*37+level*53+test*19);
            uint32_t args[]={texture,level};
            uint32_t surface=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,args);
            uint32_t alias=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,args);
            CHECK(surface && alias && surface!=alias && MEM32(surface)==0x01050001u && MEM32(texture)==0x01040003u);
            CHECK(MEM32(surface+20)==texture && MEM32(surface+4)==MEM32(texture+4)+offset);
            uint32_t desc[]={surface,0x19504},expected[]={format,1,(format>=5 && format<=7)?1:0,bytes,0x11,w,h};
            memset(memory+0x19500,0xcd,36);
            call_graphics(PILOT_D3D_D3DSurface_GetDesc,desc);
            CHECK(!memcmp(memory+0x19504,expected,sizeof(expected)));
            CHECK(MEM32(0x19500)==0xcdcdcdcdu && MEM32(0x19520)==0xcdcdcdcdu);
            uint32_t texture_desc[]={texture,level,0x19504};expected[1]=3;
            if(level)expected[2]=0;
            call_graphics(PILOT_D3D_Get2DSurfaceDesc,texture_desc);
            CHECK(!memcmp(memory+0x19504,expected,sizeof(expected)));
            CHECK(MEM32(0x19500)==0xcdcdcdcdu && MEM32(0x19520)==0xcdcdcdcdu);
            uint32_t lock[]={surface,0x19540,0,0x40};
            call_graphics(PILOT_D3D_D3DSurface_LockRect,lock);
            uint32_t data=MEM32(0x19544);
            CHECK(MEM32(0x19540)==(block?((w+3)/4)*block:w*bpp) && data==MEM32(surface+4));
            if(block)memcpy(memory+data,linear,bytes);
            else {
                unsigned cursor=0;texture_test_swizzle(memory+data,linear,w,bpp,0,0,w,h,&cursor);
                CHECK(cursor==bytes);
            }
            lock[0]=alias;lock[3]=0xc0;call_graphics(PILOT_D3D_D3DSurface_LockRect,lock);
            CHECK(MEM32(0x19544)==data);
            CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&surface)==0 && MEM32(texture)==0x01040002u);
            CHECK(pilot_gfx_texture_readback(native,level,pixels,bytes)==0 && !memcmp(pixels,linear,bytes));
            /* A persistent CPU pointer may be changed after an earlier upload. */
            MEM8(data)^=0x5a;linear[0]^=0x5a;
            CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&alias)==0 && MEM32(texture)==0x01040001u);
            CHECK(pilot_gfx_texture_readback(native,level,pixels,bytes)==0 && !memcmp(pixels,linear,bytes));
            CHECK(!vm_find(surface,24) && !vm_find(alias,24));
            offset+=bytes;
        }
        uint32_t args[]={texture,0};
        uint32_t view=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,args),data=MEM32(texture+4);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&texture)==1);
        CHECK(vm_find(data,offset) && MEM32(view+20)==texture);
        CHECK(call_graphics(PILOT_D3D_D3DResource_AddRef,&view)==2);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&view)==1);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&view)==0);
        CHECK(!vm_find(texture,20) && !vm_find(data,offset));
        CHECK(vm_committed(0,0x100000000ull)==resident);
        free(pixels);free(linear);
    }
    fprintf(stderr,"PASS: four color and three DXT formats, rectangular/full nine-level chains including BC 1x1 tails, actual GPU readback, shared CPU views, changed-data reupload, parent/view lifetime and RAM failure cleanup\n");
}
