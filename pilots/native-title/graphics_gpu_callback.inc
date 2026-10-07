/* Original XDK packet contract and external Cxbx/pbkit behavior references;
 * independently implemented using this runtime's GPU events and DPC worker.
 * READ follows consumption; WRITE also follows actual GPU completion. */
struct GraphicsGpuCallback {uint64_t serial;uint32_t routine,context,type;struct GraphicsGpuCallback *next;};
static struct GraphicsGpuCallback *graphics_gpu_head,*graphics_gpu_tail;
static unsigned graphics_gpu_running;
static uint64_t graphics_gpu_delivered;
static CONDITION_VARIABLE graphics_gpu_changed=CONDITION_VARIABLE_INIT;
static int graphics_gpu_deliver(void)
{
    AcquireSRWLockExclusive(&graphics_lock);
    struct GraphicsGpuCallback *c=graphics_gpu_head;
    if(!c){ReleaseSRWLockExclusive(&graphics_lock);return 0;}
    if(c->type && pilot_gfx_checkpoint_busy(c->serial)){
        ReleaseSRWLockExclusive(&graphics_lock);return 1;
    }
    graphics_gpu_head=c->next;if(!graphics_gpu_head)graphics_gpu_tail=NULL;
    graphics_gpu_running=1;ReleaseSRWLockExclusive(&graphics_lock);
    uint32_t top=MEM32(g_fs_base+4);g_esp=top-8;
    MEM32(g_esp)=0;MEM32(g_esp+4)=c->context;
    recomp_lookup(c->routine)();
    if(g_esp!=top-4)stop("GPU cdecl callback stack imbalance",c->routine);
    g_esp+=4;
    AcquireSRWLockExclusive(&graphics_lock);
    ++graphics_gpu_delivered;graphics_gpu_running=0;
    fprintf(stderr,"[GPU CALLBACK] type=%u routine=%08x context=%08x serial=%llu delivered=%llu\n",
        c->type,c->routine,c->context,(unsigned long long)c->serial,(unsigned long long)graphics_gpu_delivered);
    free(c);int pending=graphics_gpu_head!=NULL;WakeAllConditionVariable(&graphics_gpu_changed);
    ReleaseSRWLockExclusive(&graphics_lock);return pending;
}
/* Graphics methods after the software trap cannot overtake its callback.
 * CPU execution after Insert remains free to run; callbacks also run when the
 * caller only polls memory. Reentrant DPC calls use their own execution context. */
static void graphics_gpu_barrier(void)
{
    if(dispatch_in_dpc)return;
    while(graphics_gpu_head || graphics_gpu_running)
        if(!SleepConditionVariableSRW(&graphics_gpu_changed,&graphics_lock,INFINITE,0))
            stop("GPU callback ordering wait failed",GetLastError());
}
static void graphics_gpu_insert(uint32_t type,uint32_t routine,uint32_t context)
{
    if(type>1 || !routine || !recomp_lookup(routine))stop("unqualified GPU callback",routine);
    if(graphics_clock_closing)stop("GPU callback during device close",routine);
    struct GraphicsGpuCallback *c=calloc(1,sizeof(*c));
    if(!c)stop("GPU callback ownership allocation failed",routine);
    c->type=type;c->routine=routine;c->context=context;
    if(type)c->serial=pilot_gfx_checkpoint();
    if(graphics_gpu_tail)graphics_gpu_tail->next=c;else graphics_gpu_head=c;
    graphics_gpu_tail=c;dispatch_gpu_schedule(graphics_gpu_deliver);
}
static void graphics_gpu_close(void)
{
    graphics_gpu_barrier();ReleaseSRWLockExclusive(&graphics_lock);
    dispatch_gpu_quiesce();AcquireSRWLockExclusive(&graphics_lock);
    if(graphics_gpu_head || graphics_gpu_tail || graphics_gpu_running)stop("GPU callback ownership leak",0);
    graphics_gpu_delivered=0;
}
