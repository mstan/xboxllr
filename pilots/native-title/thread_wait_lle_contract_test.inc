/* Two live native C stacks, selected exclusively by original wait/signal and
 * priority transitions. One thread may resume before its peer's SetEvent
 * returns: nested synchronous callbacks cannot implement this contract. */
static uint32_t thread_wait_trace[256],thread_wait_trace_count,thread_wait_finished[2];
static uint32_t thread_wait_call(unsigned ordinal,const uint32_t *args,unsigned count)
{
    uint32_t sp=g_esp;g_esp-=(count+1)*4;
    MEM32(g_esp)=0x12580;
    if(count)memcpy(memory+g_esp+4,args,count*4);
    next_kernel_ordinal=ordinal;kernel_dispatch();
    CHECK(g_esp==sp);return g_eax;
}
static void test_thread_wait_body(void)
{
    uint32_t context=MEM32(g_esp+4),id=MEM32(context),rounds=MEM32(context+12);
    uint32_t saved_ebx=g_ebx,canary=0x789abc00u+id;
    CHECK(g_fs_base==0x8003da3cu && MEM32(g_fs_base+0x28)==MEM32(context+16));
    g_ebx=canary;g_fp_stack[0]=id?17.5:-9.25;g_fp_top=(int)id;
    memset(&g_xmm0,(int)(0xab+id),sizeof(g_xmm0));RecompXmm xmm=g_xmm0;
    if(MEM32(context+24)) {
        uint32_t suspend[]={id?MEM32(context+28):0xfffffffeu,context+20};
        CHECK(!thread_wait_call(id?224:231,suspend,2) && MEM32(context+20)==id);
    }
    uint32_t wait[]={MEM32(context+4),0,0},signal[]={MEM32(context+8),context+20};
    for(uint32_t round=0;round<rounds;++round) {
        CHECK(!thread_wait_call(233,wait,3));
        CHECK(g_ebx==canary && g_fp_top==(int)id && g_fp_stack[0]==(id?17.5:-9.25));
        CHECK(!memcmp(&xmm,&g_xmm0,sizeof(xmm)) && MEM32(g_fs_base+0x28)==MEM32(context+16));
        CHECK(thread_wait_trace_count<256);
        thread_wait_trace[thread_wait_trace_count++]=(round<<1)|id;
        MEM32(context+20)=0xcccccccc;
        CHECK(!thread_wait_call(225,signal,2) && !MEM32(context+20));
        CHECK(g_ebx==canary && g_fp_top==(int)id && !memcmp(&xmm,&g_xmm0,sizeof(xmm)));
    }
    thread_wait_finished[id]=1;g_ebx=saved_ebx;g_esp+=8;
}
static void test_thread_wait_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    /* The native scheduler is enabled for the enclosing caller, but never
     * chooses an original worker or completes one of its waits. */
    scheduler_enabled=1;scheduler_publish(0,0,0);scheduler_enter(0);
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    DWORD host_baseline=0;
    for(unsigned lifetime=0;lifetime<9;++lifetime) {
        const uint32_t rounds=24;
        uint32_t out=0x19800,events[2],handles[2],objects[2];
        thread_wait_trace_count=0;memset(thread_wait_finished,0,sizeof(thread_wait_finished));
        for(unsigned id=0;id<2;++id) {
            uint32_t create[]={out,0,1,!id};CHECK(!event_lle_call(189,create));events[id]=MEM32(out);
        }
        for(unsigned id=0;id<2;++id) {
            uint32_t context=0x19900+id*32;
            MEM32(context)=id;MEM32(context+4)=events[id];MEM32(context+8)=events[1-id];MEM32(context+12)=rounds;
            MEM32(context+24)=lifetime%3==0;
            uint32_t create[]={out,0,0x12540,context,0};CHECK(!event_lle_call(254,create));handles[id]=MEM32(out);
            uint32_t ref[]={handles[id],0,out+4};CHECK(!event_lle_call(246,ref));objects[id]=MEM32(out+4);
            MEM32(context+16)=objects[id];
            uint32_t priority[]={objects[id],lifetime%3==0?(id?29:30):lifetime%3==1?(id?30:29):30};
            CHECK(event_lle_call(148,priority)==8);
        }
        MEM32(0x19920+28)=handles[0];
        uint32_t join[]={objects[0],0,0,0,0};CHECK(!event_lle_call(159,join));
        CHECK(thread_wait_finished[0]);
        if(lifetime%3==0)CHECK(!thread_wait_finished[1] && thread_wait_trace_count==rounds*2-1);
        join[0]=objects[1];CHECK(!event_lle_call(159,join));
        CHECK(thread_wait_finished[1] && thread_wait_trace_count==rounds*2);
        for(unsigned i=0;i<rounds*2;++i)CHECK(thread_wait_trace[i]==i);
        for(unsigned id=0;id<2;++id) {
            CHECK(!event_lle_call(187,handles+id));event_lle_call(250,objects+id);
            CHECK(!event_lle_call(187,events+id));
        }
        CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
        DWORD count;CHECK(GetProcessHandleCount(GetCurrentProcess(),&count));
        if(lifetime)CHECK(count==host_baseline);else host_baseline=count;
    }
    scheduler_retire();scheduler_enabled=0;
    fprintf(stderr,"PASS: 18 original/native workers, 432 wait/signal rounds, self-suspend/peer resume, higher/lower/equal priorities, non-LIFO service returns, private register/FP state, real join/cleanup and stable original/host handles\n");
}

