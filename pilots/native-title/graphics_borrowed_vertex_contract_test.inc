static void test_graphics_borrowed_vertices(const char *failure)
{
    const uint32_t headers[]={0x19620,0x19640};
    uint32_t allocation=physical_allocate(4096,0,0x03ffffff,4096,PAGE_READWRITE);CHECK(allocation);
    /* One header is already registered by original code; the other traverses
     * the existing shared Register helper. Both retain the same real backing. */
    MEM32(headers[0])=0x01000001;MEM32(headers[0]+4)=(allocation+64)&0x0fffffffu;MEM32(headers[0]+8)=0;
    MEM32(headers[1])=1;MEM32(headers[1]+4)=64;MEM32(headers[1]+8)=0;
    AcquireSRWLockExclusive(&graphics_lock);graphics_vertex_register(headers[1],allocation);ReleaseSRWLockExclusive(&graphics_lock);
    CHECK(call_graphics(PILOT_D3D_D3DResource_AddRef,&headers[0])==2);
    CHECK(graphics_vertex(headers[0])->borrowed && !graphics_vertex(headers[1])->borrowed);
    CHECK(physical_find(allocation,1)->borrowers==2);
    const float positions[3][3]={{-.7f,.6f,.5f},{.6f,.6f,.5f},{0,-.6f,.5f}};
    for(unsigned variant=0;variant<2;++variant) {
        for(unsigned i=0;i<3;++i) {
            memcpy(memory+allocation+64+i*32,positions[i],12);
            memset(memory+allocation+64+i*32+12,0,20);
        }
        uint32_t stream[]={0,headers[variant],32};call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
        if(failure && !strcmp(failure,"vertex-free"))physical_free(allocation);
        if(failure && !strcmp(failure,"vertex-change"))MEM32(headers[variant]+4)+=4;
        for(unsigned edit=0;edit<2;++edit) {
            uint32_t clear[]={0,0,0xf0,0xff000000,0x3f800000,0};call_graphics(PILOT_D3D_D3DDevice_Clear,clear);
            uint32_t draw[]={5,0,3};call_graphics(PILOT_D3D_D3DDevice_DrawVertices,draw);
            unsigned char pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
            unsigned center=(24*64+36)*4;CHECK(pixels[center]==(edit?0:255));
            /* No rebind: the next draw must see retained CPU pointer writes. */
            for(unsigned i=0;i<3;++i) {
                float x=positions[i][0]+3;memcpy(memory+allocation+64+i*32,&x,4);
            }
        }
        if(!variant) {
            ++MEM32(headers[0]);CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&headers[0])==2);
            CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&headers[0])==1);
        }
        /* Authored headers share an interior backing range; retain their
         * caller reference rather than asking the XDK allocator to free it. */
        CHECK((MEM32(headers[variant])&0xffff)==1);
        stream[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
        AcquireSRWLockExclusive(&graphics_lock);
        struct GraphicsVertexObject *owner=graphics_vertex(headers[variant]);owner->refs=0;
        graphics_vertex_collect(owner); /* same host-view disposal as device teardown */
        ReleaseSRWLockExclusive(&graphics_lock);
        CHECK(graphics_vertex_find(headers[variant])<0 && MEM32(headers[variant]+4)==((allocation+64)&0x0fffffffu));
        CHECK(physical_find(allocation,1)->borrowers==1-variant);
    }
    physical_free(allocation);
    puts("PASS original/HLE registered vertex owners share physical backing: offset, byte bounds, real draw/CPU edits, inline refs, bound lifetime and caller storage retention");
}
