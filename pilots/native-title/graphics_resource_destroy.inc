/* Original Release can tail-call DestroyResource while the last user count
 * is still one; other inline callers publish zero first. The object must have
 * no internal reference. Native-owned storage retires through its real owner,
 * never through the original heap using an unrelated native allocation. */
#if defined(PILOT_D3D_RESOURCE_RETIRE_VERSION) && !defined(PILOT_SCHEDULING_LLE)
#include "graphics_allocator_call.inc"
#endif
static void graphics_original_retire_check(uint32_t header,uint32_t data,unsigned borrowers)
{
#if defined(PILOT_D3D_RESOURCE_RETIRE_VERSION) && !defined(PILOT_SCHEDULING_LLE)
    uint32_t common=MEM32(header),type=(common>>16)&7;
    if(PILOT_D3D_RESOURCE_RETIRE_VERSION!=1 || dispatch_in_dpc || graphics_clock_closing ||
       graphics_resource_retiring==UINT_MAX || graphics_resource_is_retiring(header) || (common&0x78ffffu)>1 ||
       !recomp_lookup(PILOT_D3D_RESOURCE_UNREGISTER) || !recomp_lookup(PILOT_D3D_RESOURCE_TAGGED_FREE))
        stop("unqualified original resource retirement owner",header);
    if(data) {
        if((type!=0 && type!=4) || MEM32(header+4)>=64u*1024*1024 || data!=(MEM32(header+4)|0x80000000u))
            stop("original resource retirement data differs",header);
        AcquireSRWLockExclusive(&vm_lock);struct PhysicalAllocation *allocation=physical_find(data,1);
        int valid=allocation && allocation->base==data && allocation->borrowers==borrowers;
        ReleaseSRWLockExclusive(&vm_lock);
        if(!valid)stop("original resource retirement requires exclusive allocation base",header);
    } else if(type!=1 || MEM32(header+4)!=header+12)
        stop("original resource retirement type/layout is unqualified",header);
#else
    (void)data;(void)borrowers;
    stop("original allocator resource destruction is unqualified",header);
#endif
}
static void graphics_original_retire_queue(uint32_t header,uint32_t data,uint64_t serial,unsigned borrowers)
{
    graphics_original_retire_check(header,data,borrowers);
    struct GraphicsResourceRetirement *retirement=calloc(1,sizeof(*retirement));
    if(!retirement)stop("original resource retirement allocation failed",header);
    retirement->header=header;retirement->data=data;retirement->serial=serial;retirement->owner=GetCurrentThreadId();
    retirement->next=graphics_retirements;graphics_retirements=retirement;++graphics_resource_retiring;
}
static void graphics_original_retire_drain(void)
{
#if defined(PILOT_D3D_RESOURCE_RETIRE_VERSION) && !defined(PILOT_SCHEDULING_LLE)
    for(;;) {
        struct GraphicsResourceRetirement *retirement=graphics_retirements;
        while(retirement && (retirement->active || retirement->owner!=GetCurrentThreadId()))retirement=retirement->next;
        if(!retirement)return;
        retirement->active=1;graphics_completion_wait(retirement->serial);
        if(retirement->data) {
            AcquireSRWLockExclusive(&vm_lock);struct PhysicalAllocation *allocation=physical_find(retirement->data,1);
            int valid=allocation && allocation->base==retirement->data && !allocation->borrowers;
            ReleaseSRWLockExclusive(&vm_lock);
            if(!valid)stop("original retirement still has backing borrowers",retirement->header);
        }
        uint32_t args[]={retirement->header,0x24800000u};
        ReleaseSRWLockExclusive(&graphics_lock);
        graphics_allocator_call(PILOT_D3D_RESOURCE_UNREGISTER,args,1);
        if(retirement->data) {
            uint32_t physical[]={retirement->data,0xa1800000u};
            graphics_allocator_call(PILOT_D3D_RESOURCE_TAGGED_FREE,physical,2);
        }
        graphics_allocator_call(PILOT_D3D_RESOURCE_TAGGED_FREE,args,2);
        AcquireSRWLockExclusive(&graphics_lock);
        struct GraphicsResourceRetirement **link=&graphics_retirements;
        while(*link && *link!=retirement)link=&(*link)->next;
        if(!*link || !graphics_resource_retiring)stop("original resource retirement owner lost",retirement->header);
        *link=retirement->next;--graphics_resource_retiring;
        static unsigned reported;
        if(reported++<12)fprintf(stderr,"[ORIGINAL RESOURCE RETIRED] header=%08x data=%08x serial=%llu\n",
            retirement->header,retirement->data,(unsigned long long)retirement->serial);
        free(retirement);
    }
#else
    if(graphics_retirements)stop("original resource retirement without qualified allocator",graphics_retirements->header);
#endif
}
static void graphics_resource_destroy(uint32_t header)
{
    graphics_pointer(header,12);uint32_t common=MEM32(header),type=(common>>16)&7;
    if(graphics_resource_is_retiring(header))stop("resource destruction during retirement",header);
    if((common&0x78ffffu)>1)stop("destruction requires the last unbound resource reference",header);
    int slot=graphics_vertex_find(header);
    if(slot>=0) {
        struct GraphicsVertexObject *vertex=&graphics_vertices[slot];
        if(vertex->external) {
            if(type || vertex->bindings || vertex->retire_original)stop("inconsistent original vertex destruction",header);
            graphics_original_retire_check(header,vertex->data,1);vertex->retire_original=1;
            vertex->refs=0;graphics_vertex_collect(vertex);return;
        }
        if(type || vertex->bindings || MEM32(header+4)!=(vertex->data&0x0fffffffu))
            stop("inconsistent owned vertex destruction",header);
        vertex->refs=0;graphics_vertex_collect(vertex);return;
    }
    slot=graphics_index_find(header);
    if(slot>=0) {
        struct GraphicsIndexObject *index=&graphics_indices[slot];
        if(index->borrowed) {
            if(type!=1 || index->bindings || index->retire_original)stop("inconsistent original index destruction",header);
            graphics_original_retire_check(header,0,0);index->retire_original=1;
            index->refs=0;graphics_index_collect(index);return;
        }
        if(type!=1 || index->bindings || MEM32(header+4)!=index->data)
            stop("inconsistent owned index destruction",header);
        index->refs=0;graphics_index_collect(index);return;
    }
    for(unsigned i=0;i<graphics_texture_capacity;++i)if(graphics_textures[i].header==header) {
        struct GraphicsTextureObject *texture=&graphics_textures[i];
        if(texture->guest_owned) {
            if(type!=4 || texture->bindings || texture->retire_original || !texture->cpu_borrowed || texture->parent)
                stop("inconsistent original texture destruction",header);
            graphics_original_retire_check(header,texture->data,1);texture->retire_original=1;
            texture->refs=0;graphics_texture_collect(i);return;
        }
        if(type!=(texture->parent?5u:4u) || texture->bindings || MEM32(header+4)!=texture->data)
            stop("inconsistent owned texture destruction",header);
        texture->refs=0;graphics_texture_collect(i);return;
    }
    if(type==0 || type==1 || type==4) {
        /* CPU-created resources need not have been drawn/adopted. */
        graphics_original_retire_queue(header,type==1?0:MEM32(header+4)|0x80000000u,0,0);return;
    }
    stop("unqualified original resource destruction type",header);
}
