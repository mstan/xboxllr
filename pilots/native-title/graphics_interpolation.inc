/* A fixed host presentation profile, selected before frame zero. The worker
 * cannot enter guest code or wait for graphics_lock. TryAcquire makes closing
 * and joining it safe while the graphics owner holds that lock. */
#include <fenv.h>
static HANDLE graphics_interpolation_stop,graphics_interpolation_thread;
static volatile LONG graphics_interpolation_due;
int graphics_interpolation_cooperative_enabled=-1;
/* Worker/serialized-owner observation, not a guest or saved-state ABI.
 * Read-only sampling distinguishes simulation progress from missed presents. */
struct {uint64_t attempts,lock_misses,presents,not_ready,cooperative_presents;} graphics_interpolation_observation;
/* Called by the graphics owner while it already holds graphics_lock. A busy
 * title must not starve the only presenter by repeatedly winning that lock.
 * Service the same requested host tick at the next normal graphics boundary;
 * interpolation still consumes only owned completed source-frame images. */
static void graphics_interpolation_service(void)
{
    if(!graphics_interpolation_due || dispatch_in_dpc || !graphics_interpolation_thread)return;
    if(graphics_interpolation_cooperative_enabled<0) {
        const char *value=getenv("XLLE_PILOT_INTERPOLATION_COOPERATIVE");
        graphics_interpolation_cooperative_enabled=!(value && !strcmp(value,"0"));
    }
    if(!graphics_interpolation_cooperative_enabled || !pilot_gfx_interpolation_ready() ||
       !InterlockedExchange(&graphics_interpolation_due,0))return;
    /* Unlike the independent pacer, this is the guest CPU owner's thread.
     * Use normal host presentation rounding and restore its entire FP env. */
    fenv_t saved_fp;
    if(fegetenv(&saved_fp) || fesetround(FE_TONEAREST))stop("presentation FP environment capture failed",0);
    int32_t result=pilot_gfx_interpolate(0);
    if(fesetenv(&saved_fp))stop("presentation FP environment restore failed",0);
    if(result<0)stop("cooperative interpolated presentation failed",(uint32_t)result);
    if(result!=S_FALSE)++graphics_interpolation_observation.cooperative_presents;
    else InterlockedExchange(&graphics_interpolation_due,1);
}
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
        ++graphics_interpolation_observation.attempts;
        if(TryAcquireSRWLockExclusive(&graphics_lock)) {
            InterlockedExchange(&graphics_interpolation_due,0);
            int32_t result=pilot_gfx_interpolate(0);
            ReleaseSRWLockExclusive(&graphics_lock);
            if(result<0)stop("interpolated presentation failed",(uint32_t)result);
            if(result!=S_FALSE)++graphics_interpolation_observation.presents;
            else {
                ++graphics_interpolation_observation.not_ready;
                InterlockedExchange(&graphics_interpolation_due,1);
            }
        } else {
            ++graphics_interpolation_observation.lock_misses;
            InterlockedExchange(&graphics_interpolation_due,1);
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
    InterlockedExchange(&graphics_interpolation_due,0);
}
