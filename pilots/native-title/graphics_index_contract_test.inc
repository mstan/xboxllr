static void index_test_draw(uint32_t data,unsigned visible)
{
    uint32_t clear[]={0,0,0xf0,0xff000000,0x3f800000,0};call_graphics(PILOT_D3D_D3DDevice_Clear,clear);
    uint32_t draw[]={5,3,data};call_graphics(PILOT_D3D_D3DDevice_DrawIndexedVertices,draw);
    unsigned char pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof(pixels)));
    unsigned center=(24*64+36)*4;CHECK(pixels[center]==(visible?255:0));
}
static void test_graphics_indices(const char *failure)
{
    const uint32_t header=0x19700;
    uint32_t vertex_bytes=6*32,vertex_header=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&vertex_bytes);CHECK(vertex_header);
    uint32_t allocation=MEM32(vertex_header+4)|0x80000000u;CHECK(physical_find(allocation,vertex_bytes));
    uint32_t lock[]={vertex_header,0};CHECK(call_graphics(PILOT_D3D_D3DVertexBuffer_Lock2,lock)==allocation);
    const float positions[3][3]={{-.7f,.6f,.5f},{.6f,.6f,.5f},{0,-.6f,.5f}};
    memset(memory+allocation,0,6*32);
    for(unsigned i=0;i<6;++i)memcpy(memory+allocation+i*32,positions[i%3],12);
    uint32_t stream[]={0,vertex_header,32};call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    /* Source CreateIndexBuffer2 allocates header+payload together. */
    MEM32(header)=0x01010001;MEM32(header+4)=header+12;MEM32(header+8)=0x12345678;
    const unsigned char indices[]={0,0,1,0,2,0};memcpy(memory+header+12,indices,sizeof(indices));
    if(failure && !strcmp(failure,"index-shape"))MEM32(header+4)+=4;
    uint32_t binding[]={header,0};call_graphics(PILOT_D3D_D3DDevice_SetIndices,binding);
    CHECK(MEM32(PILOT_D3D_INDEX_DATA_GLOBAL)==header+12 && MEM32(header)==0x01090001 && MEM32(header+8)==0x12345678);
    if(failure && !strcmp(failure,"index-change"))MEM32(header+4)+=4;
    if(failure && !strcmp(failure,"index-cache"))MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_INDICES)=0;
    call_graphics(PILOT_D3D_D3DDevice_SetIndices,binding);CHECK(MEM32(header)==0x01090001);
    if(failure && !strcmp(failure,"index-range")){memory[header+12]=memory[header+13]=255;index_test_draw(header+12,1);CHECK(0);}
    if(failure && !strcmp(failure,"index-base")){binding[1]=UINT32_MAX;call_graphics(PILOT_D3D_D3DDevice_SetIndices,binding);index_test_draw(header+12,1);CHECK(0);}
    index_test_draw(header+12,1);
    memory[header+16]=1;index_test_draw(header+12,0);memory[header+16]=2; /* live index edit */
    for(unsigned i=0;i<3;++i){float x=positions[i][0]+3;memcpy(memory+allocation+i*32,&x,4);}
    index_test_draw(header+12,0);binding[1]=3;call_graphics(PILOT_D3D_D3DDevice_SetIndices,binding);index_test_draw(header+12,1);
    ++MEM32(header);CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==1);
    call_graphics(PILOT_D3D_D3DDevice_SetIndices,binding);
    CHECK(graphics_index(header)->bindings==1 && (MEM32(header)&0xffff)==1);
    binding[0]=0;call_graphics(PILOT_D3D_D3DDevice_SetIndices,binding);
    AcquireSRWLockExclusive(&graphics_lock);struct GraphicsIndexObject *borrowed=graphics_index(header);
    borrowed->refs=0;graphics_index_collect(borrowed);ReleaseSRWLockExclusive(&graphics_lock);
    CHECK(graphics_index_find(header)<0 && !MEM32(PILOT_D3D_INDEX_DATA_GLOBAL) && MEM32(header+4)==header+12);
    uint32_t length=64u*1024*1024;CHECK(!call_graphics(PILOT_D3D_D3DDevice_CreateIndexBuffer2,&length));
    length=6;uint32_t created=call_graphics(PILOT_D3D_D3DDevice_CreateIndexBuffer2,&length);CHECK(created && MEM32(created)==0x01010001 && MEM32(created+4)==created+12);
    memcpy(memory+created+12,indices,sizeof(indices));binding[0]=created;
    call_graphics(PILOT_D3D_D3DDevice_SetIndices,binding);index_test_draw(created+12,1);
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&created) && test_pool_owned(created));
    binding[0]=0;call_graphics(PILOT_D3D_D3DDevice_SetIndices,binding);CHECK(!test_pool_owned(created));
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&vertex_header));stream[1]=0;
    call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);CHECK(!physical_find(allocation,1));
    MEM32(header)=0x01010001;binding[0]=header;call_graphics(PILOT_D3D_D3DDevice_SetIndices,binding);
    binding[0]=0;call_graphics(PILOT_D3D_D3DDevice_SetIndices,binding); /* teardown retains caller */
    puts("PASS indexed runtime owner: original/created headers, published CPU data, self rebind, original refs, live index/vertex edits, base projection and real GPU pixels/lifetimes");
}
static void test_graphics_resource_destruction(const char *failure)
{
    uint32_t bytes=128,vertex=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&bytes);CHECK(vertex);
    uint32_t data=MEM32(vertex+4)|0x80000000u;
    if(failure && !strcmp(failure,"destroy-reference")) {
        call_graphics(PILOT_D3D_D3DResource_AddRef,&vertex);
        call_graphics(PILOT_D3D_D3D_DestroyResource,&vertex);CHECK(0);
    }
    if(failure && !strcmp(failure,"destroy-bound")) {
        uint32_t stream[]={0,vertex,32};call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
        call_graphics(PILOT_D3D_D3D_DestroyResource,&vertex);CHECK(0);
    }
    /* Last user count is still one at original Release's destruction tail. */
    call_graphics(PILOT_D3D_D3D_DestroyResource,&vertex);
    CHECK(graphics_vertex_find(vertex)<0 && !vm_find(vertex,12) && !physical_find(data,bytes));
    uint32_t index=call_graphics(PILOT_D3D_D3DDevice_CreateIndexBuffer2,&bytes);CHECK(index);
    MEM32(index)&=~0xffffu; /* an inline caller may already have stored zero */
    call_graphics(PILOT_D3D_D3D_DestroyResource,&index);
    CHECK(graphics_index_find(index)<0 && !test_pool_owned(index));
    uint32_t create[]={4,4,1,1,0,6,3},texture=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create);CHECK(texture);
    data=MEM32(texture+4);call_graphics(PILOT_D3D_D3D_DestroyResource,&texture);
    CHECK(!test_pool_owned(texture) && !physical_find(data,1));
    puts("PASS original destruction boundary: final-count-one/zero native vertex,index,texture owners retire real GPU/CPU/header storage; live references/bindings rejected");
}
