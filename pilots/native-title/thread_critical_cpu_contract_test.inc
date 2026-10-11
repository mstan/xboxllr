/* Authored raw workers use original recursive critical sections. A higher
 * priority contender must block and receive ownership from the actual release;
 * no host lock, queue edit or manufactured wakeup can satisfy these checks. */
#define LOCK_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
#define THREAD_CRITICAL_OWNER 0x12d00u
#define THREAD_CRITICAL_WAITER 0x12e00u
static void thread_critical_code(uint32_t entry,int owner)
{
    struct ThreadChildCode code={0},*c=&code;
    LOCK_BYTES(c,0x53,0x56,0x57,0x8b,0x7c,0x24,0x10); /* preserved GPRs; EDI=context */
    LOCK_BYTES(c,0x8b,0x77,4,0x8b,0x5f,8); /* ESI=shared, EBX=lock */
    if(owner) {
        LOCK_BYTES(c,0x53);child_call(c,277);
        LOCK_BYTES(c,0x53);child_call(c,277);
        LOCK_BYTES(c,0xc7,0x06,1,0,0,0);
        child_push(c,0);child_push_field(c,12);child_call(c,225); /* ready event */
        child_push(c,0);child_push(c,0);child_push_field(c,16);child_call(c,233);
        LOCK_BYTES(c,0x89,0x47,20); /* actual wait status */
        LOCK_BYTES(c,0x53);child_call(c,294); /* recursive release retains owner */
        LOCK_BYTES(c,0x8b,0x43,0x14,0x89,0x47,24);
        LOCK_BYTES(c,0x8b,0x43,0x18,0x89,0x47,28);
        LOCK_BYTES(c,0xc7,0x06,2,0,0,0);
        LOCK_BYTES(c,0x53);child_call(c,294); /* actual signal/preemption */
        LOCK_BYTES(c,0xc7,0x47,32,1,0,0,0);
    } else {
        LOCK_BYTES(c,0x8b,0x06,0x89,0x47,20); /* holder was already inside */
        child_push(c,0);child_push_field(c,16);child_call(c,225);
        LOCK_BYTES(c,0x53);child_call(c,277); /* higher-priority worker must block */
        LOCK_BYTES(c,0x8b,0x06,0x89,0x47,24); /* holder reached final release */
        LOCK_BYTES(c,0x8b,0x43,0x10,0x89,0x47,28);
        LOCK_BYTES(c,0x8b,0x43,0x14,0x89,0x47,32);
        LOCK_BYTES(c,0x8b,0x43,0x18,0x89,0x47,36);
        LOCK_BYTES(c,0x53);child_call(c,294);
    }
    LOCK_BYTES(c,0x5f,0x5e,0x5b,0xc2,4,0);
    CHECK(c->size<0x100);memcpy(memory+entry,c->bytes,c->size);
}
static void test_thread_critical_cpu_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    thread_critical_code(THREAD_CRITICAL_OWNER,1);
    thread_critical_code(THREAD_CRITICAL_WAITER,0);
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned life=0;life<6;++life) {
        const uint32_t contexts[]={0x1b000,0x1b100};
        uint32_t shared=0x1b200,lock=0x1b300,out=0x19800,handles[2],objects[2],events[2];
        memset(memory+contexts[0],0,0x400);
        MEM32(lock-4)=0x13572468;MEM32(lock+28)=0x24681357;
        event_lle_call(291,&lock); /* VOID: EAX is not a status result. */
        CHECK(MEM32(lock+16)==UINT32_MAX && !MEM32(lock+20) && !MEM32(lock+24));
        for(unsigned i=0;i<2;++i) {
            uint32_t create_event[]={out,0,1,0};CHECK(!event_lle_call(189,create_event));events[i]=MEM32(out);
        }
        for(unsigned i=0;i<2;++i) {
            uint32_t context=contexts[i];
            MEM32(context+4)=shared;MEM32(context+8)=lock;
            MEM32(context+12)=events[0];MEM32(context+16)=events[1];
            MEM32(context+64)=i?THREAD_CRITICAL_WAITER:THREAD_CRITICAL_OWNER;
            uint32_t create[]={out,0,MEM32(context+64),context,0};CHECK(!event_lle_call(254,create));
            handles[i]=MEM32(out);uint32_t ref[]={handles[i],0,out+4};CHECK(!event_lle_call(246,ref));
            objects[i]=MEM32(out+4);MEM32(context)=objects[i];
            uint32_t priority[]={objects[i],29+i};CHECK(event_lle_call(148,priority)==8);
            if(!i) {
                uint32_t ready[]={events[0],0,0};CHECK(!event_lle_call(233,ready));
                CHECK(MEM32(shared)==1 && MEM32(lock+16)==1 && MEM32(lock+20)==2 && MEM32(lock+24)==objects[0]);
            }
        }
        for(unsigned i=0;i<2;++i) {
            uint32_t join[]={objects[i],0,0,0,0};CHECK(!event_lle_call(159,join));
            CHECK(!event_lle_call(187,handles+i));event_lle_call(250,objects+i);
            CHECK(event_lle_call(187,handles+i)==0xc0000008u);
            CHECK(!event_lle_call(187,events+i));
        }
        CHECK(!MEM32(contexts[0]+20) && MEM32(contexts[0]+24)==1 && MEM32(contexts[0]+28)==objects[0]);
        CHECK(MEM32(contexts[0]+32)==1 && MEM32(contexts[1]+20)==1 && MEM32(contexts[1]+24)==2);
        CHECK(!MEM32(contexts[1]+28) && MEM32(contexts[1]+32)==1 && MEM32(contexts[1]+36)==objects[1]);
        CHECK(MEM32(lock+16)==UINT32_MAX && !MEM32(lock+20) && !MEM32(lock+24));
        CHECK(MEM32(lock-4)==0x13572468 && MEM32(lock+28)==0x24681357);
        uint32_t live=0;CHECK(!pilot_io_device_next_thread(&live) && !live);
        MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    CHECK(thread_cpu_calls==12);
    fprintf(stderr,"PASS: six original recursive critical-section lifetimes, actual higher-priority contention/wakeup and ownership transfer, canaries and resource restoration\n");
}
#undef LOCK_BYTES
