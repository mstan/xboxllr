/* Real downstream XPP delay/raise/stall calls, not a host Sleep substitute. */
#define DELAY_CPU_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
#define THREAD_DELAY_CPU 0x18b00u
#define THREAD_DELAY_PEER 0x18e00u
#define THREAD_DELAY_CONTEXT 0x1bc00u
#define THREAD_DELAY_PEER_CONTEXT 0x1bd00u
static uint32_t thread_delay_object,thread_delay_peer_object,thread_delay_last_counter;
static unsigned thread_delay_waiting,thread_delay_progress;
static const char *test_thread_delay_clock(void *context,uint64_t *ticks)
{
    const char *error=test_thread_idle_clock_input(context,ticks);if(error)return error;
    unsigned char waiter[0x140],peer[0x140];
    if(component_wait_thread_snapshot(thread_delay_object,waiter) && waiter[0x2c]==5 &&
       component_wait_thread_snapshot(thread_delay_peer_object,peer) && peer[0x2c]==2) {
        ++thread_delay_waiting;uint32_t count=MEM32(THREAD_DELAY_PEER_CONTEXT+4);
        if(count>thread_delay_last_counter)++thread_delay_progress;
        thread_delay_last_counter=count;
    }
    return NULL;
}
static void thread_delay_time(struct ThreadChildCode *c,uint32_t offset)
{
    child_call(c,125);DELAY_CPU_BYTES(c,0x89,0x87);child_word(c,offset);
    DELAY_CPU_BYTES(c,0x89,0x97);child_word(c,offset+4);
}
static void thread_delay_status(struct ThreadChildCode *c,uint32_t offset)
{DELAY_CPU_BYTES(c,0x89,0x87);child_word(c,offset);}
static void test_thread_delay_cpu_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    const uint8_t peer[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x83,0x3f,0,0x75,5,0xff,0x47,4,0xeb,0xf6,
        0xc7,0x47,8,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+THREAD_DELAY_PEER,peer,sizeof(peer));
    struct ThreadChildCode code={0},*c=&code;
    DELAY_CPU_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x89,0x67,8,0x0f,0x10,0x47,16,0xdd,0x47,48,
        0x64,0x0f,0xb6,0x05,0x24,0,0,0,0x89,0x47,68);
    child_call(c,129);DELAY_CPU_BYTES(c,0x0f,0xb6,0xc0,0x89,0x47,80,
        0x64,0x0f,0xb6,0x05,0x24,0,0,0,0x89,0x47,72);
    DELAY_CPU_BYTES(c,0x0f,0x31,0x89,0x87);child_word(c,168);DELAY_CPU_BYTES(c,0x89,0x97);child_word(c,172);
    child_push(c,10);child_call(c,151);
    DELAY_CPU_BYTES(c,0x0f,0x31,0x89,0x87);child_word(c,176);DELAY_CPU_BYTES(c,0x89,0x97);child_word(c,180);
    DELAY_CPU_BYTES(c,0x8b,0x4f,80);child_call(c,161);
    DELAY_CPU_BYTES(c,0x64,0x0f,0xb6,0x05,0x24,0,0,0,0x89,0x47,76);
    thread_delay_time(c,104);
    DELAY_CPU_BYTES(c,0x8d,0x87);child_word(c,136);DELAY_CPU_BYTES(c,0x50);
    child_push_field(c,84);child_push_field(c,4);child_call(c,99);thread_delay_status(c,88);
    thread_delay_time(c,112);thread_delay_time(c,120);
    DELAY_CPU_BYTES(c,0x8d,0x87);child_word(c,144);DELAY_CPU_BYTES(c,0x50);
    child_push(c,0);child_push(c,0);child_call(c,99);thread_delay_status(c,92);
    thread_delay_time(c,128);
    DELAY_CPU_BYTES(c,0x8d,0x87);child_word(c,152);DELAY_CPU_BYTES(c,0x50);
    child_push(c,0);child_push(c,0);child_call(c,99);thread_delay_status(c,96);
    DELAY_CPU_BYTES(c,0x89,0x67,12,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0xc7,0x47,4,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<THREAD_DELAY_PEER-THREAD_DELAY_CPU);memcpy(memory+THREAD_DELAY_CPU,c->bytes,c->size);
    CHECK(!pilot_io_device_clock_start());CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    uint64_t next=0;CHECK(!pilot_usb_pit_clock(&thread_idle_clock.base,&next));
    thread_idle_clock.origin=dispatch_monotonic();thread_idle_clock.samples=0;
    CHECK(!pilot_io_device_clock_bind(test_thread_delay_clock,&thread_idle_clock));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t base_threads=MEM32(stats+4),base_handles=MEM32(stats+8),before[9],after[9];test_memory_snapshot(before);
    for(unsigned life=0;life<3;++life) {
        uint32_t context=THREAD_DELAY_CONTEXT,peer_context=THREAD_DELAY_PEER_CONTEXT,out=0x19800;
        memset(memory+context,0,192);memset(memory+peer_context,0,96);MEM32(context+64)=THREAD_DELAY_CPU;MEM32(peer_context+64)=THREAD_DELAY_PEER;
        MEM32(context+100)=0x87654321;MEM32(context+188)=0x13579bdf;
        MEM64(context+136)=(uint64_t)-(int64_t)200000;MEM64(context+144)=(uint64_t)-(int64_t)100001;
        MEM64(context+152)=0;MEM32(context+84)=life==2;MEM32(context+4)=life&1;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(3+i*7);
        double expected=123.625;memcpy(memory+context+48,&expected,8);
        uint32_t handles[2],objects[2];const uint32_t entries[]={THREAD_DELAY_PEER,THREAD_DELAY_CPU},contexts[]={peer_context,context};
        for(unsigned id=0;id<2;++id) {
            uint32_t birth[]={out,0,entries[id],contexts[id],0};CHECK(!event_lle_call(254,birth));handles[id]=MEM32(out);
            uint32_t ref[]={handles[id],0,out+4};CHECK(!event_lle_call(246,ref));objects[id]=MEM32(out+4);
            uint32_t priority[]={objects[id],id?30:29};CHECK(event_lle_call(148,priority)==8);
        }
        thread_delay_peer_object=objects[0];thread_delay_object=objects[1];thread_delay_last_counter=0;
        unsigned waits=thread_delay_waiting,progress=thread_delay_progress;
        uint32_t join[]={objects[1],0,0,0,0};CHECK(!event_lle_call(159,join));
        CHECK(MEM32(context+4)==1 && MEM32(context+8)==MEM32(context+12));
        CHECK(!MEM32(context+68) && MEM32(context+72)==2 && !MEM32(context+76) && !MEM32(context+80));
        CHECK(MEM64(context+176)>MEM64(context+168));
        CHECK(!MEM32(context+88) && !MEM32(context+92) && !MEM32(context+96));
        CHECK(MEM64(context+112)-MEM64(context+104)>=200000 && MEM64(context+128)-MEM64(context+120)>=100001);
        CHECK(thread_delay_waiting>waits && thread_delay_progress>progress && MEM32(peer_context+4));
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(MEM32(context+100)==0x87654321 && MEM32(context+188)==0x13579bdf);
        MEM32(peer_context)=1;join[0]=objects[0];CHECK(!event_lle_call(159,join));CHECK(MEM32(peer_context+8)==1);
        thread_delay_object=thread_delay_peer_object=0;
        for(unsigned id=0;id<2;++id){CHECK(!event_lle_call(187,handles+id));event_lle_call(250,objects+id);}
        CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==base_threads && MEM32(stats+8)==base_handles);
        test_memory_snapshot(after);CHECK(!memcmp(before,after,sizeof(before)));
        fprintf(stderr,"[RAW DELAY] life=%u waiting=%u peer_progress=%u elapsed=%llu/%llu IRQL=0/2/0 returns=0 cleanup=exact\n",life,
            thread_delay_waiting-waits,thread_delay_progress-progress,(unsigned long long)(MEM64(context+112)-MEM64(context+104)),
            (unsigned long long)(MEM64(context+128)-MEM64(context+120)));
    }
    CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    fprintf(stderr,"PASS: three actual original CPU99 delay/129 raise/151 stall/161 lower lifetimes, real waiting/IRQ peer progress, exact FP/ABI/canaries and cleanup\n");
}

#undef DELAY_CPU_BYTES
