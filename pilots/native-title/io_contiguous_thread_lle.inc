/* Allocation remains in the original kernel. This ledger only publishes its
 * actual returned pages and records device leases on that existing owner. */
static unsigned contiguous_owner(uint32_t base,uint32_t bytes)
{
    for(unsigned i=0;i<owner_count;++i)
        if(owners[i].contiguous && base>=owners[i].base &&
           (uint64_t)base+bytes<=(uint64_t)owners[i].base+owners[i].bytes)return i;
    return owner_count;
}
static void contiguous_caller_prepare(unsigned ordinal,const uint32_t *args)
{
    if(ordinal==165 || ordinal==166) {
        if(!args[0] || args[0]>RAM_BYTES || owner_count==MAX_PUBLISHED)
            failure="device LLE contiguous allocation outside qualified size/capacity";
        return;
    }
    if(ordinal==173) {
        if(publication_slot(args[0]&~4095u)==published_count)
            failure="device LLE physical query requires owned publication";
        return;
    }
    if(ordinal==4 && !args[0])return;
    /* The original protection query is read-only and also observes the
     * console-owned GPU reservation; it does not publish caller storage. */
    if(ordinal!=4 && ordinal!=171 && ordinal!=178 && ordinal!=180 && ordinal!=182)return;
    uint32_t bytes=(ordinal==178 || ordinal==182)?args[1]:1;
    unsigned owner=contiguous_owner(args[0],bytes);
    if(!bytes || owner==owner_count ||
       ((ordinal==171 || ordinal==180) && owners[owner].base!=args[0])) {
        failure="device LLE contiguous service requires owned allocation";return;
    }
    if(ordinal==182 && owners[owner].resource_references &&
       (((args[2]&6)!=PAGE_READONLY && (args[2]&6)!=PAGE_READWRITE) ||
        (args[2]&~(PAGE_READONLY|PAGE_READWRITE|PAGE_NOCACHE|PAGE_WRITECOMBINE)))) {
        failure="device LLE contiguous protection would invalidate resource read ownership";return;
    }
    if(ordinal!=171)return;
    if(owners[owner].resource_references) {
        failure="device LLE contiguous release while resource borrowed";return;
    }
    if(owners[owner].dma_leases) {
        failure="device LLE contiguous release while DMA borrowed";return;
    }
    uint64_t end=(uint64_t)args[0]+owners[owner].bytes;
    for(unsigned i=0;i<published_count;++i)
        if(published[i].va>=args[0] && published[i].va<end && published[i].pins) {
            failure="device LLE contiguous release while DMA pinned";return;
        }
}
static void contiguous_original_query(unsigned ordinal,uint32_t base,uint32_t *result)
{
    uc_context *context=NULL;
    IO_UC(uc_context_alloc(cpu,&context));
    uc_err error=uc_context_save(cpu,context);
    if(error){uc_context_free(context);failure=uc_strerror(error);return;}
    if(execution_depth>=2)failure="device LLE contiguous query nesting limit";
    if(!failure){++execution_depth;execute(ordinal,&base,result);--execution_depth;}
    error=uc_context_restore(cpu,context);uc_context_free(context);
    if(error)failure=uc_strerror(error);
}
static void contiguous_caller_complete(unsigned ordinal,const uint32_t *args,uint32_t result)
{
    if(ordinal!=165 && ordinal!=166 && ordinal!=171 && ordinal!=182)return;
    check_publications(0);if(failure)return;
    if(ordinal==171){retire_owner(args[0]);return;}
    uint32_t base=args[0],bytes=ordinal==182?args[1]:0;
    if(ordinal==165 || ordinal==166) {
        if(!result)return;
        base=result;bytes=(args[0]+4095)&~4095u;
        if((base&4095) || base<0x80000000u || (uint64_t)base+bytes>0x84000000ull) {
            failure="device LLE contiguous return outside RAM namespace";return;
        }
        publish_range(base,bytes);if(failure)return;
        owners[owner_count-1].contiguous=1;
        uint32_t actual=0;contiguous_original_query(180,base,&actual);if(failure)return;
        if(actual!=bytes){failure="device LLE original contiguous extent differs from publication";return;}
    }
    uint64_t end=(uint64_t)base+bytes;
    for(uint64_t at=base&~4095u;at<end;at+=4096) {
        uint32_t protect=0;contiguous_original_query(179,(uint32_t)at,&protect);if(failure)return;
        DWORD access=protect&6,previous;
        if(access!=PAGE_READONLY && access!=PAGE_READWRITE) {
            failure="device LLE original contiguous protection outside qualified policy";return;
        }
        if(!VirtualProtect(guest_memory+at,4096,access,&previous)) {
            failure="device LLE contiguous host protection publication failed";return;
        }
    }
}
