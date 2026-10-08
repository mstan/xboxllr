/* Bounded single-CPU guest scheduling. Phobos 05040ec's ADX analysis motivates
 * immediate readiness, safe-point preemption and retaining the CPU across
 * nonblocking services. Independently authored against the owned-kernel queue
 * oracle in tools/native_scheduler_reference.py. No timeout changes eligibility.
 * Native device work stays asynchronous; only guest execution owns this permit.
 * Dynamic priority boosts and exact Xbox quantum length remain unqualified. */
#define SCHEDULER_CALLBACK_SLOT 64
struct PilotScheduledThread {
    int live,blocked,suspended,priority;
    uint64_t order;
    volatile uint32_t request;
};
/* Slot65 is the hardware clock's idle-CPU lease, never an ordinary ready
 * thread. A running guest services the clock at its next checkpoint. */
static struct PilotScheduledThread scheduled[66];
static SRWLOCK scheduler_lock=SRWLOCK_INIT;
static CONDITION_VARIABLE scheduler_changed=CONDITION_VARIABLE_INIT;
static int scheduler_enabled;
static unsigned scheduler_owner; /* slot + 1, zero means no guest executing */
static uint64_t scheduler_order,scheduler_since;
static RECOMP_TLS unsigned scheduler_slot,scheduler_budget;
static RECOMP_TLS int scheduler_attached;
#ifdef PILOT_USB_LLE
static volatile LONG scheduler_usb_pending;
#endif

static uint64_t scheduler_clock(void)
{
    LARGE_INTEGER now,frequency;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
    return (uint64_t)(now.QuadPart/frequency.QuadPart)*1000000+
        (uint64_t)(now.QuadPart%frequency.QuadPart)*1000000/frequency.QuadPart;
}
static int scheduler_effective_priority(int delta)
{
    /* Normal process class (base 8), as exercised by the original kernel. */
    return delta< -7?1:delta>7?15:8+delta;
}
static int scheduler_ready(unsigned slot)
{return scheduled[slot].live && !scheduled[slot].blocked && !scheduled[slot].suspended;}
static unsigned scheduler_best_locked(void)
{
    unsigned best=0;
    for(unsigned i=0;i<65;++i)if(scheduler_ready(i)) {
        if(!best || scheduled[i].priority>scheduled[best-1].priority ||
            (scheduled[i].priority==scheduled[best-1].priority && scheduled[i].order<scheduled[best-1].order))best=i+1;
    }
    return best;
}
static void scheduler_notify_locked(void)
{
    if(scheduler_owner) {
        struct PilotScheduledThread *owner=&scheduled[scheduler_owner-1];
        uint32_t request=owner->suspended?4:0;
#ifdef PILOT_USB_LLE
        if(scheduler_usb_pending)request|=8;
#endif
        for(unsigned i=0;i<65;++i)if(i+1!=scheduler_owner && scheduler_ready(i)) {
            if(scheduled[i].priority>owner->priority)request|=1;
            else if(scheduled[i].priority==owner->priority)request|=2;
        }
        InterlockedExchange((LONG*)&owner->request,(LONG)request);
    }
    WakeAllConditionVariable(&scheduler_changed);
}
static void scheduler_publish(unsigned slot,int suspended,int delta)
{
    if(!scheduler_enabled)return;
    AcquireSRWLockExclusive(&scheduler_lock);
    if(slot>=65 || scheduled[slot].live)stop("scheduler duplicate/out-of-range thread",slot);
    struct PilotScheduledThread *s=&scheduled[slot];
    s->live=1;s->blocked=0;s->suspended=suspended;
    s->priority=slot==SCHEDULER_CALLBACK_SLOT?32:scheduler_effective_priority(delta);
    s->order=++scheduler_order;scheduler_notify_locked();
    ReleaseSRWLockExclusive(&scheduler_lock);
}
static void scheduler_acquire_locked(void)
{
    while(scheduler_owner || scheduler_best_locked()!=scheduler_slot+1) {
        if(!SleepConditionVariableSRW(&scheduler_changed,&scheduler_lock,INFINITE,0))
            stop("guest CPU wait failed",GetLastError());
    }
    scheduler_owner=scheduler_slot+1;scheduler_since=scheduler_clock();scheduler_budget=256;
    scheduler_notify_locked();
}
static void scheduler_enter(unsigned slot)
{
    if(!scheduler_enabled)return;
    scheduler_attached=1;scheduler_slot=slot;
    g_pilot_suspend_count=&scheduled[slot].request;
    AcquireSRWLockExclusive(&scheduler_lock);
    scheduler_acquire_locked();ReleaseSRWLockExclusive(&scheduler_lock);
}
static void scheduler_control(unsigned slot,int suspended,int delta)
{
    if(!scheduler_enabled)return;
    AcquireSRWLockExclusive(&scheduler_lock);
    struct PilotScheduledThread *s=&scheduled[slot];
    if(s->suspended && !suspended)s->order=++scheduler_order;
    s->suspended=suspended;s->priority=scheduler_effective_priority(delta);
    scheduler_notify_locked();ReleaseSRWLockExclusive(&scheduler_lock);
}
static void scheduler_checkpoint(void)
{
    if(!scheduler_attached)return;
#ifdef PILOT_USB_LLE
    /* The original kernel owns the single guest CPU's IRQL. DPC readiness
     * cannot preempt a caller at DISPATCH_LEVEL or a hardware ISR. */
    if(pilot_io_device_irql()>=2)return;
#endif
    uint32_t request=scheduled[scheduler_slot].request;
    if(!(request&7))return;
    /* Equal-priority quantum checks are amortized across loop/function safe
     * points; higher-priority readiness and suspension are never delayed. */
    if(!(request&5) && --scheduler_budget)return;
    scheduler_budget=256;
    AcquireSRWLockExclusive(&scheduler_lock);
    if(scheduler_owner!=scheduler_slot+1)stop("guest executed without CPU ownership",scheduler_slot);
    unsigned best=scheduler_best_locked();
    int yield=scheduled[scheduler_slot].suspended ||
        (best && scheduled[best-1].priority>scheduled[scheduler_slot].priority);
    if(!yield && (request&2) && scheduler_clock()-scheduler_since>=2000)yield=1;
    if(yield) {
        scheduled[scheduler_slot].order=++scheduler_order;scheduler_owner=0;
        scheduler_notify_locked();scheduler_acquire_locked();
    } else scheduler_notify_locked();
    ReleaseSRWLockExclusive(&scheduler_lock);
}
/* Call only outside native ownership locks. The original call must perform
 * its real wait/I/O; this changes CPU eligibility, never its completion. */
