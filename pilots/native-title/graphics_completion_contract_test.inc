static void graphics_test_wait_idle(uint32_t header)
{
    ULONGLONG deadline=GetTickCount64()+5000;
    while(call_graphics(PILOT_D3D_D3DResource_IsBusy,&header)) {
        CHECK(GetTickCount64()<deadline);Sleep(1);
    }
}
static void test_graphics_completion(void)
{
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t length=128,vertex=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length);
    CHECK(!call_graphics(PILOT_D3D_D3DResource_IsBusy,&vertex));
    uint32_t stream[]={4,vertex,16};call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    MEM32(vertex+8)=0x12345678;
    CHECK(call_graphics(PILOT_D3D_D3DResource_IsBusy,&vertex)==1 && MEM32(vertex+8)==0x12345678);
    stream[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    graphics_test_wait_idle(vertex);CHECK(MEM32(vertex+8)==0);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&vertex)==0);
    uint32_t create[]={8,4,1,1,1,0x12,3};
    uint32_t texture=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create);
    uint32_t surface_args[]={texture,0},surface=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,surface_args);
    uint32_t native=graphics_textures[graphics_texture_slot(texture)].native;
    CHECK(pilot_gfx_texture_clear(native,0xff2468ac)==0);
    uint32_t bind[]={2,texture};call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
    MEM32(texture+8)=0x12345678;MEM32(surface+8)=0xabcdef01;
    CHECK(call_graphics(PILOT_D3D_D3DResource_IsBusy,&surface)==1);
    CHECK(MEM32(texture+8)==0x12345678 && MEM32(surface+8)==0xabcdef01);
    bind[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
    graphics_test_wait_idle(surface);
    CHECK(MEM32(texture+8)==0 && MEM32(surface+8)==0xabcdef01); /* canonical parent cache */
    uint32_t common=MEM32(surface);MEM32(surface)=common|0x80000;MEM32(texture+8)=0x87654321;
    CHECK(call_graphics(PILOT_D3D_D3DResource_IsBusy,&surface)==1 && MEM32(texture+8)==0x87654321);
    MEM32(surface)=common;graphics_test_wait_idle(surface);CHECK(!MEM32(texture+8));
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&surface)==0);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&texture)==0);
    uint32_t back=graphics_headers;CHECK(pilot_gfx_clear(1,0xffabcdef,1,0)==0);
    uint32_t old_fence=MEM32(back+8);MEM32(back+8)=0x11223344;
    CHECK(call_graphics(PILOT_D3D_D3DResource_IsBusy,&back)==1 && MEM32(back+8)==0x11223344);
    MEM32(back+8)=old_fence; /* Built-in swap surfaces retain an internal reference. */
    CHECK(vm_committed(0,0x100000000ull)==resident);
    fprintf(stderr,"PASS: resource completion ABI, binding busy, surface-parent identity, canonical fence clearing and independent public ownership\n");
}
