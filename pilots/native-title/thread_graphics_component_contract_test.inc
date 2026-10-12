/* Real reviewed D3D entry bytes and existing whole HLE services, called from
 * an authored original CPU thread. Renderer creation is a fixture setup;
 * A separate constructor mode enters through the actual original CPU; reset,
 * release, callback and pulse ownership remain guarded. */
static void test_graphics_component_sources(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));
    long length=ftell(file);CHECK(length>0x178 && length<64*1024*1024);rewind(file);
    unsigned char *blob=malloc((size_t)length);CHECK(blob);
    CHECK(fread(blob,1,(size_t)length,file)==(size_t)length);fclose(file);
    char identity[65];test_component_hash(blob,(uint32_t)length,identity);CHECK(!strcmp(identity,PILOT_XBE_SHA256));
    uint32_t base,sections,table;memcpy(&base,blob+0x104,4);memcpy(&sections,blob+0x11c,4);memcpy(&table,blob+0x120,4);
    CHECK(table>=base && (uint64_t)table-base+sections*56<=(uint64_t)length);
    for(unsigned index=0;index<PILOT_D3D_COUNT;++index) {
        uint32_t address=pilot_d3d_calls[index].address,bytes=pilot_d3d_calls[index].bytes;int found=0;
        for(unsigned section=0;section<sections;++section) {
            uint32_t words[5];memcpy(words,blob+table-base+section*56,sizeof(words));
            if(address<words[1] || (uint64_t)address+bytes>(uint64_t)words[1]+words[4])continue;
            CHECK((uint64_t)words[3]+words[4]<=(uint64_t)length);
            commit(address,bytes);memcpy(memory+address,blob+words[3]+address-words[1],bytes);found=1;break;
        }
        CHECK(found);
    }
    free(blob);native_graphics_components_bind();
}
static void test_graphics_cpu_emit(uint32_t *cursor,uint8_t byte)
{MEM8((*cursor)++)=byte;}
static void test_graphics_cpu_word(uint32_t *cursor,uint32_t word)
{memcpy(memory+*cursor,&word,4);*cursor+=4;}
static void test_graphics_cpu_call(uint32_t *cursor,unsigned entry)
{
    test_graphics_cpu_emit(cursor,0xb8);test_graphics_cpu_word(cursor,pilot_d3d_calls[entry].address);
    test_graphics_cpu_emit(cursor,0xff);test_graphics_cpu_emit(cursor,0xd0);
}
static void test_thread_graphics_component_lle(const char *path,int failure_case)
{
    int constructor=failure_case==4;
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    vm_ensure();uint32_t page=PILOT_D3D_DEVICE_GLOBAL&~4095u;
    uint32_t bytes=((PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET+4+4095)&~4095u)-page;
    commit(page,bytes);vm_fixed_region(page,bytes);
    test_graphics_component_sources(path);
    CHECK(pilot_d3d_calls[PILOT_D3D_D3DDevice_GetPixelShader].stack_bytes==4);
    const uint32_t parameters[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};
    memcpy(memory+0x1a000,parameters,sizeof(parameters));
    if(failure_case!=1 && !constructor) {
        uint32_t create[]={0,1,0,0x40,0x1a000,0x1a080};
        CHECK(call_graphics(PILOT_D3D_Direct3D_CreateDevice,create)==0);
        CHECK(graphics_active && MEM32(0x1a080)==PILOT_D3D_DEVICE_OBJECT);
    }
    const uint8_t prefix[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8};
    uint32_t cursor=THREAD_COMPONENT_ABI;memcpy(memory+cursor,prefix,sizeof(prefix));cursor+=sizeof(prefix);
    if(failure_case && !constructor) {
        unsigned entry=failure_case==1?PILOT_D3D_D3DDevice_Reset:
            failure_case==2?PILOT_D3D_D3DDevice_InsertCallback:
            failure_case==5?PILOT_D3D_D3DDevice_BlockUntilVerticalBlank:PILOT_D3D_D3DDevice_Swap;
        for(unsigned word=0;word<pilot_d3d_calls[entry].stack_bytes/4;++word) {
            test_graphics_cpu_emit(&cursor,0x6a);test_graphics_cpu_emit(&cursor,0);
        }
        test_graphics_cpu_call(&cursor,entry);
    } else {
        if(constructor) {
            const uint32_t create[]={0,1,0,0x40,0x1a000,0x1a080};
            for(unsigned i=6;i;--i) {
                test_graphics_cpu_emit(&cursor,0x68);test_graphics_cpu_word(&cursor,create[i-1]);
            }
            test_graphics_cpu_call(&cursor,PILOT_D3D_Direct3D_CreateDevice);
        }
        const uint8_t caps_arg[]={0x8d,0x47,96,0x50};memcpy(memory+cursor,caps_arg,sizeof(caps_arg));cursor+=sizeof(caps_arg);
        test_graphics_cpu_call(&cursor,PILOT_D3D_D3DDevice_GetDeviceCaps);
        const uint8_t query_arg[]={0x8d,0x47,80,0x50};memcpy(memory+cursor,query_arg,sizeof(query_arg));cursor+=sizeof(query_arg);
        test_graphics_cpu_call(&cursor,PILOT_D3D_D3DDevice_GetPixelShader);
    }
    const uint8_t suffix[]={0x9c,0x5a,0x89,0x57,12,0x89,0x67,72,
        0x0f,0x11,0x47,32,0xdd,0x5f,56,0xc7,0x47,76,1,0,0,0,
        0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+cursor,suffix,sizeof(suffix));cursor+=sizeof(suffix);
    CHECK(cursor<THREAD_COMPONENT_REGS);
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned life=0;life<(failure_case && !constructor?1u:3u);++life) {
        uint32_t context=0x1b000;memset(memory+context,0,512);MEM32(context+64)=THREAD_COMPONENT_ABI;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7);
        double expected=12345.6789;memcpy(memory+context+48,&expected,8);MEM32(context+80)=0xdeadbeef;
        if(failure_case && !constructor) {
            uint32_t out=0x19800,create[]={out,0,THREAD_COMPONENT_ABI,context,0};CHECK(!event_lle_call(254,create));
            uint32_t handle=MEM32(out),ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
            uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
            uint32_t join[]={object,0,0,0,0},result=0;const char *error=pilot_io_device_lle(159,memory,join,&result);
            CHECK(error && !strcmp(error,failure_case==1?"original scheduling native graphics teardown requires bound clock":
                failure_case==2?"original scheduling native graphics callback ownership requires integration":
                failure_case==5?"original scheduling native graphics wait ownership requires integration":
                "original scheduling native graphics display pulse requires integration"));
            CHECK(!MEM32(context+76) && !graphics_gpu_head && !graphics_gpu_running && !graphics_callback);
            CHECK(graphics_active==(failure_case!=1) && MEM32(PILOT_D3D_DEVICE_GLOBAL)==(failure_case==1?0u:PILOT_D3D_DEVICE_OBJECT));
            fprintf(stderr,"PASS: original CPU graphics prerequisite guard before native mutation: %s\n",error);return;
        }
        thread_cpu_create_join(THREAD_COMPONENT_ABI,context,life&1);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72) && MEM32(context+76)==1);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(!memcmp(memory+context+96,pilot_d3d_device_caps,sizeof(pilot_d3d_device_caps)));
        CHECK(MEM32(context+80)==0);
        CHECK(graphics_active && MEM32(PILOT_D3D_DEVICE_GLOBAL)==PILOT_D3D_DEVICE_OBJECT);
        CHECK(MEM32(graphics_original_gate+16)==UINT32_MAX && !MEM32(graphics_original_gate+20) &&
              !MEM32(graphics_original_gate+24));
        if(constructor) {
            CHECK(MEM32(0x1a080)==PILOT_D3D_DEVICE_OBJECT);
            uint32_t words[7];CHECK(!vm_query(graphics_headers,words));
            CHECK(words[0]==graphics_headers && words[4]==MEM_COMMIT && words[5]==PAGE_READWRITE);
            for(unsigned i=0;i<3;++i) {
                CHECK(!vm_query(graphics_data[i],words));CHECK(words[4]==MEM_RESERVE);
            }
            CHECK(call_graphics(PILOT_D3D_D3DDevice_Release,NULL)==0 && !graphics_active);
        }
        CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    CHECK(!graphics_gpu_head && !graphics_gpu_running && !graphics_callback);
    if(!constructor)CHECK(call_graphics(PILOT_D3D_D3DDevice_Release,NULL)==0 && !graphics_active);
    uint32_t gate=graphics_original_gate;graphics_original_gate=0;pool_free(gate);
    fprintf(stderr,"PASS: 129 actual-byte D3D bindings; three original CPU %s lifetimes, real caps/pixel outputs, RET4/ESP/flags/x87/XMM and original thread/handle baselines\n",
        constructor?"constructor, original VM and gate":"query");
}
