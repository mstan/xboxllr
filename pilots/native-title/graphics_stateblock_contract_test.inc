static void test_graphics_blocks(void)
{
#if PILOT_D3D_STATEBLOCK_AVAILABLE
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t output=0x19984,args[]={1,output};
    /* Empty bindings remain meaningful records, rather than discarded work. */
    MEM32(output-4)=MEM32(output+4)=0xaabbccdd;
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreateStateBlock,args)==0);
    uint32_t token=MEM32(output),expected[]={4,1,1,1,16,0,96,108,128,10,1,1,1,0,0};
    CHECK(token && !memcmp(memory+token,expected,sizeof(expected)));
    CHECK(MEM32(output-4)==0xaabbccdd && MEM32(output+4)==0xaabbccdd);
    uint32_t old=MEM32(PILOT_D3D_RENDER_STATES+57*4);
    MEM32(PILOT_D3D_RENDER_STATES+57*4)=0x207;
    unsigned rs=15+8+1+1+2+16*3+96*5;
    CHECK(MEM32(token+rs*4)==57 && MEM32(token+(rs+1)*4)==old);
    MEM32(PILOT_D3D_RENDER_STATES+57*4)=old;
    graphics_block_release(0);CHECK(!vm_find(token,4));
    CHECK(vm_committed(0,0x100000000ull)==resident);

    /* Exercise all membership types with real allocated resources. Fixture
     * bindings use the guest cache as input; this is not a claim of GPU draw
     * shader/stream binding, which is a separate, guarded family. */
    uint32_t texture_args[]={4,4,1,1,0,6,3},length=64;
    uint32_t texture=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,texture_args);
    uint32_t vertex=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length);
    uint32_t pixel=graphics_pixels[0],shader=graphics_shaders[0].header;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES)=texture;
    MEM32(PILOT_D3D_STREAMS)=16;MEM32(PILOT_D3D_STREAMS+8)=vertex;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER)=pixel;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER)=shader|1;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_OBJECT)=shader;
    for(unsigned i=0;i<16*4;++i)MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_CONSTANTS+i*4)=0x3f000000+i;
    for(unsigned i=0;i<192*4;++i)MEM32(graphics_vertex_constants_address()+i*4)=0x3e000000+i;
    uint64_t populated=vm_committed(0,0x100000000ull);
    for(unsigned type=1;type<=3;++type) {
        args[0]=type;CHECK(call_graphics(PILOT_D3D_D3DDevice_CreateStateBlock,args)==0);token=MEM32(output);
        CHECK((MEM32(texture)&0xffff)==(type==1?2:1) && (MEM32(vertex)&0xffff)==(type==1?2:1));
        CHECK(MEM32(pixel)==(type==3?1:2) && MEM32(shader)==(type==2?1:2));
        if(type==1){CHECK(MEM32(token+5*4)==16 && MEM32(token+7*4)==166);}
        else if(type==2){CHECK(MEM32(token)==0 && MEM32(token+4)==1 && MEM32(token+8)==0 && MEM32(token+24)==0 && MEM32(token+28)==32 && MEM32(token+32)==27*4);}
        else {CHECK(MEM32(token)==0 && MEM32(token+4)==0 && MEM32(token+8)==1 && MEM32(token+24)==96 && MEM32(token+28)==33 && MEM32(token+32)==2*4);}
        graphics_block_release(0);CHECK(MEM32(pixel)==1 && MEM32(shader)==1);
        CHECK(vm_committed(0,0x100000000ull)==populated);
    }
    uint64_t service=vm_service_bytes;vm_service_bytes=64u*1024*1024;MEM32(output)=0xdeadbeef;
    args[0]=1;CHECK(call_graphics(PILOT_D3D_D3DDevice_CreateStateBlock,args)==0x8007000eu);
    vm_service_bytes=service;CHECK(!MEM32(output) && MEM32(pixel)==1 && MEM32(shader)==1);
    CHECK((MEM32(texture)&0xffff)==1 && (MEM32(vertex)&0xffff)==1);

    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreateStateBlock,args)==0);
    /* Creator release while a snapshot owns the resource cannot destroy it. */
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&texture)==1);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&vertex)==1);
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES)=0;
    MEM32(PILOT_D3D_STREAMS)=MEM32(PILOT_D3D_STREAMS+8)=0;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER)=0;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER)=2;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_OBJECT)=0;
    graphics_block_release(0);
    CHECK(!vm_find(texture,4) && !vm_find(vertex,4));
    CHECK(vm_committed(0,0x100000000ull)==resident);
    /* Leave one real block for outer device release/recreation to reclaim. */
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreateStateBlock,args)==0);
    fprintf(stderr,"PASS: actual state snapshots, membership, independent bytes, owned resources, OOM and device cleanup\n");
#else
    fprintf(stderr,"SKIP: state-block snapshot fixture is unavailable in the selected graphics profile\n");
#endif
}
