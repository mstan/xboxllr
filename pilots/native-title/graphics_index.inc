/* Xbox index buffers own CPU words. DrawIndexedVertices copies those words
 * into commands; the native renderer snapshots them into GPU index storage.
 * Headers made by original code remain caller-owned, as for archive textures.
 */
struct GraphicsIndexObject {uint32_t header,data,bytes,refs,bindings;unsigned borrowed;uint64_t last_use;unsigned waiters,retire_original;};
static struct GraphicsIndexObject graphics_indices[256];
static uint32_t graphics_native_index_header;
static uint64_t graphics_index_draws;
static int graphics_index_find(uint32_t header)
{
    for(unsigned i=0;header && i<256;++i)if(graphics_indices[i].header==header)return (int)i;
    return -1;
}
static struct GraphicsIndexObject *graphics_index(uint32_t header)
{
    if(graphics_resource_is_retiring(header))stop("index access during resource retirement",header);
    int slot=graphics_index_find(header);
    if(slot>=0) {
        struct GraphicsIndexObject *object=&graphics_indices[slot];
        if(object->retire_original)stop("index access during pending retirement",header);
        if(object->borrowed && (MEM32(header+4)!=object->data || ((MEM32(header)>>16)&7)!=1))
            stop("borrowed index storage changed while owned",header);
        return object;
    }
    graphics_pointer(header,12);uint32_t common=MEM32(header),data=MEM32(header+4);
    if((common&0x01070000u)!=0x01010000u || !(common&0xffff) ||
       (uint64_t)header+12>UINT32_MAX || data!=header+12)
        stop("unqualified caller-owned index header",header);
    for(slot=0;slot<256 && graphics_indices[slot].header;++slot){}
    if(slot==256)stop("index ownership slots exhausted",header);
    /* The original 12-byte header contains no byte length. Borrowed headers
     * cannot invent one from a surrounding heap page. Every consumed index
     * range still traverses the actual guest mapping at submission. */
    graphics_indices[slot]=(struct GraphicsIndexObject){header,data,0,common&0xffff,0,1,0};
    fprintf(stderr,"[INDEX BORROW] header=%08x data=%08x\n",header,data);
    return &graphics_indices[slot];
}
static uint32_t graphics_index_allocate(uint32_t bytes)
{
    if(bytes>64u*1024*1024-12)return 0;
    unsigned slot;for(slot=0;slot<256 && graphics_indices[slot].header;++slot){}
    if(slot==256)return 0;
    uint32_t header=pool_allocate(bytes+12,0x58444e49u);if(!header)return 0;
    MEM32(header)=0x01010001;MEM32(header+4)=header+12;MEM32(header+8)=0;
    graphics_indices[slot]=(struct GraphicsIndexObject){header,header+12,bytes,1,0,0,0};
    return header;
}
static void graphics_index_collect(struct GraphicsIndexObject *object)
{
    if(object->refs || object->bindings || object->waiters)return;
    if(object->borrowed && (object->retire_original || !(MEM32(object->header)&0xffffu)))
        graphics_original_retire_queue(object->header,0,object->last_use,0);
    if(!object->borrowed)pool_free(object->header);
    memset(object,0,sizeof(*object));
}
static uint32_t graphics_index_ref(uint32_t header,int acquire)
{
    struct GraphicsIndexObject *object=graphics_index(header);
    if(object->borrowed)object->refs=MEM32(header)&0xffff;
    if((!object->refs && (!acquire || !object->bindings)) || (acquire && object->refs==0xffff))
        stop("invalid index reference lifetime",header);
    if(acquire)++object->refs;else --object->refs;
    uint32_t refs=object->refs;MEM32(header)=(MEM32(header)&~0xffffu)|refs;
    graphics_index_collect(object);return refs;
}
static void graphics_index_bind(uint32_t header,uint32_t base)
{
    uint32_t cache=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_INDICES,old=MEM32(cache);
    if(old!=graphics_native_index_header)stop("index cache changed outside its binding owner",old);
#ifndef PILOT_D3D_INDEX_DATA_GLOBAL
    if(header)stop("index data publication is unqualified for this profile",header);
#endif
    struct GraphicsIndexObject *next=header?graphics_index(header):NULL;
    if(next && next->bindings==15)stop("index binding reference overflow",header);
    /* Acquire first, including self-rebind: original SetIndices publishes the
     * data pointer before dropping the old internal reference. */
    if(next){++next->bindings;MEM32(header)=(MEM32(header)&~0x780000u)|(next->bindings<<19);}
#ifdef PILOT_D3D_INDEX_DATA_GLOBAL
    MEM32(PILOT_D3D_INDEX_DATA_GLOBAL)=next?next->data:0;
#endif
    if(old) {
        struct GraphicsIndexObject *previous=graphics_index(old);
        if(previous->borrowed)previous->refs=MEM32(old)&0xffff;
        if(!previous->bindings)stop("index binding reference underflow",old);
        --previous->bindings;MEM32(old)=(MEM32(old)&~0x780000u)|(previous->bindings<<19);
        graphics_index_collect(previous);
    }
    MEM32(cache)=header;MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_BASE_VERTEX)=base;
    graphics_native_index_header=header;
}
static void graphics_indices_close(void)
{
    graphics_index_draws=0;
    for(unsigned i=0;i<256;++i)if(graphics_indices[i].header) {
        if(graphics_indices[i].bindings || !graphics_indices[i].borrowed)
            stop("device close with outstanding index ownership",graphics_indices[i].header);
        memset(&graphics_indices[i],0,sizeof(graphics_indices[i]));
    }
}
