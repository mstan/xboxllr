struct ClockWaitTest { HANDLE ready,go; uint64_t deadline,returned; };
static LONG vblank_test_count;
static uint32_t vblank_test_previous;
static int vblank_test_mode;
static HANDLE vblank_test_done,vblank_test_entered,vblank_test_release;
static void test_vblank_callback(void)
{
    CHECK(dispatch_in_dpc && MEM8(g_fs_base+0x24)==2);
    uint32_t data=MEM32(g_esp+4),frame=MEM32(data);
    CHECK(data==graphics_headers+96 && !MEM32(data+4) && !MEM32(data+8));
    if(vblank_test_previous)CHECK(frame==vblank_test_previous+1);
    vblank_test_previous=frame;
    LONG count=InterlockedIncrement(&vblank_test_count);
    if(vblank_test_mode==1 && count==1) {
        CHECK(SetEvent(vblank_test_entered));
        CHECK(WaitForSingleObject(vblank_test_release,2000)==WAIT_OBJECT_0);
    }
    if(vblank_test_mode==2 && count==3) {
        /* Reentrant self-unregister through the actual guest stdcall ABI. */
        uint32_t saved=g_esp;g_esp-=8;MEM32(g_esp)=0x12340;MEM32(g_esp+4)=0;
        pilot_d3d_dispatch(PILOT_D3D_D3DDevice_SetVerticalBlankCallback);CHECK(g_esp==saved);
    }
    if(count>=6 || (vblank_test_mode==2 && count==3))CHECK(SetEvent(vblank_test_done));
    g_eax=0xbadc0de;g_esp+=4; /* cdecl: caller removes the one argument */
}
static void test_graphics_callbacks(void)
{
    /* Original miniport observations from native_vblank_reference.py: the
     * expected-swap marker survives queue drain and advances on missed edges. */
    graphics_swap_expected=0;
    CHECK(graphics_swap_flags(0,1)==0 && graphics_swap_flags(0,2)==0);
    graphics_swap_expected=7;
    CHECK(graphics_swap_flags(1,6)==1 && graphics_swap_expected==7);
    CHECK(graphics_swap_flags(0,7)==2 && graphics_swap_expected==8);
    CHECK(graphics_swap_flags(0,8)==2 && graphics_swap_expected==9);
    graphics_swap_expected=8; /* Immediate swap at frame 8. */
    CHECK(graphics_swap_flags(0,9)==0);
    graphics_swap_expected=0;
    dispatch_ensure();DWORD before,after;
    vblank_test_done=CreateEventW(NULL,TRUE,FALSE,NULL);
    vblank_test_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
    vblank_test_release=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(vblank_test_done && vblank_test_entered && vblank_test_release);
    uint32_t callback=0x12300;vblank_test_count=0;vblank_test_previous=0;vblank_test_mode=0;
    uint64_t begin=dispatch_monotonic();g_eax=0x1234567;
    call_graphics(PILOT_D3D_D3DDevice_SetVerticalBlankCallback,&callback);
    CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VBLANK_CALLBACK_OFFSET)==callback);
    CHECK(WaitForSingleObject(vblank_test_done,2000)==WAIT_OBJECT_0);
    CHECK(g_eax==0x1234567 && vblank_test_count>=6 && dispatch_monotonic()-begin>=5*10000000/60);
    /* Replacement retains phase and does not enqueue a second clock. */
    callback=0x12320;call_graphics(PILOT_D3D_D3DDevice_SetVerticalBlankCallback,&callback);
    callback=0;call_graphics(PILOT_D3D_D3DDevice_SetVerticalBlankCallback,&callback);dispatch_pulse_quiesce();
    LONG stopped=vblank_test_count;Sleep(40);CHECK(vblank_test_count==stopped);
    /* Warm the worker's CRT/host state before measuring repeated lifetimes. */
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));

    /* A callback can be removed while in flight; only that invocation finishes. */
    ResetEvent(vblank_test_done);vblank_test_count=0;vblank_test_previous=0;vblank_test_mode=1;
    callback=0x12300;call_graphics(PILOT_D3D_D3DDevice_SetVerticalBlankCallback,&callback);
    CHECK(WaitForSingleObject(vblank_test_entered,2000)==WAIT_OBJECT_0);
    callback=0;call_graphics(PILOT_D3D_D3DDevice_SetVerticalBlankCallback,&callback);
    CHECK(SetEvent(vblank_test_release));dispatch_pulse_quiesce();Sleep(40);CHECK(vblank_test_count==1);
    /* Callback execution may call graphics services without a host lock cycle. */
    vblank_test_mode=2;vblank_test_count=0;vblank_test_previous=0;
    callback=0x12300;call_graphics(PILOT_D3D_D3DDevice_SetVerticalBlankCallback,&callback);
    CHECK(WaitForSingleObject(vblank_test_done,2000)==WAIT_OBJECT_0);dispatch_pulse_quiesce();
    CHECK(vblank_test_count==3 && !graphics_callback);
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after));
    fprintf(stderr,"[VBLANK HANDLES] before=%lu after=%lu\n",before,after);
    CHECK(before==after);
    CHECK(CloseHandle(vblank_test_done) && CloseHandle(vblank_test_entered) && CloseHandle(vblank_test_release));
    fprintf(stderr,"PASS: real phased vblank callbacks, cdecl data/TLS/IRQL, replacement, in-flight removal and reentrant unregister\n");
}
static DWORD WINAPI test_callback_release(void *unused)
{(void)unused;Sleep(30);CHECK(SetEvent(vblank_test_release));return 0;}
static void test_graphics_callback_close(void)
{
    vblank_test_mode=1;vblank_test_count=0;vblank_test_previous=0;
    vblank_test_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
    vblank_test_release=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(vblank_test_entered && vblank_test_release);
    uint32_t callback=0x12300;
    call_graphics(PILOT_D3D_D3DDevice_SetVerticalBlankCallback,&callback);
    CHECK(WaitForSingleObject(vblank_test_entered,2000)==WAIT_OBJECT_0);
    HANDLE release=CreateThread(NULL,0,test_callback_release,NULL,0,NULL);CHECK(release);
    CHECK(call_graphics(PILOT_D3D_D3DDevice_Release,NULL)==0);
    CHECK(WaitForSingleObject(release,2000)==WAIT_OBJECT_0 && vblank_test_count==1 && !graphics_callback);
    CHECK(CloseHandle(release) && CloseHandle(vblank_test_entered) && CloseHandle(vblank_test_release));
    fprintf(stderr,"PASS: device teardown waits for the actual in-flight callback before freeing its data\n");
}
static DWORD WINAPI test_clock_waiter(void *context)
{
    struct ClockWaitTest *t=context;
    CHECK(SetEvent(t->ready));CHECK(WaitForSingleObject(t->go,2000)==WAIT_OBJECT_0);
    graphics_wait_until(t->deadline);t->returned=dispatch_monotonic();
    return 0;
}
static DWORD WINAPI test_display_wait_without_renderer(void *context)
{
    HANDLE done=context;
    CHECK(call_graphics(PILOT_D3D_D3DDevice_BlockUntilVerticalBlank,NULL)==0);
    CHECK(SetEvent(done));return 0;
}
static void test_graphics_clock(void)
{
    const uint64_t origin=100000000;
    CHECK(graphics_next_vblank(origin,origin,60)==origin+166667);
    CHECK(graphics_next_vblank(origin,origin+166666,60)==origin+166667);
    CHECK(graphics_next_vblank(origin,origin+166667,60)==origin+333334);
    CHECK(graphics_next_vblank(origin,origin+9999999,60)==origin+10000000);
    CHECK(graphics_next_vblank(origin,origin+10000000,60)==origin+10166667);
    CHECK(graphics_next_vblank(origin,origin+10000000ull*86400*365,60)==origin+10000000ull*86400*365+166667);
    unsigned timer_before[3],timer_after[3];test_clock_handle_snapshot(timer_before);
    DWORD handles_before,handles_after;CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles_before));
    struct ClockWaitTest waits[3]={0};HANDLE workers[3];
    HANDLE go=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(go!=NULL);
    for(unsigned i=0;i<3;++i) {
        waits[i].go=go;waits[i].ready=CreateEventW(NULL,FALSE,FALSE,NULL);CHECK(waits[i].ready!=NULL);
        workers[i]=CreateThread(NULL,0,test_clock_waiter,&waits[i],0,NULL);CHECK(workers[i]!=NULL);
        CHECK(WaitForSingleObject(waits[i].ready,2000)==WAIT_OBJECT_0);
    }
    uint64_t deadline=dispatch_monotonic()+1000000;
    for(unsigned i=0;i<3;++i)waits[i].deadline=deadline;
    CHECK(SetEvent(go));CHECK(WaitForMultipleObjects(3,workers,TRUE,2000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<3;++i) {
        CHECK(waits[i].returned>=deadline && waits[i].returned-deadline<10000000);
        CHECK(CloseHandle(workers[i]) && CloseHandle(waits[i].ready));
    }
    CHECK(CloseHandle(go));
    /* The original entry only clears/waits for a display notification. A
     * different thread owning the renderer must not gate that notification. */
    HANDLE done=CreateEventW(NULL,TRUE,FALSE,NULL);CHECK(done);
    AcquireSRWLockExclusive(&graphics_lock);
    HANDLE independent=CreateThread(NULL,0,test_display_wait_without_renderer,done,0,NULL);CHECK(independent);
    CHECK(WaitForSingleObject(done,2000)==WAIT_OBJECT_0);
    ReleaseSRWLockExclusive(&graphics_lock);
    CHECK(WaitForSingleObject(independent,2000)==WAIT_OBJECT_0);
    CHECK(CloseHandle(independent) && CloseHandle(done));
    uint64_t start=dispatch_monotonic();
    for(unsigned i=0;i<18;++i) {
        uint64_t before=dispatch_monotonic();
        uint64_t expected=graphics_next_vblank(graphics_clock_origin,before,graphics_clock_hz);
        CHECK(call_graphics(PILOT_D3D_D3DDevice_BlockUntilVerticalBlank,NULL)==0);
        CHECK(dispatch_monotonic()>=expected);
    }
    uint64_t elapsed=dispatch_monotonic()-start;
    CHECK(elapsed>=17*10000000/60 && elapsed<20000000);
    CHECK(!graphics_clock_waiters && !graphics_display_waiters && GetProcessHandleCount(GetCurrentProcess(),&handles_after));
    if(handles_after!=handles_before)fprintf(stderr,"[VBLANK TEST HANDLES] before=%lu after=%lu\n",handles_before,handles_after);
    test_clock_handle_snapshot(timer_after);
    CHECK(timer_after[2]==timer_before[2]);
    CHECK(timer_after[0]-timer_before[0]==22 && timer_after[1]-timer_before[1]==22);
    fprintf(stderr,"[VBLANK TEST] 18 actual guest waits in %.3f ms; phase, concurrent waiters, ABI and handle lifetime passed\n",elapsed/10000.0);
}
