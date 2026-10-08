/* Native nonpaged-pool ownership. nxdk defines the ABI; the external kernel
 * reference supplies 32-byte small blocks, eight-byte overhead and the 4056
 * byte/page allocation boundary. The host-side ledger is independent of the
 * original buddy lists. Guest payloads are real, shared-budget CPU storage. */
#ifdef PILOT_IO_DEVICE_LLE
/* Original device objects retain the kernel allocator's D0 namespace. */
#define POOL_BASE 0xe0000000u
#else
#define POOL_BASE 0xd0000000u
#endif
#define POOL_BYTES (64u*1024*1024)
static struct { uint32_t base,span,payload,usable,tag; } pool_blocks[4096];
static uint16_t pool_page_refs[POOL_BYTES/4096];
static unsigned pool_find(uint32_t pointer)
{
    for(unsigned i=0;i<4096;++i)if(pool_blocks[i].span && pool_blocks[i].payload==pointer)return i;
    stop("unowned pool block",pointer);
}
static uint32_t pool_allocate(uint32_t bytes,uint32_t tag)
{
    if(!bytes)stop("zero-size pool allocation outside contract",0);
    if(bytes>POOL_BYTES)return 0;
    uint32_t large=bytes>4056,alignment=large?4096:32;
    uint32_t span=large?(bytes+4095)&~4095u:(bytes+8+31)&~31u;
    AcquireSRWLockExclusive(&vm_lock);
    unsigned slot;
    for(slot=0;slot<4096 && pool_blocks[slot].span;++slot){}
    uint32_t result=0,at=0;
    if(slot==4096)goto done;
    for(;;) {
        if(!large && (at&4095)+span>4096)at=(at+4095)&~4095u;
        if((uint64_t)at+span>POOL_BYTES)goto done;
        int conflict=0;
        for(unsigned i=0;i<4096;++i)if(pool_blocks[i].span) {
            uint32_t low=pool_blocks[i].base-POOL_BASE,high=low+pool_blocks[i].span;
            if(at<high && at+span>low) {at=(high+alignment-1)&~(alignment-1);conflict=1;break;}
        }
        if(!conflict)break;
    }
    unsigned first=at/4096,last=(at+span+4095)/4096;uint32_t additional=0;
    for(unsigned page=first;page<last;++page)if(!pool_page_refs[page])additional+=4096;
    if(vm_committed(0,0x100000000ull)+vm_service_bytes+additional>POOL_BYTES)goto done;
    if(!VirtualAlloc(memory+POOL_BASE+first*4096,(last-first)*4096,MEM_COMMIT,PAGE_READWRITE))goto done;
    for(unsigned page=first;page<last;++page)++pool_page_refs[page];
    result=POOL_BASE+at+(large?0:8);
    pool_blocks[slot].base=POOL_BASE+at;pool_blocks[slot].span=span;
    pool_blocks[slot].payload=result;pool_blocks[slot].usable=span-(large?0:8);pool_blocks[slot].tag=tag;
    fprintf(stderr,"[POOL CREATE] address=%08x requested=%u usable=%u tag=%08x\n",result,bytes,pool_blocks[slot].usable,tag);
done:
    ReleaseSRWLockExclusive(&vm_lock);return result;
}
static void pool_free(uint32_t pointer)
{
    AcquireSRWLockExclusive(&vm_lock);
    unsigned slot=pool_find(pointer),first=(pool_blocks[slot].base-POOL_BASE)/4096;
    unsigned last=(pool_blocks[slot].base-POOL_BASE+pool_blocks[slot].span+4095)/4096;
    for(unsigned page=first;page<last;++page) {
        if(!pool_page_refs[page])stop("pool page ownership underflow",pointer);
        if(!--pool_page_refs[page] && !VirtualFree(memory+POOL_BASE+page*4096,4096,MEM_DECOMMIT))
            stop("pool page release failed",pointer);
    }
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
        ReleaseSRWLockShared(&vm_lock);g_esp+=8;
    }
}
