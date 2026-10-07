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
static void test_graphics_textures(void)
{
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t create[]={4096,4096,1,1,0,6,3};
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create)==0); /* guest RAM admission fails */
    CHECK(vm_committed(0,0x100000000ull)==resident);
    for(unsigned test=0;test<5;++test) {
        unsigned format=test<4?test+4:4,width=test==4?128:(test&1?4:8),height=test==4?256:(test&1?8:4);
        unsigned bpp=format<=5?2:4,levels=test==4?9:4;
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
            unsigned bytes=w*h*bpp;
            for(unsigned i=0;i<bytes;++i)linear[i]=(unsigned char)(i*37+level*53+test*19);
            uint32_t args[]={texture,level};
            uint32_t surface=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,args);
            uint32_t alias=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,args);
            CHECK(surface && alias && surface!=alias && MEM32(surface)==0x01050001u && MEM32(texture)==0x01040003u);
            CHECK(MEM32(surface+20)==texture && MEM32(surface+4)==MEM32(texture+4)+offset);
            uint32_t desc[]={surface,0x19504},expected[]={format,1,0,bytes,0x11,w,h};
            memset(memory+0x19500,0xcd,36);
            call_graphics(PILOT_D3D_D3DSurface_GetDesc,desc);
            CHECK(!memcmp(memory+0x19504,expected,sizeof(expected)));
            CHECK(MEM32(0x19500)==0xcdcdcdcdu && MEM32(0x19520)==0xcdcdcdcdu);
            uint32_t lock[]={surface,0x19540,0,0x40};
            call_graphics(PILOT_D3D_D3DSurface_LockRect,lock);
            uint32_t data=MEM32(0x19544);
            CHECK(MEM32(0x19540)==w*bpp && data==MEM32(surface+4));
            unsigned cursor=0;texture_test_swizzle(memory+data,linear,w,bpp,0,0,w,h,&cursor);
            CHECK(cursor==bytes);
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
    fprintf(stderr,"PASS: four texture formats, rectangular/full nine-level Morton chains, actual GPU readback, shared CPU views, changed-data reupload, parent/view lifetime and RAM failure cleanup\n");
}
