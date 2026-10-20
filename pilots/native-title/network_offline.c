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
PilotNetworkResult pilot_network_offline_link(PilotNetworkOffline *state,PilotNetworkLinkQuery query,void *context)
{
    if(!state || !state->ops)return network_result(0,PILOT_NETWORK_GUARD_UNINITIALIZED);
    const PilotNetworkOps *ops=state->ops;ops->lock(ops->context);
    PilotNetworkResult result=network_result(0,0);uint32_t flags=0;
    if((state->references==0)!=(state->owner==0))result.guard=PILOT_NETWORK_GUARD_STATE;
    else if(!query || !query(context,&flags))result.guard=PILOT_NETWORK_GUARD_LINK;
    else result.hresult=(flags&1u)?flags:0;
    /* The original link query works before/after XOnline ownership. Its return
     * is raw DWORD flags, not an HRESULT, and it does not set guest LastError. */
    ops->unlock(ops->context);return result;
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
PilotNetworkResult pilot_network_offline_cached_update(PilotNetworkOffline *state,
    PilotNetworkOutputClear clear,void *output,PilotNetworkCacheProbe probe,void *context,
    unsigned partition,const char *relative)
{
    if(!state || !state->ops)return network_result(0,PILOT_NETWORK_GUARD_UNINITIALIZED);
    const PilotNetworkOps *ops=state->ops;ops->lock(ops->context);
    PilotNetworkResult result=network_result(0,0);
    if((state->references==0)!=(state->owner==0))result.guard=PILOT_NETWORK_GUARD_STATE;
    else if(!state->references)result.hresult=UINT32_C(0x80150005);
    /* The original tests owner before touching or validating the output. */
    else if(!clear || !clear(output,PILOT_NETWORK_CACHED_UPDATE_BYTES))result.guard=PILOT_NETWORK_GUARD_OUTPUT;
    else if(!probe || !relative || !*relative || !partition || partition>5)result.guard=PILOT_NETWORK_GUARD_CACHE;
    else {
        /* A transient view borrows the real owner under its lifetime lock.
         * The synchronous probe exposes no guest enum/task objects. Cleanup
         * cannot retire the owner until every outcome releases this lock. */
        struct { void *owner; uint32_t status; unsigned empty; } view={state->owner,0,0};
        view.status=probe(context,partition,relative);
        view.empty=view.status==UINT32_C(0xc0000034) || view.status==UINT32_C(0xc000003a);
        if(view.owner!=state->owner)result.guard=PILOT_NETWORK_GUARD_STATE;
        else if(!view.empty)result.guard=PILOT_NETWORK_GUARD_CACHE;
        else result.hresult=1; /* Original no-record S_FALSE; output already zero. */
    }
    ops->unlock(ops->context);return result;
}
PilotNetworkResult pilot_network_offline_random(PilotNetworkOffline *state,uint32_t bytes,
    PilotNetworkRandomStage stage,PilotNetworkRandomCommit commit,void *output,
    PilotNetworkEntropy entropy,void *context)
{
    if(!state || !state->ops)return network_result(0,PILOT_NETWORK_GUARD_UNINITIALIZED);
    const PilotNetworkOps *ops=state->ops;ops->lock(ops->context);
    PilotNetworkResult result=network_result(0,0);
    if((state->references==0)!=(state->owner==0))result.guard=PILOT_NETWORK_GUARD_STATE;
    else if(!state->references)result.hresult=UINT32_C(0x276d);
    /* Preserve owner-first precedence, including oversized/invalid requests.
     * Zero length never reads a pointer or consumes an entropy source. */
    else if(bytes>PILOT_NETWORK_RANDOM_MAX_BYTES)result.guard=PILOT_NETWORK_GUARD_RANDOM_SIZE;
    else if(bytes) {
        unsigned char original[PILOT_NETWORK_RANDOM_MAX_BYTES],random[PILOT_NETWORK_RANDOM_MAX_BYTES];
        if(!stage || !commit || !stage(output,bytes,original))result.guard=PILOT_NETWORK_GUARD_OUTPUT;
        else if(!entropy || !entropy(context,bytes,random))result.guard=PILOT_NETWORK_GUARD_ENTROPY;
        else {
            for(unsigned i=0;i<bytes;++i)original[i]^=random[i];
            /* The owner lease spans preflight, staging, entropy and commit.
             * No guest pointer, PRNG seed or stream is retained by this core. */
            commit(output,bytes,original);
        }
    }
    ops->unlock(ops->context);return result;
}
