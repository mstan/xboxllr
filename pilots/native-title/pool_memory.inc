/* Native nonpaged-pool ownership. nxdk defines the ABI; the external kernel
 * reference supplies 32-byte small blocks, eight-byte overhead and the 4056
 * byte/page allocation boundary. The host-side ledger is independent of the
 * original buddy lists. Guest payloads are real, shared-budget CPU storage.
 * USB LLE selects the actual original allocator so DMA translation and pinning
 * describe those same allocations; the native ledger still owns their lifetime. */
#ifdef PILOT_IO_DEVICE_LLE
/* Original device objects retain the kernel allocator's D0 namespace. */
#define POOL_BASE 0xe0000000u
#else
#define POOL_BASE 0xd0000000u
#endif
#define POOL_BYTES (64u*1024*1024)
static struct { uint32_t base,span,payload,usable,tag; } pool_blocks[4096];
static uint16_t pool_page_refs[POOL_BYTES/4096];
#ifndef PILOT_USB_LLE
/* Address order avoids restarting a 4096-record conflict search after every
 * occupied block. Stable ownership records and first-fit placement remain. */
static uint16_t pool_order[4096];
static unsigned pool_order_count;
#endif
#ifdef PILOT_NATIVE_SERVICE_ACCOUNTING
static uint64_t vm_native_services_committed(void)
{
    uint64_t bytes=0;
    for(unsigned page=0;page<POOL_BYTES/4096;++page)if(pool_page_refs[page])bytes+=4096;
    for(unsigned i=0;i<physical_allocation_capacity;++i)bytes+=physical_allocations[i].bytes;
    return bytes;
}
#endif
static unsigned pool_find(uint32_t pointer)
{
    for(unsigned i=0;i<4096;++i)if(pool_blocks[i].span && pool_blocks[i].payload==pointer)return i;
    stop("unowned pool block",pointer);
}
#ifdef PILOT_USB_LLE
static const char *pool_original_call(unsigned ordinal,const uint32_t *args,uint32_t *result)
{
    /* A whole native component retains its own original thread and CALL/RET
     * continuation. Re-enter that owner instead of the ordinary synchronous
     * transport; its existing device hooks publish and retire the same RAM. */
    if(pilot_io_device_thread_component_active())
        return pilot_io_device_thread_component_call(ordinal,args,result);
    return pilot_io_device_lle(ordinal,memory,args,result);
}
#endif
static uint32_t pool_allocate(uint32_t bytes,uint32_t tag)
{
    if(!bytes)stop("zero-size pool allocation outside contract",0);
    if(bytes>POOL_BYTES)return 0;
#ifdef PILOT_USB_LLE
    AcquireSRWLockExclusive(&vm_lock);
    unsigned slot;for(slot=0;slot<4096 && pool_blocks[slot].span;++slot){}
    uint32_t pointer=0,usable=0;
    if(slot<4096 && vm_committed(0,0x100000000ull)+vm_service_bytes+bytes<=POOL_BYTES) {
        uint32_t args[]={bytes,tag};
        const char *error=pool_original_call(15,args,&pointer);
        if(error)stop(error,15);
        if(pointer && vm_committed(0,0x100000000ull)+vm_service_bytes>POOL_BYTES) {
            uint32_t ignored=0;error=pool_original_call(17,&pointer,&ignored);
            if(error)stop(error,17);
            pointer=0;
        }
        if(pointer) {
            error=pool_original_call(23,&pointer,&usable);
            if(error)stop(error,23);
            if(usable<bytes)stop("original pool returned undersized allocation",pointer);
            pool_blocks[slot].base=pool_blocks[slot].payload=pointer;
            pool_blocks[slot].span=pool_blocks[slot].usable=usable;pool_blocks[slot].tag=tag;
            fprintf(stderr,"[POOL CREATE] address=%08x requested=%u usable=%u tag=%08x original=1\n",pointer,bytes,usable,tag);
        }
    }
    ReleaseSRWLockExclusive(&vm_lock);return pointer;
#else
    uint32_t large=bytes>4056,alignment=large?4096:32;
    uint32_t span=large?(bytes+4095)&~4095u:(bytes+8+31)&~31u;
    AcquireSRWLockExclusive(&vm_lock);
    unsigned slot;
    for(slot=0;slot<4096 && pool_blocks[slot].span;++slot){}
    uint32_t result=0,at=0;unsigned position=0;
    if(slot==4096)goto done;
    for(;position<pool_order_count;++position) {
        if(!large && (at&4095)+span>4096)at=(at+4095)&~4095u;
        unsigned i=pool_order[position];uint32_t low=pool_blocks[i].base-POOL_BASE;
        if((uint64_t)at+span<=low)break;
        uint32_t high=low+pool_blocks[i].span;
        if(at<high)at=(high+alignment-1)&~(alignment-1);
    }
    if(!large && (at&4095)+span>4096)at=(at+4095)&~4095u;
    if((uint64_t)at+span>POOL_BYTES)goto done;
    unsigned first=at/4096,last=(at+span+4095)/4096;uint32_t additional=0;
    for(unsigned page=first;page<last;++page)if(!pool_page_refs[page])additional+=4096;
    if(vm_committed(0,0x100000000ull)+vm_service_bytes+additional>POOL_BYTES)goto done;
    if(!VirtualAlloc(memory+POOL_BASE+first*4096,(last-first)*4096,MEM_COMMIT,PAGE_READWRITE))goto done;
    for(unsigned page=first;page<last;++page)++pool_page_refs[page];
    result=POOL_BASE+at+(large?0:8);
    pool_blocks[slot].base=POOL_BASE+at;pool_blocks[slot].span=span;
    pool_blocks[slot].payload=result;pool_blocks[slot].usable=span-(large?0:8);pool_blocks[slot].tag=tag;
    memmove(pool_order+position+1,pool_order+position,(pool_order_count-position)*sizeof(pool_order[0]));
    pool_order[position]=(uint16_t)slot;++pool_order_count;
    fprintf(stderr,"[POOL CREATE] address=%08x requested=%u usable=%u tag=%08x\n",result,bytes,pool_blocks[slot].usable,tag);
done:
    ReleaseSRWLockExclusive(&vm_lock);return result;
#endif
}
static void pool_free(uint32_t pointer)
{
    AcquireSRWLockExclusive(&vm_lock);
#ifdef PILOT_USB_LLE
    unsigned slot=pool_find(pointer);uint32_t result=0;
    const char *error=pilot_io_device_release_check(pointer,pool_blocks[slot].usable);
    if(error)stop(error,pointer);
    error=pool_original_call(17,&pointer,&result);
    if(error)stop(error,17);
#else
    unsigned slot=pool_find(pointer),first=(pool_blocks[slot].base-POOL_BASE)/4096;
    unsigned last=(pool_blocks[slot].base-POOL_BASE+pool_blocks[slot].span+4095)/4096;
    for(unsigned page=first;page<last;++page) {
        if(!pool_page_refs[page])stop("pool page ownership underflow",pointer);
        if(!--pool_page_refs[page] && !VirtualFree(memory+POOL_BASE+page*4096,4096,MEM_DECOMMIT))
            stop("pool page release failed",pointer);
    }
    unsigned position=0;while(position<pool_order_count && pool_order[position]!=slot)++position;
    if(position==pool_order_count)stop("pool order ownership missing",pointer);
    memmove(pool_order+position,pool_order+position+1,(pool_order_count-position-1)*sizeof(pool_order[0]));
    --pool_order_count;
#endif
    memset(&pool_blocks[slot],0,sizeof(pool_blocks[slot]));
    ReleaseSRWLockExclusive(&vm_lock);
}
static void pool_kernel(unsigned ordinal)
{
    uint32_t argument=MEM32(g_esp+4);
    if(ordinal==14 || ordinal==15) {
        g_eax=pool_allocate(argument,ordinal==15?MEM32(g_esp+8):0x656e6f4eu);
        g_esp+=ordinal==15?12:8;
    } else if(ordinal==17) {pool_free(argument);g_esp+=8;}
    else {
        AcquireSRWLockShared(&vm_lock);g_eax=pool_blocks[pool_find(argument)].usable;
#ifdef PILOT_USB_LLE
        uint32_t original=0;
        const char *error=pool_original_call(23,&argument,&original);
        if(error)stop(error,23);
        if(original!=g_eax)stop("original pool size differs from owned extent",argument);
        g_eax=original;
#endif
        ReleaseSRWLockShared(&vm_lock);g_esp+=8;
    }
}
