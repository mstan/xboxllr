/* Actual original contiguous allocation and source-bound D3D calls retain
 * their common backing until the last native buffer/stream owner is gone. */
#define THREAD_GRAPHICS_RESOURCE_CPU 0x16800u
#define THREAD_GRAPHICS_RESOURCE_HELPER 0x16d80u
#define GRAPHICS_RESOURCE_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static unsigned graphics_resource_calls;
static uint64_t graphics_resource_tokens[2];
static const uint32_t graphics_resource_headers[]={0x1c000,0x1c010};
static const char *test_graphics_resource_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==THREAD_GRAPHICS_RESOURCE_HELPER && pilot_io_device_thread_component_active());
    uint32_t phase=regs[1],context=regs[2],base=MEM32(context),extent=MEM32(context+4);
    CHECK(context==0x1b000 && phase<=3 && extent==8192);
    CHECK(regs[3]==0x11223344 && regs[5]==0x44556677 && regs[6]==0x22334455 && regs[7]==context);
    ++graphics_resource_calls;graphics_original_enter();AcquireSRWLockExclusive(&graphics_lock);
    CHECK(!physical_find(base,1));
    if(!phase) {
        CHECK(MEM32(context+80)==base && MEM32(context+84)==base+4096);
        unsigned char expected[8192]={0},readback[8192];
        uint32_t first=0x13572468,second=0xa5b6c7d8,last=0x24681357;
        memcpy(expected,&first,4);memcpy(expected+4096,&second,4);memcpy(expected+8192-4,&last,4);
        for(unsigned i=0;i<2;++i) {
            struct GraphicsVertexObject *vertex=graphics_vertex(graphics_resource_headers[i]);
            CHECK(vertex->data==base+i*4096 && vertex->bytes==8192-i*4096 && vertex->refs==1 && !vertex->bindings);
            CHECK(vertex->external && vertex->original_reference);
            graphics_resource_tokens[i]=vertex->original_reference;
            CHECK(MEM32(vertex->header+4)==((base+i*4096)&0x0fffffffu));
            CHECK(!memcmp(memory+vertex->data,expected+i*4096,vertex->bytes));
            pilot_gfx_vertex_upload(vertex->native,memory+vertex->data,vertex->bytes);
            CHECK(!pilot_gfx_vertex_readback(vertex->native,readback,sizeof(readback)));
            CHECK(!memcmp(readback,expected+i*4096,vertex->bytes));
        }
        CHECK(graphics_resource_tokens[0]!=graphics_resource_tokens[1]);
    } else if(phase==1) {
        CHECK(MEM32(context+88)==2 && MEM32(context+92)==1 && !MEM32(context+104) && !MEM32(context+108));
        struct GraphicsVertexObject *vertex=graphics_vertex(graphics_resource_headers[0]);
        CHECK(!vertex->refs && vertex->bindings==1 && vertex->original_reference==graphics_resource_tokens[0]);
        CHECK(graphics_vertex_find(graphics_resource_headers[1])<0);
        CHECK(!pilot_io_device_contiguous_resource_release(graphics_resource_tokens[1]));
        CHECK(MEM32(PILOT_D3D_STREAMS+8)==graphics_resource_headers[0]);
    } else if(phase==2) {
        CHECK(!MEM32(PILOT_D3D_STREAMS+8));
        for(unsigned i=0;i<2;++i) {
            CHECK(graphics_vertex_find(graphics_resource_headers[i])<0);
            CHECK(!pilot_io_device_contiguous_resource_release(graphics_resource_tokens[i]));
        }
        uint32_t available=0;
        CHECK(pilot_io_device_contiguous_resource_query(base,8192,&available) && available==8192);
        CHECK(MEM32(base)==0x13572468 && MEM32(base+8192-4)==0x24681357);
    } else {
        uint32_t available=0xdeadbeef;MEMORY_BASIC_INFORMATION host;
        CHECK(!pilot_io_device_contiguous_resource_query(base,1,&available) && available==0xdeadbeef);
        CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_RESERVE);
    }
    ReleaseSRWLockExclusive(&graphics_lock);graphics_original_leave();regs[4]+=4;return NULL;
}
static void graphics_resource_call(struct ThreadChildCode *c,unsigned entry)
{
    GRAPHICS_RESOURCE_BYTES(c,0xb8);child_word(c,pilot_d3d_calls[entry].address);
    GRAPHICS_RESOURCE_BYTES(c,0xff,0xd0);
}
static void graphics_resource_check(struct ThreadChildCode *c,unsigned phase)
{
    GRAPHICS_RESOURCE_BYTES(c,0xb9);child_word(c,phase);
    GRAPHICS_RESOURCE_BYTES(c,0x89,0xfa,0xb8);child_word(c,THREAD_GRAPHICS_RESOURCE_HELPER);
    GRAPHICS_RESOURCE_BYTES(c,0xff,0xd0);
}
static void test_thread_graphics_resource_lle(const char *path,int borrowed_free)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    vm_ensure();uint32_t page=PILOT_D3D_DEVICE_GLOBAL&~4095u;
    uint32_t bytes=((PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET+4+4095)&~4095u)-page;
    commit(page,bytes);vm_fixed_region(page,bytes);test_graphics_component_sources(path);
    const uint32_t parameters[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};
    memcpy(memory+0x1a000,parameters,sizeof(parameters));
    uint32_t create[]={0,1,0,0x40,0x1a000,0x1a080};
    CHECK(!call_graphics(PILOT_D3D_Direct3D_CreateDevice,create) && graphics_active);
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};char identity[65];
    memcpy(memory+THREAD_GRAPHICS_RESOURCE_HELPER,helper,sizeof(helper));
    test_component_hash(helper,sizeof(helper),identity);
    CHECK(!pilot_io_device_thread_component_bind_abi(THREAD_GRAPHICS_RESOURCE_HELPER,sizeof(helper),identity,0,test_graphics_resource_body));
    struct ThreadChildCode code={0},*c=&code;
    GRAPHICS_RESOURCE_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68);
    child_push_field(c,100);child_call(c,165);GRAPHICS_RESOURCE_BYTES(c,0x89,0x07);
    child_push_field(c,0);child_call(c,180);GRAPHICS_RESOURCE_BYTES(c,0x89,0x47,4);
    /* Authored CPU initializes and consumes both actual published pages. */
    GRAPHICS_RESOURCE_BYTES(c,0x8b,0x07,0x57,0x89,0xc7,0x31,0xc0,0xb9,0,8,0,0,0xf3,0xab,0x5f,
        0x8b,0x07,0xc7,0x00,0x68,0x24,0x57,0x13,
        0xc7,0x80,0,0x10,0,0,0xd8,0xc7,0xb6,0xa5,
        0xc7,0x80,0xfc,0x1f,0,0,0x57,0x13,0x68,0x24,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8);
    for(unsigned i=0;i<2;++i) {
        child_push_field(c,0);child_push(c,graphics_resource_headers[i]);graphics_resource_call(c,PILOT_D3D_D3DResource_Register);
        child_push(c,0);child_push(c,graphics_resource_headers[i]);graphics_resource_call(c,PILOT_D3D_D3DVertexBuffer_Lock2);
        GRAPHICS_RESOURCE_BYTES(c,0x89,0x47,(uint8_t)(80+i*4));
    }
    graphics_resource_check(c,0);
    if(borrowed_free) {
        child_push_field(c,0);child_call(c,171);GRAPHICS_RESOURCE_BYTES(c,0xcc);
    } else {
        child_push(c,4);child_push(c,graphics_resource_headers[0]);child_push(c,0);
        graphics_resource_call(c,PILOT_D3D_D3DDevice_SetStreamSource);
        child_push(c,graphics_resource_headers[0]);graphics_resource_call(c,PILOT_D3D_D3DResource_AddRef);
        GRAPHICS_RESOURCE_BYTES(c,0x89,0x47,88);
        child_push(c,graphics_resource_headers[0]);graphics_resource_call(c,PILOT_D3D_D3DResource_Release);
        GRAPHICS_RESOURCE_BYTES(c,0x89,0x47,92);
        child_push(c,graphics_resource_headers[0]);graphics_resource_call(c,PILOT_D3D_D3DResource_Release);
        GRAPHICS_RESOURCE_BYTES(c,0x89,0x47,104);
        child_push(c,graphics_resource_headers[1]);graphics_resource_call(c,PILOT_D3D_D3DResource_Release);
        GRAPHICS_RESOURCE_BYTES(c,0x89,0x47,108);graphics_resource_check(c,1);
        child_push(c,0);child_push(c,0);child_push(c,0);graphics_resource_call(c,PILOT_D3D_D3DDevice_SetStreamSource);
        graphics_resource_check(c,2);GRAPHICS_RESOURCE_BYTES(c,0x9c,0x5a,0x89,0x57,12);
        child_push_field(c,0);child_call(c,171);graphics_resource_check(c,3);
        GRAPHICS_RESOURCE_BYTES(c,0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
            0x89,0x5f,112,0x89,0x6f,116,0x89,0x77,120,
            0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    }
    CHECK(c->size<THREAD_GRAPHICS_RESOURCE_HELPER-THREAD_GRAPHICS_RESOURCE_CPU);
    memcpy(memory+THREAD_GRAPHICS_RESOURCE_CPU,c->bytes,c->size);
    CHECK(!pilot_io_device_resident(0x10000,0x10000));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    uint32_t before_memory[9],after_memory[9];test_memory_snapshot(before_memory);
    for(unsigned life=0;life<(borrowed_free?1u:3u);++life) {
        uint32_t context=0x1b000;memset(memory+context,0,128);MEM32(context+64)=THREAD_GRAPHICS_RESOURCE_CPU;
        MEM32(context+96)=0xa5a55a5a;MEM32(context+100)=8192;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7);
        double expected=12345.6789;memcpy(memory+context+48,&expected,8);
        for(unsigned i=0;i<2;++i) {MEM32(graphics_resource_headers[i])=1;MEM32(graphics_resource_headers[i]+4)=i*4096;MEM32(graphics_resource_headers[i]+8)=0;}
        if(borrowed_free) {
            uint32_t out=0x19800,args[]={out,0,THREAD_GRAPHICS_RESOURCE_CPU,context,0};CHECK(!event_lle_call(254,args));
            uint32_t handle=MEM32(out),ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
            uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
            uint32_t join[]={object,0,0,0,0},result=0;const char *error=pilot_io_device_lle(159,memory,join,&result);
            CHECK(error && !strcmp(error,"device LLE contiguous release while resource borrowed"));
            CHECK(!MEM32(context+76) && graphics_resource_calls==1 && MEM32(context+96)==0xa5a55a5a);
            MEMORY_BASIC_INFORMATION host;CHECK(VirtualQuery(memory+MEM32(context),&host,sizeof(host)) && host.State==MEM_COMMIT);
            for(unsigned i=0;i<2;++i)CHECK(graphics_vertex(graphics_resource_headers[i])->original_reference==graphics_resource_tokens[i]);
            fprintf(stderr,"PASS: original171 rejects free while two actual native graphics resources retain the backing; objects/bytes/canary preserved\n");return;
        }
        thread_cpu_create_join(THREAD_GRAPHICS_RESOURCE_CPU,context,life&1);
        CHECK(MEM32(context+76)==1 && MEM32(context+96)==0xa5a55a5a && MEM32(context+8)==MEM32(context+12));
        CHECK(MEM32(context+68)==MEM32(context+72) && MEM32(context+112)==0x11223344 && MEM32(context+116)==0x44556677 && MEM32(context+120)==0x22334455);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
        test_memory_snapshot(after_memory);CHECK(!memcmp(before_memory,after_memory,sizeof(before_memory)));
    }
    CHECK(graphics_resource_calls==12 && !thread_cpu_calls);
    CHECK(!call_graphics(PILOT_D3D_D3DDevice_Release,NULL) && !graphics_active);
    uint32_t gate=native_component_original_gate;graphics_original_gate=native_component_original_gate=0;pool_free(gate);
    fprintf(stderr,"PASS: three raw original CPU/native GPU resource lives; actual165/180/register/lock/ref/stream/release/171, exact overlapping GPU bytes, retained binding and last-owner collection, stale tokens, GPR/ESP/flags/x87/XMM and original memory/thread/handle cleanup\n");
}
#undef GRAPHICS_RESOURCE_BYTES
