/* Real guest ABI, backing allocations and native GPU resources across two
 * registry growth boundaries, including live bindings and retained waiters. */
#if defined(PILOT_D3D_INDEX_DATA_GLOBAL) && !defined(PILOT_D3D_PROFILE_CLASSIC)
static void test_graphics_geometry_capacity(void)
{
    enum { count=513, bytes=64 };
    GRAPHICS_TEST_REQUIRE("geometry capacity",PILOT_D3D_D3DDevice_CreateVertexBuffer2,
        PILOT_D3D_D3DDevice_CreateIndexBuffer2,PILOT_D3D_D3DResource_Release,
        PILOT_D3D_D3DDevice_SetStreamSource,PILOT_D3D_D3DDevice_SetIndices);
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t vertices[count],indices[count],data[count],native[count],length=bytes;
    struct GraphicsResourceWait vertex_wait,index_wait;
    uint32_t stream[]={0,0,16},binding[]={0,0};
    for(unsigned i=0;i<count;++i) {
        vertices[i]=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length);
        indices[i]=call_graphics(PILOT_D3D_D3DDevice_CreateIndexBuffer2,&length);
        CHECK(vertices[i] && indices[i]);
        data[i]=MEM32(vertices[i]+4)|0x80000000u;
        native[i]=graphics_vertex(vertices[i])->native;
        CHECK(physical_find(data[i],bytes) && test_pool_owned(indices[i]));
        for(unsigned word=0;word<bytes/4;++word) {
            MEM32(data[i]+word*4)=0x13579bdfu^i^(word<<16);
            MEM32(indices[i]+12+word*4)=0x2468ace0u^i^(word<<16);
        }
        pilot_gfx_vertex_upload(native[i],memory+data[i],bytes);
        if(!i) {
            stream[1]=vertices[0];binding[0]=indices[0];
            call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
            call_graphics(PILOT_D3D_D3DDevice_SetIndices,binding);
            AcquireSRWLockExclusive(&graphics_lock);
            graphics_resource_wait_retain(vertices[0],&vertex_wait);
            graphics_resource_wait_retain(indices[0],&index_wait);
            ReleaseSRWLockExclusive(&graphics_lock);
            CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&vertices[0]));
            CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&indices[0]));
        }
    }
    CHECK(graphics_vertex_capacity>=count && graphics_index_capacity>=count);
    const unsigned samples[]={0,256,512};
    uint32_t readback[bytes/4];
    for(unsigned s=0;s<3;++s) {
        unsigned i=samples[s];
        CHECK(graphics_vertex(vertices[i])->native==native[i]);
        CHECK(!pilot_gfx_vertex_readback(native[i],readback,sizeof(readback)));
        for(unsigned word=0;word<bytes/4;++word) {
            CHECK(readback[word]==(0x13579bdfu^i^(word<<16)));
            CHECK(MEM32(indices[i]+12+word*4)==(0x2468ace0u^i^(word<<16)));
        }
    }
    CHECK(graphics_vertex(vertices[0])->bindings==1 && graphics_vertex(vertices[0])->waiters==1);
    CHECK(graphics_index(indices[0])->bindings==1 && graphics_index(indices[0])->waiters==1);
    for(unsigned i=1;i<count;++i) {
        CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&vertices[i]));
        CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&indices[i]));
        CHECK(!physical_find(data[i],1) && !vm_find(vertices[i],12) && !test_pool_owned(indices[i]));
    }
    uint32_t recycled_vertex=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length);
    uint32_t recycled_index=call_graphics(PILOT_D3D_D3DDevice_CreateIndexBuffer2,&length);
    CHECK(recycled_vertex && recycled_index && graphics_vertex_find(recycled_vertex)==1 && graphics_index_find(recycled_index)==1);
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&recycled_vertex));
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&recycled_index));
    stream[1]=binding[0]=0;
    call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    call_graphics(PILOT_D3D_D3DDevice_SetIndices,binding);
    CHECK(physical_find(data[0],bytes) && test_pool_owned(indices[0]));
    AcquireSRWLockExclusive(&graphics_lock);
    graphics_resource_wait_release(&vertex_wait);
    graphics_resource_wait_release(&index_wait);
    ReleaseSRWLockExclusive(&graphics_lock);
    CHECK(!graphics_resource_waiters && graphics_vertex_find(vertices[0])<0 && graphics_index_find(indices[0])<0);
    CHECK(!physical_find(data[0],1) && !vm_find(vertices[0],12) && !test_pool_owned(indices[0]));
    length=64u*1024*1024;
    CHECK(!call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length));
    CHECK(!call_graphics(PILOT_D3D_D3DDevice_CreateIndexBuffer2,&length));
    CHECK(vm_committed(0,0x100000000ull)==resident);
    puts("PASS geometry capacity: 513 simultaneous CPU vertex/index and native GPU owners, old/new bytes, stable handles/bindings/wait leases, release/reuse and real allocation-failure cleanup");
}
#else
static void test_graphics_geometry_capacity(void)
{
    fputs("SKIP: geometry capacity requires an admitted index-buffer ABI\n",stderr);
    exit(77);
}
#endif
