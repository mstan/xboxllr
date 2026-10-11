/* Actual original x86 CALL/RET consumers of the shared device service owner.
 * The observer never fabricates a device object, allocation, pin or IRQ gate. */
#define THREAD_DEVICE_CPU 0x13d00u
static void thread_device_code(void)
{
    struct ThreadChildCode code={0},*c=&code;
    const uint8_t start[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x8b,0x5f,16,0x8d,0x74,0x9f,32,0x85,0xdb,0x0f,0x84};
    child_emit(c,start,sizeof(start));unsigned empty=c->size;child_word(c,0);
    unsigned loop=c->size;const uint8_t push[]={0x83,0xee,4,0xff,0x36,0x4b,0x0f,0x85};
    child_emit(c,push,sizeof(push));child_word(c,(uint32_t)(loop-(c->size+4)));child_patch(c,empty);
    const uint8_t finish[]={0x8b,0x47,20,0xff,0xd0,0x89,0x07,
        0xc7,0x47,4,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    child_emit(c,finish,sizeof(finish));memcpy(memory+THREAD_DEVICE_CPU,c->bytes,c->size);
}
static uint32_t thread_device_call(unsigned ordinal,const uint32_t *args,unsigned count,unsigned life)
{
    const uint32_t context=0x1b000;CHECK(count<=7);memset(memory+context,0,96);
    MEM32(context+16)=count;MEM32(context+20)=0xfe000000+ordinal*4;
    MEM32(context+64)=THREAD_DEVICE_CPU;if(count)memcpy(memory+context+32,args,count*4);
    MEM32(context+92)=0xa5a55a5a;
    thread_cpu_create_join(THREAD_DEVICE_CPU,context,life&1);
    CHECK(MEM32(context+4)==1 && MEM32(context+92)==0xa5a55a5a);
    return MEM32(context);
}
static void test_thread_device_cpu_lle(int bad_pin)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;uint32_t revision=0;
    CHECK(!usb_access(0xfed00000,0,&revision));thread_device_code();
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned life=0;life<(bad_pin?1u:3u);++life) {
        uint32_t vector[]={3,0x1c081};MEM32(0x1c080)=0xcdcdcdcd;
        CHECK(thread_device_call(44,vector,2,life)==0x33);
        CHECK(MEM32(0x1c080)==0xcdcd17cd);
        vector[0]=27;MEM32(0x1c080)=0xcdcdcdcd;
        CHECK(!thread_device_call(44,vector,2,life) && MEM32(0x1c080)==0xcdcdcdcd);
        memset(memory+0x1c000,0xcd,0x78);
        uint32_t interrupt[]={0x1c004,0x12300,0x55667788,0x33,23,0,0};
        thread_device_call(109,interrupt,7,life);
        CHECK(MEM32(0x1c000)==0xcdcdcdcd && MEM32(0x1c074)==0xcdcdcdcd);
        CHECK(MEM32(0x1c004)==0x12300 && MEM32(0x1c008)==0x55667788);
        CHECK(MEM32(0x1c00c)==3 && MEM32(0x1c010)==23);
        CHECK((thread_device_call(98,interrupt,1,life)&255)==1);
        CHECK(MEM8(0x1c014)==1);
        CHECK((thread_device_call(100,interrupt,1,life)&255)==1);
        CHECK(MEM8(0x1c014)==0);
        uint32_t event[]={0x1c100,0,1},signal[]={0x1c100,0,0};
        MEM32(0x1c0fc)=0x12345678;MEM32(0x1c110)=0x87654321;
        thread_device_call(108,event,3,life);
        CHECK(thread_device_call(138,event,1,life)==1);
        CHECK(thread_device_call(138,event,1,life)==0);
        CHECK(thread_device_call(145,signal,3,life)==0);
        CHECK(thread_device_call(145,signal,3,life)==1);
        CHECK(thread_device_call(138,event,1,life)==1);
        CHECK(MEM32(0x1c0fc)==0x12345678 && MEM32(0x1c110)==0x87654321);
        uint32_t notify[]={0x1d000,1};MEM32(notify[0])=0x12300;MEM32(notify[0]+4)=5;
        thread_device_call(47,notify,2,life);notify[1]=0;thread_device_call(47,notify,2,life);
        memset(memory+0x1e000,0,128);MEM32(0x1e400)=0xcdcdcdcd;
        uint32_t create[]={0x1e000,64,0,0x22,0,0x1e400};
        CHECK(!thread_device_call(65,create,6,life));uint32_t object=MEM32(0x1e400);
        CHECK(object && MEM16(object)==3 && MEM16(object+2)==0x88);
        CHECK(MEM32(object+8)==0x1e000 && MEM32(object+0x18)==object+0x48);
        for(unsigned i=0;i<64;++i)CHECK(!MEM8(object+0x48+i));
        MEM32(object+0x48)=0x12345678;
        uint32_t pin[]={object+0x48,64,0};thread_device_call(175,pin,3,life);
        uint32_t pa=thread_device_call(173,pin,1,life),observed=0;
        CHECK(pa && pilot_io_device_pinned_dma(pa,&observed,4,0) && observed==0x12345678);
        pin[2]=1;thread_device_call(175,pin,3,life);
        observed=0xaabbccdd;CHECK(!pilot_io_device_pinned_dma(pa,&observed,4,0) && observed==0xaabbccdd);
        if(bad_pin) {
            const uint32_t context=0x1b000;memset(memory+context,0,96);
            MEM32(context+16)=3;MEM32(context+20)=0xfe000000+175*4;
            memcpy(memory+context+32,pin,sizeof(pin));MEM32(context+64)=THREAD_DEVICE_CPU;
            uint32_t out=0x19800,birth[]={out,0,THREAD_DEVICE_CPU,context,0};
            CHECK(!event_lle_call(254,birth));uint32_t handle=MEM32(out);
            uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t thread=MEM32(out+4);
            uint32_t join[]={thread,0,0,0,0},result=0;
            const char *error=pilot_io_device_lle(159,memory,join,&result);
            fprintf(stderr,"[DEVICE PIN GUARD] %s\n",error?error:"returned without error");
            CHECK(error && !strcmp(error,"device LLE unmatched pin range or capacity"));
            CHECK(!MEM32(context+4) && MEM32(object+0x48)==0x12345678);
            fprintf(stderr,"PASS: original-thread duplicate unlock stops before caller or device mutation\n");return;
        }
        thread_device_call(68,&object,1,life);
        for(unsigned ordinal=14;ordinal<=15;++ordinal) {
            uint32_t allocation[]={72,0x584c4c45};uint32_t pool=thread_device_call(ordinal,allocation,ordinal==14?1:2,life);
            CHECK(pool);MEM32(pool)=0x87654321;CHECK(MEM32(pool)==0x87654321);
            thread_device_call(17,&pool,1,life);
        }
        CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    fprintf(stderr,"PASS: three original-thread HAL/IRQ/device/pool lifetimes, exact byte outputs, real pin/DMA/unpin and settled handles\n");
}
