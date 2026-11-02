/* Authored version ownership. No per-draw payload copies; a pinned DEFAULT
 * VB range writer preserves its untouched GPU contents with one COW copy.
 * Include once in the prepared D3D11 RHI unit after D3D11 declarations.
 * All entry points use the existing graphics lock; no independent locks. */
#include "graphics_cb_version_owner.h"
#include <stdint.h>
#include <string.h>
enum { PILOT_CB_OWNERS=1024,PILOT_CB_NODES=4096,PILOT_CB_BYTES=32*1024*1024,PILOT_CB_BUCKETS=1024 };
struct PilotCbOwner;
struct PilotCbVersion {
    ID3D11Buffer *buffer;
    struct PilotCbOwner *owner;
    struct PilotCbVersion *next;
    struct PilotCbVersion *hash_next;
    unsigned pins;
};
struct PilotCbOwner {
    ID3D11Buffer **slot;
    struct PilotCbVersion *current,*free;
    D3D11_BUFFER_DESC desc;
    unsigned used,nodes,mapped,blocked,unsupported;
};
static struct PilotCbOwner pilot_cb_owners[PILOT_CB_OWNERS];
static struct PilotCbVersion pilot_cb_nodes[PILOT_CB_NODES],*pilot_cb_free;
static struct PilotCbVersion *pilot_cb_buckets[PILOT_CB_BUCKETS];
static unsigned pilot_cb_initialized,pilot_cb_extra_bytes,pilot_cb_pins;
static void (*pilot_cb_barrier)(unsigned);
static int (*pilot_cb_owned_bindings)(ID3D11DeviceContext *,UINT,ID3D11Buffer *(*)(ID3D11Buffer *));

static unsigned pilot_cb_bucket(ID3D11Buffer *buffer)
{
    uintptr_t key=(uintptr_t)buffer>>4;key^=key>>17;
    return (unsigned)key&(PILOT_CB_BUCKETS-1);
}

