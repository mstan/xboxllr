static void test_graphics_shaders(void)
{
    uint32_t declaration[]={0x12345,0x20000000,0x40420000,0x82000064,0x3f800000,0x40000000,0x40400000,0x40800000,0xffffffffu};
    uint32_t program[]={0x00012078,0,0x0020001b,0x08000000,0x0000f801};
    memcpy(memory+0x19800,declaration,sizeof(declaration));memcpy(memory+0x19840,program,sizeof(program));
    uint32_t args[]={0x19800,0x19840,0x198c4,0};
    MEM32(0x198c0)=MEM32(0x198c4)=MEM32(0x198c8)=0xcdcdcdcdu;
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreateVertexShader,args)==0);
    uint32_t handle=MEM32(0x198c4),base=handle&~1u;
    CHECK((handle&1) && base && MEM32(base)==1 && MEM32(base+4)==0x10 && MEM32(base+8)==1);
    CHECK(MEM32(0x198c0)==0xcdcdcdcdu && MEM32(0x198c8)==0xcdcdcdcdu);
    CHECK(MEM32(base+0x14)==0 && MEM32(base+0x18)==0 && MEM32(base+0x1c)==0x42);
    CHECK(graphics_shaders[0].header==base && graphics_shaders[0].constant_mask[3]==16);
    CHECK(!memcmp(graphics_shaders[0].constants[100],declaration+4,16));
    float vertices[2][4]={{.25f,-.5f,.75f,1},{-.125f,.25f,.5f,1}},constants[192][4]={0},outputs[2][13][4];
    struct PilotVertexProbeStream streams[16]={0};streams[0]=(struct PilotVertexProbeStream){vertices,16};
    CHECK(pilot_gfx_shader_probe(graphics_shaders[0].native,streams,2,constants,&outputs[0][0][0])==0);
    CHECK(!memcmp(vertices[0],outputs[0][0],16) && !memcmp(vertices[1],outputs[1][0],16));
    uint64_t resident=vm_committed(0,0x100000000ull),service=vm_service_bytes;
    vm_service_bytes=64u*1024*1024;MEM32(0x198c4)=0xdeadbeefu;
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreateVertexShader,args)==0x8007000eu);
    vm_service_bytes=service;
    CHECK(MEM32(0x198c4)==0xdeadbeefu && graphics_shaders[0].header==base && !graphics_shaders[1].header);
    CHECK(vm_committed(0,0x100000000ull)==resident);
    fprintf(stderr,"PASS: shader stdcall/HRESULT/odd guest handle, declaration constants, real GPU output and OOM output/ownership preservation\n");
    /* The outer fixture releases/recreates the device and checks that all
     * header allocations return to baseline, exercising shader destruction. */
}

static void test_graphics_pixels(void)
{
    uint32_t definition[60];
    for(unsigned i=0;i<60;++i)definition[i]=0x10203040u+i;
    memcpy(memory+0x19800,definition,sizeof(definition));
    MEM32(0x19900)=MEM32(0x19904)=MEM32(0x19908)=0xcdcdcdcdu;
    uint32_t args[]={0x19800,0x19904};
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreatePixelShader,args)==0);
    uint32_t handle=MEM32(0x19904);
    CHECK(handle && !(handle&3) && MEM32(handle)==1 && MEM32(handle+4)==1 && MEM32(handle+8)==handle+12);
    CHECK(MEM32(0x19900)==0xcdcdcdcdu && MEM32(0x19908)==0xcdcdcdcdu);
    memset(memory+0x19800,0xcc,sizeof(definition));
    CHECK(!memcmp(memory+MEM32(handle+8),definition,sizeof(definition)));
    uint64_t service=vm_service_bytes,resident=vm_committed(0,0x100000000ull);
    vm_service_bytes=64u*1024*1024;MEM32(0x19904)=0xdeadbeefu;
    CHECK(call_graphics(PILOT_D3D_D3DDevice_CreatePixelShader,args)==0x8007000eu);
    vm_service_bytes=service;
    CHECK(MEM32(0x19904)==0xdeadbeefu && graphics_pixels[0]==handle && !graphics_pixels[1]);
    CHECK(vm_committed(0,0x100000000ull)==resident);
    fprintf(stderr,"PASS: pixel definition stdcall/HRESULT, independent owned copy, guest descriptor and OOM output preservation\n");
}
