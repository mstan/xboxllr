/* Shared original VM call postconditions for C callers and actual thread RETs.
 * Returned ranges, PTE/PFN ownership and protections remain original decisions. */
#define VM_UC(command) do {uc_err vm_error=(command);if(vm_error){failure=uc_strerror(vm_error);return;}} while(0)
static uc_err vm_caller_read(uint32_t address,void *buffer,unsigned bytes)
{
    /* Stream through the same PTE/owner admission as CPU reads; a large
     * caller range does not require retaining every alias simultaneously. */
    return owned_cpu_read(cpu,address,buffer,bytes);
}
static int vm_mutation(unsigned ordinal)
{
    return ordinal==184 || ordinal==199 || ordinal==204;
}
static void vm_original_query(uint32_t page,uint32_t info[7],uint32_t *status)
{
    /* Inspect the unchanged original VAD policy without disturbing a live
     * caller's GPRs/flags/FP context or its enclosing service's scratch frame. */
    uc_context *context=NULL;VM_UC(uc_context_alloc(cpu,&context));
    uc_err error=uc_context_save(cpu,context);
    if(!error) {
        const uint32_t slot=SCRATCH+0x2800;
        uint32_t query[]={page,slot};
        ++execution_depth;execute(217,query,status);--execution_depth;
        if(!failure && !*status)error=vm_caller_read(slot,info,7*4);
        uc_err restored=uc_context_restore(cpu,context);
        if(!error)error=restored;
    }
    uc_context_free(context);
    if(error)failure=uc_strerror(error);
}
static void vm_caller_prepare(unsigned ordinal,const uint32_t *args,struct VmCallerAccess *access)
{
    if(!vm_mutation(ordinal))return;
    if(access->active){failure="memory LLE overlapping caller access lease";return;}
    access->active=1;
    uint32_t base=0,bytes=0;
    VM_UC(vm_caller_read(args[0],&base,4));
    VM_UC(vm_caller_read(args[ordinal==184?2:1],&bytes,4));
    if((uint64_t)base+bytes>0x100000000ull) {
        failure="memory LLE caller range wraps address space";return;
    }
    if(ordinal==199 && args[2]==MEM_RELEASE && !bytes) {
        /* Zero size means the original whole reservation, not an empty
         * operation. Query only when a live DMA pin could be affected; use
         * original AllocationBase rather than inventing a second VAD map. */
        unsigned i;for(i=0;i<published_count && (!published[i].pins || published[i].va>=0x80000000u);++i){}
        if(i<published_count) {
            uint32_t info[7],result=0;
            vm_original_query(base,info,&result);if(failure)return;
            if(!result && info[1]==(base&~4095u)) {
                uint32_t allocation=info[1];
                for(;i<published_count;++i)if(published[i].pins && published[i].va<0x80000000u) {
                    vm_original_query(published[i].va,info,&result);if(failure)return;
                    if(!result && info[1]==allocation) {
                        failure="memory LLE release while DMA pinned";return;
                    }
                }
            }
        }
    }
    for(unsigned i=0;i<published_count;++i) {
        uint32_t va=published[i].va;
        if(va>=0x80000000u || va<(base&~4095u) || (uint64_t)va>=(uint64_t)base+bytes)continue;
        if(published[i].pins && ordinal==199) {
            failure="memory LLE release while DMA pinned";return;
        }
        if(!access->pages) {
            access->pages=HeapAlloc(GetProcessHeap(),0,MAX_PUBLISHED*sizeof(*access->pages));
            if(!access->pages){failure="memory LLE caller access lease allocation failed";return;}
        }
        DWORD previous;
        if(!VirtualProtect(guest_memory+va,4096,PAGE_READWRITE,&previous)) {
            failure="memory LLE kernel access protection failed";return;
        }
        access->pages[access->count++]=(struct VmAccessPage){va,previous};
    }
}
static void vm_caller_complete(unsigned ordinal,const uint32_t *args,uint32_t status,struct VmCallerAccess *access)
{
    if(!vm_mutation(ordinal))return;
    if(!access->active){failure="memory LLE mutation returned without caller access lease";return;}
    for(unsigned i=0;i<access->count;++i)if(publication_slot(access->pages[i].va)<published_count) {
        DWORD previous;
        if(!VirtualProtect(guest_memory+access->pages[i].va,4096,access->pages[i].protection,&previous))
            failure="memory LLE caller protection restore failed";
    }
    if(access->pages)HeapFree(GetProcessHeap(),0,access->pages);
    memset(access,0,sizeof(*access));
    if(failure)return;
    check_publications(0);if(failure || status)return;
    uint32_t base=0,bytes=0;
    VM_UC(vm_caller_read(args[0],&base,4));
    VM_UC(vm_caller_read(args[ordinal==184?2:1],&bytes,4));
    if(ordinal==184 && (args[3]&MEM_COMMIT)) {
        if(!bytes || (base&4095) || (bytes&4095) || (uint64_t)base+bytes>0x80000000ull) {
            failure="memory LLE successful commit outside returned user-page range";return;
        }
        vm_publication_base=base;vm_publication_bytes=bytes;
        publish_range(base,bytes);vm_publication_base=vm_publication_bytes=0;
        if(failure)return;
    }
    if(ordinal==184 || ordinal==204) {
        /* Each page policy comes from original NtQueryVirtualMemory. A
         * successful recommit is not assumed to overwrite existing policy. */
        uint64_t end=(uint64_t)base+bytes;
        for(uint64_t page=base;!failure && page<end;) {
            if(publication_slot((uint32_t)page)==published_count){page+=4096;continue;}
            uint32_t result=0,info[7];
            vm_original_query((uint32_t)page,info,&result);if(failure)return;
            if(result){failure="memory LLE query after successful mutation failed";return;}
            uint64_t region_end=(uint64_t)info[0]+info[3];
            if(info[0]>page || (info[0]&4095) || !info[3] || (info[3]&4095) ||
               region_end<=page || region_end>0x100000000ull) {
                failure="memory LLE invalid queried protection extent";return;
            }
            DWORD protect=info[5],previous;
            if(info[4]!=MEM_COMMIT || (protect!=PAGE_NOACCESS && protect!=PAGE_READONLY &&
               protect!=PAGE_READWRITE && protect!=PAGE_EXECUTE && protect!=PAGE_EXECUTE_READ &&
               protect!=PAGE_EXECUTE_READWRITE)) {
                failure="memory LLE unsupported committed-page policy";return;
            }
            /* Original217 already returns a whole contiguous policy run.
             * Reuse that actual extent rather than rescanning the same VAD
             * once per page; retain the existing published-page owner check. */
            uint64_t limit=region_end<end?region_end:end;
            for(;page<limit;page+=4096) {
                if(publication_slot((uint32_t)page)==published_count)continue;
                if(!VirtualProtect(guest_memory+page,4096,protect,&previous)) {
                    failure="memory LLE host PTE protection publication failed";return;
                }
            }
        }
    }
}

#undef VM_UC
