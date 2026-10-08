/* Bounded native dispatcher, independently implemented from the documented NT
 * API contract and Xbox structure offsets. No emulator implementation is linked.
 * Host waitable timers provide absolute/relative clock semantics; one worker
 * serializes DPCs on its own guest stack. See README for qualification limits. */
struct PilotTimer { uint32_t va, dpc, period; uint64_t deadline; int absolute; };
struct PilotDpc { uint32_t va, routine, context, arg1, arg2; };
static struct PilotTimer dispatch_timers[128];
static struct PilotDpc dispatch_dpcs[256];
static unsigned dispatch_dpc_count;
static SRWLOCK dispatch_lock=SRWLOCK_INIT;
static INIT_ONCE dispatch_once=INIT_ONCE_STATIC_INIT;
static HANDLE dispatch_changed, dispatch_absolute, dispatch_relative, dispatch_worker_handle;
static RECOMP_TLS int dispatch_in_dpc;
static uint64_t dispatch_clock_origin;
/* A display pulse shares the serialized DPC execution context. Its absolute
 * phase is independent of callback duration and ordinary timer rearming. */
static void (*dispatch_pulse_routine)(uint64_t);
static uint64_t dispatch_pulse_origin,dispatch_pulse_frame,dispatch_pulse_deadline;
static unsigned dispatch_pulse_hz;
static int dispatch_pulse_running,dispatch_worker_ready;
static CONDITION_VARIABLE dispatch_pulse_idle=CONDITION_VARIABLE_INIT;
/* GPU callbacks share the existing DPC context, but are independent of
 * display refresh. Poll only while an actual submitted queue needs service. */
static int (*dispatch_gpu_routine)(void);
static uint64_t dispatch_gpu_deadline,dispatch_gpu_generation;
static int dispatch_gpu_running;
/* Owned duplicate: only this worker replaces/closes it, never while waiting.
 * The GPU can wake the worker before the bounded fallback polling deadline. */
static HANDLE dispatch_gpu_event;
static void dispatch_gpu_wait_event(HANDLE event)
{
    if(!dispatch_in_dpc)stop("GPU event replacement outside DPC worker",0);
    if(dispatch_gpu_event)CloseHandle(dispatch_gpu_event);
    dispatch_gpu_event=event;
}
static uint64_t dispatch_pulse_time(uint64_t frame)
{return dispatch_pulse_origin+(frame/dispatch_pulse_hz)*10000000+((frame%dispatch_pulse_hz)*10000000+dispatch_pulse_hz-1)/dispatch_pulse_hz;}

static uint64_t dispatch_wall_time(void)
{
    /* Phobos's system-time service identified coarse host ticks as a polling
     * cost. Windows' precise clock supplies the same 100ns/1601 epoch directly,
     * shared with our absolute timers; no fabricated per-call advancement. */
    FILETIME ft; GetSystemTimePreciseAsFileTime(&ft);
    return ((uint64_t)ft.dwHighDateTime<<32)|ft.dwLowDateTime;
}
static uint64_t dispatch_monotonic(void)
{
    LARGE_INTEGER q,f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f);
    return (uint64_t)(q.QuadPart/f.QuadPart)*10000000ull+
           (uint64_t)(q.QuadPart%f.QuadPart)*10000000ull/(uint64_t)f.QuadPart;
}
static uint64_t dispatch_deadline(int64_t due)
{
    if (due>=0) return (uint64_t)due;
    uint64_t now=dispatch_monotonic(), delta=0-(uint64_t)due;
    return UINT64_MAX-now<delta ? UINT64_MAX : now+delta;
}
static int dispatch_queue_locked(uint32_t va,uint32_t arg1,uint32_t arg2)
{
    if (MEM16(va)!=0x13) stop("invalid DPC object",va);
    if (MEM8(va+2)) return 0;
    uint32_t routine=MEM32(va+12);
    if (!recomp_lookup(routine)) stop("DPC callback is not generated",routine);
    if (dispatch_dpc_count==256) stop("DPC queue exhausted",va);
    struct PilotDpc *d=&dispatch_dpcs[dispatch_dpc_count++];
    d->va=va; d->routine=routine; d->context=MEM32(va+16); d->arg1=arg1; d->arg2=arg2;
    MEM32(va+20)=arg1; MEM32(va+24)=arg2; MEM8(va+2)=1;
    SetEvent(dispatch_changed);
    return 1;
}
static void dispatch_expire_locked(uint64_t mono,uint64_t wall)
{
    for (unsigned i=0;i<128;++i) {
        struct PilotTimer *t=&dispatch_timers[i];
        if (!t->va || t->deadline>(t->absolute ? wall : mono)) continue;
        uint32_t va=t->va;
        MEM32(va+4)=1;
        if (t->dpc) dispatch_queue_locked(t->dpc,(uint32_t)wall,(uint32_t)(wall>>32));
        if (t->period) {
            t->absolute=0; t->deadline=mono+(uint64_t)t->period*10000;
            MEM8(va+1)=0;
            MEM64(va+16)=t->deadline-dispatch_clock_origin;
        } else { MEM8(va+3)=0; t->va=0; }
    }
}

