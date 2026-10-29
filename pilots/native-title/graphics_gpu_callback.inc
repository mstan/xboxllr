/* Original XDK packet contract and external Cxbx/pbkit behavior references;
 * independently implemented using this runtime's GPU events and DPC worker.
 * READ follows consumption; WRITE also follows actual GPU completion. */
struct GraphicsGpuCallback {uint64_t serial;uint32_t routine,context,type;struct GraphicsGpuCallback *next;
    uint64_t queued,first_poll,checkpoint_time;unsigned polls,busy_polls;};
static struct GraphicsGpuCallback *graphics_gpu_head,*graphics_gpu_tail;
#if PILOT_GPU_SCALAR_PREPARATION_AVAILABLE
static struct GraphicsGpuCallback *graphics_gpu_running_callback;
#endif
static unsigned graphics_gpu_running;
static uint64_t graphics_gpu_delivered;
static CONDITION_VARIABLE graphics_gpu_changed=CONDITION_VARIABLE_INIT;
/* Explicit diagnostic only; normal production avoids per-poll clock reads. */
static int graphics_gpu_profile(void)
{
    static int enabled=-1;
    if(enabled<0){const char *value=getenv("XLLE_PILOT_CALLBACK_PROFILE");enabled=value && !strcmp(value,"1");}
    return enabled;
}
static int graphics_gpu_deliver(void)
{
    dispatch_gpu_wait_event(NULL);
    AcquireSRWLockExclusive(&graphics_lock);
    struct GraphicsGpuCallback *c=graphics_gpu_head;
    if(!c){ReleaseSRWLockExclusive(&graphics_lock);return 0;}
    if(graphics_gpu_profile()){++c->polls;if(!c->first_poll)c->first_poll=dispatch_monotonic();}
    if(c->type && pilot_gfx_checkpoint_busy(c->serial)){
        if(graphics_gpu_profile())++c->busy_polls;
        dispatch_gpu_wait_event(pilot_gfx_checkpoint_event(c->serial));
        ReleaseSRWLockExclusive(&graphics_lock);return 1;
    }
    uint64_t ready=graphics_gpu_profile()?dispatch_monotonic():0;
    graphics_gpu_head=c->next;if(!graphics_gpu_head)graphics_gpu_tail=NULL;
    graphics_gpu_running=1;
#if PILOT_GPU_SCALAR_PREPARATION_AVAILABLE
    graphics_gpu_running_callback=c;
#endif
    ReleaseSRWLockExclusive(&graphics_lock);
    uint32_t top=MEM32(g_fs_base+4);g_esp=top-8;
    MEM32(g_esp)=0;MEM32(g_esp+4)=c->context;
    scheduler_callback_enter();recomp_lookup(c->routine)();scheduler_retire();
    if(g_esp!=top-4)stop("GPU cdecl callback stack imbalance",c->routine);
    g_esp+=4;
    AcquireSRWLockExclusive(&graphics_lock);
    ++graphics_gpu_delivered;graphics_gpu_running=0;
#if PILOT_GPU_SCALAR_PREPARATION_AVAILABLE
    graphics_gpu_running_callback=NULL;
#endif
    if(graphics_gpu_profile())fprintf(stderr,"[GPU CALLBACK PROFILE] serial=%llu context=%x checkpoint_ms=%.3f queued_ms=%.3f first_poll_ms=%.3f execution_ms=%.3f polls=%u busy=%u\n",
        (unsigned long long)c->serial,c->context,c->checkpoint_time/10000.0,(ready-c->queued)/10000.0,
        (c->first_poll-c->queued)/10000.0,(dispatch_monotonic()-ready)/10000.0,c->polls,c->busy_polls);
    if(pilot_event_trace_enabled())fprintf(stderr,"[GPU CALLBACK] type=%u routine=%08x context=%08x serial=%llu delivered=%llu\n",
        c->type,c->routine,c->context,(unsigned long long)c->serial,(unsigned long long)graphics_gpu_delivered);
    free(c);int pending=graphics_gpu_head!=NULL;WakeAllConditionVariable(&graphics_gpu_changed);
    /* A queued callback may already be ready. Inspect it immediately; only an
     * observed busy checkpoint needs the native event/fallback retry timer. */
    if(pending && pilot_gfx_completion_events())pending=2;
    ReleaseSRWLockExclusive(&graphics_lock);return pending;
}
/* Renderer commands after the software trap cannot overtake its callback.
 * The independent display-event wait flushes buffered draws without waiting
 * for GPU completion and does not enter
 * this renderer barrier.
 * CPU execution after Insert remains free to run; callbacks also run when the
 * caller only polls memory. Reentrant DPC calls use their own execution context. */
static void graphics_gpu_barrier(void)
{
    if(dispatch_in_dpc)return;
#if defined(PILOT_SCHEDULING_LLE) && !defined(PILOT_DISPLAY_LLE)
    if(graphics_original_clock_bound){graphics_original_callback_barrier();return;}
#endif
    uint64_t begin=graphics_gpu_profile() && (graphics_gpu_head || graphics_gpu_running)?dispatch_monotonic():0;
    while(graphics_gpu_head || graphics_gpu_running) {
        int blocked=scheduler_block_begin();
        if(!SleepConditionVariableSRW(&graphics_gpu_changed,&graphics_lock,INFINITE,0))
            stop("GPU callback ordering wait failed",GetLastError());
        if(blocked) {
            /* Reacquire the guest CPU without renderer ownership: another
             * eligible guest/DPC may need that lock before it can yield. */
            ReleaseSRWLockExclusive(&graphics_lock);
            scheduler_block_end(blocked);
            AcquireSRWLockExclusive(&graphics_lock);
        }
    }
    if(begin)fprintf(stderr,"[GPU BARRIER PROFILE] thread=%lu wait_ms=%.3f\n",GetCurrentThreadId(),(dispatch_monotonic()-begin)/10000.0);
}
static void graphics_gpu_insert(uint32_t type,uint32_t routine,uint32_t context)
{
    if(type>1 || !routine || !recomp_lookup(routine))stop("unqualified GPU callback",routine);
    if(graphics_clock_closing)stop("GPU callback during device close",routine);
    struct GraphicsGpuCallback *c=calloc(1,sizeof(*c));
    if(!c)stop("GPU callback ownership allocation failed",routine);
    c->type=type;c->routine=routine;c->context=context;
    uint64_t begin=graphics_gpu_profile()?dispatch_monotonic():0;
    if(type)c->serial=pilot_gfx_checkpoint();
    if(graphics_gpu_profile()){c->queued=dispatch_monotonic();c->checkpoint_time=c->queued-begin;}
    if(graphics_gpu_tail)graphics_gpu_tail->next=c;else graphics_gpu_head=c;
    graphics_gpu_tail=c;
#if defined(PILOT_SCHEDULING_LLE) && !defined(PILOT_DISPLAY_LLE)
    if(graphics_original_clock_bound)graphics_original_callback_sync();else
#endif
    dispatch_gpu_schedule(graphics_gpu_deliver);
}
static void graphics_gpu_close(void)
{
    graphics_gpu_barrier();ReleaseSRWLockExclusive(&graphics_lock);
#if defined(PILOT_SCHEDULING_LLE) && !defined(PILOT_DISPLAY_LLE)
    if(!graphics_original_clock_bound)
#endif
    dispatch_gpu_quiesce();AcquireSRWLockExclusive(&graphics_lock);
    if(graphics_gpu_head || graphics_gpu_tail || graphics_gpu_running)stop("GPU callback ownership leak",0);
    graphics_gpu_delivered=0;
}
