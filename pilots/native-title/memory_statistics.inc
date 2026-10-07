/* nxdk's 36-byte MM_STATISTICS ABI; field meanings cross-checked against
 * Cxbx's external memory-manager reference. Values describe this runtime's
 * actual bounded guest allocations, not the host's free RAM or fixed guesses.
 * Native GPU resources and host implementation overhead are outside this
 * guest-RAM model, as at allocation admission. */
static uint32_t memory_statistics(uint32_t destination)
{
    if(!destination || (uint64_t)destination+36>0x100000000ull || MEM32(destination)!=36)return VM_INVALID;
    vm_ensure();
    AcquireSRWLockShared(&thread_lock);
    AcquireSRWLockShared(&vm_lock);
    uint64_t resident=vm_committed(0,0x100000000ull),image=0,committed=0,reserved=0,contiguous=0;
    uint32_t image_span=file32(0x10c);
    if(image_span) {
        uint32_t low=image_base&~4095u;
        reserved=(((uint64_t)image_base+image_span+4095)&~4095ull)-low;
        image=vm_committed(low,reserved);
    }
    committed=image;
    for(unsigned i=0;i<vm_region_capacity;++i)if(vm_regions[i].size && !vm_regions[i].fixed) {
        reserved+=vm_regions[i].size;
        committed+=vm_committed(vm_regions[i].base,vm_regions[i].size);
    }
    for(unsigned i=0;i<256;++i)contiguous+=physical_allocations[i].bytes;
    uint64_t stacks=vm_committed(0x70000000,0x10000)+vm_committed(0x72002000,0x1e000);
    for(unsigned i=1;i<=thread_count;++i)if(threads[i].stack_size)
        stacks+=vm_committed(0x60100000u+i*0x100000u-threads[i].stack_size,threads[i].stack_size);
    /* Guest kernel/export/context backing belongs to the bounded service pool.
     * Contiguous allocations reduce AvailablePages but have no field of their
     * own, matching the ABI's non-exhaustive usage breakdown. */
    uint64_t classified=committed+contiguous+stacks,cache=(uint64_t)fsc_pages*4096;
    if(classified>resident || cache!=vm_service_bytes || resident+cache>64ull*1024*1024 || reserved>0xffffffffu)
        stop("guest memory accounting inconsistent",destination);
    uint32_t values[]={36,16384,(uint32_t)((64ull*1024*1024-resident-cache)/4096),
        (uint32_t)committed,(uint32_t)reserved,(uint32_t)(cache/4096),
        (uint32_t)((resident-classified)/4096),(uint32_t)(stacks/4096),(uint32_t)(image/4096)};
    ReleaseSRWLockShared(&vm_lock);
    ReleaseSRWLockShared(&thread_lock);
    memcpy(memory+destination,values,sizeof(values));
    fprintf(stderr,"[MEMORY STATISTICS] available=%u committed=%u reserved=%u cache=%u pool=%u stack=%u image=%u\n",
        values[2],values[3],values[4],values[5],values[6],values[7],values[8]);
    return 0;
}
