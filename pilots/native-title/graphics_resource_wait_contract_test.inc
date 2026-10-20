/* Real submitted GPU work plus public resource/ABI lifetime checks. A bound
 * resource remains publicly busy after its wait completes. */
static void test_graphics_resource_wait(void)
{
    unsigned entry=graphics_resource_wait_entry();
    if(entry>=PILOT_D3D_COUNT){puts("SKIP optional resource wait: no proved API entry");return;}
    uint32_t create[]={8,4,1,1,1,0x12,3},texture=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create);
    uint32_t level[]={texture,0},surface=call_graphics(PILOT_D3D_D3DTexture_GetSurfaceLevel2,level);
    unsigned slot=graphics_texture_slot(texture);uint32_t native=graphics_textures[slot].native;
    CHECK(!pilot_gfx_texture_clear(native,0xff2468ac));
    MEM32(texture+8)=0x12345678;MEM32(surface+8)=0xabcdef01;
    uint32_t before[6];memcpy(before,memory+texture,24);call_graphics(entry,&surface);
    CHECK(!memcmp(before,memory+texture,24) && MEM32(surface+8)==0xabcdef01);
    CHECK(!pilot_gfx_resource_busy(PILOT_GFX_TEXTURE,native) && !graphics_resource_waiters);
    uint32_t bind[]={2,texture};call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
    CHECK(!pilot_gfx_texture_clear(native,0xff13579b));memcpy(before,memory+texture,24);
    call_graphics(entry,&surface);CHECK(!memcmp(before,memory+texture,24));
    CHECK(graphics_textures[slot].bindings==1 && call_graphics(PILOT_D3D_D3DResource_IsBusy,&texture)==1);
    bind[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetTexture,bind);
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&surface));
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&texture));
    uint32_t length=128,vertex=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length),stream[]={4,vertex,16};
    call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);uint32_t common=MEM32(vertex);
    MEM32(vertex+8)=0x76543210;call_graphics(entry,&vertex);
    CHECK(MEM32(vertex)==common && MEM32(vertex+8)==0x76543210 && graphics_vertex(vertex)->bindings==1);
    stream[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,stream);
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&vertex));
#ifdef PILOT_D3D_INDEX_DATA_GLOBAL
    length=6;uint32_t index=call_graphics(PILOT_D3D_D3DDevice_CreateIndexBuffer2,&length),indices[]={index,0};
    call_graphics(PILOT_D3D_D3DDevice_SetIndices,indices);common=MEM32(index);
    call_graphics(entry,&index);CHECK(MEM32(index)==common && graphics_index(index)->bindings==1);
    indices[0]=0;call_graphics(PILOT_D3D_D3DDevice_SetIndices,indices);
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&index));
#endif
    uint32_t back=graphics_headers;common=MEM32(back);CHECK(!pilot_gfx_clear(1,0xff123456,1,0));
    call_graphics(entry,&back);CHECK(MEM32(back)==common && !pilot_gfx_resource_busy(PILOT_GFX_SURFACE,0));
    /* Exercise the production frame bridge used by the proved ECX adapter.
     * This fixture has no generated CPU dispatcher. Actual title execution
     * separately exercises that generated wrapper and its original RET0. */
    g_esp=0x18000;MEM32(g_esp)=0x12340;g_ecx=back;g_ebx=0x12345678;g_esi=0x23456789;g_edi=0x3456789a;g_ebp=0x456789ab;
    const uint32_t args[]={g_ecx};pilot_d3d_dispatch_frame(entry,args,1,0);
    CHECK(g_esp==0x18004 && g_ebx==0x12345678 && g_esi==0x23456789 && g_edi==0x3456789a && g_ebp==0x456789ab);
    /* A wait's private lease retains storage without changing the last
     * public Release result. Split the wait at its real interleaving seam. */
    texture=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create);slot=graphics_texture_slot(texture);
    uint32_t data=graphics_textures[slot].data;CHECK(!pilot_gfx_texture_clear(graphics_textures[slot].native,0xff987654));
    struct GraphicsResourceWait lease;
    AcquireSRWLockExclusive(&graphics_lock);uint64_t serial=graphics_resource_wait_retain(texture,&lease);ReleaseSRWLockExclusive(&graphics_lock);
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&texture) && test_pool_owned(texture) && physical_find(data,1));
    CHECK(!MEM32(texture) || !(MEM32(texture)&0xffff));
    AcquireSRWLockExclusive(&graphics_lock);graphics_completion_wait(serial);graphics_resource_wait_release(&lease);ReleaseSRWLockExclusive(&graphics_lock);
    CHECK(!test_pool_owned(texture) && !physical_find(data,1) && !graphics_resource_waiters);
    puts("PASS resource wait: real GPU completion, bound refs remain, parent/surface identity and fence words, vertex/index/presentation storage, register frame/RET bridge and private last-release lifetime");
}
