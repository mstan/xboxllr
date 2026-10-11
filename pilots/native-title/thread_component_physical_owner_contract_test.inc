/* Authored original CPU callers allocate/free actual contiguous RAM. Resource
 * references retain only those real owner/publication lifetimes, with no
 * duplicate native physical ledger or observer-created PTE/PFN allocation. */
#define THREAD_COMPONENT_PHYSICAL_CPU 0x16700u
#define THREAD_COMPONENT_PHYSICAL_HELPER 0x16e80u
#define COMPONENT_PHYSICAL_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static unsigned component_physical_calls,component_physical_reuses;
static uint64_t component_physical_refs[2],component_physical_previous;
static void test_memory_snapshot(uint32_t out[9]);
static void component_physical_rejected(uint32_t address,uint32_t bytes)
{
    uint32_t available=0x12345678;uint64_t reference=0xabcdef0123456789ull;
    CHECK(!pilot_io_device_contiguous_resource_query(address,bytes,&available));
    CHECK(available==0x12345678);
    CHECK(!pilot_io_device_contiguous_resource_reference(address,bytes,&reference,&available));
    CHECK(reference==0xabcdef0123456789ull && available==0x12345678);
}
static uint32_t component_physical_service(unsigned ordinal,const uint32_t *args)
{
    uint32_t result=0xbad0cafe;
    CHECK(!pilot_io_device_thread_component_call(ordinal,args,&result));return result;
}
static const char *test_component_physical_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==THREAD_COMPONENT_PHYSICAL_HELPER && pilot_io_device_thread_component_active());
    uint32_t phase=regs[1],context=regs[2],base=MEM32(context),extent=MEM32(context+4);
    CHECK(context==0x1b000 && phase<=4);
    CHECK(regs[3]==0x11223344 && regs[5]==0x44556677 && regs[6]==0x22334455 && regs[7]==context);
    ++component_physical_calls;
    uint32_t available=0;MEMORY_BASIC_INFORMATION host;
    if(!phase) {
        CHECK(base && extent==8192 && !physical_find(base,1));
        CHECK(MEM32(base)==0x13572468 && MEM32(base+extent-4)==0x24681357);
        CHECK(pilot_io_device_contiguous_resource_query(base,1,&available) && available==extent);
        CHECK(pilot_io_device_contiguous_resource_query(base+4088,16,&available) && available==extent-4088);
        component_physical_rejected(base,0);component_physical_rejected(base,extent+1);
        component_physical_rejected(base+extent,1);component_physical_rejected(context,4);
        component_physical_rejected(0xfffffffc,8);
        component_physical_rejected(MEM32(context+104),4); /* Physical number is not an owned canonical VA. */
        CHECK(!pilot_io_device_contiguous_resource_query(base,1,NULL));
        uint64_t untouched=0xfedcba9876543210ull;
        CHECK(!pilot_io_device_contiguous_resource_reference(base,1,NULL,&available));
        CHECK(!pilot_io_device_contiguous_resource_reference(base,1,&untouched,NULL));
        CHECK(untouched==0xfedcba9876543210ull);
        /* A query returning the whole tail must validate its distant pages,
         * even when the requested minimum itself remains readable. */
        DWORD previous,discard;
        CHECK(VirtualProtect(memory+base+4096,4096,PAGE_NOACCESS,&previous));
        component_physical_rejected(base,1);
        CHECK(VirtualProtect(memory+base+4096,4096,previous,&discard));
        CHECK(pilot_io_device_contiguous_resource_reference(base,extent,component_physical_refs,&available));
        CHECK(available==extent && component_physical_refs[0]);
        CHECK(pilot_io_device_contiguous_resource_reference(base+4088,extent-4088,component_physical_refs+1,&available));
        CHECK(available==extent-4088 && component_physical_refs[1] && component_physical_refs[1]!=component_physical_refs[0]);
        CHECK(!pilot_io_device_contiguous_resource_release(component_physical_previous));
        CHECK(!pilot_io_device_contiguous_resource_release(UINT64_MAX));
        uint32_t word=0;CHECK(pilot_io_device_contiguous_dma(MEM32(context+104),&word,4,0,PILOT_IO_DMA_USB));
        CHECK(word==0x13572468);pilot_io_device_dma_release(PILOT_IO_DMA_USB);
        /* Actual original CPU reads these observer-written bytes after this
         * return. DMA lease release leaves both resource tokens live. */
        MEM32(base)=0x89abcdef;MEM32(base+extent-4)=0xfedcba98;
    } else if(phase==1) {
        CHECK(MEM32(context+80)==0x89abcdef && MEM32(context+84)==0xfedcba98);
        CHECK(pilot_io_device_contiguous_resource_release(component_physical_refs[0]));
        CHECK(!pilot_io_device_contiguous_resource_release(component_physical_refs[0]));
        CHECK(pilot_io_device_contiguous_resource_query(base,extent,&available) && available==extent);
        uint32_t protect[]={base+4096,4096,PAGE_READONLY};
        (void)component_physical_service(182,protect);
        CHECK(VirtualQuery(memory+base+4096,&host,sizeof(host)) && host.Protect==PAGE_READONLY);
        uint64_t readonly=0;
        CHECK(pilot_io_device_contiguous_resource_reference(base+4088,16,&readonly,&available));
        CHECK(available==extent-4088 && readonly!=component_physical_refs[0] && readonly!=component_physical_refs[1]);
        CHECK(pilot_io_device_contiguous_resource_release(readonly));
        protect[2]=PAGE_READWRITE;(void)component_physical_service(182,protect);
        CHECK(VirtualQuery(memory+base+4096,&host,sizeof(host)) && host.Protect==PAGE_READWRITE);
        /* One original allocation reference remains for the terminal171 case. */
    } else if(phase==2) {
        CHECK(pilot_io_device_contiguous_resource_release(component_physical_refs[1]));
        CHECK(!pilot_io_device_contiguous_resource_release(component_physical_refs[1]));
        component_physical_previous=component_physical_refs[1];
        CHECK(pilot_io_device_contiguous_resource_query(base,extent,&available) && available==extent);
    } else if(phase==3) {
        uint32_t again=MEM32(context+88),actual=MEM32(context+92);
        CHECK(again && actual==extent && !physical_find(again,1));
        if(again==base)++component_physical_reuses;
        CHECK(MEM32(again)==0x10203040 && MEM32(again+actual-4)==0x50607080);
        CHECK(!pilot_io_device_contiguous_resource_release(component_physical_refs[0]));
        CHECK(!pilot_io_device_contiguous_resource_release(component_physical_refs[1]));
        uint64_t fresh=0;
        CHECK(pilot_io_device_contiguous_resource_reference(again,actual,&fresh,&available));
        CHECK(available==actual && fresh!=component_physical_refs[0] && fresh!=component_physical_refs[1]);
        CHECK(!pilot_io_device_contiguous_resource_release(component_physical_previous));
        CHECK(pilot_io_device_contiguous_resource_release(fresh));
        component_physical_previous=fresh;
    } else {
        uint32_t again=MEM32(context+88);
        component_physical_rejected(again,1);
        CHECK(!pilot_io_device_contiguous_resource_release(component_physical_previous));
        CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_RESERVE);
        CHECK(VirtualQuery(memory+again,&host,sizeof(host)) && host.State==MEM_RESERVE);
        regs[0]=0x76543210;
    }
    regs[4]+=4;return NULL;
}
static void component_physical_call(struct ThreadChildCode *c,unsigned phase)
{
    COMPONENT_PHYSICAL_BYTES(c,0xb9);child_word(c,phase);
    COMPONENT_PHYSICAL_BYTES(c,0x89,0xfa,0xb8);child_word(c,THREAD_COMPONENT_PHYSICAL_HELPER);
    COMPONENT_PHYSICAL_BYTES(c,0xff,0xd0);
}
static void test_thread_component_physical_owner_lle(int borrowed_release)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};
    memcpy(memory+THREAD_COMPONENT_PHYSICAL_HELPER,helper,sizeof(helper));
    char identity[65];test_component_hash(helper,sizeof(helper),identity);
    CHECK(!pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_PHYSICAL_HELPER,sizeof(helper),identity,0,test_component_physical_body));
    struct ThreadChildCode code={0},*c=&code;
    COMPONENT_PHYSICAL_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68);
    child_push_field(c,100);child_call(c,165);COMPONENT_PHYSICAL_BYTES(c,0x89,0x07);
    child_push_field(c,0);child_call(c,180);COMPONENT_PHYSICAL_BYTES(c,0x89,0x47,4);
    child_push_field(c,0);child_call(c,173);COMPONENT_PHYSICAL_BYTES(c,0x89,0x47,104);
    COMPONENT_PHYSICAL_BYTES(c,0x8b,0x07,0xc7,0x00,0x68,0x24,0x57,0x13,
        0xc7,0x80,0xfc,0x1f,0,0,0x57,0x13,0x68,0x24,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8);
    component_physical_call(c,0);
    COMPONENT_PHYSICAL_BYTES(c,0x8b,0x37,0x8b,0x06,0x89,0x47,80,
        0x8b,0x86,0xfc,0x1f,0,0,0x89,0x47,84,0xbe,0x55,0x44,0x33,0x22);
    component_physical_call(c,1);
    COMPONENT_PHYSICAL_BYTES(c,0x9c,0x5a,0x89,0x57,12);
    if(!borrowed_release)component_physical_call(c,2);
    if(borrowed_release==2) {
        child_push(c,PAGE_NOACCESS);child_push(c,8192);child_push_field(c,0);child_call(c,182);
    } else {child_push_field(c,0);child_call(c,171);}
    if(borrowed_release)COMPONENT_PHYSICAL_BYTES(c,0xcc);
    else {
        child_push(c,PAGE_READWRITE|PAGE_WRITECOMBINE);child_push(c,4096);
        child_push(c,0x3ffffff);child_push(c,0);child_push_field(c,100);child_call(c,166);
        COMPONENT_PHYSICAL_BYTES(c,0x89,0x47,88);
        child_push_field(c,88);child_call(c,180);COMPONENT_PHYSICAL_BYTES(c,0x89,0x47,92);
        COMPONENT_PHYSICAL_BYTES(c,0x8b,0x47,88,0xc7,0x00,0x40,0x30,0x20,0x10,
            0xc7,0x80,0xfc,0x1f,0,0,0x80,0x70,0x60,0x50);
        component_physical_call(c,3);
        child_push_field(c,88);child_call(c,171);component_physical_call(c,4);
        COMPONENT_PHYSICAL_BYTES(c,0x89,0x47,124,0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
            0x89,0x5f,108,0x89,0x6f,112,0x89,0x77,116,
            0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    }
    CHECK(c->size<THREAD_COMPONENT_PHYSICAL_HELPER-THREAD_COMPONENT_PHYSICAL_CPU);
    memcpy(memory+THREAD_COMPONENT_PHYSICAL_CPU,c->bytes,c->size);
#ifdef PILOT_MEMORY_LLE
    CHECK(!pilot_io_device_resident(0x10000,0x10000));
#endif
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    uint32_t before_memory[9],after_memory[9];test_memory_snapshot(before_memory);
    for(unsigned life=0;life<(borrowed_release?1u:3u);++life) {
        uint32_t context=0x1b000;memset(memory+context,0,128);MEM32(context+64)=THREAD_COMPONENT_PHYSICAL_CPU;
        MEM32(context+96)=0xa5a55a5a;MEM32(context+100)=life==1?8191:5001;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7);
        double expected=12345.6789;memcpy(memory+context+48,&expected,8);
        if(borrowed_release) {
            uint32_t out=0x19800,create[]={out,0,THREAD_COMPONENT_PHYSICAL_CPU,context,0};
            CHECK(!event_lle_call(254,create));uint32_t handle=MEM32(out);
            uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
            uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
            uint32_t join[]={object,0,0,0,0},result=0;
            const char *error=pilot_io_device_lle(159,memory,join,&result);
            CHECK(error && !strcmp(error,borrowed_release==2?
                "device LLE contiguous protection would invalidate resource read ownership":
                "device LLE contiguous release while resource borrowed"));
            uint32_t base=MEM32(context);MEMORY_BASIC_INFORMATION host;
            CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_COMMIT && host.Protect==PAGE_READWRITE);
            CHECK(VirtualQuery(memory+base+4096,&host,sizeof(host)) && host.State==MEM_COMMIT && host.Protect==PAGE_READWRITE);
            CHECK(MEM32(base)==0x89abcdef && MEM32(base+8192-4)==0xfedcba98);
            CHECK(MEM32(context+8)==MEM32(context+12) && !MEM32(context+76) && MEM32(context+96)==0xa5a55a5a);
            CHECK(component_physical_calls==2);
            fprintf(stderr,"PASS: actual original%u stops with the final resource reference still live after the first was released; allocation/protection/CPU bytes/canary retained\n",
                borrowed_release==2?182:171);return;
        }
        thread_cpu_create_join(THREAD_COMPONENT_PHYSICAL_CPU,context,life&1);
        CHECK(MEM32(context+124)==0x76543210 && MEM32(context+76)==1 && MEM32(context+96)==0xa5a55a5a);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(MEM32(context+108)==0x11223344 && MEM32(context+112)==0x44556677 && MEM32(context+116)==0x22334455);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(!memcmp(memory+THREAD_COMPONENT_PHYSICAL_HELPER,helper,sizeof(helper)));
        CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
        test_memory_snapshot(after_memory);CHECK(!memcmp(before_memory,after_memory,sizeof(before_memory)));
    }
    CHECK(component_physical_calls==15);
#ifdef PILOT_SCHEDULING_LLE
    CHECK(!thread_cpu_calls);
#else
    CHECK(thread_cpu_calls==3);
#endif
    fprintf(stderr,"PASS: three raw original CPU contiguous lifetimes, exact full/offset extents, two overlapping resource references, CPU/DMA shared bytes, readonly/protected/overflow/unowned/stale guards, last-release/free/reallocation, ESP/GPR/flags/x87/XMM and memory/thread/handle cleanup; same-life VA reuse=%u\n",component_physical_reuses);
}
#undef COMPONENT_PHYSICAL_BYTES
