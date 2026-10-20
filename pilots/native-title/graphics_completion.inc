/* Binding references remain busy even after their last command completes.
 * Surface views share their parent's GPU work and fence cache, as in the
 * original Resource_IsBusy entry. Native serials are private implementation
 * state: guest fence words are cleared only after actual completion. */
static uint64_t graphics_fences[64];
static uint32_t graphics_fence_next=1,graphics_fence_done;
static void graphics_fence_poll(void)
{
    while(graphics_fence_done+1<graphics_fence_next &&
          !pilot_gfx_checkpoint_busy(graphics_fences[(graphics_fence_done+1)%64]))++graphics_fence_done;
}
static void graphics_fence_wait(uint32_t token)
{
    if(!token || token>=graphics_fence_next)stop("unowned GPU fence",token);
#ifdef PILOT_SCHEDULING_LLE
    struct ComponentWait wait={0};
#endif
    for(;;) {
        graphics_fence_poll();if(token<=graphics_fence_done) {
#ifdef PILOT_SCHEDULING_LLE
            if(wait.event) {
                ReleaseSRWLockExclusive(&graphics_lock);
                const char *error=component_wait_close(&wait);
                AcquireSRWLockExclusive(&graphics_lock);
                if(error)stop(error,token);
            }
#endif
            return;
        }
        ++graphics_clock_waiters;ReleaseSRWLockExclusive(&graphics_lock);
#ifdef PILOT_SCHEDULING_LLE
        /* Only the native GPU query above retires a fence. The finite
         * original event timeout gives the real dispatcher/clock ownership
         * of this caller while GPU work progresses, with no host lock held.
         * Retain the shared original component gate to serialize passive
         * graphics/VM owners; interrupt/device work never takes that gate. */
        const char *error=wait.event?NULL:component_wait_open(&wait);
        uint32_t status=0;
        if(!error)error=component_wait_slice(&wait,10000,&status);
#else
        int blocked=scheduler_block_begin();
        Sleep(1);
        scheduler_block_end(blocked);
#endif
        AcquireSRWLockExclusive(&graphics_lock);--graphics_clock_waiters;
#ifdef PILOT_SCHEDULING_LLE
        if(error)stop(error,token);
#endif
    }
}
static uint32_t graphics_fence_insert(void)
{
    graphics_fence_poll();
    if(graphics_fence_next-graphics_fence_done>=64)graphics_fence_wait(graphics_fence_done+1);
    if(graphics_fence_next==UINT32_MAX)stop("GPU fence identity exhausted",graphics_fence_next);
    uint32_t token=graphics_fence_next++;
    graphics_fences[token%64]=pilot_gfx_checkpoint();return token;
}
static uint32_t graphics_resource_busy(uint32_t header)
{
    uint32_t canonical=header,handle;unsigned kind;
    if(header==graphics_headers || header==graphics_headers+32 || header==graphics_headers+64) {
        kind=PILOT_GFX_SURFACE;handle=graphics_surface_index(header);
    } else if(graphics_index_find(header)>=0 || (MEM32(header)&0x01070000u)==0x01010000u) {
        struct GraphicsIndexObject *index=graphics_index(header);
        if(index->bindings || pilot_gfx_checkpoint_busy(index->last_use))return 1;
        MEM32(header+8)=0;return 0;
    } else if(graphics_vertex_find(header)>=0) {
        kind=PILOT_GFX_VERTEX;handle=graphics_vertex(header)->native;
    } else {
        struct GraphicsTextureObject *texture=&graphics_textures[graphics_texture_slot(header)];
        if(texture->parent) {
            if(MEM32(header)&0x780000u)return 1;
            texture=&graphics_textures[texture->parent-1];canonical=texture->header;
        }
        kind=PILOT_GFX_TEXTURE;handle=texture->native;
    }
    if(MEM32(canonical)&0x780000u)return 1;
    if(pilot_gfx_resource_busy(kind,handle))return 1;
    MEM32(canonical+8)=0;return 0;
}
