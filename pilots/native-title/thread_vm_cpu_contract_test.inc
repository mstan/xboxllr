/* Authored actual-CPU VM lifetimes. Original APIs choose ranges/PFNs/policy;
 * the waiting native observer independently checks host and pinned DMA views. */
#define THREAD_VM_CPU 0x12f00u
#define VM_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static void vm_status(struct ThreadChildCode *c,uint8_t offset)
{
    VM_BYTES(c,0x89,0x47,offset);
}
static void vm_allocate_code(struct ThreadChildCode *c,uint32_t type,uint32_t protect)
{
    child_push(c,protect);child_push(c,type);child_output(c,4);child_push(c,0);child_output(c,0);child_call(c,184);
}
static void vm_query_code(struct ThreadChildCode *c,uint8_t output)
{
    child_output(c,output);child_push_field(c,0);child_call(c,217);
}
static void thread_vm_code(int pinned_release)
{
    struct ThreadChildCode code={0};struct ThreadChildCode *c=&code;
    VM_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14);
    vm_allocate_code(c,MEM_RESERVE,PAGE_READWRITE);vm_status(c,8);
    vm_query_code(c,96);vm_status(c,16);
    VM_BYTES(c,0x8b,0x47,112,0x89,0x47,56);
    vm_allocate_code(c,MEM_COMMIT,PAGE_READWRITE);vm_status(c,12);
    VM_BYTES(c,0x8b,0x37,0x89,0x77,76,0x8b,0x06,0x89,0x47,88,
        0x8b,0x86,0,0x10,0,0,0x89,0x47,92,
        0xc7,0x06,0x68,0x24,0x57,0x13,
        0xc7,0x86,0,0x10,0,0,0x57,0x13,0x68,0x24,
        0x8b,0x06,0x89,0x47,68,
        0xc7,0x47,4,0,0x10,0,0);
    child_output(c,20);child_push(c,PAGE_READONLY);child_output(c,4);child_output(c,0);child_call(c,204);vm_status(c,24);
    vm_query_code(c,96);VM_BYTES(c,0x8b,0x47,116,0x89,0x47,60);
    child_push(c,0);child_push_field(c,80);child_call(c,225);
    child_push(c,0);child_push(c,0);child_push_field(c,84);child_call(c,233);
    if(pinned_release) {
        VM_BYTES(c,0xc7,0x47,4,0,0,0,0);
        child_push(c,MEM_RELEASE);child_output(c,4);child_output(c,0);child_call(c,199);
        VM_BYTES(c,0xcc); /* Guard must stop before any successful return. */
        CHECK(c->size<512);memcpy(memory+THREAD_VM_CPU,c->bytes,c->size);return;
    }
    /* Ask the original kernel again after recommit; do not infer its policy
     * from the request, the old protection, or the host publication. */
    vm_allocate_code(c,MEM_COMMIT,PAGE_READWRITE);vm_status(c,28);
    vm_query_code(c,96);VM_BYTES(c,0x8b,0x47,116,0x89,0x47,64);
    child_push(c,0);child_push_field(c,80);child_call(c,225);
    child_push(c,0);child_push(c,0);child_push_field(c,84);child_call(c,233);
    /* Real partial decommit/recommit must produce fresh zero bytes. */
    VM_BYTES(c,0x8b,0x47,76,0x05,0,0x10,0,0,0x89,0x07);
    child_push(c,MEM_DECOMMIT);child_output(c,4);child_output(c,0);child_call(c,199);vm_status(c,124);
    vm_query_code(c,32);vm_status(c,16);
    vm_allocate_code(c,MEM_COMMIT,PAGE_READWRITE);vm_status(c,12);
    VM_BYTES(c,0x8b,0x37,0x8b,0x06,0x89,0x47,72,
        0x8b,0x47,76,0x89,0x07,0xc7,0x47,4,0,0,0,0);
    child_push(c,MEM_RELEASE);child_output(c,4);child_output(c,0);child_call(c,199);vm_status(c,28);
    VM_BYTES(c,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<512);memcpy(memory+THREAD_VM_CPU,c->bytes,c->size);
}
static void test_thread_vm_cpu_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));thread_vm_code(0);
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned life=0;life<6;++life) {
        const uint32_t context=0x1b000,out=0x19800;uint32_t events[2];
        memset(memory+context,0,256);MEM32(context+4)=8192;MEM32(context+64)=THREAD_VM_CPU;
        MEM32(context+128)=0xa5a55a5a;
        for(unsigned i=0;i<2;++i) {
            uint32_t create[]={out,0,1,0};CHECK(!event_lle_call(189,create));events[i]=MEM32(out);
            MEM32(context+80+i*4)=events[i];
        }
        uint32_t create[]={out,0,THREAD_VM_CPU,context,0};CHECK(!event_lle_call(254,create));uint32_t handle=MEM32(out);
        uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
        uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
        uint32_t base=0;MEMORY_BASIC_INFORMATION host;
        for(unsigned phase=0;phase<2;++phase) {
            uint32_t wait[]={events[0],0,0};CHECK(!event_lle_call(233,wait));
            CHECK(!MEM32(context+8) && !MEM32(context+12) && !MEM32(context+24) && !MEM32(context+28));
            CHECK(MEM32(context+20)==PAGE_READWRITE && MEM32(context+56)==MEM_RESERVE);
            CHECK(MEM32(context+60)==PAGE_READONLY);
            if(phase)CHECK(MEM32(context+64)==PAGE_READWRITE);
            CHECK(!MEM32(context+88) && !MEM32(context+92));
            base=MEM32(context+76);CHECK(base && MEM32(context+68)==0x13572468);
            CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_COMMIT);
            CHECK(host.Protect==(phase?MEM32(context+64):MEM32(context+60)));
            CHECK(test_write_faults(base)==!phase);
            if(phase)MEM32(base)=0x13572468;
            CHECK(VirtualQuery(memory+base+4096,&host,sizeof(host)) && host.State==MEM_COMMIT && host.Protect==PAGE_READWRITE);
            uint32_t pin[]={base,8192,0};CHECK(!pilot_io_device_lle(175,memory,pin,&g_eax));
            for(unsigned i=0;i<2;++i) {
                uint32_t address=base+i*4096,physical=0,word=0;
                CHECK(!pilot_io_device_lle(173,memory,&address,&physical));CHECK(physical);
                CHECK(pilot_io_device_pinned_dma(physical,&word,4,0));
                CHECK(word==(i?0x24681357:0x13572468));
            }
            pin[2]=1;CHECK(!pilot_io_device_lle(175,memory,pin,&g_eax));
            uint32_t signal[]={events[1],0};CHECK(!event_lle_call(225,signal));
        }
        uint32_t join[]={object,0,0,0,0};CHECK(!event_lle_call(159,join));
        CHECK(!MEM32(context+12) && !MEM32(context+16) && !MEM32(context+28) && !MEM32(context+124));
        CHECK(!MEM32(context+72) && MEM32(context+48)==MEM_RESERVE && MEM32(context+128)==0xa5a55a5a);
        CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_RESERVE);
        CHECK(VirtualQuery(memory+base+4096,&host,sizeof(host)) && host.State==MEM_RESERVE);
        CHECK(!event_lle_call(187,&handle));event_lle_call(250,&object);
        for(unsigned i=0;i<2;++i)CHECK(!event_lle_call(187,events+i));
        CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    CHECK(thread_cpu_calls==6);
    fprintf(stderr,"PASS: six actual-CPU VM lifetimes, reserve/commit/protect/original recommit policy/decommit/recommit/release, host protections and real pinned DMA aliases\n");
}
static void test_thread_vm_pinned_release_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));thread_vm_code(1);
    const uint32_t context=0x1b000,out=0x19800;uint32_t events[2];
    memset(memory+context,0,256);MEM32(context+4)=8192;MEM32(context+64)=THREAD_VM_CPU;
    MEM32(context+128)=0xa5a55a5a;
    for(unsigned i=0;i<2;++i) {
        uint32_t create[]={out,0,1,0};CHECK(!event_lle_call(189,create));events[i]=MEM32(out);
        MEM32(context+80+i*4)=events[i];
    }
    uint32_t create[]={out,0,THREAD_VM_CPU,context,0};CHECK(!event_lle_call(254,create));uint32_t handle=MEM32(out);
    uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
    uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
    uint32_t wait[]={events[0],0,0};CHECK(!event_lle_call(233,wait));
    uint32_t base=MEM32(context+76);CHECK(base && MEM32(context+60)==PAGE_READONLY);
    /* Only the second page is pinned: whole-allocation release must inspect
     * more than the first output page or the caller's zero-sized interval. */
    uint32_t pin[]={base+4096,4096,0};CHECK(!pilot_io_device_lle(175,memory,pin,&g_eax));
    uint32_t signal[]={events[1],0};CHECK(!event_lle_call(225,signal));
    uint32_t join[]={object,0,0,0,0},result=0;
    const char *error=pilot_io_device_lle(159,memory,join,&result);
    CHECK(error && !strcmp(error,"memory LLE release while DMA pinned"));
    MEMORY_BASIC_INFORMATION host;
    CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_COMMIT && host.Protect==PAGE_READONLY);
    CHECK(VirtualQuery(memory+base+4096,&host,sizeof(host)) && host.State==MEM_COMMIT && host.Protect==PAGE_READWRITE);
    CHECK(MEM32(base)==0x13572468 && MEM32(base+4096)==0x24681357);
    CHECK(MEM32(context)==base && !MEM32(context+4) && MEM32(context+128)==0xa5a55a5a);
    fprintf(stderr,"PASS: actual-CPU zero-size release stops before freeing a reservation with a pinned second page; bytes/protections/output canary retained\n");
}
#undef VM_BYTES
