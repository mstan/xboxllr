/* Only immutable local lowering may
 * cross the callback barrier. All guest-cache/native-context publication stays
 * after it: another eligible guest may run during that real wait. */
#if PILOT_GPU_SCALAR_PREPARATION_AVAILABLE
struct GraphicsScalarPreparation {
    struct PilotPreparedScalarState native;
    uint32_t state,value,input,cache;
    unsigned valid;
};
struct {uint64_t frontiers,unknown,aliases,unavailable,prepared,consumed,changed;} graphics_scalar_preparation_stats;
static int graphics_scalar_preparation_enabled(void)
{
    static int enabled=-1;
    if(enabled<0){const char *value=getenv("XLLE_PILOT_GPU_SCALAR_PREPARATION");enabled=!value || strcmp(value,"0")!=0;
        fprintf(stderr,"[GPU SCALAR PREPARATION] enabled=%d certificate=%u; publication and guest-return barrier retained\n",enabled,PILOT_GPU_CALLBACK_EFFECTS_VERSION);}
    return enabled;
}
static int graphics_scalar_capture(unsigned entry,const uint32_t *writes,unsigned count,struct GraphicsScalarPreparation *p)
{
    uint32_t state=entry==PILOT_D3D_D3DDevice_SetRenderState_CullMode?147:
        entry==PILOT_D3D_D3DDevice_SetRenderState_StencilEnable?144:0;
    if(!state)return 0;
    int index=graphics_render_guest_index(state);if(index<0)return 0;
    uint32_t cache=PILOT_D3D_RENDER_STATES+4*(unsigned)index;
    if(!graphics_scalar_effect_span(g_esp,8,writes,count) ||
       !graphics_scalar_effect_span(cache,4,writes,count)){
        ++graphics_scalar_preparation_stats.aliases;return 0;}
    unsigned permission=0;
    if(!pilot_commit_owner_permissions(g_esp,8,&permission) || !(permission&PILOT_MEMORY_READ) ||
       !pilot_write_watch_native_read_begin(memory+g_esp,8)){
        ++graphics_scalar_preparation_stats.unavailable;return 0;}
    uint32_t value=MEM32(g_esp+4);
    int prepared=pilot_gfx_scalar_prepare(state,value,&p->native);
    pilot_write_watch_native_read_end();
    if(!prepared)return 0;
    p->state=state;p->value=value;p->input=g_esp+4;p->cache=cache;p->valid=1;
    ++graphics_scalar_preparation_stats.prepared;return 1;
}
static void graphics_scalar_prepare_frontier(unsigned entry,struct GraphicsScalarPreparation *p)
{
    p->valid=0;
    if((entry!=PILOT_D3D_D3DDevice_SetRenderState_CullMode && entry!=PILOT_D3D_D3DDevice_SetRenderState_StencilEnable) ||
       !graphics_active || dispatch_in_dpc || !graphics_scalar_preparation_enabled() ||
       (!graphics_gpu_head && !graphics_gpu_running))return;
    ++graphics_scalar_preparation_stats.frontiers;
    AcquireSRWLockShared(&scheduler_lock);
    int cpu=scheduler_enabled && scheduler_attached && scheduler_owner==scheduler_slot+1;
    ReleaseSRWLockShared(&scheduler_lock);if(!cpu)return;
    uint32_t writes[128];unsigned count=0;
    if(!graphics_scalar_effects(graphics_gpu_running,graphics_gpu_running_callback,graphics_gpu_head,writes,&count)){
        ++graphics_scalar_preparation_stats.unknown;return;}
    graphics_scalar_capture(entry,writes,count,p);
}
static int graphics_scalar_consume(struct GraphicsScalarPreparation *p)
{
    if(!p->valid)return 0;p->valid=0;
    /* The real barrier may have run another guest or completed an OS writer.
     * Never publish lowering of an argument that changed during that wait. */
    unsigned permission=0;
    if(!pilot_commit_owner_permissions(p->input,4,&permission) || !(permission&PILOT_MEMORY_READ) ||
       !pilot_write_watch_native_read_begin(memory+p->input,4)){
        ++graphics_scalar_preparation_stats.unavailable;return 0;}
    int same=MEM32(p->input)==p->value;pilot_write_watch_native_read_end();
    if(!same){++graphics_scalar_preparation_stats.changed;return 0;}
    pilot_gfx_scalar_commit(&p->native);MEM32(p->cache)=p->value;
    ++graphics_scalar_preparation_stats.consumed;return 1;
}
#endif
