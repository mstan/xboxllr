/* An original standalone depth surface owns its CPU header/storage, rather
 * than a native CreateTexture parent. Exercise real GPU depth and stencil. */
static void test_graphics_borrowed_depth_surface(void)
{
    enum { width=16,height=16,bytes=width*height*4,header=0x19600 };
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t data=physical_allocate(bytes,0,0x03ffffff,4096,PAGE_READWRITE);CHECK(data);
    uint32_t initial[width*height],observed[width*height];
    for(unsigned i=0;i<width*height;++i)initial[i]=(i&1)?0xffffff3cu:0x0000005au;
    memcpy(memory+data,initial,bytes);
    MEM32(header)=0x00050001;MEM32(header+4)=data&0x0fffffffu;MEM32(header+8)=0;
    MEM32(header+12)=0x00012e2a;MEM32(header+16)=(width-1)|((height-1)<<12);
    const uint32_t saved[]={MEM32(header),MEM32(header+4),MEM32(header+8),MEM32(header+12),MEM32(header+16)};
    uint32_t args[]={width,height,1,1,1,0x12,3};
    uint32_t color=graphics_texture_allocate(args),view=graphics_texture_surface(color,0);CHECK(color && view);
    graphics_set_targets(view,header);
    unsigned slot=graphics_texture_slot(header);struct GraphicsTextureObject *depth=&graphics_textures[slot];
    CHECK(depth->standalone_surface && depth->cpu_borrowed && !depth->parent && depth->usage==2);
    CHECK(depth->refs==2 && physical_find(data,bytes)->borrowers==1);
    CHECK(!pilot_gfx_texture_readback(depth->native,0,observed,bytes) && !memcmp(initial,observed,bytes));
    CHECK(!pilot_gfx_clear(6,0,1,0x77));
    CHECK(!pilot_gfx_texture_readback(depth->native,0,observed,bytes));
    for(unsigned i=0;i<width*height;++i)CHECK(observed[i]==0xffffff77u);
    graphics_set_targets(graphics_headers,graphics_headers+64);
    CHECK(depth->refs==1);
    graphics_set_targets(view,header);
    CHECK(!pilot_gfx_texture_readback(depth->native,0,observed,bytes));
    for(unsigned i=0;i<width*height;++i)CHECK(observed[i]==0xffffff77u);
    CHECK(!memcmp(memory+data,initial,bytes)); /* Rebind must not upload this stale CPU image. */
    graphics_set_targets(graphics_headers,graphics_headers+64);
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&view));
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&color));
    graphics_texture_aliases_close();
    CHECK(!physical_find(data,bytes)->borrowers && !memcmp(memory+header,saved,sizeof(saved)));
    CHECK(!memcmp(memory+data,initial,bytes));physical_free(data);
    CHECK(vm_committed(0,0x100000000ull)==resident);
    puts("PASS borrowed depth surface: original header/storage, GPU depth/stencil bytes, target references, GPU content across rebind and caller ownership after teardown");
}
