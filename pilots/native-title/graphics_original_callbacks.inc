/* The native GPU/display supply readiness only. Actual KeSetTimerEx expiry
 * and the original dispatcher own this KDPC's queue, IRQL and stack. This
 * transport never starts the native guest dispatcher and never enters the
 * passive component gate from a DPC. KTIMER40/KDPC28 follow the nxdk ABI. */
enum { GRAPHICS_ORIGINAL_CALLBACK_BYTES=72, GRAPHICS_ORIGINAL_CALLBACK_MAGIC=0x47584450 };
static uint32_t graphics_original_callback_owner;
static int graphics_original_callback_armed;
static uint64_t graphics_original_vblank_delivered,graphics_original_dpc_delivered;

static uint32_t graphics_original_callback_service(unsigned ordinal,const uint32_t *args)
{
    uint32_t result=0;
    const char *error=pilot_io_device_thread_component_active()?
        pilot_io_device_thread_component_call(ordinal,args,&result):
        pilot_io_device_lle(ordinal,memory,args,&result);
    if(error)stop(error,ordinal);
    return result;
}

/* Called with graphics_lock held, only from a passive whole API. Kernel
 * services can run original DPCs: release the lock for every such service.
 * The original component gate serializes passive owners across those calls. */
static void graphics_original_callback_sync(void)
{
    if(!graphics_original_clock_bound)return;
    int wanted=graphics_callback || graphics_gpu_head || graphics_gpu_running;
    if(wanted && !graphics_original_callback_owner) {
        ReleaseSRWLockExclusive(&graphics_lock);
        uint32_t owner=pool_allocate(GRAPHICS_ORIGINAL_CALLBACK_BYTES,0x4478674fu);
        if(!owner)stop("original graphics callback owner allocation failed",0);
        memset(memory+owner,0,GRAPHICS_ORIGINAL_CALLBACK_BYTES);
        MEM32(owner+68)=GRAPHICS_ORIGINAL_CALLBACK_MAGIC;
        uint32_t timer[]={owner,0},dpc[]={owner+40,PILOT_IO_GRAPHICS_DPC_ENTRY,owner};
        graphics_original_callback_service(113,timer);
        graphics_original_callback_service(107,dpc);
        if(MEM16(owner+40)!=0x13 || MEM8(owner+42) ||
           MEM32(owner+52)!=PILOT_IO_GRAPHICS_DPC_ENTRY || MEM32(owner+56)!=owner)
            stop("original graphics callback KDPC initialization differs",owner);
        AcquireSRWLockExclusive(&graphics_lock);
        graphics_original_callback_owner=owner;
    }
    if(wanted && !graphics_original_callback_armed) {
        uint32_t owner=graphics_original_callback_owner;
        graphics_original_callback_armed=1;
        ReleaseSRWLockExclusive(&graphics_lock);
        /* Period1 has the already qualified original next-tick cadence2ms.
         * This timer polls real GPU readiness; it does not complete GPU work. */
        uint32_t set[]={owner,UINT32_MAX,UINT32_MAX,1,owner+40};
        graphics_original_callback_service(150,set);
        AcquireSRWLockExclusive(&graphics_lock);
    } else if(!wanted && graphics_original_callback_owner) {
        uint32_t owner=graphics_original_callback_owner,dpc=owner+40;
        graphics_original_callback_armed=0;
        ReleaseSRWLockExclusive(&graphics_lock);
        graphics_original_callback_service(97,&owner);
        graphics_original_callback_service(137,&dpc);
        if(MEM8(owner+3) || MEM8(dpc+2) || MEM32(owner+68)!=GRAPHICS_ORIGINAL_CALLBACK_MAGIC)
            stop("original graphics callback owner remains queued",owner);
        AcquireSRWLockExclusive(&graphics_lock);
        if(graphics_callback || graphics_gpu_head || graphics_gpu_running)
            stop("original graphics callback retirement lost passive ownership",owner);
        graphics_original_callback_owner=0;
        ReleaseSRWLockExclusive(&graphics_lock);pool_free(owner);
        AcquireSRWLockExclusive(&graphics_lock);
    }
}

static void graphics_original_callback_barrier(void)
{
    struct ComponentWait wait={0};
    while(graphics_gpu_head || graphics_gpu_running) {
        ReleaseSRWLockExclusive(&graphics_lock);
        const char *error=wait.event?NULL:component_wait_open(&wait);uint32_t status=0;
        if(!error)error=component_wait_slice(&wait,10000,&status);
        AcquireSRWLockExclusive(&graphics_lock);
        if(error)stop(error,0);
    }
    if(wait.event) {
        ReleaseSRWLockExclusive(&graphics_lock);
        const char *error=component_wait_close(&wait);
        AcquireSRWLockExclusive(&graphics_lock);if(error)stop(error,0);
    }
}

