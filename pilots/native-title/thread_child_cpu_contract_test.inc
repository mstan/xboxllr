/* Authored x86 parent/child programs exercise the real creation, scheduling
 * and lifetime APIs. The observer does not edit original queues or objects. */
#define THREAD_CHILD_PARENT 0x12a00u
#define THREAD_CHILD_SIMPLE 0x12c00u
#define THREAD_CHILD_SYSTEM 0x12c80u
struct ThreadChildCode {uint8_t bytes[512];unsigned size;};
static void child_emit(struct ThreadChildCode *code,const uint8_t *bytes,unsigned size)
{
    CHECK(size<=sizeof(code->bytes)-code->size);
    memcpy(code->bytes+code->size,bytes,size);code->size+=size;
}
#define CHILD_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static void child_word(struct ThreadChildCode *code,uint32_t value)
{
    child_emit(code,(const uint8_t*)&value,4);
}
static void child_push(struct ThreadChildCode *code,uint32_t value)
{
    CHILD_BYTES(code,0x68);child_word(code,value);
}
static void child_push_field(struct ThreadChildCode *code,uint8_t offset)
{
    CHILD_BYTES(code,0xff,0x77,offset);
}
static void child_output(struct ThreadChildCode *code,uint8_t offset)
{
    CHILD_BYTES(code,0x8d,0x47,offset,0x50);
}
static void child_call(struct ThreadChildCode *code,unsigned ordinal)
{
    CHILD_BYTES(code,0xb8);child_word(code,0xfe000000u+ordinal*4);
    CHILD_BYTES(code,0xff,0xd0);
}
static unsigned child_branch(struct ThreadChildCode *code,int conditional)
{
    if(conditional)CHILD_BYTES(code,0x0f,0x85);else CHILD_BYTES(code,0xe9);
    unsigned at=code->size;child_word(code,0);return at;
}
static void child_patch(struct ThreadChildCode *code,unsigned at)
{
    int32_t distance=(int32_t)code->size-(int32_t)(at+4);
    memcpy(code->bytes+at,&distance,4);
}
static void child_parent_code(void)
{
    struct ThreadChildCode code={0};struct ThreadChildCode *c=&code;
    CHILD_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14);
    /* Distinct parent controls and live x87/XMM results survive both bridge
     * ownership calls and a child that preempts before creation returns. */
    CHILD_BYTES(c,0xd9,0x6f,98,0x0f,0xae,0x57,108,
        0xdd,0x47,72,0xdc,0x47,80,0x0f,0x10,0x47,112);
    child_push_field(c,68);child_push_field(c,0);child_call(c,148);
    CHILD_BYTES(c,0x89,0x47,100,0x83,0x7f,32,0);
    unsigned system=child_branch(c,1);
    child_push(c,0);child_push_field(c,24);
    CHILD_BYTES(c,0x8b,0x77,24,0xff,0x76,64);
    child_push(c,0);child_output(c,4);child_call(c,254);
    unsigned created=child_branch(c,0);child_patch(c,system);
    CHILD_BYTES(c,0x8b,0x77,24,0xff,0x76,64);
    child_push(c,0);child_push_field(c,28);child_push_field(c,40);child_push_field(c,24);
    child_output(c,12);child_push(c,64);child_push(c,0x7000);child_push(c,32);
    child_output(c,4);child_call(c,255);child_patch(c,created);
    CHILD_BYTES(c,0x89,0x47,44,0xc7,0x47,16,1,0,0,0,
        0xdd,0x5f,88,0x0f,0x11,0x87,144,0,0,0,
        0xd9,0x7f,96,0x0f,0xae,0x5f,104,0x83,0x7f,36,0);
    unsigned closed_by_child=child_branch(c,1);
    child_output(c,8);child_push(c,0);child_push_field(c,4);child_call(c,246);
    CHILD_BYTES(c,0x89,0x47,56,0x83,0x7f,28,0);
    /* JZ skips resume when the original request was unsuspended. */
    CHILD_BYTES(c,0x0f,0x84);unsigned not_suspended=c->size;child_word(c,0);
    child_push(c,0);child_push_field(c,4);child_call(c,224);
    CHILD_BYTES(c,0x89,0x47,60);child_patch(c,not_suspended);
    for(unsigned i=0;i<4;++i)child_push(c,0);
    child_push_field(c,8);child_call(c,159);CHILD_BYTES(c,0x89,0x47,48);
    CHILD_BYTES(c,0x8b,0x47,8,0x8b,0x80,0x20,1,0,0,0x89,0x87,164,0,0,0);
    child_push_field(c,4);child_call(c,187);CHILD_BYTES(c,0x89,0x47,52);
    CHILD_BYTES(c,0x8b,0x4f,8);child_call(c,250);
    child_patch(c,closed_by_child);
    CHILD_BYTES(c,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<THREAD_CHILD_SIMPLE-THREAD_CHILD_PARENT);
    memcpy(memory+THREAD_CHILD_PARENT,c->bytes,c->size);
}
static void child_worker_code(uint32_t entry,unsigned arguments)
{
    struct ThreadChildCode code={0};struct ThreadChildCode *c=&code;
    CHILD_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x8b,0x37,0x8b,0x46,16,0x89,0x47,4,0xc7,0x47,8,1,0,0,0);
    if(arguments==2)CHILD_BYTES(c,0x8b,0x44,0x24,24,0x89,0x47,16);
    CHILD_BYTES(c,0xd9,0x7f,28,0x0f,0xae,0x5f,32,
        0x0f,0x57,0xc0,0xd9,0xee,0xdd,0x5f,40,0x83,0x7f,20,0);
    CHILD_BYTES(c,0x0f,0x84);unsigned keep_handle=c->size;child_word(c,0);
    CHILD_BYTES(c,0xff,0x76,4);child_call(c,187);CHILD_BYTES(c,0x89,0x47,24);
    child_patch(c,keep_handle);
    CHILD_BYTES(c,0x5f,0x5e,0x5d,0x5b);
    if(arguments==2) {
        child_push(c,0x12345678);child_call(c,258);CHILD_BYTES(c,0xcc);
    } else CHILD_BYTES(c,0xc2,4,0);
    CHECK(c->size<0x80);memcpy(memory+entry,c->bytes,c->size);
}
static void test_thread_child_cpu_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    child_parent_code();child_worker_code(THREAD_CHILD_SIMPLE,1);child_worker_code(THREAD_CHILD_SYSTEM,2);
    CHECK(!pilot_io_device_clock_start());
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8),settled_handles=0;
    unsigned early_children=0,child_closes=0,suspended_children=0;
    for(unsigned lifetime=0;lifetime<12;++lifetime) {
        uint32_t context=0x1a000,child=0x1a100,out=0x19800;
        int system=lifetime&1,lower=lifetime%4<2,suspended=system && lifetime%6==3;
        int self_close=lower && !suspended;
        memset(memory+context,0xcd,512);
        MEM32(context+4)=MEM32(context+8)=MEM32(context+12)=MEM32(context+16)=0;
        MEM32(context+24)=child;MEM32(context+28)=suspended;MEM32(context+32)=system;
        MEM32(context+36)=self_close;MEM32(context+40)=0x2468ace0;
        MEM32(context+64)=THREAD_CHILD_PARENT;MEM32(context+68)=lower?7:8;
        double a=1.5+lifetime,b=2.25;
        memcpy(memory+context+72,&a,8);memcpy(memory+context+80,&b,8);
        MEM16(context+98)=0x077f;MEM32(context+108)=0x3f80;
        const float xmm[]={1.25f,2,-1,4};memcpy(memory+context+112,xmm,sizeof(xmm));
        MEM32(child)=context;MEM32(child+20)=self_close;
        MEM32(child+64)=system?THREAD_CHILD_SYSTEM:THREAD_CHILD_SIMPLE;
        uint32_t create[]={out,0,THREAD_CHILD_PARENT,context,0};CHECK(!event_lle_call(254,create));
        uint32_t handle=MEM32(out),ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));
        uint32_t object=MEM32(out+4);MEM32(context)=object;
        CHECK(((MEM32(object+0x30)>>16)&255)==8);
        uint32_t join[]={object,0,0,0,0};CHECK(!event_lle_call(159,join));
        CHECK(!event_lle_call(187,&handle));event_lle_call(250,&object);
        CHECK(!MEM32(context+44) && MEM32(context+16)==1 && MEM32(child+8)==1);
        CHECK(MEM32(context+100)==8);
        double real=0;memcpy(&real,memory+context+88,8);CHECK(real==3.75+lifetime);
        CHECK(!memcmp(memory+context+144,xmm,sizeof(xmm)));
        CHECK(MEM16(context+96)==0x077f && MEM32(context+104)==0x3f80);
        CHECK(MEM16(child+28)==0x027f && MEM32(child+32)==0x1f80 && !MEM64(child+40));
        if(system)CHECK(MEM32(child+16)==0x2468ace0 && MEM32(context+12));
        if(lower && !suspended){CHECK(!MEM32(child+4));++early_children;}
        if(suspended){CHECK(MEM32(child+4)==1 && !MEM32(context+60));++suspended_children;}
        if(self_close){CHECK(!MEM32(child+24));++child_closes;}
        else {
            CHECK(!MEM32(context+48) && !MEM32(context+52) && !MEM32(context+56));
            CHECK(MEM32(context+164)==(system?0x12345678u:0));
        }
        uint32_t child_handle=MEM32(context+4);CHECK(event_lle_call(187,&child_handle)==0xc0000008u);
        uint32_t live=0;CHECK(!pilot_io_device_next_thread(&live) && !live);
        MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==baseline_threads);
        if(!lifetime){CHECK(MEM32(stats+8)<=baseline_handles);settled_handles=MEM32(stats+8);}
        else CHECK(MEM32(stats+8)==settled_handles);
    }
    CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    CHECK(thread_cpu_calls==24 && early_children && child_closes && suspended_children);
    fprintf(stderr,"PASS: 12 original CPU parent/child lifetimes, both creation ABIs, %u children before creator return, %u early child closes, %u initial suspensions, live parent FP, actual fastcall dereference and original resource baselines\n",
        early_children,child_closes,suspended_children);
}
#undef CHILD_BYTES
