/* Independent whole-API vblank wait. One display epoch, separate waiter timers:
 * an early return/re-wait cannot consume another thread's notification. The
 * nominal 60 Hz matches the current machine LLE clock; it is not host refresh. */
static uint64_t graphics_clock_origin;
static unsigned graphics_clock_hz,graphics_clock_waiters;
/* Display-event lifetime is independent of renderer ownership. Original XDK
 * BlockUntilVerticalBlank submits no GPU work; PCRTC IRQ progress also survives
 * a stalled FIFO in the runnable LLE fixture. Never acquire graphics_lock while
 * holding this lock. Creation/close take graphics_lock, then this short lock. */
static SRWLOCK graphics_display_lock=SRWLOCK_INIT;
static unsigned graphics_display_waiters;
static int graphics_display_open,graphics_display_profile;
static uint32_t graphics_callback;
static int graphics_clock_closing,graphics_clock_presented;
static struct GraphicsPresent {uint64_t serial,target;uint32_t swap;} graphics_presents[2];
static unsigned graphics_present_count;
static unsigned graphics_present_retired;
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
static void graphics_present_interval(uint32_t value)
{
    if(value!=1 && value!=0x80000000u)stop("unqualified presentation interval",value);
    if(graphics_parameters[12]!=value)graphics_swap_deadline=0;
    graphics_parameters[12]=value;
}
/* target==0 is an immediate swap. Its real GPU event, rather than the display
 * epoch, permits retirement. Paced swaps are retired only by the vblank path. */
static unsigned graphics_present_retire(uint64_t frame)
{
    unsigned retired=0;
    while(graphics_present_count && graphics_presents[0].target<=frame) {
        struct GraphicsPresent *present=&graphics_presents[0];
        if(pilot_gfx_checkpoint_busy(present->serial))break;
        graphics_swap_completed=present->swap;++retired;
        memmove(graphics_presents,graphics_presents+1,--graphics_present_count*sizeof(*graphics_presents));
    }
    if(retired)WakeAllConditionVariable(&graphics_present_changed);
    return retired;
}
static void graphics_present_drain(void)
{
    ++graphics_clock_waiters;
    for(;;) {
        graphics_present_retired+=graphics_present_retire(0);
        if(!graphics_present_count)break;
        if(!SleepConditionVariableSRW(&graphics_present_changed,&graphics_lock,1,0) && GetLastError()!=ERROR_TIMEOUT)
            stop("presentation drain wait failed",GetLastError());
    }
    --graphics_clock_waiters;
}
static void graphics_vblank_deliver(uint64_t frame)
{
    uint64_t entered=graphics_service_profile_enabled()?dispatch_monotonic():0;
    AcquireSRWLockExclusive(&graphics_lock);
    uint64_t acquired=entered?dispatch_monotonic():0;
    unsigned retired=0;
    /* Late DPC delivery must not assign newly observed GPU completion to an
     * already elapsed historical vblank while the worker catches up. */
    uint64_t observed_frame=graphics_frame_at(dispatch_monotonic());
    if(frame>=observed_frame) {
        retired=graphics_present_retired+graphics_present_retire(frame);
        graphics_present_retired=0;
    }
    uint32_t flags=graphics_swap_flags(retired,graphics_present_count && graphics_presents[0].target && graphics_presents[0].target<=frame);
    uint32_t callback=graphics_callback;
    uint32_t data=graphics_headers+96;
    if(callback) {
        MEM32(data)=(uint32_t)frame;MEM32(data+4)=graphics_swap_completed;MEM32(data+8)=flags;
    }
    uint64_t released=entered?dispatch_monotonic():0;
    ReleaseSRWLockExclusive(&graphics_lock);
    if(entered && released-entered>500000)
        fprintf(stderr,"[VBLANK DPC SLOW] thread=%lu begin=%llu acquired=%llu end=%llu\n",
            GetCurrentThreadId(),(unsigned long long)entered,(unsigned long long)acquired,(unsigned long long)released);
    if(!callback)return;
    uint32_t top=MEM32(g_fs_base+4);
    g_esp=top-8;MEM32(g_esp)=0;MEM32(g_esp+4)=data;
    recomp_func_t routine=recomp_lookup(callback);
    if(!routine)stop("vblank callback is not generated",callback);
    scheduler_callback_enter();routine();scheduler_retire();
    if(g_esp!=top-4)stop("vblank cdecl callback stack imbalance",callback);
    g_esp+=4;
    if(pilot_event_trace_enabled())fprintf(stderr,"[VBLANK CALLBACK] frame=%llu routine=%08x swap=%u flags=%u\n",(unsigned long long)frame,callback,MEM32(data+4),MEM32(data+8));
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
    AcquireSRWLockExclusive(&graphics_display_lock);
    if(graphics_display_waiters)stop("device release with active vertical-blank waits",graphics_display_waiters);
    graphics_display_open=0;
    ReleaseSRWLockExclusive(&graphics_display_lock);
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
    uint64_t started=graphics_service_profile_enabled()?dispatch_monotonic():0;
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
    if(started) {
        uint64_t ended=dispatch_monotonic();
        if(ended-started>500000)fprintf(stderr,"[DISPLAY TIMER SLOW] thread=%lu requested_ms=%.3f elapsed_ms=%.3f\n",
            GetCurrentThreadId(),((int64_t)deadline-(int64_t)started)/10000.0,(ended-started)/10000.0);
    }
}
/* Whole display-event API: its lifetime and deadline do not need renderer
 * ownership or completion. Real GPU callbacks/presents retain their ordering
 * elsewhere; this call reports only an elapsed display edge. */
