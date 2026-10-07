struct ClockWaitTest { HANDLE ready,go; uint64_t deadline,returned; };
static DWORD WINAPI test_clock_waiter(void *context)
{
    struct ClockWaitTest *t=context;
    CHECK(SetEvent(t->ready));CHECK(WaitForSingleObject(t->go,2000)==WAIT_OBJECT_0);
    graphics_wait_until(t->deadline);t->returned=dispatch_monotonic();
    return 0;
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
    uint64_t start=dispatch_monotonic();
    for(unsigned i=0;i<18;++i) {
        uint64_t before=dispatch_monotonic();
        uint64_t expected=graphics_next_vblank(graphics_clock_origin,before,graphics_clock_hz);
        CHECK(call_graphics(PILOT_D3D_D3DDevice_BlockUntilVerticalBlank,NULL)==0);
        CHECK(dispatch_monotonic()>=expected);
    }
    uint64_t elapsed=dispatch_monotonic()-start;
    CHECK(elapsed>=17*10000000/60 && elapsed<20000000);
    CHECK(!graphics_clock_waiters && GetProcessHandleCount(GetCurrentProcess(),&handles_after));
    CHECK(handles_after==handles_before);
    fprintf(stderr,"[VBLANK TEST] 18 actual guest waits in %.3f ms; phase, concurrent waiters, ABI and handle lifetime passed\n",elapsed/10000.0);
}
