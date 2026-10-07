/* Bounded contiguous-memory service for native consumers. nxdk supplies the
 * kernel ABI. Each allocation owns real committed CPU bytes at its Xbox
 * 0x80000000 alias, aligned and constrained in a 64 MiB physical namespace.
 * This is not a page-table/MMIO model: low physical numbers identify storage
 * to native graphics; arbitrary CPU aliases and device DMA remain unqualified.
 * WRITE_COMBINE is retained as logical protection; host cached CPU storage is
 * coherent with the renderer's explicit upload boundary. */
struct PhysicalAllocation { uint32_t base,bytes,protect,borrowers; };
static struct PhysicalAllocation physical_allocations[256];
static struct PhysicalAllocation *physical_find(uint32_t address,uint32_t bytes)
{
    for(unsigned i=0;i<256;++i) {
        struct PhysicalAllocation *a=&physical_allocations[i];
        if(a->bytes && address>=a->base && (uint64_t)address+bytes<=(uint64_t)a->base+a->bytes) return a;
    }
    return NULL;
}
static uint32_t physical_allocate(uint32_t length,uint32_t low,uint32_t high,uint32_t alignment,uint32_t protect)
{
    if(protect!=PAGE_READWRITE && protect!=(PAGE_READWRITE|PAGE_WRITECOMBINE))
        stop("unqualified contiguous memory protection",protect);
    if(!length || length>64u*1024*1024 || low>high || (alignment && (alignment&(alignment-1))))return 0;
    if(alignment<4096)alignment=4096;
    uint32_t bytes=(length+4095)&~4095u;
    uint64_t limit=(uint64_t)high+1;
    if(limit>64u*1024*1024)limit=64u*1024*1024;
    uint64_t at=low<0x10000?0x10000:low;
    at=(at+alignment-1)&~((uint64_t)alignment-1);
    vm_ensure(); AcquireSRWLockExclusive(&vm_lock);
    uint32_t result=0; struct PhysicalAllocation *slot=NULL;
    for(unsigned i=0;i<256;++i)if(!physical_allocations[i].bytes){slot=&physical_allocations[i];break;}
    if(!slot || vm_committed(0,0x100000000ull)+vm_service_bytes+bytes>64ull*1024*1024)goto done;
    for(;;) {
        if(at+bytes>limit)goto done;
        int conflict=0;
        for(unsigned i=0;i<256;++i) {
            struct PhysicalAllocation *a=&physical_allocations[i];
            uint32_t start=a->base&0x0fffffffu;
            if(a->bytes && at+bytes>start && at<(uint64_t)start+a->bytes) {
                at=((uint64_t)start+a->bytes+alignment-1)&~((uint64_t)alignment-1);
                conflict=1;break;
            }
        }
        if(!conflict)break;
    }
    uint32_t base=0x80000000u|(uint32_t)at;
    if(!VirtualAlloc(memory+base,bytes,MEM_COMMIT,PAGE_READWRITE))goto done;
    *slot=(struct PhysicalAllocation){base,bytes,protect,0}; result=base;
    fprintf(stderr,"[CONTIGUOUS CREATE] base=%08x bytes=%u alignment=%u protect=%x\n",base,bytes,alignment,protect);
done:
    ReleaseSRWLockExclusive(&vm_lock);return result;
}
static void physical_free(uint32_t base)
{
    AcquireSRWLockExclusive(&vm_lock);
    struct PhysicalAllocation *a=physical_find(base,1);
    if(!a || a->base!=base)stop("unowned contiguous allocation release",base);
    if(a->borrowers)stop("contiguous allocation still owns registered resources",base);
    if(!VirtualFree(memory+base,a->bytes,MEM_DECOMMIT))stop("contiguous release failed",base);
    memset(a,0,sizeof(*a));ReleaseSRWLockExclusive(&vm_lock);
}
static void physical_kernel(unsigned ordinal)
{
    uint32_t first=MEM32(g_esp+4);
    if(ordinal==166) {
        g_eax=physical_allocate(first,MEM32(g_esp+8),MEM32(g_esp+12),MEM32(g_esp+16),MEM32(g_esp+20));
        g_esp+=24;
    } else if(ordinal==165) {
        g_eax=physical_allocate(first,0,0xffffffffu,0,PAGE_READWRITE);g_esp+=8;
    } else { physical_free(first);g_esp+=8; }
}
