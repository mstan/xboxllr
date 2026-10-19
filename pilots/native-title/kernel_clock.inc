/* Native-only shared monotonic clock owner. dispatch_ensure must run before
 * initialize; no guest IRQ/DPC/APC or scheduler lease is used by publication. */
#if !defined(PILOT_KERNEL_CLOCK_NATIVE) || !PILOT_KERNEL_CLOCK_NATIVE
#error Native kernel clock requires its immutable native build profile
#endif
#if defined(PILOT_SCHEDULING_LLE) || defined(PILOT_IO_DEVICE_LLE) || defined(PILOT_USB_LLE)
#error Original scheduler/device clock ownership requires its own qualified bridge
#endif
#include "kernel_clock.h"
enum {KERNEL_CLOCK_OFF,KERNEL_CLOCK_STARTING,KERNEL_CLOCK_RUNNING,KERNEL_CLOCK_STOPPING,KERNEL_CLOCK_STOPPED,KERNEL_CLOCK_FAULT};
static SRWLOCK kernel_clock_lock=SRWLOCK_INIT,kernel_clock_lifecycle=SRWLOCK_INIT;
static PilotKernelClock kernel_clock_model;
static unsigned kernel_clock_phase;
static uint32_t kernel_clock_tick_address,kernel_clock_published_tick;
static HANDLE kernel_clock_thread,kernel_clock_timer,kernel_clock_stop,kernel_clock_ready;
static DWORD kernel_clock_worker_error;

