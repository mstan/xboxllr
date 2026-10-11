/* A selected native component requests actual original VM services with its
 * leased original stack. Authored CPU callers retain live flags/x87/XMM and
 * independently consume the published RAM. No graphics or VM host lock is
 * held around the original calls; creation/lock ownership remains separate. */
#define THREAD_COMPONENT_VM_CPU 0x13b00u
#define THREAD_COMPONENT_VM_HELPER 0x13b80u
static unsigned component_vm_calls,component_vm_services;
static int component_vm_workspace_guard;
struct ComponentVmResult {uint32_t before,words[7],after;};
static uint32_t component_vm_service(unsigned ordinal,const uint32_t inputs[5],uint32_t outputs[7])
{
    struct ComponentVmResult result={0x13579bdf,{0},0x2468ace0};
    for(unsigned i=0;i<7;++i)result.words[i]=0xabcdef00u+i;
    uint32_t status=0xbad0cafe;
    CHECK(!pilot_io_device_vm(ordinal,memory,inputs,result.words,&status));
    CHECK(result.before==0x13579bdf && result.after==0x2468ace0);
    if(ordinal!=217)for(unsigned i=3;i<7;++i)CHECK(result.words[i]==0xabcdef00u+i);
    memcpy(outputs,result.words,28);++component_vm_services;return status;
}
static void component_vm_query(uint32_t base,uint32_t state,uint32_t protection,uint32_t allocation)
{
    uint32_t inputs[]={base,0,0,0,0},outputs[7];
    CHECK(!component_vm_service(217,inputs,outputs));
    CHECK(outputs[0]==base && outputs[1]==allocation && outputs[4]==state);
    if(state==MEM_COMMIT)CHECK(outputs[5]==protection);
}
static const char *test_component_vm_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==THREAD_COMPONENT_VM_HELPER && pilot_io_device_thread_component_active());
    ++component_vm_calls;uint32_t context=regs[2],phase=regs[1];
    CHECK(context==0x1b000 && phase<=1);
    /* Rejected calls do not mutate caller outputs or consume a stack lease. */
    uint32_t inputs[]={0,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,0};
    uint32_t outputs[7]={0},status=0x13579bdf,untouched[7]={0};
    CHECK(pilot_io_device_vm(184,NULL,inputs,outputs,&status));
    CHECK(pilot_io_device_vm(184,memory,NULL,outputs,&status));
    CHECK(pilot_io_device_vm(184,memory,inputs,NULL,&status));
    CHECK(pilot_io_device_vm(184,memory,inputs,outputs,NULL));
    CHECK(pilot_io_device_vm(181,memory,inputs,outputs,&status));
    CHECK(pilot_io_device_vm(184,memory+4096,inputs,outputs,&status));
    CHECK(status==0x13579bdf && !memcmp(outputs,untouched,sizeof(outputs)));
    if(component_vm_workspace_guard) {
        uint32_t lease=0;CHECK(!pilot_io_device_thread_component_workspace(32,&lease));
        memset(memory+lease,0x65,32);
        const char *error=pilot_io_device_vm(184,memory,inputs,outputs,&status);
        CHECK(error && !strcmp(error,"selected helper workspace outside its original stack lease"));
        CHECK(status==0x13579bdf && !memcmp(outputs,untouched,sizeof(outputs)));
        for(unsigned i=0;i<32;++i)CHECK(MEM8(lease+i)==0x65);
        regs[0]=0x76543210;regs[4]+=4;return NULL;
    }
    MEMORY_BASIC_INFORMATION host;
    if(!phase) {
        CHECK(!component_vm_service(184,inputs,outputs));
        uint32_t base=outputs[0];CHECK(base && !(base&4095) && outputs[1]==8192);
        MEM32(context+80)=base;
        component_vm_query(base,MEM_COMMIT,PAGE_READWRITE,base);
        CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_COMMIT && host.Protect==PAGE_READWRITE);
        CHECK(!MEM32(base) && !MEM32(base+4096));
        MEM32(base+4096)=0xbaadf00d;
        uint32_t protect[]={base,4096,0,PAGE_READONLY,0};
        CHECK(!component_vm_service(204,protect,outputs));
        CHECK(outputs[0]==base && outputs[1]==4096 && outputs[2]==PAGE_READWRITE);
        component_vm_query(base,MEM_COMMIT,PAGE_READONLY,base);
        CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.Protect==PAGE_READONLY);
        CHECK(test_write_faults(base));
        protect[3]=PAGE_READWRITE;CHECK(!component_vm_service(204,protect,outputs));
        CHECK(outputs[2]==PAGE_READONLY);
        uint32_t tail[]={base+4096,4096,MEM_DECOMMIT,0,0};
        CHECK(!component_vm_service(199,tail,outputs));
        CHECK(outputs[0]==base+4096 && outputs[1]==4096);
        component_vm_query(base+4096,MEM_RESERVE,0,base);
        CHECK(VirtualQuery(memory+base+4096,&host,sizeof(host)) && host.State==MEM_RESERVE);
        tail[2]=MEM_COMMIT;tail[3]=PAGE_READWRITE;
        CHECK(!component_vm_service(184,tail,outputs));
        CHECK(outputs[0]==base+4096 && outputs[1]==4096 && !MEM32(base+4096));
        component_vm_query(base+4096,MEM_COMMIT,PAGE_READWRITE,base);
        regs[0]=base;
    } else {
        uint32_t base=MEM32(context+80);
        /* These words were stored by actual original CPU instructions after
         * allocation/publication, not by the native observer. */
        CHECK(MEM32(base)==0x13572468 && MEM32(base+4096)==0x24681357);
        component_vm_query(base,MEM_COMMIT,PAGE_READWRITE,base);
        uint32_t release[]={base,0,MEM_RELEASE,0,0};
        CHECK(!component_vm_service(199,release,outputs));
        CHECK(outputs[0]==base && outputs[1]==8192);
        CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_RESERVE);
        CHECK(VirtualQuery(memory+base+4096,&host,sizeof(host)) && host.State==MEM_RESERVE);
        component_vm_query(base,MEM_FREE,0,0);
        CHECK(component_vm_service(199,release,outputs)==VM_NOT_ALLOCATED);
        regs[0]=0x76543210;
    }
    /* A real VM call already leased this component's stack exactly once.
     * Multiple VM calls reused it; allocating another workspace must stop. */
    uint32_t extra=0;CHECK(pilot_io_device_thread_component_workspace(64,&extra));
    regs[4]+=4;return NULL;
}
static void test_thread_component_vm_lle(int workspace_guard)
{
    component_vm_workspace_guard=workspace_guard;
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};
    memcpy(memory+THREAD_COMPONENT_VM_HELPER,helper,sizeof(helper));
    char identity[65];test_component_hash(helper,sizeof(helper),identity);
    CHECK(!pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_VM_HELPER,sizeof(helper),identity,0,test_component_vm_body));
    struct ThreadChildCode code={0};struct ThreadChildCode *c=&code;
