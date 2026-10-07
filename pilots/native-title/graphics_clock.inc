/* Independent whole-API vblank wait. One display epoch, separate waiter timers:
 * an early return/re-wait cannot consume another thread's notification. The
 * nominal 60 Hz matches the current machine LLE clock; it is not host refresh. */
static uint64_t graphics_clock_origin;
static unsigned graphics_clock_hz,graphics_clock_waiters;
static uint32_t graphics_callback;
static int graphics_clock_closing,graphics_clock_presented;
static struct GraphicsPresent {uint64_t serial,target;uint32_t swap;} graphics_presents[2];
static unsigned graphics_present_count;
static uint32_t graphics_swap_submitted,graphics_swap_completed;
static uint64_t graphics_swap_deadline;
static CONDITION_VARIABLE graphics_present_changed=CONDITION_VARIABLE_INIT;
static uint64_t graphics_frame_at(uint64_t now)
{
    uint64_t elapsed=now-graphics_clock_origin;
    return elapsed/10000000*graphics_clock_hz+(elapsed%10000000)*graphics_clock_hz/10000000;
}
static uint32_t graphics_swap_flags(unsigned retired,int missed)
{return retired?1u:missed?2u:0u;}
static void graphics_vblank_deliver(uint64_t frame)
{
    AcquireSRWLockExclusive(&graphics_lock);
    unsigned retired=0;
    /* Late DPC delivery must not assign newly observed GPU completion to an
     * already elapsed historical vblank while the worker catches up. */
    uint64_t observed_frame=graphics_frame_at(dispatch_monotonic());
    while(graphics_present_count && graphics_presents[0].target<=frame && frame>=observed_frame) {
        struct GraphicsPresent *present=&graphics_presents[0];
        if(pilot_gfx_checkpoint_busy(present->serial))break;
        graphics_swap_completed=present->swap;++retired;
        memmove(graphics_presents,graphics_presents+1,--graphics_present_count*sizeof(*graphics_presents));
    }
    uint32_t flags=graphics_swap_flags(retired,graphics_present_count && graphics_presents[0].target<=frame);
    if(retired)WakeAllConditionVariable(&graphics_present_changed);
    uint32_t callback=graphics_callback;
    uint32_t data=graphics_headers+96;
    if(callback) {
        MEM32(data)=(uint32_t)frame;MEM32(data+4)=graphics_swap_completed;MEM32(data+8)=flags;
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
    fprintf(stderr,"[VBLANK CALLBACK] frame=%llu routine=%08x swap=%u flags=%u\n",(unsigned long long)frame,callback,MEM32(data+4),MEM32(data+8));
}
/* Caller holds graphics_lock. Registration is the original pointer-store
 * contract; a callback already running may finish after replacement/removal. */
static void graphics_set_vblank_callback(uint32_t callback)
{
    if(graphics_clock_closing)stop("callback registration during device close",callback);
    if(callback && !recomp_lookup(callback))stop("vblank callback is not generated",callback);
    uint32_t previous=graphics_callback;graphics_callback=callback;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VBLANK_CALLBACK_OFFSET)=callback;
    if(!graphics_clock_presented && (!previous || !callback))
        dispatch_pulse_set(graphics_clock_origin,graphics_clock_hz,callback?graphics_vblank_deliver:NULL);
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
/* One native presentation per virtual display interval; at most two actual
 * GPU checkpoints may remain outstanding. Waiting releases the graphics lock,
 * allowing the display/DPC worker to report real progress independently. */
static uint32_t graphics_present(void)
{
    if(graphics_clock_closing || dispatch_in_dpc)stop("swap in closing device or DPC",0);
    ++graphics_clock_waiters;
    for(;;) {
        if(graphics_present_count==2) {
            if(!SleepConditionVariableSRW(&graphics_present_changed,&graphics_lock,INFINITE,0))
                stop("presentation queue wait failed",GetLastError());
            continue;
        }
        if(dispatch_monotonic()>=graphics_swap_deadline)break;
        uint64_t deadline=graphics_swap_deadline;
        ReleaseSRWLockExclusive(&graphics_lock);graphics_wait_until(deadline);AcquireSRWLockExclusive(&graphics_lock);
    }
    --graphics_clock_waiters;
    uint64_t submitted=dispatch_monotonic(),target=graphics_frame_at(submitted)+1;
    int32_t hr=pilot_gfx_present();
    if(hr!=0)stop("native presentation was not accepted",(uint32_t)hr);
    uint64_t serial=pilot_gfx_checkpoint();
    uint32_t swap=++graphics_swap_submitted;
    graphics_presents[graphics_present_count++]=(struct GraphicsPresent){serial,target,swap};
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SWAP_COUNTER)=swap;
    graphics_swap_deadline=graphics_next_vblank(graphics_clock_origin,submitted,graphics_clock_hz);
    if(!graphics_clock_presented) {
        graphics_clock_presented=1;
        if(!graphics_callback)dispatch_pulse_set(graphics_clock_origin,graphics_clock_hz,graphics_vblank_deliver);
    }
    fprintf(stderr,"[PRESENT SUBMIT] swap=%u target=%llu serial=%llu time=%llu\n",swap,
        (unsigned long long)target,(unsigned long long)serial,(unsigned long long)submitted);
    return swap;
}
