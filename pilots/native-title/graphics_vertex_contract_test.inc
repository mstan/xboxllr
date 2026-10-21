static void test_graphics_vertex_header_storage(void)
{
    uint64_t resident=vm_committed(0,0x100000000ull);
    /* Raw user VM reuse must not acquire ownership of a live driver header. */
    uint32_t raw=0,raw_bytes=4096;
    CHECK(!vm_allocate(&raw,0,&raw_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    uint32_t stale=raw;raw_bytes=0;CHECK(!vm_free(&raw,&raw_bytes,MEM_RELEASE));
    uint32_t live=graphics_vertex_allocate(128);
    CHECK(live && live!=stale && test_pool_owned(live) && !vm_find(live,12));
    struct GraphicsVertexObject *vertex=graphics_vertex(live);
    uint32_t data=vertex->data,native=vertex->native,expected[32],readback[32];
    for(unsigned i=0;i<32;++i)expected[i]=0x13579bdfu^(i*79);
    memcpy(memory+data,expected,sizeof(expected));
    pilot_gfx_vertex_upload(native,memory+data,sizeof(expected));
    raw=0;raw_bytes=4096;
    CHECK(!vm_allocate(&raw,0,&raw_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(raw==stale);raw_bytes=0;CHECK(!vm_free(&raw,&raw_bytes,MEM_RELEASE));
    CHECK(MEM32(live)==0x01000001u && vertex->refs==1);
    CHECK(!pilot_gfx_vertex_readback(native,readback,sizeof(readback)));
    CHECK(!memcmp(expected,readback,sizeof(expected)));
    graphics_vertex_bind(0,live,16);
    CHECK(!graphics_vertex_ref(live,0) && test_pool_owned(live) && physical_find(data,128));
    graphics_vertex_bind(0,0,0);
    CHECK(!test_pool_owned(live) && !physical_find(data,128) && graphics_vertex_find(live)<0);
    CHECK(vm_committed(0,0x100000000ull)==resident);
    puts("PASS vertex header storage: raw VM reuse/free, live CPU/GPU bytes, bound retention and final allocator cleanup");
}
static void test_graphics_vertices(void)
{
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t length=64u*1024*1024;
    CHECK(!call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length));
    CHECK(vm_committed(0,0x100000000ull)==resident);
    const unsigned sizes[]={1,4097,0x1c000};
    for(unsigned pass=0;pass<3;++pass) {
        length=sizes[pass];
        uint32_t header=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length);
        CHECK(header && MEM32(header)==0x01000001u && !MEM32(header+8));
        CHECK(test_pool_owned(header) && !vm_find(header,12));
        uint32_t lock[]={header,0},data=call_graphics(PILOT_D3D_D3DVertexBuffer_Lock2,lock);
        CHECK(data==(MEM32(header+4)|0x80000000u) && physical_find(data,length));
        unsigned char *expected=malloc(length),*readback=malloc(length);
        CHECK(expected && readback);
        for(unsigned i=0;i<length;++i)expected[i]=(unsigned char)(i*79+pass*31);
        memcpy(memory+data,expected,length);
        uint32_t native=graphics_vertex(header)->native;
        pilot_gfx_vertex_upload(native,memory+data,length);
        CHECK(pilot_gfx_vertex_readback(native,readback,length)==0 && !memcmp(readback,expected,length));
        lock[1]=0xf0;
        CHECK(call_graphics(PILOT_D3D_D3DVertexBuffer_Lock2,lock)==data);
        memory[data+length-1]^=0x69;expected[length-1]^=0x69;
        pilot_gfx_vertex_upload(native,memory+data,length);
        CHECK(pilot_gfx_vertex_readback(native,readback,length)==0 && !memcmp(readback,expected,length));
        CHECK(call_graphics(PILOT_D3D_D3DResource_AddRef,&header)==2 && MEM32(header)==0x01000002u);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==1);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==0);
        CHECK(!physical_find(data,length) && !vm_find(header,12));
        CHECK(!test_pool_owned(header));
        CHECK(vm_committed(0,0x100000000ull)==resident);
        free(expected);free(readback);
    }
    test_graphics_vertex_header_storage();
    /* Externally initialized headers alias one caller-owned allocation. A
     * nonzero Data offset is part of registration, not a second allocation. */
    uint32_t allocation=physical_allocate(8192,0,0xffffffffu,0,0x404);
    CHECK(allocation);
    const uint32_t headers[]={0x19600,0x19610};
    for(unsigned i=0;i<2;++i) {
        MEM32(headers[i])=1;MEM32(headers[i]+4)=i*4096;MEM32(headers[i]+8)=0;
        uint32_t args[]={headers[i],allocation};call_graphics(PILOT_D3D_D3DResource_Register,args);
        CHECK(MEM32(headers[i])==1 && MEM32(headers[i]+4)==((allocation+i*4096)&0x0fffffffu));
        uint32_t lock[]={headers[i],0xa0};
        CHECK(call_graphics(PILOT_D3D_D3DVertexBuffer_Lock2,lock)==allocation+i*4096);
    }
    MEM32(allocation+4096)=0xa5b6c7d8;
    unsigned char readback[8192];
    for(unsigned i=0;i<2;++i) {
        struct GraphicsVertexObject *v=graphics_vertex(headers[i]);
        pilot_gfx_vertex_upload(v->native,memory+v->data,v->bytes);
        CHECK(pilot_gfx_vertex_readback(v->native,readback,sizeof(readback))==0);
        CHECK(!memcmp(readback,memory+v->data,v->bytes));
    }
    CHECK(call_graphics(PILOT_D3D_D3DResource_AddRef,&headers[0])==2 && MEM32(headers[0])==2);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&headers[0])==1);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&headers[0])==0);
    CHECK(MEM32(allocation+4096)==0xa5b6c7d8 && MEM32(headers[0]+4)==(allocation&0x0fffffffu));
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&headers[1])==0);
    CHECK(physical_find(allocation,8192) && !physical_find(allocation,8192)->borrowers);
    physical_free(allocation);CHECK(vm_committed(0,0x100000000ull)==resident);
    fprintf(stderr,"PASS: vertex ABI, real CPU/GPU bytes, persistent pointer updates, references and allocation-failure cleanup\n");
}
