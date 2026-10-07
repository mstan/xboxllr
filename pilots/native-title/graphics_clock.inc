/* Independent whole-API vblank wait. One display epoch, separate waiter timers:
 * an early return/re-wait cannot consume another thread's notification. The
 * nominal 60 Hz matches the current machine LLE clock; it is not host refresh. */
static uint64_t graphics_clock_origin;
static unsigned graphics_clock_hz,graphics_clock_waiters;
static uint64_t graphics_next_vblank(uint64_t origin,uint64_t now,unsigned hz)
{
    if(!hz || hz>1000 || now<origin)stop("invalid display clock",hz);
    uint64_t elapsed=now-origin;
    uint64_t frame=elapsed/10000000*hz+(elapsed%10000000)*hz/10000000+1;
    return origin+frame/hz*10000000+((frame%hz)*10000000+hz-1)/hz;
}
static void graphics_wait_until(uint64_t deadline)
{
    HANDLE timer=CreateWaitableTimerExW(NULL,NULL,0x2,TIMER_ALL_ACCESS);
    if(!timer && GetLastError()==ERROR_INVALID_PARAMETER)timer=CreateWaitableTimerW(NULL,FALSE,NULL);
    if(!timer)stop("display timer creation failed",GetLastError());
    for(;;) {
        uint64_t now=dispatch_monotonic();
        if(now>=deadline)break;
        LARGE_INTEGER due;due.QuadPart=-(LONGLONG)(deadline-now);
        if(!SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE) || WaitForSingleObject(timer,INFINITE)!=WAIT_OBJECT_0)
            stop("display timer wait failed",GetLastError());
    }
    if(!CloseHandle(timer))stop("display timer close failed",GetLastError());
}
/* Called with graphics_lock held; returns with it held. Native rendering and
 * other waiters can proceed while this caller sleeps. No guest callback or
 * submitted work is discarded; callback registration remains an explicit stop. */
static void graphics_wait_vblank(void)
{
    uint64_t deadline=graphics_next_vblank(graphics_clock_origin,dispatch_monotonic(),graphics_clock_hz);
    ++graphics_clock_waiters;
    ReleaseSRWLockExclusive(&graphics_lock);
    graphics_wait_until(deadline);
    AcquireSRWLockExclusive(&graphics_lock);
    --graphics_clock_waiters;
    g_eax=0; /* Original nonalertable KeWaitForSingleObject returns STATUS_SUCCESS. */
}