static uint32_t thread_timeout_ticks,thread_timeout_done,thread_timeout_cases;
static uint64_t thread_interrupt_time(void)
{
    uint32_t low=thread_wait_call(125,NULL,0);
    return ((uint64_t)g_edx<<32)|low;
}
static void test_thread_timeout_body(void)
{
    uint32_t context=MEM32(g_esp+4);
    if(MEM32(context)) {
        /* Explicit virtual clock is an external test input, not a replaced
         * kernel operation. A real runnable peer keeps unrelated captured
         * boot workers out of this bounded two-thread qualification. */
        while(!thread_timeout_done) {
            CHECK(++thread_timeout_ticks<=128);
            CHECK(!pilot_usb_pit_time((uint64_t)thread_timeout_ticks*1125));
            (void)thread_interrupt_time(); // actual IRQ0 may preempt this call
        }
    } else {
        const uint32_t durations[]={1,9999,10000,10001,100000,330000};
        for(unsigned ex=0;ex<2;++ex)for(unsigned i=0;i<6;++i) {
            MEM64(context+8)=(uint64_t)-(int64_t)durations[i];
            uint32_t wait[]={MEM32(context+4),0,context+8},wait_ex[]={MEM32(context+4),0,0,context+8};
            uint64_t before=thread_interrupt_time();
            uint32_t ticks=thread_timeout_ticks;
            CHECK(thread_wait_call(ex?234:233,ex?wait_ex:wait,ex?4:3)==0x102);
            uint64_t elapsed=thread_interrupt_time()-before;
            CHECK(thread_timeout_ticks-ticks==1+durations[i]/10000);
            CHECK(elapsed==(uint64_t)(1+durations[i]/10000)*10000);
            ++thread_timeout_cases;
        }
        thread_timeout_done=1;
    }
    g_esp+=8;
}
static void test_thread_timeout_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    uint32_t command=0x34,low=0x65,high=4;
    CHECK(!pilot_usb_pic_port(0x43,1,1,&command));
    CHECK(!pilot_usb_pic_port(0x40,1,1,&low));CHECK(!pilot_usb_pic_port(0x40,1,1,&high));
    uint32_t out=0x19800,create_event[]={out,0,1,0};
    CHECK(!event_lle_call(189,create_event));uint32_t event=MEM32(out),handles[2],objects[2];
    for(unsigned id=0;id<2;++id) {
        uint32_t context=0x19900+id*32;MEM32(context)=id;MEM32(context+4)=event;
        uint32_t create[]={out,0,0x125c0,context,0};CHECK(!event_lle_call(254,create));handles[id]=MEM32(out);
        uint32_t ref[]={handles[id],0,out+4};CHECK(!event_lle_call(246,ref));objects[id]=MEM32(out+4);
        uint32_t priority[]={objects[id],id?29:30};CHECK(event_lle_call(148,priority)==8);
    }
    for(unsigned id=0;id<2;++id) {
        uint32_t wait[]={objects[id],0,0,0,0};CHECK(!event_lle_call(159,wait));
        CHECK(!event_lle_call(187,handles+id));event_lle_call(250,objects+id);
    }
    CHECK(thread_timeout_cases==12 && thread_timeout_ticks==102 && pilot_usb_pic_vector(0)<0);
    CHECK(!event_lle_call(187,&event));
    fprintf(stderr,"PASS: 12 blocked native waits expire through 102 real PIT/PIC/original IRQ0 deliveries and original scheduling, with actual 64-bit clock returns and peer resumption\n");
}
