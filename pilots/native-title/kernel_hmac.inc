#include "kernel_hmac.h"

static int kernel_hmac_span(uint32_t address,uint32_t bytes)
{
    return (!bytes || address) && (uint64_t)address+bytes<=0x100000000ull;
}

/* Returns zero for unrelated ordinals. XcHMAC is VOID/NTAPI with seven
 * stack arguments: preserve integer/FP flags and registers, consume RET+28.
 * Guest mappings remain checked by the runtime's normal memory owner. */
static int kernel_hmac_dispatch(unsigned ordinal)
{
    if(ordinal!=340)return 0;
    if((uint64_t)g_esp+32>0x100000000ull)stop("invalid XcHMAC argument frame",g_esp);
    uint32_t args[7];
    for(unsigned i=0;i<7;++i)args[i]=MEM32(g_esp+4+i*4);
    const uint32_t key_bytes=args[1]>64?64:args[1];
    if(!kernel_hmac_span(args[0],key_bytes) ||
       !kernel_hmac_span(args[2],args[3]) ||
       !kernel_hmac_span(args[4],args[5]) ||
       !args[6] || !kernel_hmac_span(args[6],20))
        stop("invalid XcHMAC memory span",ordinal);
    if(!pilot_xc_hmac(memory+args[0],args[1],memory+args[2],args[3],
                       memory+args[4],args[5],memory+args[6]))
        stop("invalid XcHMAC input",ordinal);
    g_esp+=32;
    return 1;
}
