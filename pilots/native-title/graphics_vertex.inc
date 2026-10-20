/* Created vertex resources expose real CPU storage. The enhanced renderer
 * snapshots changed bytes into its GPU buffer before drawing; no GPU operation
 * writes this storage, so Lock2 needs no emulated FIFO fence or CPU/GPU wait.
 * Physical/cache aliases use one canonical guest VA, as for native textures. */
#define GRAPHICS_VERTEX_OBJECTS 256
struct GraphicsVertexObject { uint32_t header,data,bytes,refs,native,external,bindings; uint64_t original_reference; unsigned borrowed,waiters,retire_original; };
static struct GraphicsVertexObject graphics_vertices[GRAPHICS_VERTEX_OBJECTS];
static unsigned graphics_vertex_adopt(uint32_t header);
static int graphics_vertex_defer_upload(void)
{
    static int enabled=-1;
    if(enabled<0) {
        const char *value=getenv("XLLE_PILOT_DEFER_STREAM_UPLOAD");
        enabled=!value || strcmp(value,"0");
    }
    return enabled;
}
static int graphics_vertex_find(uint32_t header)
{
    for(unsigned i=0;i<GRAPHICS_VERTEX_OBJECTS;++i)
        if(header && graphics_vertices[i].header==header) return (int)i;
    return -1;
}
static struct GraphicsVertexObject *graphics_vertex(uint32_t header)
{
    if(graphics_resource_is_retiring(header))stop("vertex access during resource retirement",header);
    int slot=graphics_vertex_find(header);
    if(slot<0)slot=(int)graphics_vertex_adopt(header);
    if(graphics_vertices[slot].retire_original)stop("vertex access during pending retirement",header);
    if(graphics_vertices[slot].borrowed &&
       (MEM32(header+4)!=(graphics_vertices[slot].data&0x0fffffffu) || ((MEM32(header)>>16)&7)))
        stop("borrowed vertex storage changed while owned",header);
    return &graphics_vertices[slot];
}
static uint32_t graphics_vertex_allocate(uint32_t length)
{
    if(!length)stop("unqualified zero-length vertex buffer",length);
    if(length>64u*1024*1024) return 0; /* Cannot fit the configured guest RAM. */
    unsigned slot;
    for(slot=0;slot<GRAPHICS_VERTEX_OBJECTS && graphics_vertices[slot].header;++slot) {}
    if(slot==GRAPHICS_VERTEX_OBJECTS) return 0;
    struct GraphicsVertexObject vertex={0};
    vertex.bytes=length; vertex.refs=1; uint32_t bytes=4096;
    if(vm_allocate(&vertex.header,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return 0;
    /* Original CreateVertexBuffer2 stores a physical offset; Lock2 and inline
     * CPU consumers set bit31 to read the cached alias. Ordinary virtual data
     * cannot satisfy that contract even when native Lock happens to work. */
    vertex.data=physical_allocate(length,0,0x03ffffffu,4096,PAGE_READWRITE|PAGE_WRITECOMBINE);
    if(!vertex.data) {
        bytes=0; if(vm_free(&vertex.header,&bytes,MEM_RELEASE))stop("vertex header cleanup failed",vertex.header);
        return 0;
    }
    int32_t result=pilot_gfx_vertex_create(length,&vertex.native);
    if(result<0) {
        physical_free(vertex.data);
        bytes=0; if(vm_free(&vertex.header,&bytes,MEM_RELEASE))stop("vertex header cleanup failed",vertex.header);
        if((uint32_t)result!=0x8007000eu)stop("native vertex allocation failed",(uint32_t)result);
        return 0;
    }
    MEM32(vertex.header)=0x01000001u; MEM32(vertex.header+4)=vertex.data&0x0fffffffu; MEM32(vertex.header+8)=0;
    graphics_vertices[slot]=vertex;
    fprintf(stderr,"[VERTEX CREATE] header=%08x data=%08x bytes=%u\n",vertex.header,vertex.data,length);
    return vertex.header;
}
static void graphics_vertex_collect(struct GraphicsVertexObject *vertex)
{
    if(vertex->refs || vertex->bindings || vertex->waiters)return;
    if(vertex->external && (vertex->retire_original || !(MEM32(vertex->header)&0xffffu)))
        graphics_original_retire_queue(vertex->header,vertex->data,pilot_gfx_resource_serial(PILOT_GFX_VERTEX,vertex->native),1);
    pilot_gfx_vertex_destroy(vertex->native);
    if(vertex->external) {
#ifdef PILOT_SCHEDULING_LLE
        if(!pilot_io_device_contiguous_resource_release(vertex->original_reference))
            stop("lost original registered vertex backing",vertex->data);
#else
        AcquireSRWLockExclusive(&vm_lock);
        struct PhysicalAllocation *allocation=physical_find(vertex->data,vertex->bytes);
        if(!allocation || !allocation->borrowers)stop("lost registered vertex backing",vertex->data);
        --allocation->borrowers;ReleaseSRWLockExclusive(&vm_lock);
#endif
        memset(vertex,0,sizeof(*vertex));return;
    }
    physical_free(vertex->data);
    uint32_t bytes=0;
    if(vm_free(&vertex->header,&bytes,MEM_RELEASE))stop("vertex header release failed",vertex->header);
    memset(vertex,0,sizeof(*vertex));
}
static uint32_t graphics_vertex_ref(uint32_t header,int acquire)
{
    struct GraphicsVertexObject *vertex=graphics_vertex(header);
    if(vertex->borrowed)vertex->refs=MEM32(header)&0xffff;
    if((!vertex->refs && (!acquire || !vertex->bindings)) || (acquire && vertex->refs==0xffff))stop("invalid vertex reference lifetime",header);
    if(acquire)++vertex->refs;else --vertex->refs;
    uint32_t refs=vertex->refs;
    MEM32(header)=(MEM32(header)&~0xffffu)|refs;
    graphics_vertex_collect(vertex);return refs;
}
static void graphics_vertex_binding_ref(uint32_t header,int acquire)
{
    struct GraphicsVertexObject *vertex=graphics_vertex(header);
    if(vertex->borrowed)vertex->refs=MEM32(header)&0xffff;
    if(acquire?vertex->bindings==15:!vertex->bindings)stop("invalid vertex binding ownership",header);
    if(acquire)++vertex->bindings;
    else {--vertex->bindings;MEM32(header+8)=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_RESOURCE_FENCE);}
    MEM32(header)=(MEM32(header)&~0x780000u)|(vertex->bindings<<19);
    graphics_vertex_collect(vertex);
}
static void graphics_vertex_bind(unsigned stream,uint32_t header,uint32_t stride)
{
    if(stream>=16 || stride>2048)stop("unqualified vertex stream/stride",stream);
    uint32_t cache=PILOT_D3D_STREAMS+stream*12,old=MEM32(cache+8),native=0;
    if(header) {
        struct GraphicsVertexObject *vertex=graphics_vertex(header);native=vertex->native;
        /* Phobos hle_d3d8_vertex.c records the binding and consumes data at
         * DrawVertices. Our draw path already snapshots every consumed range,
         * including writes through a retained CPU pointer after this bind.
         * Keep the eager copy as a startup-selected comparison path. */
        if(!graphics_vertex_defer_upload())
            pilot_gfx_vertex_upload(native,memory+vertex->data,vertex->bytes);
    }
    pilot_gfx_vertex_bind(stream,native,stride);
    if(header!=old) {
        if(header)graphics_vertex_binding_ref(header,1);
        MEM32(cache+8)=header;
        if(old)graphics_vertex_binding_ref(old,0);
    } else if(old)MEM32(old+8)=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_RESOURCE_FENCE);
    MEM32(cache)=stride;
}
static uint32_t graphics_vertex_lock(uint32_t header,uint32_t flags)
{
    if(flags&~0xf0u)stop("unqualified vertex lock flags",flags);
    return graphics_vertex(header)->data;
}
static unsigned graphics_vertex_borrow(uint32_t header,uint32_t address,uint32_t refs,unsigned borrowed)
{
    unsigned slot;
    for(slot=0;slot<GRAPHICS_VERTEX_OBJECTS && graphics_vertices[slot].header;++slot) {}
    if(slot==GRAPHICS_VERTEX_OBJECTS)stop("registered vertex resource slots exhausted",header);
    uint32_t bytes;uint64_t original_reference=0;
#ifdef PILOT_SCHEDULING_LLE
    if(!pilot_io_device_contiguous_resource_query((uint32_t)address,1,&bytes))
        stop("resource registration requires original physical backing",(uint32_t)address);
    uint32_t available=0;
    if(!pilot_io_device_contiguous_resource_reference((uint32_t)address,bytes,&original_reference,&available) || available!=bytes)
        stop("resource registration lost original physical backing",(uint32_t)address);
#else
    AcquireSRWLockExclusive(&vm_lock);
    struct PhysicalAllocation *allocation=physical_find((uint32_t)address,1);
    if(!allocation)stop("resource registration requires owned physical backing",(uint32_t)address);
    bytes=allocation->bytes-((uint32_t)address-allocation->base);
    ++allocation->borrowers;ReleaseSRWLockExclusive(&vm_lock);
#endif
    struct GraphicsVertexObject vertex={header,address,bytes,refs,0,1,0,original_reference,borrowed};
    int32_t result=pilot_gfx_vertex_create(bytes,&vertex.native);
    if(result<0) {
#ifdef PILOT_SCHEDULING_LLE
        if(!pilot_io_device_contiguous_resource_release(original_reference))
            stop("registered vertex original backing cleanup failed",(uint32_t)address);
#else
        AcquireSRWLockExclusive(&vm_lock);--allocation->borrowers;ReleaseSRWLockExclusive(&vm_lock);
#endif
        stop("registered vertex native allocation failed",(uint32_t)result);
    }
    graphics_vertices[slot]=vertex;
    return slot;
}
static void graphics_vertex_register(uint32_t header,uint32_t base)
{
    graphics_pointer(header,12);
    uint32_t common=MEM32(header),offset=MEM32(header+4);
    if(common!=1 || graphics_vertex_find(header)>=0)
        stop("unqualified resource registration type/lifetime",common);
    uint64_t address=(uint64_t)base+offset;
    if(address>0xffffffffu)stop("resource registration address overflow",header);
    unsigned slot=graphics_vertex_borrow(header,(uint32_t)address,1,0);
    /* The original Register adds Base to Data then keeps the low 28 bits for
     * vertex resources. The header and allocation remain owned by the caller. */
    MEM32(header+4)=(uint32_t)address&0x0fffffffu;
    fprintf(stderr,"[VERTEX REGISTER] header=%08x data=%08x bytes=%u\n",header,graphics_vertices[slot].data,graphics_vertices[slot].bytes);
}
static unsigned graphics_vertex_adopt(uint32_t header)
{
    graphics_pointer(header,12);
    uint32_t common=MEM32(header),data=MEM32(header+4);
    /* Original archive loading may execute Register itself. Reuse its same
     * contiguous backing lease and GPU buffer owner without registering again
     * or modifying the caller's already-relocated Data. */
    if((common&0x01070000u)!=0x01000000u || !(common&0xffff) || data>=64u*1024*1024)
        stop("unqualified caller-owned vertex header",header);
    unsigned slot=graphics_vertex_borrow(header,data|0x80000000u,common&0xffff,1);
    fprintf(stderr,"[VERTEX BORROW] header=%08x data=%08x cpu=%08x bytes=%u\n",
        header,data,graphics_vertices[slot].data,graphics_vertices[slot].bytes);
    return slot;
}
