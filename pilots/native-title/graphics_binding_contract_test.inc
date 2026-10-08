static void test_graphics_stream_consumption(void)
{
    uint32_t previous=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER);
    graphics_state_init();
    graphics_render_state(143,0,1);graphics_render_state(147,0,1);
    graphics_render_state(60,0,1);graphics_render_state(59,0,1);
    graphics_texture_state(0,12,2,1);graphics_texture_state(0,14,0,1);
    graphics_texture_state(0,16,2,1);graphics_texture_state(0,18,0,1);
    uint32_t fixed=0,fvf=0x44;
    call_graphics(PILOT_D3D_D3DDevice_SetPixelShader,&fixed);
    call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&fvf);
    struct Vertex {float x,y,z,w;uint32_t color;};
    struct Vertex source[]={{0},{8,8,.5f,1,0xffff0000},
        {56,8,.5f,1,0xffff0000},{32,56,.5f,1,0xffff0000}};
    uint32_t length=sizeof(source),header=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length);
    uint32_t lock[]={header,0},data=call_graphics(PILOT_D3D_D3DVertexBuffer_Lock2,lock);
    memcpy(memory+data,source,sizeof(source));
    uint32_t bind[]={0,header,sizeof(struct Vertex)};
    call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,bind);
    uint32_t draw[]={5,1,3};
    for(unsigned pass=0;pass<2;++pass) {
        /* No rebind/Lock: the original persistent CPU pointer remains live.
         * A bind-time snapshot alone produces the wrong real GPU pixels. */
        for(unsigned i=1;i<4;++i)MEM32(data+i*sizeof(struct Vertex)+16)=pass?0xff0000ff:0xff00ff00;
        CHECK(pilot_gfx_clear(1,0xff000000,1,0)==0);
        call_graphics(PILOT_D3D_D3DDevice_DrawVertices,draw);
        graphics_test_pixel(0,pass?0:255,pass?255:0,0);
    }
    bind[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,bind);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==0);
    call_graphics(PILOT_D3D_D3DDevice_SetVertexShader,&previous);
    fprintf(stderr,"PASS: actual guest DrawVertices consumes post-bind and later CPU writes at nonzero first vertex\n");
}
static void test_graphics_resource_bindings(void)
{
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t fence=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_RESOURCE_FENCE,old_fence=MEM32(fence);
    MEM32(fence)=0x12345678;
    for(unsigned pass=0;pass<2;++pass) {
        uint32_t args[]={8,8,1,1,pass,0x12,3};
        uint32_t header=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,args);
        struct GraphicsTextureObject *texture=&graphics_textures[graphics_texture_slot(header)];
        uint32_t native=texture->native,data=texture->data;
        for(unsigned i=0;i<texture->bytes/4;++i)MEM32(data+i*4)=0xff123456;
        if(pass)CHECK(pilot_gfx_texture_clear(native,0xff987654)==0);
        uint32_t states[4][32];memcpy(states,memory+PILOT_D3D_TEXTURE_STATES,sizeof(states));
        uint32_t bind[]={0,header};call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
        CHECK(MEM32(header)==0x010c0001 && MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES)==header);
        CHECK(!memcmp(states,memory+PILOT_D3D_TEXTURE_STATES,sizeof(states)));
        unsigned char pixels[8*8*4];CHECK(pilot_gfx_texture_readback(native,0,pixels,sizeof(pixels))==0);
        uint32_t first;memcpy(&first,pixels,4);CHECK(first==(pass?0xff987654u:0xff123456u));
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==0 && MEM32(header)==0x010c0000);
        call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind); /* only binding owns self-rebind */
        CHECK(MEM32(header)==0x010c0000 && MEM32(header+8)==0x12345678);
        CHECK(call_graphics(PILOT_D3D_D3DResource_AddRef,&header)==1 && MEM32(header)==0x010c0001);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==0);
        bind[0]=2;call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);CHECK(MEM32(header)==0x01140000);
        bind[0]=0;bind[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);CHECK(MEM32(header)==0x010c0000);
        bind[0]=2;call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
        CHECK(!vm_find(header,24) && !vm_find(data,1));
    }
    uint32_t length=96,header=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length);
    uint32_t data=graphics_vertex(header)->data;
    for(unsigned i=0;i<length;++i)memory[data+i]=(unsigned char)(i*19);
    uint32_t stream[]={15,header,24};call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    CHECK(MEM32(header)==0x01080001 && MEM32(PILOT_D3D_STREAMS+15*12)==24 && MEM32(PILOT_D3D_STREAMS+15*12+8)==header);
    /* Binding retains guest ownership; private GPU bytes are observed only
     * when consumed. The actual draw/pixel test above covers that boundary. */
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==0 && MEM32(header)==0x01080000);
    memory[data+8]^=0x55;stream[2]=0;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    CHECK(MEM32(header+8)==0x12345678 && MEM32(PILOT_D3D_STREAMS+15*12)==0);
    stream[0]=3;stream[2]=16;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);CHECK(MEM32(header)==0x01100000);
    CHECK(call_graphics(PILOT_D3D_D3DResource_AddRef,&header)==1);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==0 && MEM32(header)==0x01100000);
    stream[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);CHECK(MEM32(header)==0x01080000);
    stream[0]=15;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);CHECK(!vm_find(data,96) && !vm_find(header,12));
    uint32_t allocation=physical_allocate(4096,0,0xffffffffu,0,0x404);
    header=0x19600;MEM32(header)=1;MEM32(header+4)=0;MEM32(header+8)=0;
    uint32_t registration[]={header,allocation};call_graphics(PILOT_D3D_D3DResource_Register,registration);
    stream[0]=7;stream[1]=header;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==0 && MEM32(header)==0x80000);
    CHECK(physical_find(allocation,4096)->borrowers==1);
    stream[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    CHECK(graphics_vertex_find(header)<0 && MEM32(header)==0);
    CHECK(physical_find(allocation,4096)->borrowers==0);physical_free(allocation);
    /* Replacing an internal-only resource must collect the old object after
     * the GPU binding has moved, with no growth over repeated replacements. */
    uint32_t previous_texture=0,previous_vertex=0;
    for(unsigned i=0;i<12;++i) {
        uint32_t create[]={4,4,1,1,0,6,3};header=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create);
        uint32_t bind[]={0,header};call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==0);
        if(previous_texture)CHECK(!vm_find(previous_texture,24));previous_texture=header;
        header=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length);
        stream[0]=15;stream[1]=header;stream[2]=16;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==0);
        if(previous_vertex)CHECK(!vm_find(previous_vertex,12));previous_vertex=header;
    }
    uint32_t empty[]={0,0};call_graphics(PILOT_D3D_D3DDevice_SetTexture,empty);
    stream[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    MEM32(fence)=old_fence;CHECK(vm_committed(0,0x100000000ull)==resident);
    /* Leave internal-only resources for device teardown to release. */
    uint32_t texture_args[]={4,4,1,1,0,6,3};header=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,texture_args);
    uint32_t bind[]={3,header};call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==0);
    header=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length);
    stream[0]=12;stream[1]=header;stream[2]=16;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==0);
    fprintf(stderr,"PASS: texture/16-stream binding, real uploads, GPU-target contents, independent binding references, caller-owned storage and teardown\n");
}