static uint64_t kernel_clock_sample_locked(uint64_t now)
{
    uint64_t elapsed=pilot_kernel_clock_sample(&kernel_clock_model,now);
    uint32_t tick=(uint32_t)(elapsed/10000); /* public DWORD wraps after49.7days */
    if(kernel_clock_tick_address && tick!=kernel_clock_published_tick) {
        LONG bits;memcpy(&bits,&tick,4);
        InterlockedExchange((volatile LONG*)(memory+kernel_clock_tick_address),bits);
        kernel_clock_published_tick=tick;
    }
    return elapsed;
}
static DWORD WINAPI kernel_clock_publish(void *argument)
{
    (void)argument;
    HANDLE waits[2]={kernel_clock_stop,kernel_clock_timer};
    for(;;) {
        AcquireSRWLockExclusive(&kernel_clock_lock);
        if(kernel_clock_phase==KERNEL_CLOCK_STOPPING){ReleaseSRWLockExclusive(&kernel_clock_lock);return 0;}
        uint64_t now=dispatch_monotonic();kernel_clock_sample_locked(now);
        uint64_t deadline=pilot_kernel_clock_next_tick(&kernel_clock_model);
        uint64_t delay=deadline>kernel_clock_model.latest?deadline-kernel_clock_model.latest:1;
        LARGE_INTEGER due;due.QuadPart=-(LONGLONG)delay;
        if(!SetWaitableTimer(kernel_clock_timer,&due,0,NULL,NULL,FALSE)) {
            DWORD error=GetLastError();unsigned previous=kernel_clock_phase;
            kernel_clock_worker_error=error;kernel_clock_phase=KERNEL_CLOCK_FAULT;
            ReleaseSRWLockExclusive(&kernel_clock_lock);SetEvent(kernel_clock_ready);
            if(previous==KERNEL_CLOCK_STARTING)return error;
            stop("native kernel clock publication failed",error);
        }
        int starting=kernel_clock_phase==KERNEL_CLOCK_STARTING;
        kernel_clock_phase=KERNEL_CLOCK_RUNNING;
        ReleaseSRWLockExclusive(&kernel_clock_lock);
        if(starting && !SetEvent(kernel_clock_ready))stop("native kernel clock ready publication failed",GetLastError());
        DWORD result=WaitForMultipleObjects(2,waits,FALSE,INFINITE);
        if(result==WAIT_OBJECT_0)return 0;
        if(result!=WAIT_OBJECT_0+1) {
            DWORD error=GetLastError();AcquireSRWLockExclusive(&kernel_clock_lock);
            kernel_clock_worker_error=error;kernel_clock_phase=KERNEL_CLOCK_FAULT;
            ReleaseSRWLockExclusive(&kernel_clock_lock);
            stop("native kernel clock wait failed",error);
        }
    }
}
static void kernel_clock_initialize(uint32_t tick_address,uint64_t origin)
{
    if(tick_address && ((tick_address&3u) || tick_address>UINT32_MAX-3))stop("invalid native tick backing",tick_address);
    if(origin>dispatch_monotonic())stop("native clock epoch is in the future",0);
    AcquireSRWLockExclusive(&kernel_clock_lifecycle);
    AcquireSRWLockExclusive(&kernel_clock_lock);
    if(kernel_clock_phase!=KERNEL_CLOCK_OFF || kernel_clock_model.initialized)stop("native clock epoch is immutable",0);
    ReleaseSRWLockExclusive(&kernel_clock_lock);
    HANDLE ready=CreateEventW(NULL,TRUE,FALSE,NULL),finish=CreateEventW(NULL,TRUE,FALSE,NULL);
    /* High-resolution timer is an optional Windows facility. Normal waitable
     * timer fallback keeps the same elapsed-time model at host wake granularity. */
    HANDLE timer=CreateWaitableTimerExW(NULL,NULL,0x2,TIMER_ALL_ACCESS);
    if(!timer)timer=CreateWaitableTimerW(NULL,FALSE,NULL);
    HANDLE worker=NULL;
    if(ready && finish && timer)worker=CreateThread(NULL,0,kernel_clock_publish,NULL,CREATE_SUSPENDED,NULL);
    if(!worker) {
        DWORD error=GetLastError();if(ready)CloseHandle(ready);if(finish)CloseHandle(finish);if(timer)CloseHandle(timer);
        ReleaseSRWLockExclusive(&kernel_clock_lifecycle);stop("native clock publisher creation failed",error);
    }
    AcquireSRWLockExclusive(&kernel_clock_lock);
    if(!pilot_kernel_clock_initialize(&kernel_clock_model,origin))stop("native clock initialization failed",0);
    kernel_clock_ready=ready;kernel_clock_stop=finish;kernel_clock_timer=timer;kernel_clock_thread=worker;
    kernel_clock_tick_address=tick_address;kernel_clock_published_tick=0;
    if(tick_address)InterlockedExchange((volatile LONG*)(memory+tick_address),0);
    kernel_clock_phase=KERNEL_CLOCK_STARTING;
    ReleaseSRWLockExclusive(&kernel_clock_lock);
    if(ResumeThread(worker)==(DWORD)-1)stop("native clock publisher resume failed",GetLastError());
    if(WaitForSingleObject(ready,INFINITE)!=WAIT_OBJECT_0)stop("native clock publisher startup wait failed",GetLastError());
    AcquireSRWLockShared(&kernel_clock_lock);
    int running=kernel_clock_phase==KERNEL_CLOCK_RUNNING;DWORD error=kernel_clock_worker_error;
    ReleaseSRWLockShared(&kernel_clock_lock);ReleaseSRWLockExclusive(&kernel_clock_lifecycle);
    if(!running)stop("native clock publisher startup failed",error);
    fprintf(stderr,"[KERNEL CLOCK] native-monotonic-v1; PM=%uHz TSC=%uHz tick=1ms; independent host publication, no guest IRQ\n",
        PILOT_CLOCK_PM_HZ,PILOT_CLOCK_TSC_HZ);
}
static uint64_t kernel_clock_counter(uint32_t frequency)
{
    AcquireSRWLockExclusive(&kernel_clock_lock);
    if(kernel_clock_phase!=KERNEL_CLOCK_RUNNING)stop("native clock owner is not running",kernel_clock_phase);
    uint64_t elapsed=kernel_clock_sample_locked(dispatch_monotonic()),counter;
    if(!pilot_kernel_clock_scale(elapsed,frequency,&counter))stop("native clock counter overflow",frequency);
    ReleaseSRWLockExclusive(&kernel_clock_lock);return counter;
}
static uint64_t kernel_clock_tsc_counter(void){return kernel_clock_counter(PILOT_CLOCK_TSC_HZ);}
static int kernel_clock_dispatch(unsigned ordinal)
{
    if(ordinal!=126 && ordinal!=127)return 0;
    if((uint64_t)g_esp+4>UINT64_C(0x100000000))stop("invalid native clock argument frame",g_esp);
    uint64_t value;
    if(ordinal==126)value=kernel_clock_counter(PILOT_CLOCK_PM_HZ);
    else {
        AcquireSRWLockShared(&kernel_clock_lock);
        if(kernel_clock_phase!=KERNEL_CLOCK_RUNNING)stop("native clock owner is not running",kernel_clock_phase);
        ReleaseSRWLockShared(&kernel_clock_lock);value=PILOT_CLOCK_PM_HZ;
    }
    g_eax=(uint32_t)value;g_edx=(uint32_t)(value>>32);g_esp+=4;return 1;
}
static void kernel_clock_shutdown(void)
{
    AcquireSRWLockExclusive(&kernel_clock_lifecycle);
    AcquireSRWLockExclusive(&kernel_clock_lock);
    if(kernel_clock_phase==KERNEL_CLOCK_OFF || kernel_clock_phase==KERNEL_CLOCK_STOPPED) {
        ReleaseSRWLockExclusive(&kernel_clock_lock);ReleaseSRWLockExclusive(&kernel_clock_lifecycle);return;
    }
    if(GetThreadId(kernel_clock_thread)==GetCurrentThreadId())stop("native clock publisher cannot join itself",0);
    kernel_clock_phase=KERNEL_CLOCK_STOPPING;
    if(!SetEvent(kernel_clock_stop))stop("native clock stop signal failed",GetLastError());
    HANDLE worker=kernel_clock_thread;
    ReleaseSRWLockExclusive(&kernel_clock_lock);
    if(WaitForSingleObject(worker,INFINITE)!=WAIT_OBJECT_0)stop("native clock publisher join failed",GetLastError());
    AcquireSRWLockExclusive(&kernel_clock_lock);
    CloseHandle(kernel_clock_thread);CloseHandle(kernel_clock_timer);CloseHandle(kernel_clock_stop);CloseHandle(kernel_clock_ready);
    kernel_clock_thread=kernel_clock_timer=kernel_clock_stop=kernel_clock_ready=NULL;
    kernel_clock_tick_address=0;kernel_clock_phase=KERNEL_CLOCK_STOPPED;
    ReleaseSRWLockExclusive(&kernel_clock_lock);ReleaseSRWLockExclusive(&kernel_clock_lifecycle);
}