static int scheduler_block_begin(void)
{
    if(!scheduler_enabled || !scheduler_attached || scheduler_slot==SCHEDULER_CALLBACK_SLOT)return 0;
#ifdef PILOT_USB_LLE
    if(pilot_io_device_irql()>=2)stop("blocking service at raised IRQL",pilot_io_device_irql());
#endif
    AcquireSRWLockExclusive(&scheduler_lock);
    /* A whole blocking service may contain a narrower renderer wait. Only
     * the outer owner restores eligibility; nested wait scopes are inert. */
    if(scheduled[scheduler_slot].blocked){ReleaseSRWLockExclusive(&scheduler_lock);return 0;}
    if(scheduler_owner!=scheduler_slot+1)stop("blocking without guest CPU",scheduler_slot);
    scheduled[scheduler_slot].blocked=1;scheduler_owner=0;scheduler_notify_locked();
    ReleaseSRWLockExclusive(&scheduler_lock);return 1;
}
static void scheduler_block_end(int blocked)
{
    if(!blocked)return;
    AcquireSRWLockExclusive(&scheduler_lock);
    scheduled[scheduler_slot].blocked=0;scheduled[scheduler_slot].order=++scheduler_order;
    scheduler_notify_locked();scheduler_acquire_locked();ReleaseSRWLockExclusive(&scheduler_lock);
}
static void scheduler_retire(void)
{
    if(!scheduler_enabled || !scheduler_attached)return;
#ifdef PILOT_USB_LLE
    if(scheduler_slot==SCHEDULER_CALLBACK_SLOT) {
        uint32_t level=0,result=0;
        const char *error=pilot_io_device_lle(161,memory,&level,&result);
        if(error)stop(error,161);
    }
#endif
    AcquireSRWLockExclusive(&scheduler_lock);
    scheduled[scheduler_slot].live=0;
    if(scheduler_owner==scheduler_slot+1)scheduler_owner=0;
    scheduler_notify_locked();ReleaseSRWLockExclusive(&scheduler_lock);
    scheduler_attached=0;g_pilot_suspend_count=NULL;
}
static void scheduler_callback_enter(void)
{
    if(!scheduler_enabled)return;
    if(scheduler_attached)stop("recursive guest interrupt execution unqualified",scheduler_slot);
    scheduler_publish(SCHEDULER_CALLBACK_SLOT,0,0);
    scheduler_enter(SCHEDULER_CALLBACK_SLOT);
#ifdef PILOT_USB_LLE
    uint32_t level=0,result=0;
    const char *error=pilot_io_device_lle(129,memory,&level,&result);
    if(error)stop(error,129);
    if((result&255)!=0)stop("DPC acquired CPU at raised IRQL",result&255);
#endif
}
static int scheduler_kernel_blocks(unsigned ordinal)
{
    switch(ordinal) {
    case 190:case 198:case 202:case 207:case 211:case 218:case 219:case 226:case 236:
    case 158:case 159:case 233:case 234:case 277:case 327:case 328:
        return 1;
    case 187: /* Closing a file joins its real outstanding I/O. */
        return (MEM32(g_esp+4)&0xf0000000u)==0x50000000u;
    default:return 0;
    }
}
static void scheduler_configure(void)
{
    const char *value=getenv("XLLE_PILOT_GUEST_SCHEDULER");
    if(value && strcmp(value,"0") && strcmp(value,"1"))stop("invalid guest scheduler selection",0);
    scheduler_enabled=value && !strcmp(value,"1");
    if(scheduler_enabled)fprintf(stderr,"[SCHEDULER] bounded single-CPU priority scheduler; no timeout overrides\n");
}
#ifdef PILOT_USB_LLE
static int scheduler_usb_tick_request(void)
{
    AcquireSRWLockExclusive(&scheduler_lock);
    InterlockedExchange(&scheduler_usb_pending,1);
    int idle=!scheduler_owner && !scheduler_best_locked();
    if(idle) {
        scheduler_owner=66;scheduler_slot=65;scheduler_attached=1;
        g_pilot_suspend_count=&scheduled[65].request;
    }
    scheduler_notify_locked();ReleaseSRWLockExclusive(&scheduler_lock);return idle;
}
#endif
