/* Shared C/raw-thread device ownership. Original routines still perform every
 * allocation, page change and interrupt initialization; these hooks publish
 * their actual effects on the same RAM/IRQ owners after the genuine return. */
static void device_caller_prepare(unsigned ordinal,const uint32_t *args,struct DeviceCallerAccess *access)
{
    access->pin_slot=MAX_PUBLISHED;access->retire=0;
    if(gpu_bootstrap_published && (ordinal==17 || ordinal==171) && args[0]==0x80000000u) {
        failure="device LLE console bootstrap page is not a caller allocation";return;
    }
    if(ordinal==173 || ordinal==175) {
        uint32_t start=args[0],bytes=ordinal==175?args[1]:1;
        if(!bytes || (uint64_t)start+bytes>0x100000000ull) {
            failure="device LLE invalid page service range";return;
        }
        if(ordinal==175 && (start<0x80000000u || start>=0xd0000000u)) {
            unsigned empty=MAX_PUBLISHED;
            for(unsigned i=0;i<MAX_PUBLISHED;++i) {
                if(!pin_ranges[i].count){if(empty==MAX_PUBLISHED)empty=i;continue;}
                if(pin_ranges[i].base==start && pin_ranges[i].bytes==bytes){access->pin_slot=i;break;}
            }
            if(access->pin_slot==MAX_PUBLISHED && !args[2])access->pin_slot=empty;
            if(access->pin_slot==MAX_PUBLISHED){failure="device LLE unmatched pin range or capacity";return;}
        }
        for(uint64_t page=start&~4095u;page<(uint64_t)start+bytes;page+=4096) {
            /* Every caller mapping must be an actual published original-kernel
             * allocation, not unrelated PTEs left in the bootstrap capture. */
            unsigned i=publication_slot((uint32_t)page);
            if(i==published_count && caller_owned((uint32_t)page,4096) && guest_page((uint32_t)page) &&
               !(ordinal==175 && args[2])) {
                image_vm((uint32_t)page,4096,MEM_COMMIT);if(failure)return;
                i=publication_slot((uint32_t)page);
            }
            if(i==published_count){
                fprintf(stderr,"[IO DEVICE LLE PAGE] ordinal=%u start=%08x bytes=%u unlock=%u page=%08x\n",
                        ordinal,start,bytes,ordinal==175?args[2]:0,(uint32_t)page);
                failure="device LLE page service requires owned mapping";return;
            }
            if(ordinal==175 && (page<0x80000000u || page>=0xd0000000u) &&
               (args[2]?published[i].pins==0:published[i].pins==UINT32_MAX)) {
                failure="device LLE page pin imbalance";return;
            }
        }
    }
    contiguous_caller_prepare(ordinal,args);if(failure)return;
    if(ordinal==168 && args[0]!=UINT32_MAX) {
        // This owned kernel capture has an existing GPU reservation. Original
        // MmClaimGpuInstanceMemory releases surplus pages; it does not grow
        // an already reduced reservation. Reject that unqualified lifecycle.
        uint32_t current=kernel_word(PILOT_IO_GPU_RESERVATION_BYTES);
        if(!args[0] || args[0]>current) {
            failure="device LLE GPU instance reservation growth/empty claim";return;
        }
    }
    /* Bound the qualified small device allocation family. Larger allocations
     * require separate original paging/allocator qualification, not guessed NTSTATUS. */
    if(ordinal==65 && args[1]>0x170){failure="device LLE extension exceeds qualified scope";return;}
    access->retire=ordinal==17 || (ordinal==68 && kernel_word(args[0]+4)==0);
    if(ordinal==109 && (args[3]<0x30 || args[3]>=0x40))
        failure="device LLE interrupt outside PIC scope";
}
static void device_caller_complete(unsigned ordinal,const uint32_t *args,uint32_t result,struct DeviceCallerAccess *access)
{
    refresh_mappings();if(failure)return;
    /* Retire/remap existing publications before exposing a new contiguous
     * allocation that may occupy their former physical pages. */
    check_publications(0);if(failure)return;
    if(ordinal==65 && (int32_t)result>=0) {
        /* The output slot may itself be in an existing device extension.
         * Read the completed kernel view before publishing changed pages. */
        uint32_t object;IO_UC(uc_mem_read(cpu,args[5],&object,4));
        publish_range(object,0x48+args[1]);if(failure)return;
    }
    contiguous_caller_complete(ordinal,args,result);if(failure)return;
    if((ordinal==14 || ordinal==15) && result){publish_range(result,args[0]);if(failure)return;}
    check_publications(0);
    if(failure)return;
    if(access->retire){retire_owner(args[0]);if(failure)return;}
    if(ordinal==175) {
        if(access->pin_slot<MAX_PUBLISHED) {
            if(args[2])--pin_ranges[access->pin_slot].count;
            else {pin_ranges[access->pin_slot].base=args[0];pin_ranges[access->pin_slot].bytes=args[1];++pin_ranges[access->pin_slot].count;}
        }
        for(unsigned i=0;i<published_count;++i)if((published[i].va<0x80000000u || published[i].va>=0xd0000000u) &&
            published[i].va>=(args[0]&~4095u) && published[i].va<(uint64_t)args[0]+args[1]) {
            if(args[2])--published[i].pins;else ++published[i].pins;
        }
    }
    if(ordinal==109) {
        uint32_t irq=args[3]-0x30;
        if(irq>=16){failure="device LLE interrupt outside PIC scope";return;}
        /* A second initialized object must not replace an active vector. */
        if(!interrupt_objects[irq])interrupt_objects[irq]=args[0];
    }
    if(ordinal==98 && (result&255)) {
        uint32_t irq;IO_UC(uc_mem_read(cpu,args[0]+8,&irq,4));
        if(irq>=16){failure="device LLE connected IRQ out of scope";return;}
        interrupt_objects[irq]=args[0];
    }
    InterlockedExchange(&current_irql,(LONG)(kernel_word(0x8003da60u)&255));
}
