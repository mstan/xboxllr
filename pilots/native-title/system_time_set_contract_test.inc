/* Console time changes must move wall deadlines, never host time or duration
 * clocks. Real worker waits expose ownership/races rather than mocking clocks. */
static uint32_t time_test_object=0x1b000,time_test_timeout=0x1b100;
static volatile LONG time_test_result;
static DWORD WINAPI time_test_wait(void *unused)
{
    (void)unused;
    uint32_t result=dispatch_wait(&time_test_object,1,0,0,time_test_timeout);
    InterlockedExchange(&time_test_result,(LONG)result);return 0;
}
static HANDLE time_test_wait_start(int64_t due)
{
    dispatch_initialize(time_test_object,0);MEM64(time_test_timeout)=(uint64_t)due;
    InterlockedExchange(&time_test_result,-1);
    HANDLE thread=CreateThread(NULL,0,time_test_wait,NULL,0,NULL);CHECK(thread);
    uint64_t limit=dispatch_monotonic()+50000000;
    for(;;) {
        AcquireSRWLockExclusive(&dispatch_lock);
        int found=0;for(struct PilotWaiter *w=dispatch_waiters;w;w=w->next)
            if(w->objects==&time_test_object)found=1;
        ReleaseSRWLockExclusive(&dispatch_lock);
        if(found)break;CHECK(dispatch_monotonic()<limit);Sleep(1);
    }
    return thread;
}
static void time_test_set(uint64_t desired)
{
    MEM64(0x1b200)=desired;uint32_t args[]={0x1b200,0x1b208};
    call_kernel(228,args,2);CHECK(!g_eax);
}
static DWORD WINAPI time_test_native_wait(void *parameter)
{
    g_esp=0x1c000;MEM32(g_esp)=0x12340;
    MEM32(g_esp+4)=(uint32_t)(uintptr_t)parameter;MEM32(g_esp+8)=0;
    MEM32(g_esp+12)=time_test_timeout;
    wait_single_kernel(233);return g_eax;
}
static void test_system_time_set(int guard)
{
    dispatch_ensure();
    if(guard) {
        uint32_t event=native_wait_event(0);
        MEM64(time_test_timeout)=dispatch_wall_time()+600000000;
        CHECK(CreateThread(NULL,0,time_test_native_wait,(void*)(uintptr_t)event,0,NULL));
        uint64_t limit=dispatch_monotonic()+50000000;
        for(;;) {
            AcquireSRWLockExclusive(&dispatch_lock);unsigned active=dispatch_native_absolute_waits;
            ReleaseSRWLockExclusive(&dispatch_lock);
            if(active)break;CHECK(dispatch_monotonic()<limit);Sleep(1);
        }
        time_test_set(dispatch_wall_time());CHECK(0);
    }
    uint64_t host=dispatch_host_wall_time(),mono=dispatch_monotonic();
    memset(memory+0x1b400,0xa7,16);
    uint32_t refurb[]={0x1b400,16,0};
    call_kernel(25,refurb,3);CHECK(g_eax==0xc00000bbu);
    refurb[2]=0xffff0001u;call_kernel(25,refurb,3);CHECK(g_eax==0xc00000bbu);
    for(unsigned i=0;i<16;++i)CHECK(MEM8(0x1b400+i)==0xa7);
    refurb[1]=15;call_kernel(25,refurb,3);CHECK(g_eax==0xc000000du);
    refurb[0]=0;refurb[1]=16;call_kernel(25,refurb,3);CHECK(g_eax==0xc0000005u);
    uint32_t args[]={0,0};call_kernel(228,args,2);CHECK(g_eax==0xc0000005u);
    args[0]=0x1b200;args[1]=0x30000;MEM64(args[0])=host;
    call_kernel(228,args,2);CHECK(g_eax==0xc0000005u && !dispatch_wall_offset);
    args[1]=0;
    const uint64_t invalid[]={0,UINT64_MAX,UINT64_C(0x2000000100000000)};
    for(unsigned i=0;i<3;++i) {
        MEM64(args[0])=invalid[i];call_kernel(228,args,2);
        CHECK(g_eax==0xc000000du && !dispatch_wall_offset);
    }
    /* Aliasing captures the requested time before publishing previous time. */
    args[1]=args[0];MEM64(args[0])=host+864000000000ull;
    call_kernel(228,args,2);CHECK(!g_eax && MEM64(args[0])>=host);
    CHECK(MEM64(args[0])<host+100000000);
    uint32_t query[]={0x1b218};call_kernel(128,query,1);
    CHECK(MEM64(query[0])>=host+864000000000ull);
    CHECK(dispatch_host_wall_time()-host<100000000 && dispatch_monotonic()-mono<100000000);

    /* Already armed absolute timers retain console deadlines across backward
     * and forward changes; relative timers retain their QPC deadline exactly. */
    const uint32_t absolute_timer=0x1b300,relative_timer=0x1b340;
    dispatch_initialize(absolute_timer,8);dispatch_initialize(relative_timer,8);
    uint64_t wall=dispatch_wall_time();
    dispatch_set_timer(absolute_timer,(int64_t)(wall+36000000000ull),0,0);
    dispatch_set_timer(relative_timer,-50000000,0,0);
    uint64_t relative_deadline=0;
    AcquireSRWLockExclusive(&dispatch_lock);
    for(unsigned i=0;i<128;++i)if(dispatch_timers[i].va==relative_timer)relative_deadline=dispatch_timers[i].deadline;
    ReleaseSRWLockExclusive(&dispatch_lock);CHECK(relative_deadline);
    time_test_set(wall-36000000000ull);CHECK(!MEM32(absolute_timer+4));
    time_test_set(wall+72000000000ull);CHECK(MEM32(absolute_timer+4)==1);
    AcquireSRWLockExclusive(&dispatch_lock);
    for(unsigned i=0;i<128;++i)if(dispatch_timers[i].va==relative_timer)CHECK(dispatch_timers[i].deadline==relative_deadline);
    ReleaseSRWLockExclusive(&dispatch_lock);CHECK(dispatch_cancel_timer(relative_timer));

    wall=dispatch_wall_time();
    HANDLE thread=time_test_wait_start((int64_t)(wall+100000000));
    time_test_set(wall-100000000);CHECK(WaitForSingleObject(thread,20)==WAIT_TIMEOUT);
    time_test_set(wall+200000000);
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0 && time_test_result==0x102 && CloseHandle(thread));
    /* Relative blocking waits ignore both directions of wall-clock changes. */
    uint64_t relative_start=dispatch_monotonic();
    thread=time_test_wait_start(-3000000);
    time_test_set(dispatch_wall_time()+864000000000ull);
    CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0 && time_test_result==0x102 && CloseHandle(thread));
    CHECK(dispatch_monotonic()-relative_start>=2900000);

    /* Actual NtWaitForSingleObject converts a guest absolute deadline after
     * setting time; an already expired deadline must not block for a day. */
    uint32_t event=native_wait_event(0),timeout=0x1b228;
    MEM64(timeout)=dispatch_wall_time()-10000000;
    uint32_t wait[]={event,0,timeout};call_kernel(233,wait,3);CHECK(g_eax==0x102 && !dispatch_native_absolute_waits);
    call_kernel(187,&event,1);CHECK(!g_eax);
    time_test_set(dispatch_host_wall_time());
    CHECK(dispatch_host_wall_time()-host<100000000);
    fprintf(stderr,"PASS console system-time ABI, aliases/errors, host/monotonic isolation, absolute timer/wait rebasing and relative deadline preservation\n");
}
