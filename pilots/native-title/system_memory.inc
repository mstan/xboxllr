/* MmAllocateSystemMemory/MmFreeSystemMemory (nxdk stdcall ABI, ordinals
 * 167/172). Page-aligned, nonpaged CPU buffers share the actual 64 MiB guest
 * budget. This arena is separate from pool blocks and contiguous/DMA storage;
 * it makes no physical-address or device-translation claim. Allocation/free
 * semantics were checked against the external kernel memory-manager reference;
 * no donor implementation is incorporated.
 * https://github.com/XboxDev/nxdk/blob/master/lib/xboxkrnl/xboxkrnl.h */
#define SYSTEM_MEMORY_BASE 0xd4000000u
#define SYSTEM_MEMORY_PAGES 16384u
static uint16_t system_memory_owner[SYSTEM_MEMORY_PAGES];
static uint16_t system_memory_end[SYSTEM_MEMORY_PAGES];
static uint32_t system_memory_allocate(uint32_t bytes,uint32_t protect)
{
    unsigned access=protect&6,cache=protect&0x600;
    if(!bytes || bytes>64u*1024*1024 || (protect&~0x606u) ||
       (access!=2 && access!=4) || cache==0x600)return 0;
#if defined(PILOT_MEMORY_LLE) || defined(PILOT_IO_DEVICE_LLE) || defined(PILOT_USB_LLE)
    stop("system CPU memory requires the native memory owner",167);
#endif
    unsigned pages=(bytes+4095u)/4096u,first=0,run=0;
    vm_ensure();AcquireSRWLockExclusive(&vm_lock);uint32_t result=0;
    if(vm_committed(0,UINT64_C(0x100000000))+vm_service_bytes+(uint64_t)pages*4096>64ull*1024*1024)goto done;
    for(unsigned page=0;page<SYSTEM_MEMORY_PAGES;++page) {
        if(system_memory_owner[page])run=0;
        else if(++run==pages){first=page+1-pages;goto found;}
    }
    goto done;
found:
    result=SYSTEM_MEMORY_BASE+first*4096;
    if(!VirtualAlloc(memory+result,pages*4096,MEM_COMMIT,access)){result=0;goto done;}
    for(unsigned page=first;page<first+pages;++page)system_memory_owner[page]=(uint16_t)(first+1);
    system_memory_end[first]=(uint16_t)(first+pages);
    fprintf(stderr,"[SYSTEM MEMORY CREATE] base=%08x bytes=%u protect=%x\n",result,pages*4096,protect);
done:
    ReleaseSRWLockExclusive(&vm_lock);return result;
}
static uint32_t system_memory_free(uint32_t base,uint32_t bytes)
{
    if(base<SYSTEM_MEMORY_BASE || base>=SYSTEM_MEMORY_BASE+SYSTEM_MEMORY_PAGES*4096u ||
       (base&4095u) || bytes>64u*1024*1024)return 0;
    unsigned first=(base-SYSTEM_MEMORY_BASE)/4096,owner,end,pages;
    vm_ensure();AcquireSRWLockExclusive(&vm_lock);uint32_t result=0;
    if(!system_memory_owner[first])goto done;
    owner=system_memory_owner[first]-1;end=system_memory_end[owner];
    pages=bytes?(bytes+4095u)/4096u:end-first;
    if(!pages || pages>end-first)goto done;
    for(unsigned page=first;page<first+pages;++page)if(system_memory_owner[page]!=owner+1)goto done;
    if(!VirtualFree(memory+base,pages*4096,MEM_DECOMMIT))stop("system CPU memory decommit failed",base);
    memset(system_memory_owner+first,0,pages*sizeof(*system_memory_owner));
    /* A partial release splits the surviving ownership intervals, so a later
     * zero-size release cannot cross a hole or release a replacement buffer. */
    if(first>owner)system_memory_end[owner]=(uint16_t)first;
    else system_memory_end[owner]=0;
    if(first+pages<end) {
        unsigned right=first+pages;system_memory_end[right]=(uint16_t)end;
        for(unsigned page=right;page<end;++page)system_memory_owner[page]=(uint16_t)(right+1);
    }
    result=pages;fprintf(stderr,"[SYSTEM MEMORY FREE] base=%08x pages=%u\n",base,pages);
done:
    ReleaseSRWLockExclusive(&vm_lock);return result;
}
static void system_memory_kernel(unsigned ordinal)
{
    uint32_t first=MEM32(g_esp+4),second=MEM32(g_esp+8);
    g_eax=ordinal==167?system_memory_allocate(first,second):system_memory_free(first,second);
    g_esp+=12;
}
