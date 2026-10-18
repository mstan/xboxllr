/* A selected native component uses the production contiguous helpers while
 * its real original thread/CALL/RET and FP context remain retained. No GPU,
 * host scheduler completion or observer-created PTE/PFN is involved. */
#if defined(PILOT_SCHEDULING_LLE)
#define COMPONENT_CONTIGUOUS_CPU 0x1e800u
#define COMPONENT_CONTIGUOUS_HELPER 0x1ef80u
#define CONTIGUOUS_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static unsigned component_contiguous_calls;
static int component_contiguous_root_guard;
static void test_memory_snapshot(uint32_t out[9]);
static unsigned component_contiguous_owned(void)
{
    unsigned count=0;
    for(unsigned i=0;i<physical_allocation_capacity;++i)if(physical_allocations[i].bytes)++count;
    return count;
}
static uint32_t component_contiguous_kernel(unsigned ordinal,uint32_t sp)
{
    uint32_t old_sp=g_esp,old_ax=g_eax;
    g_esp=sp;physical_kernel(ordinal);uint32_t result=g_eax;
    g_esp=old_sp;g_eax=old_ax;return result;
}
static const char *test_component_contiguous_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==COMPONENT_CONTIGUOUS_HELPER && pilot_io_device_thread_component_active());
    uint32_t context=regs[2],phase=regs[1],base=MEM32(context);
    CHECK(context==0x1b000 && phase<=3);
    CHECK(MEM32(0x8003da64u)==MEM32(context+128));
    CHECK(regs[3]==0x11223344 && regs[5]==0x44556677 && regs[6]==0x22334455 && regs[7]==context);
    ++component_contiguous_calls;
    const char *error=native_component_original_enter();if(error)return error;
    if(component_contiguous_root_guard) {
        uint32_t args[]={5001,0,0x03ffffff,0x10000,PAGE_READWRITE|PAGE_WRITECOMBINE};
        uint32_t result=0xabcdef01,owned=component_contiguous_owned();
        error=pilot_io_device_lle(166,memory,args,&result);
        CHECK(error && !strcmp(error,"thread LLE continuation handoff requires its real guest-call frame"));
        CHECK(result==0xabcdef01 && component_contiguous_owned()==owned && !MEM32(context));
        fprintf(stderr,"[CONTIGUOUS ROOT GUARD] unchanged result/owners; original component not completed\n");
        return error; /* Failed CPU ownership is terminal; do not release or resume it. */
    }
    if(!phase) {
        CHECK(MEM32(regs[4]+4)==MEM32(context+100));
        CHECK(!physical_allocate(0,0,0x03ffffff,4096,PAGE_READWRITE));
        CHECK(!physical_allocate(5001,1,0,4096,PAGE_READWRITE));
        CHECK(!physical_allocate(5001,0,0x03ffffff,3,PAGE_READWRITE));
        fprintf(stderr,"[CONTIGUOUS COMPONENT ALLOCATE] actual thread=%08x call=%08x bytes=%u\n",
            MEM32(context+128),regs[4],MEM32(context+100));
        base=component_contiguous_kernel(166,regs[4]);CHECK(base && !(base&0xffff));
        struct PhysicalAllocation *owner=physical_find(base,8192);
        CHECK(owner && owner->base==base && owner->bytes==8192 && owner->protect==0x404 && !owner->borrowers);
        uint32_t available=0;CHECK(pilot_io_device_contiguous_resource_query(base,8192,&available) && available==8192);
        /* Allocation initial bytes are not a fixture golden. Actual original
         * CPU stores below establish both independently checked sentinels. */
        regs[0]=base;
    } else if(phase==1) {
        CHECK(MEM32(regs[4]+4)==base && MEM32(base)==0x13572468 && MEM32(base+8192-4)==0x24681357);
        CHECK(component_contiguous_kernel(180,regs[4])==8192);
        CHECK(component_contiguous_kernel(179,regs[4])==0x404);
        physical_protect(base+4096,4096,PAGE_READONLY|PAGE_NOCACHE);
        CHECK(physical_query_protection(base)==0x404 && physical_query_protection(base+4096)==0x202);
        CHECK(test_write_faults(base+4096));
        physical_protect(base,8192,PAGE_NOACCESS); /* Mm rejects this policy without mutation. */
        CHECK(physical_query_protection(base)==0x404 && physical_query_protection(base+4096)==0x202);
        physical_persist(base+4095,2,1);
        unsigned page=(base&0x0fffffff)/4096;
        CHECK(physical_persistent[page] && physical_persistent[page+1]);
    } else if(phase==2) {
        CHECK(MEM32(context+80)==0x24681357); /* Original CPU consumed the read-only publication. */
        physical_protect(base+4096,4096,PAGE_READWRITE|PAGE_WRITECOMBINE);
        CHECK(physical_query_protection(base+4096)==0x404 && !test_write_faults(base+4096));
        physical_persist(base,8192,0);
        unsigned page=(base&0x0fffffff)/4096;
        CHECK(!physical_persistent[page] && !physical_persistent[page+1]);
        (void)component_contiguous_kernel(4,regs[4]);
        CHECK(component_contiguous_kernel(1,regs[4])==base && av_saved_data==base);
    } else {
        CHECK(!MEM32(regs[4]+4) && MEM32(base)==0x89abcdef && MEM32(base+8192-4)==0xfedcba98);
        (void)component_contiguous_kernel(4,regs[4]);
        CHECK(!component_contiguous_kernel(1,regs[4]) && !av_saved_data);
        physical_free(base);CHECK(!physical_find(base,1));
        uint32_t available=0xabcdef01;
        CHECK(!pilot_io_device_contiguous_resource_query(base,1,&available) && available==0xabcdef01);
        MEMORY_BASIC_INFORMATION host;
        CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_RESERVE);
        regs[0]=0x76543210;
    }
    error=native_component_original_leave();if(error)return error;
    regs[4]+=24;return NULL;
}
static void component_contiguous_call(struct ThreadChildCode *c,unsigned phase)
{
    if(!phase) {
        child_push(c,PAGE_READWRITE|PAGE_WRITECOMBINE);child_push(c,0x10000);
        child_push(c,0x03ffffff);child_push(c,0);child_push_field(c,100);
    } else {
        for(unsigned i=0;i<4;++i)child_push(c,0);
        if(phase==3)child_push(c,0);else child_push_field(c,0);
    }
    CONTIGUOUS_BYTES(c,0xb9);child_word(c,phase);
    CONTIGUOUS_BYTES(c,0x89,0xfa,0xb8);child_word(c,COMPONENT_CONTIGUOUS_HELPER);
    CONTIGUOUS_BYTES(c,0xff,0xd0);
}
static void test_thread_component_contiguous_lle(int root_guard)
{
    component_contiguous_root_guard=root_guard;
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));vm_ensure();
    /* Preserve the existing root-service route, on an actual original allocation. */
    uint32_t direct=physical_allocate(5001,0,0x03ffffff,0x10000,PAGE_READWRITE|PAGE_WRITECOMBINE);
    CHECK(direct && physical_query_protection(direct)==0x404);physical_free(direct);
    uint32_t zero=0,original_saved=0;CHECK(!pilot_io_device_lle(1,memory,&zero,&original_saved));
    CHECK(!original_saved); /* Preserve the bound capture's actual empty saved-data owner. */
    CHECK(!physical_find(direct,1));native_component_gate_bind();
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc2,20,0};char sha[65];
    memcpy(memory+COMPONENT_CONTIGUOUS_HELPER,helper,sizeof(helper));test_component_hash(helper,sizeof(helper),sha);
    CHECK(!pilot_io_device_thread_component_bind_abi(COMPONENT_CONTIGUOUS_HELPER,sizeof(helper),sha,20,test_component_contiguous_body));
    struct ThreadChildCode code={0},*c=&code;
    CONTIGUOUS_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x64,0xa1,0x28,0,0,0,0x89,0x87,0x80,0,0,0,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8);
    component_contiguous_call(c,0);
    CONTIGUOUS_BYTES(c,0x89,0x07,0xc7,0x00,0x68,0x24,0x57,0x13,
        0xc7,0x80,0xfc,0x1f,0,0,0x57,0x13,0x68,0x24);
    component_contiguous_call(c,1);
    CONTIGUOUS_BYTES(c,0x8b,0x07,0x8b,0x80,0xfc,0x1f,0,0,0x89,0x47,80);
    component_contiguous_call(c,2);
    CONTIGUOUS_BYTES(c,0x8b,0x07,0xc7,0x00,0xef,0xcd,0xab,0x89,
        0xc7,0x80,0xfc,0x1f,0,0,0x98,0xba,0xdc,0xfe);
    component_contiguous_call(c,3);
    CONTIGUOUS_BYTES(c,0x89,0x47,84,0x9c,0x5a,0x89,0x57,12,0x89,0x67,72,
        0x0f,0x11,0x47,32,0xdd,0x5f,56,0x89,0x5f,108,0x89,0x6f,112,0x89,0x77,116,
        0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<COMPONENT_CONTIGUOUS_HELPER-COMPONENT_CONTIGUOUS_CPU);
    memcpy(memory+COMPONENT_CONTIGUOUS_CPU,c->bytes,c->size);
    CHECK(!pilot_io_device_resident(0x10000,0x10000));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8),baseline_owned=component_contiguous_owned();
    uint32_t before_memory[9],after_memory[9];test_memory_snapshot(before_memory);
    for(unsigned life=0;life<(root_guard?1u:3u);++life) {
        uint32_t context=0x1b000;memset(memory+context,0,132);MEM32(context+64)=COMPONENT_CONTIGUOUS_CPU;
        MEM32(context+96)=0xa5a55a5a;MEM32(context+100)=life==1?8191:5001;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7);
        double fp=12345.6789;memcpy(memory+context+48,&fp,8);
        if(root_guard) {
            uint32_t out=0x19800,birth[]={out,0,COMPONENT_CONTIGUOUS_CPU,context,0};CHECK(!event_lle_call(254,birth));
            uint32_t handle=MEM32(out),ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
            uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
            uint32_t join[]={object,0,0,0,0},result=0xfeedcafe;
            const char *error=pilot_io_device_lle(159,memory,join,&result);
            CHECK(error && !strcmp(error,"thread LLE continuation handoff requires its real guest-call frame"));
            CHECK(result==0xfeedcafe && !MEM32(context+76) && component_contiguous_calls==1);
            CHECK(!MEM32(context) && MEM32(context+96)==0xa5a55a5a && component_contiguous_owned()==baseline_owned);
            fprintf(stderr,"PASS: root-only166 rejects selected original worker; result/owners/canary unchanged and original join unfinished\n");return;
        }
        thread_cpu_create_join(COMPONENT_CONTIGUOUS_CPU,context,life&1);
        CHECK(MEM32(context+84)==0x76543210 && MEM32(context+76)==1 && MEM32(context+96)==0xa5a55a5a);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(MEM32(context+108)==0x11223344 && MEM32(context+112)==0x44556677 && MEM32(context+116)==0x22334455);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(!physical_find(MEM32(context),1) && component_contiguous_owned()==baseline_owned);
        CHECK(!memcmp(memory+COMPONENT_CONTIGUOUS_HELPER,helper,sizeof(helper)));
        CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
        test_memory_snapshot(after_memory);CHECK(!memcmp(before_memory,after_memory,sizeof(before_memory)));
        CHECK(MEM32(native_component_original_gate+16)==UINT32_MAX && !MEM32(native_component_original_gate+20) && !MEM32(native_component_original_gate+24));
    }
    CHECK(component_contiguous_calls==12 && !thread_cpu_calls);
    uint32_t gate=native_component_original_gate;native_component_original_gate=0;pool_free(gate);
    fprintf(stderr,"PASS: three raw original CPU/native contiguous component lifetimes; real166/180/179/182/178/4/1/171, shared CPU bytes/read-only publication, retained ESP/GPR/flags/x87/XMM and exact memory/thread/handle/ledger cleanup; no GPU\n");
}
#undef CONTIGUOUS_BYTES
#endif
