/* Private R06 feasibility integration. Existing GPU barriers are retained.
 * No guest callback is replaced. Unknown proof/clock/scheduler configurations
 * retain the old renderer path, even if the diagnostic setting is supplied. */
#if defined(__has_include)
#if __has_include("pilot_r06_callback_certificate.h")
#include "pilot_r06_callback_certificate.h"
#endif
#endif
#ifndef PILOT_R06_CALLBACK_CERTIFICATE_VERSION
#define PILOT_R06_CALLBACK_CERTIFICATE_VERSION 0
#endif
static int graphics_scalar_callback_effect(const struct GraphicsGpuCallback *callback,uint32_t *address)
{
#if PILOT_R06_CALLBACK_CERTIFICATE_VERSION == 1 && !defined(PILOT_SCHEDULING_LLE) && !defined(PILOT_AUDIO_REFERENCE) && !defined(PILOT_AUDIO_DEVICE_LLE) && !defined(PILOT_KERNEL_CLOCK_NATIVE) && !defined(PILOT_USB_LLE) && !defined(PILOT_IO_DEVICE_LLE) && !defined(PILOT_MEMORY_LLE) && !defined(PILOT_FILES_LLE)
    if(callback->routine!=PILOT_R06_CALLBACK_ROUTINE)return 0;
    /* Original signed divide/recombine reconstructs the input mod2^32, then
     * shifts it. Conservatively refuse arithmetic wrap rather than guessing
     * an index bound (real observed contexts exceed900). */
    uint64_t at=(uint64_t)callback->context*16+PILOT_R06_CALLBACK_WRITE_BASE;
    if(at>UINT32_MAX-7)return 0;
    unsigned permissions=0;
    if(!pilot_r06_callback_write_section((uint32_t)at,8) ||
       !pilot_commit_owner_permissions((uint32_t)at,8,&permissions) || !(permissions&PILOT_MEMORY_WRITE))return 0;
    *address=(uint32_t)at;return 1;
#else
    (void)callback;(void)address;return 0;
#endif
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
