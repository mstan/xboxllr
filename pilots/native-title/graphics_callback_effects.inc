/* No callback is replaced. Qualified CPU preparation stays effect-disjoint;
 * publication and every guest-observation callback barrier remain ordered. */
#if PILOT_GPU_SCALAR_PREPARATION_AVAILABLE
static int graphics_scalar_callback_effect(const struct GraphicsGpuCallback *callback,uint32_t *address)
{
    if(callback->routine!=PILOT_GPU_CALLBACK_ROUTINE)return 0;
    /* Original signed divide/recombine reconstructs the input mod2^32, then
     * shifts it. Conservatively refuse arithmetic wrap rather than guessing
     * an index bound (real observed contexts exceed900). */
    uint64_t at=(uint64_t)callback->context*16+PILOT_GPU_CALLBACK_WRITE_BASE;
    if(at>UINT32_MAX-7)return 0;
    unsigned permissions=0;
    if(!pilot_gpu_callback_write_section((uint32_t)at,8) ||
       !pilot_commit_owner_permissions((uint32_t)at,8,&permissions) || !(permissions&PILOT_MEMORY_WRITE))return 0;
    *address=(uint32_t)at;return 1;
}
/* Inspect both the already-running callback and every queued callback. Pure
 * metadata inspection also permits fixture checks without publishing fake work. */
static int graphics_scalar_effects(unsigned running,const struct GraphicsGpuCallback *active,
    const struct GraphicsGpuCallback *head,uint32_t writes[128],unsigned *count)
{
    *count=0;
    if(running){if(!active || !graphics_scalar_callback_effect(active,&writes[0]))return 0;++*count;}
    for(const struct GraphicsGpuCallback *c=head;c;c=c->next){
        if(*count==128 || !graphics_scalar_callback_effect(c,&writes[*count]))return 0;++*count;}
    return 1;
}
static int graphics_scalar_effect_span(uint32_t address,uint32_t bytes,const uint32_t *writes,unsigned count)
{
    if(!bytes || (uint64_t)address+bytes>0x100000000ull)return 0;
    for(unsigned i=0;i<count;++i)if((uint64_t)address+bytes>writes[i] && address<(uint64_t)writes[i]+8){
        return 0;}
    return 1;
}
#endif
