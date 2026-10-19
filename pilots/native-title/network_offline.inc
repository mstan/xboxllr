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
#ifndef PILOT_NETWORK_HAS_CACHED_UPDATE
#define PILOT_NETWORK_HAS_CACHED_UPDATE 0
#endif
static PilotNetworkOffline network_offline;
static PilotNetworkCacheProbe network_cached_update_probe;
static void *network_cached_update_context;
static void pilot_network_initialize(PilotNetworkCacheProbe probe,void *context)
{
    if(!pilot_network_offline_initialize(&network_offline,pilot_network_host_operations()))
        stop("native network owner initialization failed",0);
    network_cached_update_probe=probe;network_cached_update_context=context;
#if PILOT_NETWORK_HAS_CACHED_UPDATE
    fprintf(stderr,"[NETWORK POLICY] cached_update=absence-only mounted_probe=%u partition=%u\n",
        probe!=NULL,PILOT_NETWORK_CACHE_PARTITION);
#endif
}
#if PILOT_NETWORK_HAS_CACHED_UPDATE
static int network_clear_cached_update(void *context,unsigned bytes)
{
    uint32_t address=*(const uint32_t*)context;
    if(bytes!=PILOT_NETWORK_CACHED_UPDATE_BYTES || !address ||
       (uint64_t)address+bytes>UINT64_C(0x100000000))return 0;
    uintptr_t start=(uintptr_t)(memory+address),end=start+bytes;
    if(end<start)return 0;
    /* Preflight the complete output before the first store. This is direct
     * native mapped memory, with no guest call or scheduler handoff. */
    for(uintptr_t at=start;at<end;) {
        MEMORY_BASIC_INFORMATION info;
        if(!VirtualQuery((const void*)at,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
           !(info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return 0;
        uintptr_t next=(uintptr_t)info.BaseAddress+info.RegionSize;
        if(next<=at)return 0;
        at=next;
    }
    memset((void*)start,0,bytes);return 1;
}
#endif
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
#if PILOT_NETWORK_HAS_CACHED_UPDATE
    } else if(action==PILOT_NETWORK_ACTION_CACHED_UPDATE) {
        if(stack_bytes!=4)stop("invalid cached-update query ABI",entry);
        uint32_t output=MEM32(g_esp+4); /* Snapshot before an aliased clear. */
        result=pilot_network_offline_cached_update(&network_offline,network_clear_cached_update,&output,
            network_cached_update_probe,network_cached_update_context,PILOT_NETWORK_CACHE_PARTITION,PILOT_NETWORK_CACHE_RELATIVE);
#endif
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
