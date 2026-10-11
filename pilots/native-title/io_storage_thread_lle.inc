/* Share original file-driver DMA preparation across C and actual-thread
 * service callers. The existing image/stack owner supplies admission; the
 * original VM creates real PTEs/PFNs before the driver builds its own MDL. */
static void storage_caller_prepare(unsigned ordinal,const uint32_t *args)
{
#ifdef PILOT_FILES_LLE
    if(ordinal!=219 && ordinal!=236 && ordinal!=196 && ordinal!=200 && ordinal!=207)return;
    if(args[2]) {failure="original file APC requires guest callback interop";return;}
    unsigned field=ordinal==196 || ordinal==200?8:5;
    uint32_t base=args[field],bytes=args[field+1];
    if((uint64_t)base+bytes>0x100000000ull) {failure="storage LLE buffer wraps guest RAM";return;}
    for(uint64_t page=base&~4095u;bytes && page<(uint64_t)base+bytes;page+=4096) {
        if(publication_slot((uint32_t)page)<published_count ||
           !caller_owned((uint32_t)page,4096) || !guest_page((uint32_t)page))continue;
        uc_context *context=NULL;IO_UC(uc_context_alloc(cpu,&context));
        uc_err error=uc_context_save(cpu,context);
        if(!error) {
            ++execution_depth;image_vm((uint32_t)page,4096,MEM_COMMIT);--execution_depth;
            error=uc_context_restore(cpu,context);
        }
        uc_context_free(context);
        if(error){failure=uc_strerror(error);return;}
        if(failure)return;
    }
#else
    (void)ordinal;(void)args;
#endif
}