static void graphics_wait_vblank(void)
{
    AcquireSRWLockExclusive(&graphics_display_lock);
    if(!graphics_display_open)stop("vertical-blank wait without active display",0);
    int profile=graphics_display_profile;
    uint64_t started=dispatch_monotonic();
    uint64_t deadline=graphics_next_vblank(graphics_clock_origin,started,graphics_clock_hz);
    ++graphics_display_waiters;
    ReleaseSRWLockExclusive(&graphics_display_lock);
    graphics_wait_until(deadline);
    uint64_t awakened=profile?dispatch_monotonic():0;
    AcquireSRWLockExclusive(&graphics_display_lock);
    --graphics_display_waiters;
    ReleaseSRWLockExclusive(&graphics_display_lock);
    if(profile) {
        uint64_t returned=dispatch_monotonic();
        if(returned-started>500000)
            fprintf(stderr,"[VBLANK WAIT] thread=%lu priority=%d start=%llu deadline=%llu awake=%llu returned=%llu\n",
                GetCurrentThreadId(),GetThreadPriority(GetCurrentThread()),(unsigned long long)started,
                (unsigned long long)deadline,(unsigned long long)awakened,(unsigned long long)returned);
    }
    g_eax=0; /* Original nonalertable KeWaitForSingleObject returns STATUS_SUCCESS. */
}
/* Optional direct GPU capture avoids desktop occlusion. Diagnostic readback
 * is never enabled implicitly and is unsuitable for performance measurements. */
