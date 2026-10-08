/* Bounded contiguous-memory service for native consumers. nxdk supplies the
 * kernel ABI. Each allocation owns real committed CPU bytes at its Xbox
 * 0x80000000 alias, aligned and constrained in a 64 MiB physical namespace.
 * This is not a page-table/MMIO model: low physical numbers identify storage
 * to native graphics; arbitrary CPU aliases and device DMA remain unqualified.
 * WRITE_COMBINE is retained as logical protection; host cached CPU storage is
 * coherent with the renderer's explicit upload boundary. */
struct PhysicalAllocation { uint32_t base,bytes,protect,borrowers; };
static struct PhysicalAllocation physical_allocations[256];
/* Cache policy is logical on the coherent host; access permission is enforced
 * by actual host pages. Track policy per page, including partial-range updates. */
static uint16_t physical_protection[64*1024*1024/4096];
static uint8_t physical_persistent[64*1024*1024/4096];
static uint32_t av_saved_data;
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
#ifdef PILOT_USB_LLE
    /* The original USB driver consumes real physical addresses. Allocate with
     * the same original kernel that owns page translation, rather than asking
     * its captured PTEs to describe an unrelated native allocation. */
    uint32_t args[]={length,low,high,alignment,protect},base=0;
    const char *error=pilot_io_device_lle(166,memory,args,&base);
    if(error)stop(error,166);
    if(!base)goto done;
#else
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
#endif
    *slot=(struct PhysicalAllocation){base,bytes,protect,0}; result=base;
    for(uint32_t page=(base&0x0fffffff)/4096;page<((base&0x0fffffff)+bytes)/4096;++page)
        physical_protection[page]=(uint16_t)protect;
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
#ifdef PILOT_USB_LLE
    uint32_t result=0;
    const char *error=pilot_io_device_lle(171,memory,&base,&result);
    if(error)stop(error,171);
#else
    if(!VirtualFree(memory+base,a->bytes,MEM_DECOMMIT))stop("contiguous release failed",base);
#endif
    memset(physical_protection+(base&0x0fffffff)/4096,0,a->bytes/4096*sizeof(physical_protection[0]));
    memset(physical_persistent+(base&0x0fffffff)/4096,0,a->bytes/4096);
    memset(a,0,sizeof(*a));ReleaseSRWLockExclusive(&vm_lock);
}
static void physical_persist(uint32_t base,uint32_t bytes,int persist)
{
    /* Original ordinal 178 changes PTE bit 10 on every touched page. Real
     * allocations survive graphics teardown; firmware relaunch still stops. */
    if(!bytes || (uint64_t)base+bytes>0x100000000ull)stop("invalid persistence range",base);
    uint32_t low=base&~4095u;
    uint64_t end=((uint64_t)base+bytes+4095)&~4095ull;
    AcquireSRWLockExclusive(&vm_lock);
    if(!physical_find(low,(uint32_t)(end-low)))stop("persistence requires owned contiguous pages",base);
#ifdef PILOT_USB_LLE
    uint32_t args[]={base,bytes,(uint32_t)!!persist},result=0;
    const char *error=pilot_io_device_lle(178,memory,args,&result);
    if(error)stop(error,178);
#endif
    memset(physical_persistent+(low&0x0fffffff)/4096,!!persist,(size_t)((end-low)/4096));
    ReleaseSRWLockExclusive(&vm_lock);
}
static void physical_protect(uint32_t base,uint32_t size,uint32_t protect)
{
    /* Original kernel export 182 accepts RO/RW plus one cache policy. Invalid
     * protection leaves all PTEs unchanged. This is a void stdcall service. */
    uint32_t access=protect&6,cache=protect&0x600;
    if((protect&~0x606u) || (access!=2 && access!=4) || cache==0x600)return;
    if(!size || (uint64_t)base+size>0x100000000ull)stop("unqualified contiguous protection range",base);
    uint32_t low=base&~4095u;
    uint64_t end=((uint64_t)base+size+4095)&~4095ull;
    vm_ensure();AcquireSRWLockExclusive(&vm_lock);
    if(!physical_find(low,(uint32_t)(end-low)))stop("protection requires owned contiguous pages",base);
#ifdef PILOT_USB_LLE
    uint32_t args[]={base,size,protect},result=0;
    const char *error=pilot_io_device_lle(182,memory,args,&result);
    if(error)stop(error,182);
#endif
    DWORD old;
    if(!VirtualProtect(memory+low,(SIZE_T)(end-low),access,&old))stop("host contiguous protection failed",GetLastError());
    for(uint32_t page=(low&0x0fffffff)/4096;page<(end-0x80000000ull)/4096;++page)
        physical_protection[page]=(uint16_t)protect;
    ReleaseSRWLockExclusive(&vm_lock);
}
static uint32_t physical_query_protection(uint32_t address)
{
    AcquireSRWLockShared(&vm_lock);
    if(!physical_find(address,1))stop("protection query requires owned contiguous storage",address);
    MEMORY_BASIC_INFORMATION info;
    if(!VirtualQuery(memory+address,&info,sizeof(info)) || info.State!=MEM_COMMIT)
        stop("host contiguous protection query failed",address);
    uint32_t protect=physical_protection[(address&0x0fffffff)/4096];
    if(info.Protect!=(protect&6))stop("contiguous protection ownership mismatch",address);
    ReleaseSRWLockShared(&vm_lock);return protect;
}
static void physical_kernel(unsigned ordinal)
{
    uint32_t first=ordinal==1?0:MEM32(g_esp+4);
    if(ordinal==1) {g_eax=av_saved_data;g_esp+=4;}
    else if(ordinal==4) {av_saved_data=first;g_eax=first;g_esp+=8;}
    else if(ordinal==178) {physical_persist(first,MEM32(g_esp+8),MEM8(g_esp+12));g_esp+=16;}
    else if(ordinal==182) {
        physical_protect(first,MEM32(g_esp+8),MEM32(g_esp+12));g_esp+=16;
    } else if(ordinal==179) {
        g_eax=physical_query_protection(first);g_esp+=8;
    } else if(ordinal==180) {
        AcquireSRWLockShared(&vm_lock);
        struct PhysicalAllocation *a=physical_find(first,1);
        if(!a || a->base!=first)stop("allocation-size query requires an owned contiguous base",first);
        g_eax=a->bytes;
        ReleaseSRWLockShared(&vm_lock);g_esp+=8;
    } else if(ordinal==166) {
        g_eax=physical_allocate(first,MEM32(g_esp+8),MEM32(g_esp+12),MEM32(g_esp+16),MEM32(g_esp+20));
        g_esp+=24;
    } else if(ordinal==165) {
        g_eax=physical_allocate(first,0,0xffffffffu,0,PAGE_READWRITE);g_esp+=8;
    } else { physical_free(first);g_esp+=8; }
}
