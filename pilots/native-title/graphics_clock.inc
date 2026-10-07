/* Independent whole-API vblank wait. One display epoch, separate waiter timers:
 * an early return/re-wait cannot consume another thread's notification. The
 * nominal 60 Hz matches the current machine LLE clock; it is not host refresh. */
static uint64_t graphics_clock_origin;
static unsigned graphics_clock_hz,graphics_clock_waiters;
static uint32_t graphics_callback;
static int graphics_clock_closing,graphics_clock_presented;
static void graphics_vblank_deliver(uint64_t frame)
{
    AcquireSRWLockExclusive(&graphics_lock);
    uint32_t callback=graphics_callback;
    uint32_t data=graphics_headers+96;
    if(callback) {
        MEM32(data)=(uint32_t)frame;MEM32(data+4)=0;MEM32(data+8)=0;
    }
    ReleaseSRWLockExclusive(&graphics_lock);
    if(!callback)return;
    uint32_t top=MEM32(g_fs_base+4);
    g_esp=top-8;MEM32(g_esp)=0;MEM32(g_esp+4)=data;
    recomp_func_t routine=recomp_lookup(callback);
    if(!routine)stop("vblank callback is not generated",callback);
    routine();
    if(g_esp!=top-4)stop("vblank cdecl callback stack imbalance",callback);
    g_esp+=4;
    fprintf(stderr,"[VBLANK CALLBACK] frame=%llu routine=%08x\n",(unsigned long long)frame,callback);
}
/* Caller holds graphics_lock. Registration is the original pointer-store
 * contract; a callback already running may finish after replacement/removal. */
static void graphics_set_vblank_callback(uint32_t callback)
{
    if(graphics_clock_closing)stop("callback registration during device close",callback);
    if(callback && graphics_clock_presented)stop("post-presentation vblank callback data unqualified",callback);
    if(callback && !recomp_lookup(callback))stop("vblank callback is not generated",callback);
    uint32_t previous=graphics_callback;graphics_callback=callback;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VBLANK_CALLBACK_OFFSET)=callback;
    if(!previous || !callback)dispatch_pulse_set(graphics_clock_origin,graphics_clock_hz,callback?graphics_vblank_deliver:NULL);
}
static void graphics_clock_close(void)
{
    graphics_clock_closing=1;graphics_callback=0;
    if(dispatch_changed) {
        dispatch_pulse_set(0,0,NULL);
        ReleaseSRWLockExclusive(&graphics_lock);
        dispatch_pulse_quiesce();
        AcquireSRWLockExclusive(&graphics_lock);
    }
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VBLANK_CALLBACK_OFFSET)=0;
}
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
 * submitted work is discarded; callbacks run independently on the DPC worker. */
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
