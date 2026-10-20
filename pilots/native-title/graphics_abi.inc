/* XDK register/LTCG adapters normalize arguments without changing the renderer
 * service ABI. Scratch frames are bounded, per-thread guest memory; callbacks
 * can nest without overwriting the caller's real stack or another frame.
 * Generated adapters prove argument sources and the original RET separately. */
void pilot_d3d_dispatch(unsigned entry);
#ifndef PILOT_SCHEDULING_LLE
static RECOMP_TLS uint32_t graphics_abi_frames;
static RECOMP_TLS unsigned graphics_abi_depth;
static RECOMP_TLS uint32_t graphics_abi_callers[16];
#endif
static void graphics_abi_thread_retire(void)
{
#ifndef PILOT_SCHEDULING_LLE
    if(graphics_abi_depth)stop("thread exits inside graphics ABI callback",graphics_abi_depth);
    if(graphics_abi_frames) {
        uint32_t base=graphics_abi_frames,bytes=0;
        uint32_t status=vm_free(&base,&bytes,MEM_RELEASE);
        if(status)stop("graphics ABI frame retirement failed",status);
        graphics_abi_frames=0;
    }
#endif
}
void pilot_d3d_dispatch_frame(unsigned entry,const uint32_t *arguments,
                              unsigned count,unsigned original_stack_bytes)
{
#ifdef PILOT_SCHEDULING_LLE
    stop("register graphics ABI requires a native scheduling profile",entry);
#else
    enum { frame_bytes=256,frame_count=16,max_arguments=32 };
    if(entry>=PILOT_D3D_COUNT || count>max_arguments ||
       count*4!=pilot_d3d_calls[entry].stack_bytes || (count && !arguments) ||
       (original_stack_bytes&3) || original_stack_bytes>256 || graphics_abi_depth>=frame_count ||
       g_esp>UINT32_MAX-original_stack_bytes-4)
        stop("invalid graphics ABI frame",entry);
    if(!graphics_abi_frames) {
        uint32_t bytes=frame_bytes*frame_count;
        if(vm_allocate(&graphics_abi_frames,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))
            stop("graphics ABI frame allocation failed",entry);
    }
    const uint32_t previous=g_esp,saved_ebx=g_ebx,saved_esi=g_esi,
                   saved_edi=g_edi,saved_ebp=g_ebp;
    graphics_abi_callers[graphics_abi_depth]=previous;
    const uint32_t frame=graphics_abi_frames+(graphics_abi_depth++)*frame_bytes;
    MEM32(frame)=MEM32(previous);
    if(count)memcpy(memory+frame+4,arguments,count*4);
    g_esp=frame;
    pilot_d3d_dispatch(entry);
    if(g_esp!=frame+4+count*4)stop("graphics ABI canonical cleanup mismatch",entry);
    graphics_abi_callers[--graphics_abi_depth]=0;
    g_esp=previous+4+original_stack_bytes;
    g_ebx=saved_ebx;g_esi=saved_esi;g_edi=saved_edi;g_ebp=saved_ebp;
#endif
}
static uint32_t graphics_abi_caller_stack(void)
{
#ifndef PILOT_SCHEDULING_LLE
    return graphics_abi_depth?graphics_abi_callers[graphics_abi_depth-1]:g_esp;
#else
    return g_esp;
#endif
}
#ifdef PILOT_D3D_NATIVE_CPU_SERVICE_VERSION
#if PILOT_D3D_NATIVE_CPU_SERVICE_VERSION != 1
#error Unsupported original CPU graphics service bridge
#endif
static void graphics_original_cpu_service(void (*query)(void),unsigned stack_bytes)
{
    /* Generated CPU routines use downward-growing Xbox stacks. The temporary
     * service frame has no such stack reservation; borrow the actual caller's
     * unchanged stdcall frame and validate its exact cleanup before returning. */
    uint32_t service=g_esp,caller=graphics_abi_caller_stack();
    g_esp=caller;query();
    if(g_esp!=caller+4+stack_bytes)stop("original CPU service cleanup mismatch",g_esp);
    g_esp=service+4+stack_bytes;
}
#endif