/* Waiters have private host events so a scheduler wakeup cannot consume theirs.
 * State inspection and subscription share dispatch_lock, preventing lost wakes. */
struct PilotWaiter { HANDLE wake; const uint32_t *objects; unsigned count; int all,completed; uint32_t result; struct PilotWaiter *next; };
static struct PilotWaiter *dispatch_waiters;
static int dispatch_try_wait_locked(const uint32_t *objects,unsigned count,int all,uint32_t *result)
{
    unsigned ready=0,first=count;
    for (unsigned i=0;i<count;++i) if (MEM32(objects[i]+4)) { ++ready; if (first==count) first=i; }
    if ((all && ready==count) || (!all && ready)) {
        for (unsigned i=0;i<count;++i) if ((all || i==first) && (MEM8(objects[i])&1)) MEM32(objects[i]+4)=0;
        *result=all ? 0 : first; return 1;
    }
    return 0;
}
static void dispatch_wake_locked(void)
{
    for (struct PilotWaiter *w=dispatch_waiters;w;w=w->next)
        if (!w->completed && dispatch_try_wait_locked(w->objects,w->count,w->all,&w->result)) {
            w->completed=1; SetEvent(w->wake);
        }
}

static DWORD WINAPI dispatch_worker(void *unused)
{
    (void)unused;
    const uint32_t base=0x72000000u,top=base+0x20000u;
    commit(base,0x20000);
    g_fs_base=base;
    MEM32(base)=0xffffffffu; MEM32(base+4)=top; MEM32(base+8)=base+0x2000;
    MEM32(base+0x18)=base; MEM32(base+0x1c)=base; MEM32(base+0x20)=base+0x28;
    MEM32(base+0x28)=base+0x1000; MEM32(base+0x1000+0x12c)=GetCurrentThreadId();
    AcquireSRWLockExclusive(&dispatch_lock);
    dispatch_worker_ready=1;WakeAllConditionVariable(&dispatch_pulse_idle);
    ReleaseSRWLockExclusive(&dispatch_lock);
    HANDLE waits[]={dispatch_changed,dispatch_absolute,dispatch_relative,NULL};
    for (;;) {
        AcquireSRWLockExclusive(&dispatch_lock);
        uint64_t mono=dispatch_monotonic();
        dispatch_expire_locked(mono,dispatch_wall_time());
        dispatch_wake_locked();
        if (dispatch_dpc_count) {
            struct PilotDpc d=dispatch_dpcs[0];
            memmove(dispatch_dpcs,dispatch_dpcs+1,--dispatch_dpc_count*sizeof(*dispatch_dpcs));
            MEM8(d.va+2)=0;
            ReleaseSRWLockExclusive(&dispatch_lock);
            g_esp=top-20;
            MEM32(g_esp)=0; MEM32(g_esp+4)=d.va; MEM32(g_esp+8)=d.context;
            MEM32(g_esp+12)=d.arg1; MEM32(g_esp+16)=d.arg2;
            MEM8(base+0x24)=2; dispatch_in_dpc=1;
            recomp_lookup(d.routine)();
            if (g_esp!=top) stop("DPC callback stack imbalance",d.routine);
            dispatch_in_dpc=0; MEM8(base+0x24)=0;
            continue;
        }
        if(dispatch_pulse_routine && dispatch_pulse_deadline<=mono) {
            void (*routine)(uint64_t)=dispatch_pulse_routine;
            uint64_t frame=dispatch_pulse_frame++;
            dispatch_pulse_deadline=dispatch_pulse_time(dispatch_pulse_frame);
            dispatch_pulse_running=1;
            ReleaseSRWLockExclusive(&dispatch_lock);
            MEM8(base+0x24)=2;dispatch_in_dpc=1;
            routine(frame);
            dispatch_in_dpc=0;MEM8(base+0x24)=0;
            AcquireSRWLockExclusive(&dispatch_lock);
            dispatch_pulse_running=0;WakeAllConditionVariable(&dispatch_pulse_idle);
            ReleaseSRWLockExclusive(&dispatch_lock);
            continue;
        }
        if(dispatch_gpu_routine && (dispatch_gpu_deadline<=mono ||
            (dispatch_gpu_event && WaitForSingleObject(dispatch_gpu_event,0)==WAIT_OBJECT_0))) {
            int (*routine)(void)=dispatch_gpu_routine;
            uint64_t generation=dispatch_gpu_generation;dispatch_gpu_running=1;
            ReleaseSRWLockExclusive(&dispatch_lock);
            MEM8(base+0x24)=2;dispatch_in_dpc=1;
            int pending=routine();
            dispatch_in_dpc=0;MEM8(base+0x24)=0;
            AcquireSRWLockExclusive(&dispatch_lock);
            if(generation==dispatch_gpu_generation) {
                if(pending)dispatch_gpu_deadline=pending>1?0:dispatch_monotonic()+10000;
                else dispatch_gpu_routine=NULL;
            }
            dispatch_gpu_running=0;WakeAllConditionVariable(&dispatch_pulse_idle);
            ReleaseSRWLockExclusive(&dispatch_lock);
            continue;
        }
        uint64_t absolute=UINT64_MAX,relative=UINT64_MAX;
        if(dispatch_pulse_routine)relative=dispatch_pulse_deadline;
        if(dispatch_gpu_routine && dispatch_gpu_deadline<relative)relative=dispatch_gpu_deadline;
        for (unsigned i=0;i<128;++i) if (dispatch_timers[i].va) {
            struct PilotTimer *t=&dispatch_timers[i];
            uint64_t *next=t->absolute ? &absolute : &relative;
            if (t->deadline<*next) *next=t->deadline;
        }
        LARGE_INTEGER due;
        if (absolute!=UINT64_MAX) {
            due.QuadPart=(LONGLONG)absolute;
            if (!SetWaitableTimer(dispatch_absolute,&due,0,NULL,NULL,FALSE)) stop("absolute timer arm failed",GetLastError());
        } else CancelWaitableTimer(dispatch_absolute);
        if (relative!=UINT64_MAX) {
            mono=dispatch_monotonic();
            uint64_t delay=relative>mono ? relative-mono : 1;
            due.QuadPart=-(LONGLONG)(delay>INT64_MAX ? INT64_MAX : delay);
            if (!SetWaitableTimer(dispatch_relative,&due,0,NULL,NULL,FALSE)) stop("relative timer arm failed",GetLastError());
        } else CancelWaitableTimer(dispatch_relative);
        waits[3]=dispatch_gpu_event;
        ReleaseSRWLockExclusive(&dispatch_lock);
        if (WaitForMultipleObjects(waits[3]?4:3,waits,FALSE,INFINITE)==WAIT_FAILED) stop("dispatcher wait failed",GetLastError());
    }
}
static BOOL CALLBACK dispatch_start(PINIT_ONCE once,void *parameter,void **context)
{
    (void)once; (void)parameter; (void)context;
    dispatch_clock_origin=dispatch_monotonic();
    dispatch_changed=CreateEventW(NULL,FALSE,FALSE,NULL);
    dispatch_absolute=CreateWaitableTimerW(NULL,FALSE,NULL);
    /* GPU retry and display deadlines can be much shorter than the default
     * host timer tick. Preserve their deadlines without rounding each retry
     * up to that tick; older hosts retain the supported coarse timer path. */
    dispatch_relative=CreateWaitableTimerExW(NULL,NULL,0x2,TIMER_ALL_ACCESS);
    if(!dispatch_relative && GetLastError()==ERROR_INVALID_PARAMETER)
        dispatch_relative=CreateWaitableTimerW(NULL,FALSE,NULL);
    if (!dispatch_changed || !dispatch_absolute || !dispatch_relative) stop("dispatcher allocation failed",GetLastError());
    dispatch_worker_handle=CreateThread(NULL,0,dispatch_worker,NULL,0,NULL);
    if (!dispatch_worker_handle) stop("dispatcher thread creation failed",GetLastError());
    AcquireSRWLockExclusive(&dispatch_lock);
    while(!dispatch_worker_ready)
        if(!SleepConditionVariableSRW(&dispatch_pulse_idle,&dispatch_lock,INFINITE,0))stop("dispatcher startup wait failed",GetLastError());
    ReleaseSRWLockExclusive(&dispatch_lock);
    return TRUE;
}
static void dispatch_ensure(void)
{
    if (!InitOnceExecuteOnce(&dispatch_once,dispatch_start,NULL,NULL)) stop("dispatcher initialization failed",GetLastError());
}
static void dispatch_pulse_set(uint64_t origin,unsigned hz,void (*routine)(uint64_t))
{
    dispatch_ensure();
    AcquireSRWLockExclusive(&dispatch_lock);
    if(routine) {
        uint64_t now=dispatch_monotonic();
        if(!hz || hz>1000 || now<origin)stop("invalid display pulse clock",hz);
        uint64_t elapsed=now-origin;
        dispatch_pulse_origin=origin;dispatch_pulse_hz=hz;
        dispatch_pulse_frame=elapsed/10000000*hz+(elapsed%10000000)*hz/10000000+1;
        dispatch_pulse_deadline=dispatch_pulse_time(dispatch_pulse_frame);
    }
    dispatch_pulse_routine=routine;SetEvent(dispatch_changed);
    ReleaseSRWLockExclusive(&dispatch_lock);
}
static void dispatch_pulse_quiesce(void)
{
    if(dispatch_in_dpc)stop("display shutdown from its callback",0);
    AcquireSRWLockExclusive(&dispatch_lock);
    while(dispatch_pulse_running)
        if(!SleepConditionVariableSRW(&dispatch_pulse_idle,&dispatch_lock,INFINITE,0))stop("display callback join failed",GetLastError());
    ReleaseSRWLockExclusive(&dispatch_lock);
}
static void dispatch_gpu_schedule(int (*routine)(void))
{
    dispatch_ensure();AcquireSRWLockExclusive(&dispatch_lock);
    dispatch_gpu_routine=routine;dispatch_gpu_deadline=0;++dispatch_gpu_generation;
    SetEvent(dispatch_changed);ReleaseSRWLockExclusive(&dispatch_lock);
}
static void dispatch_gpu_quiesce(void)
{
    if(dispatch_in_dpc)stop("GPU callback shutdown from DPC",0);
    AcquireSRWLockExclusive(&dispatch_lock);
    while(dispatch_gpu_running || dispatch_gpu_routine)
        if(!SleepConditionVariableSRW(&dispatch_pulse_idle,&dispatch_lock,INFINITE,0))stop("GPU callback join failed",GetLastError());
    ReleaseSRWLockExclusive(&dispatch_lock);
}
static void dispatch_initialize(uint32_t va,unsigned type)
{
    dispatch_ensure();
    AcquireSRWLockExclusive(&dispatch_lock);
    if (type==8 || type==9) {
        for (unsigned i=0;i<128;++i) if (dispatch_timers[i].va==va) stop("reinitializing armed timer",va);
    }
    unsigned bytes=type>=8 ? 40 : 16;
    memset(memory+va,0,bytes); MEM8(va)=(uint8_t)type; MEM8(va+2)=(uint8_t)(bytes/4);
    MEM32(va+8)=va+8; MEM32(va+12)=va+8;
    ReleaseSRWLockExclusive(&dispatch_lock);
}
static unsigned dispatch_set_timer(uint32_t va,int64_t due,uint32_t period,uint32_t dpc)
{
    dispatch_ensure();
    AcquireSRWLockExclusive(&dispatch_lock);
    if ((MEM8(va)!=8 && MEM8(va)!=9) || (int32_t)period<0) stop("invalid timer arguments",va);
    if (dpc && (MEM16(dpc)!=0x13 || !recomp_lookup(MEM32(dpc+12)))) stop("invalid timer DPC",dpc);
    struct PilotTimer *slot=NULL;
    unsigned previous=0;
    for (unsigned i=0;i<128;++i) {
        if (dispatch_timers[i].va==va) { slot=&dispatch_timers[i]; previous=1; break; }
        if (!dispatch_timers[i].va) slot=&dispatch_timers[i];
    }
    if (!slot) stop("active timer table exhausted",va);
    slot->va=va; slot->dpc=dpc; slot->period=period; slot->absolute=due>=0; slot->deadline=dispatch_deadline(due);
    MEM8(va+1)=(uint8_t)slot->absolute; MEM8(va+3)=1; MEM32(va+4)=0;
    uint64_t clock=slot->absolute ? dispatch_wall_time() : dispatch_monotonic();
    uint64_t remaining=slot->deadline>clock ? slot->deadline-clock : 0;
    MEM64(va+16)=dispatch_monotonic()-dispatch_clock_origin+remaining;
    MEM32(va+32)=dpc; MEM32(va+36)=period;
    dispatch_expire_locked(dispatch_monotonic(),dispatch_wall_time());
    dispatch_wake_locked(); SetEvent(dispatch_changed);
    ReleaseSRWLockExclusive(&dispatch_lock);
    return previous;
}
static unsigned dispatch_cancel_timer(uint32_t va)
{
    unsigned previous=0;
    AcquireSRWLockExclusive(&dispatch_lock);
    if (MEM8(va)!=8 && MEM8(va)!=9) stop("invalid timer object",va);
    for (unsigned i=0;i<128;++i) if (dispatch_timers[i].va==va) { dispatch_timers[i].va=0; previous=1; }
    MEM8(va+3)=0; /* Already queued/running DPCs retain their independent lifetime. */
    if (dispatch_changed) SetEvent(dispatch_changed);
    ReleaseSRWLockExclusive(&dispatch_lock);
    return previous;
}
static unsigned dispatch_event(uint32_t va,int signal)
{
    AcquireSRWLockExclusive(&dispatch_lock);
    if (MEM8(va)>1) stop("invalid event object",va);
    unsigned previous=MEM32(va+4); MEM32(va+4)=signal;
    if (signal) dispatch_wake_locked();
    ReleaseSRWLockExclusive(&dispatch_lock);
    return previous;
}
static unsigned dispatch_queue(uint32_t va,uint32_t a,uint32_t b,int remove)
{
    dispatch_ensure(); AcquireSRWLockExclusive(&dispatch_lock);
    unsigned result=0;
    if (!remove) result=dispatch_queue_locked(va,a,b);
    else for (unsigned i=0;i<dispatch_dpc_count;++i) if (dispatch_dpcs[i].va==va) {
        memmove(dispatch_dpcs+i,dispatch_dpcs+i+1,(--dispatch_dpc_count-i)*sizeof(*dispatch_dpcs));
        MEM8(va+2)=0; result=1; break;
    }
    ReleaseSRWLockExclusive(&dispatch_lock); return result;
}
static uint32_t dispatch_wait(const uint32_t *objects,unsigned count,int all,int alertable,uint32_t timeout)
{
    if (!count || count>64) return 0xc000000du;
    if (alertable) stop("alertable guest wait requires APC delivery",objects[0]);
    int64_t due=timeout ? (int64_t)MEM64(timeout) : 0;
    if (dispatch_in_dpc && (!timeout || due)) stop("blocking wait in DPC",objects[0]);
    dispatch_ensure();
    struct PilotWaiter waiter={0};
    waiter.objects=objects; waiter.count=count; waiter.all=all;
    waiter.wake=CreateEventW(NULL,FALSE,FALSE,NULL);
    HANDLE alarm=timeout && due ? CreateWaitableTimerW(NULL,FALSE,NULL) : NULL;
    if (!waiter.wake || (timeout && due && !alarm)) stop("wait allocation failed",objects[0]);
    if (alarm) { LARGE_INTEGER t; t.QuadPart=due; if (!SetWaitableTimer(alarm,&t,0,NULL,NULL,FALSE)) stop("wait timeout arm failed",objects[0]); }
    uint32_t result=0x102;
    AcquireSRWLockExclusive(&dispatch_lock);
    for (unsigned i=0;i<count;++i) {
        unsigned type=MEM8(objects[i]);
        if (type!=0 && type!=1 && type!=8 && type!=9) stop("wait object type unsupported",objects[i]);
        for (unsigned j=0;j<i;++j) if (objects[i]==objects[j]) stop("duplicate wait object",objects[i]);
    }
    waiter.next=dispatch_waiters; dispatch_waiters=&waiter;
    for (;;) {
        if (waiter.completed) { result=waiter.result; break; }
        if (dispatch_try_wait_locked(objects,count,all,&result)) break;
        if (timeout && !due) break;
        ReleaseSRWLockExclusive(&dispatch_lock);
        HANDLE waits[]={waiter.wake,alarm};
        DWORD rc=WaitForMultipleObjects(alarm ? 2 : 1,waits,FALSE,INFINITE);
        AcquireSRWLockExclusive(&dispatch_lock);
        if (rc==WAIT_OBJECT_0+1) { if (waiter.completed) result=waiter.result; break; }
        if (rc!=WAIT_OBJECT_0) stop("native object wait failed",objects[0]);
    }
    struct PilotWaiter **link=&dispatch_waiters;
    while (*link!=&waiter) link=&(*link)->next;
    *link=waiter.next;
    ReleaseSRWLockExclusive(&dispatch_lock);
    CloseHandle(waiter.wake); if (alarm) CloseHandle(alarm);
    return result;
}
