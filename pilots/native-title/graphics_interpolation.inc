/* A fixed host presentation profile, selected before frame zero. The worker
 * cannot enter guest code or wait for graphics_lock. TryAcquire makes closing
 * and joining it safe while the graphics owner holds that lock. */
static HANDLE graphics_interpolation_stop,graphics_interpolation_thread;
static DWORD WINAPI graphics_interpolation_worker(void *parameter)
{
    unsigned rate=(unsigned)(uintptr_t)parameter;
    HANDLE timer=CreateWaitableTimerExW(NULL,NULL,2,TIMER_ALL_ACCESS);
    if(!timer)timer=CreateWaitableTimerW(NULL,FALSE,NULL);
    if(!timer)stop("interpolation timer creation failed",GetLastError());
    uint64_t origin=dispatch_monotonic(),frame=0;
    HANDLE events[]={graphics_interpolation_stop,timer};
    for(;;) {
        uint64_t now=dispatch_monotonic();
        uint64_t next=origin+(++frame)*10000000ull/rate;
        if(next<=now){frame=(now-origin)*rate/10000000ull+1;next=origin+frame*10000000ull/rate;}
        LARGE_INTEGER due;due.QuadPart=-(int64_t)(next>now?next-now:1);
        if(!SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE))stop("interpolation timer setup failed",GetLastError());
        DWORD event=WaitForMultipleObjects(2,events,FALSE,INFINITE);
        if(event==WAIT_OBJECT_0)break;
        if(event!=WAIT_OBJECT_0+1)stop("interpolation wait failed",GetLastError());
        if(TryAcquireSRWLockExclusive(&graphics_lock)) {
            int32_t result=pilot_gfx_interpolate(0);
            ReleaseSRWLockExclusive(&graphics_lock);
            if(result<0)stop("interpolated presentation failed",(uint32_t)result);
        }
    }
    CancelWaitableTimer(timer);CloseHandle(timer);return 0;
}
static void graphics_interpolation_start(void)
{
    unsigned rate=pilot_gfx_interpolation_rate();if(!rate || graphics_interpolation_thread)return;
    graphics_interpolation_stop=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!graphics_interpolation_stop)stop("interpolation stop event creation failed",GetLastError());
    graphics_interpolation_thread=CreateThread(NULL,0,graphics_interpolation_worker,(void*)(uintptr_t)rate,0,NULL);
    if(!graphics_interpolation_thread)stop("interpolation worker creation failed",GetLastError());
    fprintf(stderr,"[INTERPOLATION] blended presentation target=%u; guest cadence unchanged; one source-frame latency\n",rate);
}
static void graphics_interpolation_close(void)
{
    if(!graphics_interpolation_thread)return;
    if(!SetEvent(graphics_interpolation_stop) || WaitForSingleObject(graphics_interpolation_thread,5000)!=WAIT_OBJECT_0)
        stop("interpolation worker did not close",GetLastError());
    CloseHandle(graphics_interpolation_thread);CloseHandle(graphics_interpolation_stop);
    graphics_interpolation_thread=graphics_interpolation_stop=NULL;
}