static void graphics_original_callback_close(void)
{
    if(!graphics_original_clock_bound)return;
    graphics_original_callback_barrier();graphics_original_callback_sync();
    if(graphics_original_callback_owner || graphics_original_callback_armed)
        stop("original graphics callback owner leak",graphics_original_callback_owner);
}

/* A user callback has one cdecl argument inside the four-argument stdcall
 * KDPC. Use the current actual original stack frame, never its top or a host
 * callback stack; the enclosing original bridge restores interrupted FP/TLS. */
static void graphics_original_user_callback(uint32_t routine,uint32_t context)
{
    recomp_func_t body=recomp_lookup(routine);
    uint32_t sp=g_esp,thread=MEM32(0x8003da64u),top=MEM32(0x8003da98u);
    uint32_t bottom=top-0x3000u;
    if(sp<bottom || (uint64_t)sp+20>top) {
        bottom=MEM32(thread+0x20);top=MEM32(thread+0x1c);
    }
    if(!body || !dispatch_in_dpc || !usb_device_callback_depth || pilot_io_device_irql()!=2 ||
       sp<bottom+8 || (uint64_t)sp+20>top)
        stop("original graphics user callback lost original DPC stack/owner",routine);
    uint32_t nonvol[]={g_ebx,g_ebp,g_esi,g_edi};
    g_esp=sp-8;MEM32(g_esp)=PILOT_IO_GRAPHICS_DPC_ENTRY;MEM32(g_esp+4)=context;
    body();
    if(g_esp!=sp-4 || g_ebx!=nonvol[0] || g_ebp!=nonvol[1] || g_esi!=nonvol[2] || g_edi!=nonvol[3])
        stop("original graphics cdecl callback ABI differs",routine);
    g_esp+=4;
}

static void graphics_original_callback_dpc(void)
{
    uint32_t sp=g_esp,owner=graphics_original_callback_owner;
    if(!owner || MEM32(sp+4)!=owner+40 || MEM32(sp+8)!=owner ||
       MEM16(owner+40)!=0x13 || MEM8(owner+42) || MEM32(owner+68)!=GRAPHICS_ORIGINAL_CALLBACK_MAGIC ||
       MEM32(owner+52)!=PILOT_IO_GRAPHICS_DPC_ENTRY || MEM32(owner+56)!=owner ||
       !dispatch_in_dpc || !usb_device_callback_depth || pilot_io_device_irql()!=2)
        stop("original graphics callback has no dequeued KDPC owner",owner);
    /* A suspended component may retain a renderer lock during a VM service.
     * Leave pending work untouched and retry on the next actual timer DPC. */
    if(!TryAcquireSRWLockExclusive(&graphics_lock)){g_esp+=20;return;}
    if(graphics_active && graphics_original_callback_armed) {
        ++graphics_original_dpc_delivered;
        if(graphics_callback && graphics_original_pulse_frame>graphics_original_callback_frame) {
            uint32_t callback=graphics_callback,data=graphics_headers+96;
            graphics_original_callback_frame=graphics_original_pulse_frame;
            MEM32(data)=(uint32_t)graphics_original_callback_frame;
            MEM32(data+4)=graphics_swap_completed;MEM32(data+8)=graphics_original_pulse_flags;
            ReleaseSRWLockExclusive(&graphics_lock);
            graphics_original_user_callback(callback,data);
            AcquireSRWLockExclusive(&graphics_lock);++graphics_original_vblank_delivered;
        }
        while(graphics_gpu_head) {
            struct GraphicsGpuCallback *c=graphics_gpu_head;
            if(c->type && pilot_gfx_checkpoint_busy(c->serial))break;
            graphics_gpu_head=c->next;if(!graphics_gpu_head)graphics_gpu_tail=NULL;
            graphics_gpu_running=1;ReleaseSRWLockExclusive(&graphics_lock);
            graphics_original_user_callback(c->routine,c->context);
            AcquireSRWLockExclusive(&graphics_lock);
            ++graphics_gpu_delivered;graphics_gpu_running=0;free(c);
        }
    }
    ReleaseSRWLockExclusive(&graphics_lock);
    if(g_esp!=sp)stop("original graphics DPC subframe leak",owner);
    g_esp+=20;
}
