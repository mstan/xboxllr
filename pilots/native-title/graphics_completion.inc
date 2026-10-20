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
static void graphics_completion_wait(uint64_t serial)
{
#ifdef PILOT_SCHEDULING_LLE
    struct ComponentWait wait={0};
#endif
    for(;;) {
        if(!pilot_gfx_checkpoint_busy(serial)) {
#ifdef PILOT_SCHEDULING_LLE
            if(wait.event) {
                ReleaseSRWLockExclusive(&graphics_lock);
                const char *error=component_wait_close(&wait);
                AcquireSRWLockExclusive(&graphics_lock);
                if(error)stop(error,(uint32_t)serial);
            }
#endif
            return;
        }
        if(dispatch_in_dpc)stop("GPU completion wait at DPC",(uint32_t)serial);
#ifndef PILOT_SCHEDULING_LLE
        HANDLE event=(HANDLE)pilot_gfx_checkpoint_event(serial);
#endif
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
        DWORD result=event?WaitForSingleObject(event,INFINITE):WAIT_TIMEOUT;
        DWORD error=result==WAIT_FAILED?GetLastError():0;
        if(!event)Sleep(1);
        if(event)CloseHandle(event);
        scheduler_block_end(blocked);
#endif
        AcquireSRWLockExclusive(&graphics_lock);--graphics_clock_waiters;
#ifdef PILOT_SCHEDULING_LLE
        if(error)stop(error,(uint32_t)serial);
#else
        if(error)stop("native GPU completion wait failed",error);
#endif
    }
}
static void graphics_fence_wait(uint32_t token)
{
    if(!token || token>=graphics_fence_next)stop("unowned GPU fence",token);
    graphics_fence_poll();if(token<=graphics_fence_done)return;
    graphics_completion_wait(graphics_fences[token%64]);graphics_fence_poll();
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

/* Optional services are resolved from the source-proved canonical API table.
 * Old generated profiles need no new enum or header migration. This cache is
 * thread-local; the selected table is immutable for the process lifetime. */
static unsigned graphics_resource_wait_entry(void)
{
    static RECOMP_TLS unsigned cached;
    if(!cached) {
        cached=PILOT_D3D_COUNT+1;
        for(unsigned i=0;i<PILOT_D3D_COUNT;++i)if(!strcmp(pilot_d3d_calls[i].name,"D3D_BlockOnResource")){cached=i+1;break;}
    }
    return cached-1;
}
static unsigned graphics_resource_waiters;
enum { GRAPHICS_WAIT_INDEX=4 };
struct GraphicsResourceWait {unsigned kind,slot;};
static uint64_t graphics_resource_wait_retain(uint32_t header,struct GraphicsResourceWait *lease)
{
    graphics_pointer(header,12);
    uint32_t canonical=header,handle=0,common=MEM32(header);unsigned kind=0,slot=0;
    if(graphics_clock_closing || graphics_resource_waiters==UINT_MAX)stop("resource wait during device close",header);
    if(header==graphics_headers || header==graphics_headers+32 || header==graphics_headers+64) {
        kind=PILOT_GFX_SURFACE;slot=handle=graphics_surface_index(header);
    } else if(graphics_index_find(header)>=0 || ((common>>16)&7)==1) {
        struct GraphicsIndexObject *object=graphics_index(header);slot=(unsigned)(object-graphics_indices);
        if(object->waiters==UINT_MAX)stop("index wait ownership overflow",header);
        ++object->waiters;kind=GRAPHICS_WAIT_INDEX;
    } else if(graphics_vertex_find(header)>=0 || ((common>>16)&7)==0) {
        struct GraphicsVertexObject *object=graphics_vertex(header);slot=(unsigned)(object-graphics_vertices);
        if(object->waiters==UINT_MAX)stop("vertex wait ownership overflow",header);
        ++object->waiters;handle=object->native;kind=PILOT_GFX_VERTEX;
    } else {
        if(((common>>16)&7)!=4 && ((common>>16)&7)!=5)stop("unsupported resource wait type",header);
        slot=graphics_texture_slot(header);struct GraphicsTextureObject *object=&graphics_textures[slot];
        if(object->waiters==UINT_MAX)stop("texture wait ownership overflow",header);
        ++object->waiters;kind=PILOT_GFX_TEXTURE;
        if(object->parent)object=&graphics_textures[object->parent-1];
        canonical=object->header;handle=object->native;
    }
    lease->kind=kind;lease->slot=slot;++graphics_resource_waiters;
    /* Original bound resources wait for the current submitted stream, not
     * for their internal binding references to vanish. Surface views also
     * inspect the parent's references. Unbound resources wait for their use. */
    if((common|MEM32(canonical))&0x780000u)return pilot_gfx_checkpoint();
    return kind==GRAPHICS_WAIT_INDEX?graphics_indices[slot].last_use:pilot_gfx_resource_serial(kind,handle);
}
static void graphics_resource_wait_release(const struct GraphicsResourceWait *lease)
{
    if(!graphics_resource_waiters)stop("resource wait ownership underflow",lease->kind);
    --graphics_resource_waiters;
    switch(lease->kind) {
    case PILOT_GFX_SURFACE:break;
    case PILOT_GFX_VERTEX: {
        struct GraphicsVertexObject *object=&graphics_vertices[lease->slot];
        if(!object->waiters)stop("vertex wait ownership underflow",lease->slot);
        --object->waiters;graphics_vertex_collect(object);break;
    }
    case PILOT_GFX_TEXTURE: {
        struct GraphicsTextureObject *object=&graphics_textures[lease->slot];
        if(!object->waiters)stop("texture wait ownership underflow",lease->slot);
        --object->waiters;graphics_texture_collect(lease->slot);break;
    }
    case GRAPHICS_WAIT_INDEX: {
        struct GraphicsIndexObject *object=&graphics_indices[lease->slot];
        if(!object->waiters)stop("index wait ownership underflow",lease->slot);
        --object->waiters;graphics_index_collect(object);break;
    }
    default:stop("invalid resource wait owner",lease->kind);
    }
}
static void graphics_resource_wait(uint32_t header)
{
    struct GraphicsResourceWait lease;uint64_t serial=graphics_resource_wait_retain(header,&lease);
    graphics_completion_wait(serial);graphics_resource_wait_release(&lease);
    /* BlockOnResource has no public-reference or fence-cache write. IsBusy
     * retains its distinct bound/busy and canonical-cache-clearing contract. */
    static unsigned reported;
    if(reported++<3)fprintf(stderr,"[RESOURCE WAIT] header=%08x kind=%u serial=%llu completed\n",header,lease.kind,(unsigned long long)serial);
}
