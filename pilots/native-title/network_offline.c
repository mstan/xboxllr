#include "network_offline.h"

static PilotNetworkResult network_result(uint32_t status, unsigned guard)
{
    PilotNetworkResult result={status,0,0,guard}; return result;
}
static uint32_t network_hresult(int32_t error)
{
    uint32_t status=error>0 ? UINT32_C(0x80070000)|((uint32_t)error&65535u) : (uint32_t)error;
    return (status&UINT32_C(0x80000000)) ? status : UINT32_C(0x80004005);
}
int pilot_network_offline_initialize(PilotNetworkOffline *state,const PilotNetworkOps *ops)
{
    if(!state || !ops || !ops->lock || !ops->unlock || !ops->startup || !ops->cleanup ||
       !ops->create_owner || !ops->destroy_owner || state->ops || state->owner || state->references)return 0;
    state->ops=ops;return 1;
}
PilotNetworkResult pilot_network_offline_startup(PilotNetworkOffline *state,uint32_t parameters)
{
    if(!state || !state->ops)return network_result(0,PILOT_NETWORK_GUARD_UNINITIALIZED);
    /* Non-NULL descriptors are outside this adapter, even on repeated startup. */
    if(parameters)return network_result(0,PILOT_NETWORK_GUARD_PARAMETER);
    const PilotNetworkOps *ops=state->ops;ops->lock(ops->context);
    PilotNetworkResult result=network_result(0,0);
    if((state->references==0)!=(state->owner==0))result.guard=PILOT_NETWORK_GUARD_STATE;
    /* Original WSA reference storage is a WORD. Do not emulate its wraparound
     * by silently losing ownership of a real backend reference. */
    else if(state->references>=65535u)result.guard=PILOT_NETWORK_GUARD_REFCOUNT;
    else {
        int32_t error=ops->startup(ops->context);
        if(error) {
            result.hresult=network_hresult(error);
            result.last_error=(uint32_t)error;result.set_last_error=1;
        } else {
            if(!state->references)state->owner=ops->create_owner(ops->context);
            if(!state->owner) {
                ops->cleanup(ops->context);result.hresult=UINT32_C(0x8007000e);
            } else ++state->references;
        }
    }
    ops->unlock(ops->context);return result;
}
PilotNetworkResult pilot_network_offline_cleanup(PilotNetworkOffline *state)
{
    if(!state || !state->ops)return network_result(0,PILOT_NETWORK_GUARD_UNINITIALIZED);
    const PilotNetworkOps *ops=state->ops;ops->lock(ops->context);
    PilotNetworkResult result=network_result(0,0);
    if((state->references==0)!=(state->owner==0))result.guard=PILOT_NETWORK_GUARD_STATE;
    else if(!state->references)result.hresult=UINT32_C(0x80150005);
    else {
        if(!--state->references) {
            ops->destroy_owner(ops->context,state->owner);state->owner=0;
        }
        /* Original public cleanup ignores the WSA cleanup result. */
        ops->cleanup(ops->context);
    }
    ops->unlock(ops->context);return result;
}