static struct PilotCbVersion *pilot_cb_lookup(ID3D11Buffer *buffer)
{
    if(!buffer)return NULL;
    /* Each registered node holds a strong buffer ref. Remove it before that
     * ref is released, so a reused COM address cannot acquire an old owner. */
    for(struct PilotCbVersion *p=pilot_cb_buckets[pilot_cb_bucket(buffer)];p;p=p->hash_next)
        if(p->buffer==buffer)return p;
    return NULL;
}
static void pilot_cb_drop(struct PilotCbVersion *p)
{
    struct PilotCbOwner *owner=p->owner;
    struct PilotCbVersion **entry=&pilot_cb_buckets[pilot_cb_bucket(p->buffer)];
    while(*entry && *entry!=p)entry=&(*entry)->hash_next;
    if(*entry)*entry=p->hash_next;
    ID3D11Buffer_Release(p->buffer);
    /* Exactly max(nodes-1,0) physical backings are optional for each owner. */
    if(owner->nodes>1)pilot_cb_extra_bytes-=owner->desc.ByteWidth;
    memset(p,0,sizeof(*p));p->next=pilot_cb_free;pilot_cb_free=p;
    if(!--owner->nodes && !owner->slot)memset(owner,0,sizeof(*owner));
}
static struct PilotCbVersion *pilot_cb_node(struct PilotCbOwner *owner,ID3D11Buffer *buffer)
{
    if(!pilot_cb_free || (owner->nodes && owner->desc.ByteWidth>PILOT_CB_BYTES-pilot_cb_extra_bytes))return NULL;
    struct PilotCbVersion *p=pilot_cb_free;pilot_cb_free=p->next;
    memset(p,0,sizeof(*p));p->buffer=buffer;p->owner=owner;
    ID3D11Buffer_AddRef(buffer);
    unsigned bucket=pilot_cb_bucket(buffer);p->hash_next=pilot_cb_buckets[bucket];pilot_cb_buckets[bucket]=p;
    if(owner->nodes)pilot_cb_extra_bytes+=owner->desc.ByteWidth;
    ++owner->nodes;return p;
}
static struct PilotCbOwner *pilot_cb_owner(ID3D11Buffer **slot,int create)
{
    if(!slot || !*slot || (create && !pilot_cb_barrier))return NULL;
    for(unsigned i=0;i<PILOT_CB_OWNERS;++i)if(pilot_cb_owners[i].used && pilot_cb_owners[i].slot==slot)
        return pilot_cb_owners[i].current->buffer==*slot?&pilot_cb_owners[i]:NULL;
    if(!create || !pilot_cb_barrier)return NULL;
    /* One physical CB cannot acquire conflicting logical writable owners.
     * Decline that route and suppress already retained optional HD results. */
    struct PilotCbVersion *alias=pilot_cb_lookup(*slot);
    if(alias) {
        alias->owner->unsupported=1;
        if(alias->pins)pilot_cb_barrier(PILOT_CB_UNSUPPORTED_MAP);
        return NULL;
    }
    D3D11_BUFFER_DESC desc;ID3D11Buffer_GetDesc(*slot,&desc);
    int constant=desc.BindFlags==D3D11_BIND_CONSTANT_BUFFER && !(desc.ByteWidth&15) &&
        (desc.Usage==D3D11_USAGE_DEFAULT?desc.CPUAccessFlags==0:
         desc.Usage==D3D11_USAGE_DYNAMIC && desc.CPUAccessFlags==D3D11_CPU_ACCESS_WRITE);
    int index=desc.BindFlags==D3D11_BIND_INDEX_BUFFER && !(desc.ByteWidth&3) &&
        desc.Usage==D3D11_USAGE_DYNAMIC && desc.CPUAccessFlags==D3D11_CPU_ACCESS_WRITE;
    int vertex=desc.BindFlags==D3D11_BIND_VERTEX_BUFFER && !(desc.ByteWidth&3) &&
        desc.Usage==D3D11_USAGE_DEFAULT && desc.CPUAccessFlags==0 && desc.ByteWidth<=PILOT_CB_BYTES;
    if(!desc.ByteWidth || desc.MiscFlags || desc.StructureByteStride ||
       (!(constant || index) && !vertex) || (!vertex && desc.ByteWidth>65536))return NULL;
    if(!pilot_cb_initialized) {
        for(unsigned i=0;i<PILOT_CB_NODES;++i){pilot_cb_nodes[i].next=pilot_cb_free;pilot_cb_free=&pilot_cb_nodes[i];}
        pilot_cb_initialized=1;
    }
    for(unsigned i=0;i<PILOT_CB_OWNERS;++i)if(!pilot_cb_owners[i].used) {
        struct PilotCbOwner *owner=&pilot_cb_owners[i];owner->desc=desc;owner->used=1;owner->slot=slot;
        owner->current=pilot_cb_node(owner,*slot);
        if(!owner->current){memset(owner,0,sizeof(*owner));return NULL;}
        return owner;
    }
    return NULL;
}
static struct PilotCbVersion *pilot_cb_available(struct PilotCbOwner *owner,ID3D11DeviceContext *context)
{
    if(owner->free) {
        struct PilotCbVersion *p=owner->free;owner->free=p->next;p->next=NULL;return p;
    }
    if(!pilot_cb_free || owner->desc.ByteWidth>PILOT_CB_BYTES-pilot_cb_extra_bytes)return NULL;
    ID3D11Device *device=NULL;ID3D11Buffer *buffer=NULL;ID3D11DeviceContext_GetDevice(context,&device);
    HRESULT result=ID3D11Device_CreateBuffer(device,&owner->desc,NULL,&buffer);ID3D11Device_Release(device);
    if(FAILED(result))return NULL;
    struct PilotCbVersion *p=pilot_cb_node(owner,buffer);ID3D11Buffer_Release(buffer);return p;
}
static void pilot_cb_rebind(ID3D11DeviceContext *context,struct PilotCbOwner *owner)
{
    if(owner->nodes<2)return;
    if(pilot_cb_owned_bindings && pilot_cb_owned_bindings(context,owner->desc.BindFlags,pilot_native_cb_current))return;
    if(owner->desc.BindFlags==D3D11_BIND_INDEX_BUFFER) {
        ID3D11Buffer *bound=NULL;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;UINT offset=0;
        ID3D11DeviceContext_IAGetIndexBuffer(context,&bound,&format,&offset);
        struct PilotCbVersion *p=pilot_cb_lookup(bound);
        if(p && p->owner==owner && bound!=owner->current->buffer)
            ID3D11DeviceContext_IASetIndexBuffer(context,owner->current->buffer,format,offset);
        if(bound)ID3D11Buffer_Release(bound);
        return;
    }
    if(owner->desc.BindFlags==D3D11_BIND_VERTEX_BUFFER) {
        ID3D11Buffer *bound[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]={0};
        UINT strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT],offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
        ID3D11DeviceContext_IAGetVertexBuffers(context,0,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,bound,strides,offsets);
        int changed=0;
        for(unsigned n=0;n<D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;++n) {
            struct PilotCbVersion *p=pilot_cb_lookup(bound[n]);
            if(p && p->owner==owner && bound[n]!=owner->current->buffer) {
                ID3D11Buffer_Release(bound[n]);bound[n]=owner->current->buffer;ID3D11Buffer_AddRef(bound[n]);changed=1;
            }
        }
        if(changed)ID3D11DeviceContext_IASetVertexBuffers(context,0,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT,bound,strides,offsets);
        for(unsigned n=0;n<D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;++n)if(bound[n])ID3D11Buffer_Release(bound[n]);
        return;
    }
    /* Actual bindings, including direct COM state restores. Never overwrite a
     * slot now bound to a different owner. All six stage counts are fixed. */
