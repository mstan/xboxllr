/* Generated-body equivalents execute on the actual original kernel stack.
 * Thread creation/dispatch/termination/reaping are never replaced by fixtures. */
static unsigned thread_lle_calls,thread_lle_arguments,thread_lle_tls;
static uint32_t thread_lle_object,thread_lle_stack;
static int thread_lle_reject;
static void test_thread_lle_body(void)
{
    CHECK(g_fs_base==0x8003da3cu && !pilot_io_device_irql() && !dispatch_in_dpc);
    CHECK(MEM32(g_fs_base+0x28)==thread_lle_object);
    CHECK(MEM32(g_esp+4)==0x13579bdf);
    CHECK(g_esp>=MEM32(thread_lle_object+0x20) && g_esp<MEM32(thread_lle_object+0x1c));
    thread_lle_stack=MEM32(thread_lle_object+0x20);
    uint32_t tls=MEM32(thread_lle_object+0x28);
    if(thread_lle_tls) {
        CHECK(tls>=thread_lle_stack && (uint64_t)tls+thread_lle_tls<=MEM32(thread_lle_object+0x1c));
        memset(memory+tls,0x5a,thread_lle_tls);
    } else CHECK(!tls);
    ++thread_lle_calls;
    if(thread_lle_arguments==2)memset(memory+thread_lle_object+0x140,0xbc,32);
    if(thread_lle_reject) {
        uint32_t wait[]={thread_lle_object,0,0,0,0},result=0xcccccccc;
        const char *error=pilot_io_device_lle(159,memory,wait,&result);
        CHECK(error && strstr(error,"continuation handoff") && result==0xcccccccc);
        g_esp+=8;return;
    }
    uint32_t sp=g_esp;
    g_esp-=4;MEM32(g_esp)=0x12500;next_kernel_ordinal=121;kernel_dispatch();
    CHECK(g_eax==0 && g_esp==sp);
    g_eax=0xaaaaaaaa;g_fp_stack[0]=-19;g_fp_top=7;memset(&g_xmm0,0x42,sizeof(g_xmm0));
    if(thread_lle_arguments==2) {
        CHECK(MEM32(g_esp+8)==0x2468ace0);
        /* A real SystemRoutine terminates instead of returning. */
        g_esp-=8;MEM32(g_esp)=0x12500;MEM32(g_esp+4)=0x12345678;
        next_kernel_ordinal=258;kernel_dispatch();CHECK(0);
    }
    g_esp+=8;
}
static void test_thread_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned lifetime=0;lifetime<72;++lifetime) {
        uint32_t out=0x19600;
        thread_lle_arguments=lifetime%3?2:1;
        thread_lle_tls=thread_lle_arguments==2 && lifetime%4?(lifetime%2?64:8192):0;
        uint32_t simple[]={out,0,0x12500,0x13579bdf,0};
        uint32_t extra[]={out,32,lifetime%2?0:0x7000,thread_lle_tls,out+4,
            0x13579bdf,0x2468ace0,1,0,0x12500};
        CHECK(!event_lle_call(thread_lle_arguments==1?254:255,thread_lle_arguments==1?simple:extra));
        uint32_t handle=MEM32(out),ref[]={handle,0,out+8};
        CHECK(!event_lle_call(246,ref));thread_lle_object=MEM32(out+8);
        uint32_t priority[]={thread_lle_object,30};CHECK(event_lle_call(148,priority)==8);
        CHECK(!MEM32(thread_lle_object+4) && thread_lle_calls==lifetime);
        if(thread_lle_arguments==2) {
            CHECK(MEM32(out+4)==MEM32(thread_lle_object+0x12c));
            uint32_t resume[]={handle,out+12};
            CHECK(!event_lle_call(231,resume) && MEM32(out+12)==1);
            CHECK(!event_lle_call(224,resume) && MEM32(out+12)==2);
            CHECK(!event_lle_call(224,resume) && MEM32(out+12)==1);
        }
        int close_first=lifetime%2;
        if(close_first)CHECK(!event_lle_call(187,&handle));
        uint32_t wait[]={thread_lle_object,0,0,0,0};
        g_eax=0x12345678;g_fp_stack[0]=3.25;g_fp_top=2;memset(&g_xmm0,0xab,sizeof(g_xmm0));
        uint32_t sp=g_esp;RecompXmm xmm=g_xmm0;
        if(thread_lle_reject) {
            uint32_t result=0xcccccccc;
            const char *error=pilot_io_device_lle(159,memory,wait,&result);
            CHECK(error && strstr(error,"continuation handoff") && result==0xcccccccc);
            CHECK(thread_lle_calls==1);
            fprintf(stderr,"PASS: root-service API rejects a native worker without its guest call frame; original join is not completed\n");return;
        }
        CHECK(!event_lle_call(159,wait));
        CHECK(thread_lle_calls==lifetime+1 && MEM32(thread_lle_object+4)==1);
        CHECK(MEM32(thread_lle_object+0x120)==(thread_lle_arguments==2?0x12345678u:0));
        if(thread_lle_arguments==2)
            for(unsigned i=0;i<32;++i)CHECK(MEM8(thread_lle_object+0x140+i)==0xbc);
        CHECK(g_eax==0x12345678 && g_esp==sp && g_fp_stack[0]==3.25 && g_fp_top==2);
        CHECK(g_fs_base==0x19000 && !memcmp(&g_xmm0,&xmm,sizeof(xmm)));
        MEMORY_BASIC_INFORMATION view;
        CHECK(VirtualQuery(memory+thread_lle_stack,&view,sizeof(view))==sizeof(view) && view.State!=MEM_COMMIT);
        if(!close_first)CHECK(!event_lle_call(187,&handle));
        event_lle_call(250,&thread_lle_object);
        MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    fprintf(stderr,"PASS: 72 original threads into generated native bodies, normal/system termination, original stack/TLS/PCR, suspended start, join/close/reference lifetimes and stable original thread/handle counts\n");
}