#define COMPONENT_VM_BYTES(...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
    COMPONENT_VM_BYTES(0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8,
        0xb9,0,0,0,0,0x89,0xfa,0xb8);
    child_word(c,THREAD_COMPONENT_VM_HELPER);COMPONENT_VM_BYTES(0xff,0xd0,0x89,0x07);
    if(!workspace_guard)COMPONENT_VM_BYTES(0xc7,0x00,0x68,0x24,0x57,0x13,
        0xc7,0x80,0,0x10,0,0,0x57,0x13,0x68,0x24);
    COMPONENT_VM_BYTES(0xb9,1,0,0,0,0x89,0xfa,0xb8);child_word(c,THREAD_COMPONENT_VM_HELPER);
    COMPONENT_VM_BYTES(0xff,0xd0,0x89,0x47,4,0x9c,0x5a,0x89,0x57,12,
        0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
#undef COMPONENT_VM_BYTES
    CHECK(c->size<THREAD_COMPONENT_VM_HELPER-THREAD_COMPONENT_VM_CPU);
    memcpy(memory+THREAD_COMPONENT_VM_CPU,c->bytes,c->size);
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned life=0;life<3;++life) {
        uint32_t context=0x1b000;memset(memory+context,0,100);MEM32(context+64)=THREAD_COMPONENT_VM_CPU;
        MEM32(context+96)=0xa5a55a5a;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7);
        double expected=12345.6789;memcpy(memory+context+48,&expected,8);
        thread_cpu_create_join(THREAD_COMPONENT_VM_CPU,context,life&1);
        CHECK(MEM32(context+4)==0x76543210 && MEM32(context+76)==1 && MEM32(context+96)==0xa5a55a5a);
        if(workspace_guard)CHECK(MEM32(context)==0x76543210);
        else CHECK(MEM32(context)==MEM32(context+80));
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(!memcmp(memory+THREAD_COMPONENT_VM_HELPER,helper,sizeof(helper)));
        CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    CHECK(component_vm_calls==6 && thread_cpu_calls==3);
    CHECK(workspace_guard?!component_vm_services:component_vm_services==39);
    fprintf(stderr,"PASS: three selected-component original CPU lifetimes, %s, per-component stack reuse/reset, ESP/flags/x87/XMM and thread/handle cleanup\n",
        workspace_guard?"overlapping workspace guard before VM mutation":"actual VM allocate/query/protect/decommit/recommit/release plus CPU RAM stores");
}