#define PILOT_CB_REBIND(stage) do { \
    ID3D11Buffer *bound[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT]={0};int changed=0; \
    ID3D11DeviceContext_##stage##GetConstantBuffers(context,0,D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,bound); \
    for(unsigned i=0;i<D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;++i) { \
        struct PilotCbVersion *p=pilot_cb_lookup(bound[i]); \
        if(p && p->owner==owner && bound[i]!=owner->current->buffer) { \
            ID3D11Buffer_Release(bound[i]);bound[i]=owner->current->buffer;ID3D11Buffer_AddRef(bound[i]);changed=1; \
        } \
    } \
    if(changed)ID3D11DeviceContext_##stage##SetConstantBuffers(context,0,D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT,bound); \
    for(unsigned i=0;i<D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;++i)if(bound[i])ID3D11Buffer_Release(bound[i]); \
} while(0)
    PILOT_CB_REBIND(VS);PILOT_CB_REBIND(PS);PILOT_CB_REBIND(GS);
    PILOT_CB_REBIND(HS);PILOT_CB_REBIND(DS);PILOT_CB_REBIND(CS);
#undef PILOT_CB_REBIND
}
static void pilot_cb_publish(struct PilotCbOwner *owner,struct PilotCbVersion *next,ID3D11DeviceContext *context)
{
    ID3D11Buffer *old=*owner->slot;owner->current=next;
    ID3D11Buffer_AddRef(next->buffer);*owner->slot=next->buffer;
    pilot_cb_rebind(context,owner);ID3D11Buffer_Release(old);
}
static void pilot_cb_pressure(struct PilotCbOwner *owner,unsigned reason)
{
    /* Pins can remain GPU-in-flight after the callback. Already-submitted
     * draws keep their D3D11 resource contents through ordinary DISCARD/
     * UpdateSubresource command ordering. No future CPU replay may use them. */
    owner->blocked=1;pilot_cb_barrier(reason);
}
int pilot_native_cb_version_barrier(void (*callback)(unsigned))
{
    if(pilot_cb_pins)return 0;pilot_cb_barrier=callback;return 1;
}
void pilot_native_cb_binding_owner(int (*callback)(ID3D11DeviceContext *,UINT,ID3D11Buffer *(*)(ID3D11Buffer *)))
{pilot_cb_owned_bindings=callback;}
void pilot_native_cb_track(ID3D11Buffer **slot)
{(void)pilot_cb_owner(slot,1);}
void pilot_native_cb_before_write(ID3D11Buffer **slot)
{
    struct PilotCbOwner *owner=pilot_cb_owner(slot,1);if(!owner)return;
    /* Index ownership admits only WRITE_DISCARD through map(). The original
     * caller still performs any unexpected update, but it cannot publish HD. */
    if(owner->desc.BindFlags!=D3D11_BIND_CONSTANT_BUFFER && owner->desc.BindFlags!=D3D11_BIND_VERTEX_BUFFER) {
        owner->unsupported=1;
        if(owner->current->pins)pilot_cb_pressure(owner,PILOT_CB_UNSUPPORTED_MAP);
        return;
    }
    if(owner->nodes==1 && !owner->current->pins)return;
    ID3D11Device *device=NULL;ID3D11DeviceContext *context=NULL;
    ID3D11Buffer_GetDevice(*slot,&device);ID3D11Device_GetImmediateContext(device,&context);ID3D11Device_Release(device);
    if(owner->current->pins) {
        struct PilotCbVersion *next=pilot_cb_available(owner,context);
        if(next)pilot_cb_publish(owner,next,context);else pilot_cb_pressure(owner,PILOT_CB_PRESSURE);
    }
    pilot_cb_rebind(context,owner);ID3D11DeviceContext_Release(context);
}
HRESULT pilot_native_cb_map(ID3D11Buffer **slot,ID3D11DeviceContext *context,D3D11_MAP mode,D3D11_MAPPED_SUBRESOURCE *mapped)
{
    struct PilotCbOwner *owner=pilot_cb_owner(slot,1);
    if(owner && (mode!=D3D11_MAP_WRITE_DISCARD || owner->desc.BindFlags==D3D11_BIND_VERTEX_BUFFER)) {
        owner->unsupported=1;if(owner->current->pins)pilot_cb_pressure(owner,PILOT_CB_UNSUPPORTED_MAP);
    } else if(owner && owner->current->pins) {
        struct PilotCbVersion *next=pilot_cb_available(owner,context);
        if(next) {
            HRESULT result=ID3D11DeviceContext_Map(context,(ID3D11Resource*)next->buffer,0,mode,0,mapped);
            if(SUCCEEDED(result)){pilot_cb_publish(owner,next,context);owner->mapped=1;return result;}
            next->next=owner->free;owner->free=next;
        }
        pilot_cb_pressure(owner,PILOT_CB_PRESSURE);
    }
    if(owner)pilot_cb_rebind(context,owner);
    HRESULT result=ID3D11DeviceContext_Map(context,(ID3D11Resource*)*slot,0,mode,0,mapped);
    if(owner && SUCCEEDED(result))owner->mapped=1;return result;
}
void pilot_native_cb_unmapped(ID3D11Buffer **slot)
{if(pilot_cb_barrier){struct PilotCbOwner *owner=pilot_cb_owner(slot,0);if(owner)owner->mapped=0;}}
int pilot_native_cb_version_borrow(ID3D11Buffer *buffer,void **token)
{
    if(!token || *token || !pilot_cb_barrier)return 0;
    struct PilotCbVersion *p=pilot_cb_lookup(buffer);if(!p)return 0;
    struct PilotCbOwner *owner=p->owner;
    /* A retained binding may outlive forget()/device-owner retirement. */
    if(!owner->slot || !owner->current)return 0;
    if(owner->blocked && !owner->current->pins)owner->blocked=0;
    if(!owner->slot || owner->mapped || owner->unsupported || owner->blocked || owner->current!=p || p->pins==UINT32_MAX || pilot_cb_pins==UINT32_MAX)return 0;
    ++p->pins;++pilot_cb_pins;*token=p;return 1;
}
void pilot_native_cb_version_release(void **token)
{
    if(!token || !*token)return;struct PilotCbVersion *p=*token;*token=NULL;
    --p->pins;--pilot_cb_pins;
    if(!p->pins) {
        struct PilotCbOwner *owner=p->owner;
        if(!owner->slot)pilot_cb_drop(p);
        else if(owner->current!=p){p->next=owner->free;owner->free=p;}
    }
}
ID3D11Buffer *pilot_native_cb_current(ID3D11Buffer *buffer)
{
    struct PilotCbVersion *p=pilot_cb_lookup(buffer);
    return p && p->owner->slot?p->owner->current->buffer:buffer;
}
void pilot_native_cb_forget(ID3D11Buffer **slot)
{
    struct PilotCbOwner *owner=pilot_cb_owner(slot,0);if(!owner)return;
    owner->slot=NULL;owner->current=owner->free=NULL;
    for(unsigned i=0;i<PILOT_CB_NODES;++i)if(pilot_cb_nodes[i].owner==owner && !pilot_cb_nodes[i].pins)
        pilot_cb_drop(&pilot_cb_nodes[i]);
}
int pilot_native_buffer_registered(ID3D11Buffer *buffer)
{return pilot_cb_lookup(buffer)!=NULL;}
static struct PilotCbOwner *pilot_native_vertex_current_owner(ID3D11Buffer *buffer)
{
    if(!pilot_cb_barrier)return NULL;
    struct PilotCbVersion *p=pilot_cb_lookup(buffer);if(!p)return NULL;
    struct PilotCbOwner *owner=p->owner;
    if(!owner->slot || !owner->current || owner->current!=p || *owner->slot!=buffer ||
       owner->mapped || owner->unsupported || owner->desc.BindFlags!=D3D11_BIND_VERTEX_BUFFER ||
       owner->desc.Usage!=D3D11_USAGE_DEFAULT || owner->desc.CPUAccessFlags)return NULL;
    return owner;
}
int pilot_native_buffer_full_write_owned(ID3D11Buffer *buffer)
{return pilot_native_vertex_current_owner(buffer)!=NULL;}
ID3D11Buffer *pilot_native_buffer_before_range_write(ID3D11Buffer *buffer,ID3D11DeviceContext *context)
{
    if(!context)return NULL;
    struct PilotCbOwner *owner=pilot_native_vertex_current_owner(buffer);if(!owner)return NULL;
    if(owner->current->pins) {
        struct PilotCbVersion *next=pilot_cb_available(owner,context);
        if(next) {
            /* One copy per actual changed resource write, never per draw.
             * Full RHI updates do not need this preservation copy. */
            ID3D11DeviceContext_CopyResource(context,(ID3D11Resource*)next->buffer,(ID3D11Resource*)owner->current->buffer);
            pilot_cb_publish(owner,next,context);
        } else pilot_cb_pressure(owner,PILOT_CB_PRESSURE);
    }
    pilot_cb_rebind(context,owner);
    return owner->current->buffer;
}
void pilot_native_cb_discard_owners(void)
{
    for(unsigned i=0;i<PILOT_CB_OWNERS;++i)if(pilot_cb_owners[i].slot)
        pilot_native_cb_forget(pilot_cb_owners[i].slot);
}
