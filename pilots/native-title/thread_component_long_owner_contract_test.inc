/* A real infinite thread join owns an application lifetime. Successful whole
 * component returns and clock slices cannot impose an 8192-transition exit. */
#define THREAD_COMPONENT_LONG_CPU 0x17200u
#define THREAD_COMPONENT_LONG_HELPER 0x17d80u
#define COMPONENT_LONG_COUNT 10240u
static unsigned component_long_calls;
static const char *test_component_long_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==THREAD_COMPONENT_LONG_HELPER && pilot_io_device_thread_component_active());
    CHECK(regs[7]==0x1b000 && regs[3]==0x11223344 && regs[5]==0x44556677);
    CHECK(regs[6] && regs[6]<=COMPONENT_LONG_COUNT);
    CHECK(regs[6]==COMPONENT_LONG_COUNT-(component_long_calls%COMPONENT_LONG_COUNT));
    ++component_long_calls;regs[0]=regs[6];regs[4]+=4;return NULL;
}
static void test_thread_component_long_owner_lle(int old_budget)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};char identity[65];
    memcpy(memory+THREAD_COMPONENT_LONG_HELPER,helper,sizeof(helper));test_component_hash(helper,sizeof(helper),identity);
    CHECK(!pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_LONG_HELPER,sizeof(helper),identity,0,test_component_long_body));
    const uint8_t caller[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,
        0xbe,0,0x28,0,0,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8,
        0xb8,0x80,0x7d,1,0,0xff,0xd0,0x9c,0x5a,0x89,0x57,12,
        0x01,0x47,84,0x4e,0x75,0xe1,
        0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x5f,88,0x89,0x6f,92,0xc7,0x47,76,1,0,0,0,
        0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+THREAD_COMPONENT_LONG_CPU,caller,sizeof(caller));
    CHECK(!pilot_io_device_resident(0x10000,0x10000));
    uint64_t next=0;CHECK(!pilot_usb_pit_clock(&thread_idle_clock.base,&next));
    thread_idle_clock.origin=dispatch_monotonic();thread_idle_clock.samples=0;
    CHECK(!pilot_io_device_clock_bound());
    if(old_budget!=2) {
        CHECK(!pilot_io_device_clock_bind(test_thread_idle_clock_input,&thread_idle_clock));
        CHECK(pilot_io_device_clock_bound());
    }
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    uint32_t before_memory[9],after_memory[9];test_memory_snapshot(before_memory);
    for(unsigned life=0;life<(old_budget?1u:3u);++life) {
        uint32_t context=0x1b000;memset(memory+context,0,128);MEM32(context+64)=THREAD_COMPONENT_LONG_CPU;MEM32(context+80)=0xa5a55a5a;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7);
        double expected=12345.6789;memcpy(memory+context+48,&expected,8);
        uint32_t out=0x19800,create[]={out,0,THREAD_COMPONENT_LONG_CPU,context,0};CHECK(!event_lle_call(254,create));
        uint32_t handle=MEM32(out),ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
        uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
        if(!old_budget && life==2)CHECK(!event_lle_call(187,&handle));
        MEM64(context+104)=(uint64_t)-(int64_t)30000000;
        uint32_t join[]={object,0,0,0,life==1?context+104:0},result=0;
        const char *error=pilot_io_device_lle(159,memory,join,&result);
        if(old_budget) {
            CHECK(error && !strcmp(error,"device LLE callback budget exhausted"));
            CHECK(component_long_calls && component_long_calls<COMPONENT_LONG_COUNT && !MEM32(context+76));
            CHECK(MEM32(context+80)==0xa5a55a5a);
            fprintf(stderr,"PASS: causal old8192 transition limit stops a legitimate original thread join after%u successful whole components; no fabricated completion\n",component_long_calls);return;
        }
        CHECK(!error && !result);if(life!=2)CHECK(!event_lle_call(187,&handle));event_lle_call(250,&object);
        CHECK(MEM32(context+76)==1 && MEM32(context+80)==0xa5a55a5a && MEM32(context+84)==COMPONENT_LONG_COUNT*(COMPONENT_LONG_COUNT+1)/2);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(MEM32(context+88)==0x11223344 && MEM32(context+92)==0x44556677);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
        test_memory_snapshot(after_memory);CHECK(!memcmp(before_memory,after_memory,sizeof(before_memory)));
    }
    CHECK(component_long_calls==3*COMPONENT_LONG_COUNT && thread_idle_clock.samples>8192);
    CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    CHECK(!pilot_io_device_clock_bound());
    fprintf(stderr,"PASS: three real original CPU infinite/finite/closed-handle joins,30720 source-bound whole components, genuine return/sum/clock progress, GPR/ESP/flags/x87/XMM and original memory/thread/handle cleanup\n");
}
