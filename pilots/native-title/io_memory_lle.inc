/* Original virtual-memory service transport. The captured kernel owns VADs,
 * PTEs/PFNs, allocation, zeroing, protection, queries and release. Native CPU
 * pages expose those same bytes; this is not a second allocator implementation.
 * Caller serializes the guest CPU and runtime ownership ledger. */
const char *pilot_io_device_vm(unsigned ordinal,unsigned char *guest,
    const uint32_t inputs[5],uint32_t outputs[7],uint32_t *status)
{
    AcquireSRWLockExclusive(&lock);
    if(!guest_memory)initialize(guest);
    if(!failure && (guest_memory!=guest || execution_depth))failure="memory LLE invalid caller context";
    if(!failure && ordinal!=184 && ordinal!=199 && ordinal!=204 && ordinal!=217)
        failure="memory LLE unsupported VM ordinal";
    if(failure)goto done;
    ++execution_depth;
    const uint32_t slots=SCRATCH+0x2000;
    uint32_t words[]={inputs[0],inputs[1],0xdeadbeef},args[5],count;
    uc_err error=uc_mem_write(cpu,slots,words,sizeof(words));
    if(error){failure=uc_strerror(error);goto finish;}
    if(ordinal==184) {
        args[0]=slots;args[1]=inputs[4];args[2]=slots+4;args[3]=inputs[2];args[4]=inputs[3];count=5;
    } else if(ordinal==199) {
        args[0]=slots;args[1]=slots+4;args[2]=inputs[2];count=3;
    } else if(ordinal==204) {
        args[0]=slots;args[1]=slots+4;args[2]=inputs[3];args[3]=slots+8;count=4;
    } else {args[0]=inputs[0];args[1]=slots+32;count=2;}
    (void)count; /* execute owns the independently checked stdcall arity. */
    /* Kernel allocation/free may access its physical backing regardless of
     * user PTE permissions. Lift host-only access restrictions while this
     * sole guest CPU executes the original call, then restore actual policy. */
    uint32_t lifted[MAX_PUBLISHED],protections[MAX_PUBLISHED];unsigned lift_count=0;
    if(ordinal!=217)for(unsigned i=0;i<published_count;++i) {
        uint32_t va=published[i].va;
        if(va<0x80000000u && va>=(inputs[0]&~4095u) &&
           va<(uint64_t)inputs[0]+inputs[1]) {
            if(published[i].pins && ordinal==199) {failure="memory LLE release while DMA pinned";goto restore;}
            DWORD previous;
            if(!VirtualProtect(guest_memory+va,4096,PAGE_READWRITE,&previous)) {
                failure="memory LLE kernel access protection failed";goto restore;
            }
            lifted[lift_count]=va;protections[lift_count++]=previous;
        }
    }
    execute(ordinal,args,status);
    if(!failure && *status)fprintf(stderr,"[MEMORY LLE RESULT] ordinal=%u base=%08x bytes=%u type=%x status=%08x\n",
        ordinal,inputs[0],inputs[1],inputs[2],*status);
    if(!failure) {
        error=uc_mem_read(cpu,ordinal==217?slots+32:slots,outputs,ordinal==217?28:12);
        if(error)failure=uc_strerror(error);
    }
    if(!failure && *status==0xc0000017u) {
        /* Record real original counters on allocation failure, after saving
         * caller outputs. This diagnostic cannot change the returned status. */
        uint32_t statistics[9]={36},address=slots+64,queried=0;
        error=uc_mem_write(cpu,address,statistics,sizeof(statistics));
        if(error)failure=uc_strerror(error);
        else execute(181,&address,&queried);
        if(!failure && !queried) {
            error=uc_mem_read(cpu,address,statistics,sizeof(statistics));
            if(error)failure=uc_strerror(error);
            else fprintf(stderr,"[MEMORY LLE EXHAUSTED] available=%u committed=%u reserved=%u cache=%u pool=%u stack=%u image=%u published=%u\n",
                statistics[2],statistics[3],statistics[4],statistics[5],statistics[6],statistics[7],statistics[8],published_count);
        }
    }
restore:
    for(unsigned i=0;i<lift_count;++i)if(publication_slot(lifted[i])<published_count) {
        DWORD previous;
        if(!VirtualProtect(guest_memory+lifted[i],4096,protections[i],&previous))
            failure="memory LLE caller protection restore failed";
    }
    if(!failure && !*status && ordinal==184 && (inputs[2]&MEM_COMMIT)) {
        vm_publication_base=outputs[0];vm_publication_bytes=outputs[1];
        publish_range(outputs[0],outputs[1]);
        vm_publication_base=vm_publication_bytes=0;
    }
    if(!failure && !*status && (ordinal==184 || ordinal==204)) {
        /* Query the original PTE-derived protection, including recommit of
         * existing pages. Never infer that a successful call changed all PTEs. */
        uint64_t end=(uint64_t)outputs[0]+outputs[1];
        for(uint32_t page=outputs[0];!failure && (uint64_t)page<end;page+=4096) {
            if(publication_slot(page)==published_count)continue;
            uint32_t query[]={page,slots+32},result=0,info[7];
            execute(217,query,&result);
            if(failure)break;
            if(result){failure="memory LLE query after successful mutation failed";break;}
            error=uc_mem_read(cpu,slots+32,info,sizeof(info));
            if(error){failure=uc_strerror(error);break;}
            DWORD previous,protect=info[5];
            if(info[4]!=MEM_COMMIT || (protect!=PAGE_NOACCESS && protect!=PAGE_READONLY &&
               protect!=PAGE_READWRITE && protect!=PAGE_EXECUTE && protect!=PAGE_EXECUTE_READ &&
               protect!=PAGE_EXECUTE_READWRITE)) {
                failure="memory LLE unsupported committed-page policy";break;
            }
            if(!VirtualProtect(guest_memory+page,4096,protect,&previous))
                failure="memory LLE host PTE protection publication failed";
        }
    }
finish:
    --execution_depth;
done:
    const char *diagnostic=failure;ReleaseSRWLockExclusive(&lock);return diagnostic;
}