static void graphics_capture_present(void)
{
    const char *capture=getenv("XLLE_PILOT_PRESENT_CAPTURE");
    if(!capture || !*capture)return;
    static uint64_t next,interval;static unsigned sequence;
    if(!interval) {
        const char *setting=getenv("XLLE_PILOT_PRESENT_CAPTURE_INTERVAL_MS");
        unsigned milliseconds=setting?(unsigned)strtoul(setting,NULL,10):1000;
        if(milliseconds<100 || milliseconds>60000)stop("present capture interval outside range",milliseconds);
        interval=(uint64_t)milliseconds*10000;
    }
    uint64_t now=dispatch_monotonic();if(sequence>=180 || now<next)return;
    unsigned width=graphics_parameters[0],height=graphics_parameters[1];
    unsigned char *pixels=malloc(width*height*4);char path[1024];
    if(!pixels || snprintf(path,sizeof(path),"%s/present-%u.ppm",capture,++sequence)>=(int)sizeof(path))stop("present capture allocation/path",0);
    if(pilot_gfx_readback(pixels,width*height*4)<0)stop("present capture readback",0);
    FILE *file=fopen(path,"wb");if(!file || fprintf(file,"P6\n%u %u\n255\n",width,height)<0)stop("present capture open",0);
    for(unsigned i=0;i<width*height;++i)if(fwrite(pixels+i*4,3,1,file)!=1)stop("present capture write",0);
    free(pixels);if(fclose(file))stop("present capture close",0);
    next=dispatch_monotonic()+interval;
    fprintf(stderr,"[PRESENT CAPTURE] sequence=%u time=%llu file=%s\n",sequence,(unsigned long long)now,path);
}
/* Interval-one swaps follow the display clock; immediate swaps bypass it.
 * At most two actual GPU checkpoints may remain outstanding. Waiting releases the graphics lock,
 * allowing the display/DPC worker to report real progress independently. */
static uint32_t graphics_present(void)
{
    if(graphics_clock_closing || dispatch_in_dpc)stop("swap in closing device or DPC",0);
    graphics_present_interval(MEM32(PILOT_D3D_PRESENT_INTERVAL));
    ++graphics_clock_waiters;
    for(;;) {
        graphics_present_retired+=graphics_present_retire(0);
        if(graphics_present_count==2) {
            if(!SleepConditionVariableSRW(&graphics_present_changed,&graphics_lock,1,0) && GetLastError()!=ERROR_TIMEOUT)
                stop("presentation queue wait failed",GetLastError());
            continue;
        }
        if(graphics_parameters[12]==0x80000000u || dispatch_monotonic()>=graphics_swap_deadline)break;
        uint64_t deadline=graphics_swap_deadline;
        ReleaseSRWLockExclusive(&graphics_lock);graphics_wait_until(deadline);AcquireSRWLockExclusive(&graphics_lock);
    }
    --graphics_clock_waiters;
    graphics_capture_present();
    uint64_t submitted=dispatch_monotonic(),target=graphics_parameters[12]==0x80000000u?0:graphics_frame_at(submitted)+1;
    int32_t hr=pilot_gfx_present();
    if(hr!=0)stop("native presentation was not accepted",(uint32_t)hr);
    uint64_t serial=pilot_gfx_checkpoint();
    uint32_t swap=++graphics_swap_submitted;
    graphics_movie_capture_present(submitted,swap,serial);
    graphics_presents[graphics_present_count++]=(struct GraphicsPresent){serial,target,swap};
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SWAP_COUNTER)=swap;
    graphics_swap_deadline=target?graphics_next_vblank(graphics_clock_origin,submitted,graphics_clock_hz):0;
    if(!graphics_clock_presented) {
        graphics_clock_presented=1;
        if(!graphics_callback)dispatch_pulse_set(graphics_clock_origin,graphics_clock_hz,graphics_vblank_deliver);
    }
    uint64_t logging=graphics_service_profile_enabled()?dispatch_monotonic():0;
    if(pilot_event_trace_enabled())fprintf(stderr,"[PRESENT SUBMIT] swap=%u target=%llu serial=%llu time=%llu\n",swap,
        (unsigned long long)target,(unsigned long long)serial,(unsigned long long)submitted);
    if(logging) {
        uint64_t ended=dispatch_monotonic();
        if(ended-logging>500000)fprintf(stderr,"[PRESENT LOG SLOW] elapsed_ms=%.3f\n",(ended-logging)/10000.0);
    }
    return swap;
}
