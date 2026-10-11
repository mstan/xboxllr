/* Authored original x86 bytes execute on the original scheduler's sole CPU.
 * The native body only submits its unchanged entry/frame and awaits return. */
#define THREAD_CPU_ARITHMETIC 0x12740u
#define THREAD_CPU_LOOP 0x127a0u
#define THREAD_CPU_FP 0x12840u
static unsigned thread_cpu_calls,thread_cpu_peer_done;
static uint32_t thread_cpu_context;
static void test_thread_cpu_body(void)
{
    uint32_t context=MEM32(g_esp+4),entry=MEM32(context+64);
    uint32_t regs[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi};
    uint32_t original[8];memcpy(original,regs,sizeof(regs));
    CHECK(pilot_io_device_thread_execute(entry+1,regs));
    CHECK(!memcmp(regs,original,sizeof(regs)));
    uint32_t invalid[8];memcpy(invalid,regs,sizeof(regs));invalid[4]=0x10000;
    CHECK(pilot_io_device_thread_execute(entry,invalid));
    CHECK(invalid[4]==0x10000 && !memcmp(regs,original,sizeof(regs)));
    const char *execution_error=pilot_io_device_thread_execute(entry,regs);
    if(execution_error)fprintf(stderr,"[RAW CPU FAILURE] entry=%08x reason=%s\n",entry,execution_error);
    CHECK(!execution_error);
    uint32_t returned[8];memcpy(returned,regs,sizeof(regs));
    CHECK(pilot_io_device_thread_execute(entry,regs));
    CHECK(!memcmp(returned,regs,sizeof(regs)));
    g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_ebx=regs[3];
    g_esp=regs[4];g_ebp=regs[5];g_esi=regs[6];g_edi=regs[7];
    ++thread_cpu_calls;
    int terminated=0;uint32_t status=0;
    CHECK(!pilot_io_device_thread_completion(&terminated,&status));
    if(terminated){original_thread_exit.status=status;longjmp(original_thread_exit.target,1);}
}
static void test_thread_cpu_peer(void)
{
    uint32_t context=MEM32(g_esp+4);
    MEM64(context+8)=(uint64_t)-(int64_t)330000;
    uint32_t wait[]={MEM32(context+4),0,context+8};
    CHECK(thread_wait_call(233,wait,3)==0x102);
    CHECK(MEM32(thread_cpu_context+24));
    MEM32(thread_cpu_context)=1;thread_cpu_peer_done=1;
    g_eax=0xbaadf00d;g_ecx=0xf00dbaad;g_edx=0xabcdef01;
    g_esp+=8;
}
static void thread_cpu_create_join(uint32_t entry,uint32_t context,int close_first)
{
    uint32_t out=0x19800,create[]={out,0,entry,context,0};
    CHECK(!event_lle_call(254,create));uint32_t handle=MEM32(out);
    uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
    uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
    if(close_first)CHECK(!event_lle_call(187,&handle));
    uint32_t join[]={object,0,0,0,0};CHECK(!event_lle_call(159,join));
    if(!close_first)CHECK(!event_lle_call(187,&handle));
    event_lle_call(250,&object);
}
static void test_thread_cpu_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    uint32_t outside[8]={0};CHECK(pilot_io_device_thread_execute(THREAD_CPU_ARITHMETIC,outside));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    /* push EBX/EBP/ESI/EDI; load original argument; ADD; JMP to a new block;
     * PUSHFD/POP EDX; store result/flags; ADC EAX,0; store; restore; RET4. */
    const uint8_t arithmetic[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x8b,0x47,4,0x8b,0x4f,8,0x01,0xc8,0xeb,0,0x9c,0x5a,
        0x89,0x47,12,0x89,0x57,16,0x83,0xd0,0,0x89,0x47,20,
        0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    /* Register sentinels stay live while the real PIT/IRQ0 scheduler switches
     * to a higher-priority native peer after its original blocked timeout. */
    const uint8_t loop[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0xb9,0xdf,0x9b,0x57,0x13,0xba,0xe0,0xac,0x68,0x24,
        0xbb,0x98,0xba,0xdc,0xfe,0xbd,0x21,0x43,0x65,0x87,
        0xbe,0x78,0x56,0x34,0x12,
        0x83,0x3f,0,0x75,5,0xff,0x47,24,0xeb,0xf6,
        0x89,0x4f,28,0x89,0x57,32,0x89,0x5f,36,0x89,0x6f,40,0x89,0x77,44,
        0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+THREAD_CPU_ARITHMETIC,arithmetic,sizeof(arithmetic));
    memcpy(memory+THREAD_CPU_LOOP,loop,sizeof(loop));
    /* Observe original NPX startup; keep live x87/XMM values across a real
     * original wait; then keep MMX values across a second original wait.
     * FE0003A4 is the private NtWaitForSingleObject import (ordinal233).
     * Both calls use real CALL frames and original kernel return addresses. */
    const uint8_t fp[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0xd9,0x7f,4,0xdd,0x7f,6,0x0f,0xae,0x5f,8,
        0x0f,0x11,0x8f,0x90,0,0,0,
        0xd9,0xaf,0x82,0,0,0,0x0f,0xae,0x97,0x84,0,0,0,
        0x0f,0x10,0x47,16,0x0f,0x58,0x47,32,0xdd,0x47,72,0xdc,0x47,80,
        0x8d,0x87,0x88,0,0,0,0x50,0x6a,0,0xff,0x37,0xb8,0xa4,3,0,0xfe,0xff,0xd0,
        0x89,0x87,0x8c,0,0,0,
        0x0f,0x11,0x47,48,0xdd,0x5f,88,0xd9,0xbf,0x80,0,0,0,0x0f,0xae,0x5f,12,
        0x0f,0x6f,0x47,96,0x0f,0xfe,0x47,104,
        0x8d,0x87,0x88,0,0,0,0x50,0x6a,0,0xff,0x37,0xb8,0xa4,3,0,0xfe,0xff,0xd0,
        0x0f,0x7f,0x47,112,0x0f,0x77,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+THREAD_CPU_FP,fp,sizeof(fp));
    const uint32_t cases[][4]={{0x7fffffff,1,0x80000000,0x894},
        {0xffffffff,1,0,0x55},{0,0,0,0x44},{1,1,2,0},
        {0x80000000,0x80000000,0,0x845},{15,1,16,0x10}};
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned lifetime=0;lifetime<18;++lifetime) {
        const uint32_t *test=cases[lifetime%6];uint32_t context=0x19900;
        memset(memory+context,0,80);MEM32(context+4)=test[0];MEM32(context+8)=test[1];
        MEM32(context+64)=THREAD_CPU_ARITHMETIC;
        thread_cpu_create_join(THREAD_CPU_ARITHMETIC,context,lifetime&1);
        CHECK(MEM32(context+12)==test[2] && (MEM32(context+16)&0x8d5)==test[3]);
        CHECK(MEM32(context+20)==test[2]+(test[3]&1));
    }
    uint32_t command=0x34,low=0x65,high=4;
    CHECK(!pilot_usb_pic_port(0x43,1,1,&command));
    CHECK(!pilot_usb_pic_port(0x40,1,1,&low));CHECK(!pilot_usb_pic_port(0x40,1,1,&high));
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        thread_cpu_context=0x19900;thread_cpu_peer_done=0;
        memset(memory+thread_cpu_context,0,80);MEM32(thread_cpu_context+64)=THREAD_CPU_LOOP;
        uint32_t out=0x19800,event_args[]={out,0,1,0};
        CHECK(!event_lle_call(189,event_args));uint32_t event=MEM32(out),handles[2],objects[2];
        for(unsigned id=0;id<2;++id) {
            uint32_t context=id?0x19a00:thread_cpu_context;MEM32(context+4)=event;
            uint32_t create[]={out,0,id?0x12800u:THREAD_CPU_LOOP,context,0};
            CHECK(!event_lle_call(254,create));handles[id]=MEM32(out);
            uint32_t ref[]={handles[id],0,out+4};CHECK(!event_lle_call(246,ref));objects[id]=MEM32(out+4);
            uint32_t priority[]={objects[id],id?30:29};CHECK(event_lle_call(148,priority)==8);
        }
        uint64_t next=0;CHECK(!pilot_usb_pit_clock(&thread_idle_clock.base,&next));
        thread_idle_clock.origin=dispatch_monotonic();thread_idle_clock.samples=0;
        CHECK(!pilot_io_device_clock_bind(test_thread_idle_clock_input,&thread_idle_clock));
        for(unsigned id=0;id<2;++id) {
            uint32_t join[]={objects[id],0,0,0,0};CHECK(!event_lle_call(159,join));
            CHECK(!event_lle_call(187,handles+id));event_lle_call(250,objects+id);
        }
        CHECK(!pilot_io_device_clock_bind(NULL,NULL));CHECK(!event_lle_call(187,&event));
        CHECK(thread_cpu_peer_done && MEM32(thread_cpu_context+24));
        CHECK(MEM32(thread_cpu_context+28)==0x13579bdf && MEM32(thread_cpu_context+32)==0x2468ace0);
        CHECK(MEM32(thread_cpu_context+36)==0xfedcba98 && MEM32(thread_cpu_context+40)==0x87654321);
        CHECK(MEM32(thread_cpu_context+44)==0x12345678);
    }
    MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    uint32_t settled_handles=0;
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        uint32_t out=0x19800,event_args[]={out,0,1,0};
        CHECK(!event_lle_call(189,event_args));uint32_t event=MEM32(out),handles[2],objects[2];
        for(unsigned id=0;id<2;++id) {
            uint32_t context=0x19b00+id*256;memset(memory+context,0,192);
            MEM32(context)=event;MEM32(context+64)=THREAD_CPU_FP;
            float left[]={1.25f+id,2.0f+lifetime,-1,4},right[]={3,-2,.5f,0};
            memcpy(memory+context+16,left,sizeof(left));memcpy(memory+context+32,right,sizeof(right));
            double a=1.5+lifetime,b=2.25+id;
            memcpy(memory+context+72,&a,8);memcpy(memory+context+80,&b,8);
            MEM64(context+96)=0x00000002fffffffeull;MEM64(context+104)=0xffffffff00000003ull;
            MEM16(context+130)=id?0x077f:0x027f;MEM32(context+132)=id?0x3f80:0x1f80;
            MEM64(context+136)=(uint64_t)-(int64_t)(100000+id*100000);
            uint32_t create[]={out,0,THREAD_CPU_FP,context,0};
            CHECK(!event_lle_call(254,create));handles[id]=MEM32(out);
            uint32_t ref[]={handles[id],0,out+4};CHECK(!event_lle_call(246,ref));objects[id]=MEM32(out+4);
            uint32_t priority[]={objects[id],30};CHECK(event_lle_call(148,priority)==8);
        }
        uint64_t next=0;CHECK(!pilot_usb_pit_clock(&thread_idle_clock.base,&next));
        thread_idle_clock.origin=dispatch_monotonic();
        CHECK(!pilot_io_device_clock_bind(test_thread_idle_clock_input,&thread_idle_clock));
        for(unsigned id=0;id<2;++id) {
            uint32_t join[]={objects[id],0,0,0,0};CHECK(!event_lle_call(159,join));
            CHECK(!event_lle_call(187,handles+id));event_lle_call(250,objects+id);
            CHECK(event_lle_call(187,handles+id)==0xc0000008u);
            uint32_t context=0x19b00+id*256;float result[4];double real;
            memcpy(result,memory+context+48,sizeof(result));memcpy(&real,memory+context+88,8);
            CHECK(MEM16(context+4)==0x027f && MEM16(context+6)==0 && MEM32(context+8)==0x1f80);
            CHECK(MEM16(context+128)==(id?0x077f:0x027f) && MEM32(context+12)==(id?0x3f80:0x1f80));
            CHECK(result[0]==4.25f+id && result[1]==(float)lifetime && result[2]==-.5f && result[3]==4);
            CHECK(real==3.75+lifetime+id && MEM64(context+112)==0x0000000100000001ull);
            CHECK(MEM32(context+140)==0x102 && !MEM64(context+144) && !MEM64(context+152));
        }
        CHECK(!pilot_io_device_clock_bind(NULL,NULL));CHECK(!event_lle_call(187,&event));
        CHECK(event_lle_call(187,&event)==0xc0000008u);
        uint32_t live=0;CHECK(!pilot_io_device_next_thread(&live) && !live);
        MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads);
        /* The longer first idle interval lets a pre-existing captured file
         * worker close handle10. Trace identifies its original NtClose
         * callsites; it is not one of this fixture's handles. Every authored
         * handle is independently invalid above, and later batches must
         * restore the settled global count exactly. */
        if(!lifetime) {
            CHECK(MEM32(stats+8)<=baseline_handles);settled_handles=MEM32(stats+8);
        } else CHECK(MEM32(stats+8)==settled_handles);
    }
    MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    fprintf(stderr,"[ORIGINAL CPU BASELINE] threads=%u/%u handles=%u/%u\n",
        MEM32(stats+4),baseline_threads,MEM32(stats+8),baseline_handles);
    CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==settled_handles);
    CHECK(thread_cpu_calls==27);
    fprintf(stderr,"PASS: 18 original CPU ADD/ADC/PUSHFD lifetimes, three clock/preemption lifetimes, six x87/XMM/MMX workers across original waits, entry/return guards and original thread/handle baselines\n");
    test_thread_fp_compute_lle();CHECK(thread_cpu_calls==33);
}
