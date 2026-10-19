/* Exact reviewed public lifecycle; every other admitted wrapper stops before
 * its retained original body can read an unowned original network object. */
#if !defined(PILOT_NETWORK_OFFLINE) || !PILOT_NETWORK_OFFLINE
#error Native offline network services require the explicit immutable policy
#endif
#include "pilot_network_bindings.h"
#include "network_host.h"
#if PILOT_NETWORK_ABI_VERSION != 1
#error Unsupported native network binding ABI
#endif
static PilotNetworkOffline network_offline;
static void pilot_network_initialize(void)
{
    if(!pilot_network_offline_initialize(&network_offline,pilot_network_host_operations()))
        stop("native network owner initialization failed",0);
}
void pilot_network_dispatch(unsigned entry,recomp_func_t set_last_error)
{
    if(entry>=PILOT_NETWORK_COUNT)stop("unqualified native network entry",entry);
    const unsigned action=pilot_network_calls[entry].action;
    if(action==PILOT_NETWORK_ACTION_GUARD)
        stop("unqualified offline network operation",pilot_network_calls[entry].address);
    if(!set_last_error || g_esp<8)stop("invalid network LastError callback frame",g_esp);
    unsigned stack_bytes=pilot_network_calls[entry].stack_bytes;
    if((uint64_t)g_esp+4+stack_bytes>UINT64_C(0x100000000))
        stop("invalid native network argument frame",g_esp);
    PilotNetworkResult result;
    if(action==PILOT_NETWORK_ACTION_STARTUP) {
        if(stack_bytes!=4)stop("invalid native network startup ABI",entry);
        result=pilot_network_offline_startup(&network_offline,MEM32(g_esp+4));
    } else if(action==PILOT_NETWORK_ACTION_CLEANUP) {
        if(stack_bytes)stop("invalid native network cleanup ABI",entry);
        result=pilot_network_offline_cleanup(&network_offline);
    } else {stop("invalid native network action",action);return;}
    if(result.guard)stop("unsupported native network lifecycle",result.guard);
    if(result.set_last_error) {
        uint32_t original_stack=g_esp;
        uint32_t saved_ebx=g_ebx,saved_esi=g_esi,saved_edi=g_edi;
        PUSH32(g_esp,result.last_error);PUSH32(g_esp,0);set_last_error();
        if(g_esp!=original_stack || g_ebx!=saved_ebx || g_esi!=saved_esi || g_edi!=saved_edi)
            stop("network LastError callback ABI mismatch",g_esp);
    }
    g_eax=result.hresult;g_esp+=4+stack_bytes;
}
