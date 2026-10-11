/* Whole original loader lifetimes use the actual installed XBE and descriptor
 * bytes. Native section_change is never called in this fixture. */
#define SECTION_CPU 0x16800u
#define SECTION_PAIR_CPU 0x16a00u
#define SECTION_HELPER 0x16f00u
#define SECTION_CONTEXT 0x1b000u
static void test_memory_snapshot(uint32_t out[9]);
static uint32_t section_test_descriptor,section_test_head,section_test_tail;
static unsigned section_test_checks;
static int section_test_pinned;
static const char *section_test_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==SECTION_HELPER && regs[1]<10 && regs[7]==SECTION_CONTEXT && !regs[0]);
    CHECK(regs[3]==0x11223344 && regs[5]==0x44556677 && regs[6]==0x22334455);
    unsigned phase=regs[1];uint32_t d=section_test_descriptor;
    if(phase>=6) {
        uint32_t a=section_table+38*56,b=section_table+39*56,shared=MEM32(a+32);
        CHECK(shared==MEM32(b+28));
        CHECK(MEM32(a+24)==(phase<8) && MEM32(b+24)==(phase==7 || phase==8));
        CHECK(MEM16(shared)==(phase==7?2:phase==9?0:1));
        if(phase<8)CHECK(!memcmp(memory+MEM32(a+4),image_bytes+MEM32(a+12),MEM32(a+16)));
        if(phase==7 || phase==8)CHECK(!memcmp(memory+MEM32(b+4),image_bytes+MEM32(b+12),MEM32(b+16)));
        MEMORY_BASIC_INFORMATION host;
        CHECK(VirtualQuery(memory+(MEM32(a+4)&~4095u),&host,sizeof(host)) && host.State==(phase<8?MEM_COMMIT:MEM_RESERVE));
        CHECK(VirtualQuery(memory+(MEM32(b+4)&~4095u),&host,sizeof(host)) && host.State==(phase<9?MEM_COMMIT:MEM_RESERVE));
        if(phase==8) {
            uint32_t retained=MEM32(b+4)&~4095u,offset=retained-MEM32(a+4);
            CHECK(!memcmp(memory+retained,image_bytes+MEM32(a+12)+offset,MEM32(a+16)-offset));
        }
        ++section_test_checks;regs[4]+=4;return NULL;
    }
    uint32_t address=MEM32(d+4),size=MEM32(d+8),raw=MEM32(d+12),bytes=MEM32(d+16);
    int loaded=phase!=3 && phase!=5;
    CHECK(MEM32(d+24)==(phase==1?2:loaded?1:0));
    unsigned aliased=MEM32(d+28)==MEM32(d+32);
    CHECK(MEM16(MEM32(d+28))==section_test_head+(loaded?(aliased?2:1):0));
    CHECK(MEM16(MEM32(d+32))==section_test_tail+(loaded?(aliased?2:1):0));
    if(loaded) {
        CHECK(!memcmp(memory+address+(phase==1 || phase==2),image_bytes+raw+(phase==1 || phase==2),bytes-(phase==1 || phase==2)));
        for(uint32_t i=bytes;i<size;++i)CHECK(!MEM8(address+i));
        if(phase==1 || phase==2)CHECK(MEM8(address)==(uint8_t)(image_bytes[raw]^0x5a));
        MEMORY_BASIC_INFORMATION host;
        CHECK(VirtualQuery(memory+((address+4095)&~4095u),&host,sizeof(host)) && host.State==MEM_COMMIT);
        uint32_t query[]={((address+4095)&~4095u),SECTION_CONTEXT+400},result=0;
        const char *error=pilot_io_device_thread_component_call(217,query,&result);
        if(error)return error;CHECK(!result);
        DWORD actual=MEM32(SECTION_CONTEXT+420);
        CHECK(host.Protect==actual);
        CHECK(MEM32(d)&1?(actual==PAGE_READWRITE || actual==PAGE_EXECUTE_READWRITE):
            (actual==PAGE_READONLY || actual==PAGE_EXECUTE_READ));
        if(!phase) {
            /* A second reference must retain live bytes; a cold reload must
             * restore the actual file bytes, including retained shared pages. */
            if(MEM32(d)&1)MEM8(address)^=0x5a;
            else {
                DWORD previous;CHECK(VirtualProtect(memory+(address&~4095u),4096,PAGE_READWRITE,&previous));
                MEM8(address)^=0x5a;DWORD unused;CHECK(VirtualProtect(memory+(address&~4095u),4096,previous,&unused));
            }
            if(section_test_pinned) {
                uint32_t pin[]={address&~4095u,4096,0},result=0;
                const char *error=pilot_io_device_thread_component_call(175,pin,&result);
                if(error)return error;
            }
        }
    } else {
        uint32_t page=(address+4095)&~4095u;
        MEMORY_BASIC_INFORMATION host;
        CHECK(VirtualQuery(memory+page,&host,sizeof(host)) && host.State==MEM_RESERVE);
    }
    ++section_test_checks;regs[4]+=4;return NULL;
}
#define SECTION_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static void section_test_code(int pair)
{
    struct ThreadChildCode code={0};struct ThreadChildCode *c=&code;
    SECTION_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22);
    const unsigned ordinals[]={327,327,328,328,327,328};
    for(unsigned phase=0;phase<(pair?4u:6u);++phase) {
        if(pair)child_push(c,section_table+(phase&1?39:38)*56);else child_push_field(c,0);
        child_call(c,pair?(phase<2?327:328):ordinals[phase]);
        SECTION_BYTES(c,0xb9);child_word(c,phase+(pair?6:0));
        /* The independent observer receives the untouched NTSTATUS in EAX. */
        SECTION_BYTES(c,0xba);child_word(c,SECTION_HELPER);
        SECTION_BYTES(c,0xff,0xd2);
    }
    SECTION_BYTES(c,0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x5f,80,0x89,0x6f,84,0x89,0x77,88,0xc7,0x47,76,1,0,0,0,
        0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    uint32_t entry=pair?SECTION_PAIR_CPU:SECTION_CPU;
    CHECK(c->size<(pair?SECTION_HELPER:SECTION_PAIR_CPU)-entry);memcpy(memory+entry,c->bytes,c->size);
}
static void test_thread_sections_cpu_lle(const char *comparison)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    FILE *f=fopen(comparison,"rb");CHECK(f && !fseek(f,0,SEEK_END));long length=ftell(f);CHECK(length>0x2000);rewind(f);
    free(image_bytes);image_bytes=malloc(length);image_size=length;
    CHECK(image_bytes && fread(image_bytes,1,length,f)==(size_t)length);fclose(f);
    CHECK(file32(0x104)==0x10000);image_base=0x10000;header_bytes=file32(0x108);
    section_count=file32(0x11c);section_table=file32(0x120);
    commit(image_base,header_bytes);memcpy(memory+image_base,image_bytes,header_bytes);
    for(unsigned i=0;i<section_count;++i) {
        size_t o=section_table-image_base+i*56;
        if(!(file32(o)&2))continue;
        uint32_t address=file32(o+4),size=file32(o+8),raw=file32(o+12),bytes=file32(o+16);
        CHECK((uint64_t)raw+bytes<=image_size && bytes<=size);
        commit(address,size);memcpy(memory+address,image_bytes+raw,bytes);memset(memory+address+bytes,0,size-bytes);
    }
    sections_initialize();section_test_code(0);section_test_code(1);
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};memcpy(memory+SECTION_HELPER,helper,sizeof(helper));
    char sha[65];test_component_hash(helper,sizeof(helper),sha);
    CHECK(!pilot_io_device_resident(image_base,(file32(0x10c)+4095)&~4095u));
    /* This unit bootstrap intentionally keeps its authored .text scratch
     * writable. Independently establish the real preloaded DOLBY policy so
     * original327 must copy XTIMAGE through its already-readonly shared head. */
    uint32_t dolby_protect[]={0x31bf000u,0x6260u,0,PAGE_EXECUTE_READ,0},protected[7],protection_status=~0u;
    CHECK(!pilot_io_device_vm(204,memory,dolby_protect,protected,&protection_status) && !protection_status);
    CHECK(protected[0]==0x31bf000u && protected[1]==0x7000u);
    CHECK(!pilot_io_device_thread_component_bind_abi(SECTION_HELPER,sizeof(helper),sha,0,section_test_body));
    char path[1200];snprintf(path,sizeof(path),"%s\\default.xbe",PILOT_FILES_TITLE_ROOT);
    CHECK(!pilot_io_device_sections(memory,path));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    CHECK(!pilot_io_device_clock_start());
    const char *launch_error=pilot_io_device_sections_start();
    if(launch_error)fprintf(stderr,"[SECTION LAUNCH GUARD] %s\n",launch_error);
    CHECK(!launch_error);
    /* Complete a genuine original file read/hash once before lifetime baselines. */
    CHECK(!files_verify(PILOT_XBE_SHA256));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t threads=MEM32(stats+4),handles=MEM32(stats+8),baseline[9],after[9];test_memory_snapshot(baseline);
    const unsigned selected[]={12,38,39,55};
    for(unsigned life=0;life<3;++life)for(unsigned j=0;j<4;++j) {
        uint32_t d=section_table+selected[j]*56;CHECK(!MEM32(d+24));section_test_descriptor=d;
        section_test_head=MEM16(MEM32(d+28));section_test_tail=MEM16(MEM32(d+32));
        memset(memory+SECTION_CONTEXT,0,512);MEM32(SECTION_CONTEXT)=d;MEM32(SECTION_CONTEXT+64)=SECTION_CPU;
        for(unsigned i=0;i<16;++i)MEM8(SECTION_CONTEXT+16+i)=(uint8_t)(0x35+i*7+life);
        double fp=12345.6789+life;memcpy(memory+SECTION_CONTEXT+48,&fp,8);
        if(section_test_pinned) {
            uint32_t out=0x19800,create[]={out,0,SECTION_CPU,SECTION_CONTEXT,0};
            CHECK(!event_lle_call(254,create));uint32_t handle=MEM32(out);
            uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
            uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
            uint32_t join[]={object,0,0,0,0},result=0;
            const char *error=pilot_io_device_lle(159,memory,join,&result);
            CHECK(error && !strcmp(error,"original section unload while DMA pinned"));
            CHECK(MEM32(d+24)==2 && section_test_checks==2);
            CHECK(MEM16(MEM32(d+28))==section_test_head+1 && MEM16(MEM32(d+32))==section_test_tail+1);
            CHECK(MEM8(MEM32(d+4))==(uint8_t)(image_bytes[MEM32(d+12)]^0x5a));
            fprintf(stderr,"PASS: actual original loaded/refcount2 section pin then unload guard before refcount/counter/byte mutation; stopped session retained\n");return;
        }
        thread_cpu_create_join(SECTION_CPU,SECTION_CONTEXT,life&1);
        CHECK(MEM32(SECTION_CONTEXT+76)==1 && MEM32(SECTION_CONTEXT+68)==MEM32(SECTION_CONTEXT+72));
        CHECK(MEM32(SECTION_CONTEXT+80)==0x11223344 && MEM32(SECTION_CONTEXT+84)==0x44556677 && MEM32(SECTION_CONTEXT+88)==0x22334455);
        CHECK(!memcmp(memory+SECTION_CONTEXT+16,memory+SECTION_CONTEXT+32,16) && !memcmp(memory+SECTION_CONTEXT+48,memory+SECTION_CONTEXT+56,8));
        CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==threads && MEM32(stats+8)==handles);
        test_memory_snapshot(after);CHECK(!memcmp(baseline,after,sizeof(baseline)));
        fprintf(stderr,"[SECTION CPU LIFE] life=%u section=%u actual_file_bytes/zero/refcounts/reload/protection=1 memory/thread/handle_baselines=1\n",life,selected[j]);
    }
    memset(memory+SECTION_CONTEXT,0,512);MEM32(SECTION_CONTEXT+64)=SECTION_PAIR_CPU;
    thread_cpu_create_join(SECTION_PAIR_CPU,SECTION_CONTEXT,0);
    CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==threads && MEM32(stats+8)==handles);
    test_memory_snapshot(after);CHECK(!memcmp(baseline,after,sizeof(baseline)));
    CHECK(section_test_checks==76);
    fprintf(stderr,"PASS: twelve whole original section lifetimes plus adjacent-section coexist/unload, actual installed XBE bytes/zero-fill/refcount/reload/shared boundaries/readonly policy and exact CPU/FP/memory/thread/handle baselines\n");
}
#undef SECTION_BYTES
