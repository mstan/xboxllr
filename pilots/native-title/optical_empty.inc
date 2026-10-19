/* Selected before guest entry. No mounted media, asynchronous board events,
 * motor commands, DVD authentication or original device-owner coexistence. */
#if !defined(PILOT_OPTICAL_EMPTY_NATIVE) || !PILOT_OPTICAL_EMPTY_NATIVE
#error Empty optical services require the explicit immutable native profile
#endif
#if defined(PILOT_SCHEDULING_LLE) || defined(PILOT_IO_DEVICE_LLE) || defined(PILOT_USB_LLE)
#error Empty optical policy cannot replace an original device owner
#endif
#include "optical_empty.h"
static unsigned kernel_optical_initialized;
static int kernel_optical_is_empty(void){return kernel_optical_initialized==1;}
static void kernel_optical_initialize(void)
{
    if(kernel_optical_initialized)stop("native optical policy is immutable",0);
    kernel_optical_initialized=1;
}
static int kernel_optical_dispatch(unsigned ordinal)
{
    if(ordinal!=9)return 0;
    if(!kernel_optical_is_empty())stop("native optical owner is not initialized",ordinal);
    if((uint64_t)g_esp+12>UINT64_C(0x100000000))stop("invalid optical argument frame",g_esp);
    uint32_t state=MEM32(g_esp+4),changes=MEM32(g_esp+8);
    if(!state || state>UINT32_MAX-3 || (changes && changes>UINT32_MAX-3))
        stop("invalid optical output range",ordinal);
    /* Original NTSTATUS/RET8. Both stores precede the result; aliased output
     * pointers therefore retain the change count, not the state. */
    MEM32(state)=PILOT_OPTICAL_EMPTY_STATE;
    if(changes)MEM32(changes)=PILOT_OPTICAL_EMPTY_CHANGES;
    g_eax=0;g_esp+=12;return 1;
}
