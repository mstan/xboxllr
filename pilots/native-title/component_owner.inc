/* Whole native components share VM/pool host locks. Serialize their original
 * thread owners before any such host lock, including across device families.
 * Original IRQ/DPC callbacks never acquire this passive-level owner. */
static uint32_t native_component_original_gate;
static RECOMP_TLS unsigned native_component_original_depth;
static void native_component_gate_bind(void)
{
    if(native_component_original_gate)return;
    native_component_original_gate=pool_allocate(28,0x4c78664fu);
    if(!native_component_original_gate)stop("original component gate allocation failed",28);
    uint32_t ignored=0;const char *error=pilot_io_device_lle(291,memory,&native_component_original_gate,&ignored);
    if(error)stop(error,291);
    if(MEM32(native_component_original_gate+16)!=UINT32_MAX || MEM32(native_component_original_gate+20) ||
       MEM32(native_component_original_gate+24))stop("original component gate initialization differs",native_component_original_gate);
}
static const char *native_component_original_enter(void)
{
    if(!native_component_original_gate || !pilot_io_device_thread_component_active() || native_component_original_depth)
        return "original shared component owner outside a nonrecursive component";
    uint32_t ignored=0;
    const char *error=pilot_io_device_thread_component_call(277,&native_component_original_gate,&ignored);
    if(error)return error;
    if(MEM32(native_component_original_gate+20)!=1 ||
       MEM32(native_component_original_gate+24)!=MEM32(0x8003da64u))
        return "original shared component gate lost its actual thread owner";
    native_component_original_depth=1;return NULL;
}
static const char *native_component_original_leave(void)
{
    if(native_component_original_depth!=1)return "original shared component owner release outside its lifetime";
    uint32_t ignored=0;
    const char *error=pilot_io_device_thread_component_call(294,&native_component_original_gate,&ignored);
    if(!error)native_component_original_depth=0;
    return error;
}
