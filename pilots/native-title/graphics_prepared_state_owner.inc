/* Authored R09 binding owner. Link ONCE in the runtime. Not a state cache over
 * driver getters: wrapped setters maintain actual bindings, including private
 * passes. Getters below are only the explicit unknown-boundary resync. */
#include "graphics_prepared_state_owner.h"
#if defined(_WIN32) && defined(COBJMACROS)
#include <string.h>
#include <stdint.h>
enum { PREPARED_ALL_STAGES=6, PREPARED_FACTS=128, PREPARED_STATE_FACTS=32 };
struct PreparedExtraStage {
    IUnknown *shader;
    ID3D11ClassInstance *classes[PREPARED_CLASSES]; UINT class_count;
    ID3D11Buffer *constants[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    ID3D11ShaderResourceView *resources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ID3D11SamplerState *samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
};
struct PreparedFactEntry { ID3D11View *key; struct PreparedViewFacts facts; };
struct PreparedDepthFact { ID3D11DepthStencilState *key; D3D11_DEPTH_STENCIL_DESC desc; };
struct PreparedBlendFact { ID3D11BlendState *key; D3D11_BLEND_DESC desc; };
static struct {
    ID3D11DeviceContext *context; unsigned known;
    struct PreparedState state; struct PreparedExtraStage extra[3];
    ID3D11Buffer *so[D3D11_SO_BUFFER_SLOT_COUNT];
    ID3D11UnorderedAccessView *om_uavs[D3D11_PS_CS_UAV_REGISTER_COUNT];
    ID3D11UnorderedAccessView *cs_uavs[D3D11_PS_CS_UAV_REGISTER_COUNT];
    struct PreparedFactEntry facts[PREPARED_FACTS]; unsigned next_fact;
    struct PreparedDepthFact depths[PREPARED_STATE_FACTS]; unsigned next_depth;
    struct PreparedBlendFact blends[PREPARED_STATE_FACTS]; unsigned next_blend;
    D3D11_DEPTH_STENCIL_DESC depth_desc; D3D11_BLEND_DESC blend_desc;
    unsigned buffer_input[6][D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    unsigned buffer_inputs,non_graphics_inputs;
} prepared_owner;
static void prepared_add(void *p) { if(p)((IUnknown*)p)->lpVtbl->AddRef((IUnknown*)p); }
static void prepared_drop(void *p) { if(p)((IUnknown*)p)->lpVtbl->Release((IUnknown*)p); }
/* memcpy avoids aliasing a typed interface** as an IUnknown**. */
static void prepared_assign(void *slot,void *value)
{
    void *old;memcpy(&old,slot,sizeof(old));if(old==value)return;
    prepared_add(value);memcpy(slot,&value,sizeof(value));prepared_drop(old);
}
static ID3D11Buffer **prepared_constants(unsigned stage)
{ return stage<3?prepared_owner.state.constants[stage]:prepared_owner.extra[stage-3].constants; }
static ID3D11ShaderResourceView **prepared_resources(unsigned stage)
{ return stage<3?prepared_owner.state.resources[stage]:prepared_owner.extra[stage-3].resources; }
static ID3D11SamplerState **prepared_samplers(unsigned stage)
{ return stage<3?prepared_owner.state.samplers[stage]:prepared_owner.extra[stage-3].samplers; }
static void *prepared_shader_slot(unsigned stage)
{
    if(stage==0)return &prepared_owner.state.vertex;
    if(stage==1)return &prepared_owner.state.geometry;
    if(stage==2)return &prepared_owner.state.pixel;
    return &prepared_owner.extra[stage-3].shader;
}
static ID3D11ClassInstance **prepared_classes(unsigned stage)
{ return stage<3?prepared_owner.state.classes[stage]:prepared_owner.extra[stage-3].classes; }
static UINT *prepared_class_count(unsigned stage)
{ return stage<3?&prepared_owner.state.class_count[stage]:&prepared_owner.extra[stage-3].class_count; }
static int prepared_known(ID3D11DeviceContext *context)
{ return context && context==prepared_owner.context && prepared_owner.known; }

/* Read pointer representation with memcpy, without aliasing typed slots. */
static UINT prepared_live_extent(const void *slots,UINT limit)
{
    while(limit) {
        void *value;memcpy(&value,(const unsigned char*)slots+(limit-1)*sizeof(value),sizeof(value));
        if(value)break;--limit;
    }
    return limit;
}
static void prepared_extent_slot(UINT *extent,const void *slots,UINT slot,int live)
{
    if(live){if(*extent<=slot)*extent=slot+1;}
    else if(*extent==slot+1)*extent=prepared_live_extent(slots,*extent);
}
void prepared_state_derive_extents(struct PreparedState *s)
{
    s->vertex_count=prepared_live_extent(s->vertices,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT);
    for(unsigned stage=0;stage<PREPARED_STAGES;++stage) {
        s->constant_count[stage]=prepared_live_extent(s->constants[stage],D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
        s->resource_count[stage]=prepared_live_extent(s->resources[stage],D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
        s->sampler_count[stage]=prepared_live_extent(s->samplers[stage],D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT);
    }
}
static void prepared_state_refs(struct PreparedState *s,int add)
{
#define PREPARED_REF(p) do {if(add)prepared_add(p);else prepared_drop(p);}while(0)
    PREPARED_REF(s->layout);PREPARED_REF(s->index);
    for(unsigned i=0;i<s->vertex_count;++i)PREPARED_REF(s->vertices[i]);
    PREPARED_REF(s->vertex);PREPARED_REF(s->geometry);PREPARED_REF(s->pixel);
    for(unsigned stage=0;stage<3;++stage) {
        for(unsigned i=0;i<s->class_count[stage];++i)PREPARED_REF(s->classes[stage][i]);
        for(unsigned i=0;i<s->constant_count[stage];++i)PREPARED_REF(s->constants[stage][i]);
        for(unsigned i=0;i<s->resource_count[stage];++i)PREPARED_REF(s->resources[stage][i]);
        for(unsigned i=0;i<s->sampler_count[stage];++i)PREPARED_REF(s->samplers[stage][i]);
    }
    PREPARED_REF(s->raster);PREPARED_REF(s->blend);PREPARED_REF(s->depth);PREPARED_REF(s->predicate);
    for(unsigned i=0;i<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++i)PREPARED_REF(s->outputs[i]);
    PREPARED_REF(s->target_depth);
#undef PREPARED_REF
}
void prepared_state_release(struct PreparedState *s)
{ if(s){prepared_state_refs(s,0);memset(s,0,sizeof(*s));} }
static void prepared_clear_bindings(void)
{
    prepared_state_release(&prepared_owner.state);
    for(unsigned stage=0;stage<3;++stage) {
        struct PreparedExtraStage *e=&prepared_owner.extra[stage];prepared_drop(e->shader);
        for(unsigned i=0;i<e->class_count;++i)prepared_drop(e->classes[i]);
        for(unsigned i=0;i<D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;++i)prepared_drop(e->constants[i]);
        for(unsigned i=0;i<D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++i)prepared_drop(e->resources[i]);
        for(unsigned i=0;i<D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;++i)prepared_drop(e->samplers[i]);
    }
    memset(prepared_owner.extra,0,sizeof(prepared_owner.extra));
    for(unsigned i=0;i<D3D11_SO_BUFFER_SLOT_COUNT;++i)prepared_drop(prepared_owner.so[i]);
    for(unsigned i=0;i<D3D11_PS_CS_UAV_REGISTER_COUNT;++i) {
        prepared_drop(prepared_owner.om_uavs[i]);prepared_drop(prepared_owner.cs_uavs[i]);
    }
    memset(prepared_owner.so,0,sizeof(prepared_owner.so));
    memset(prepared_owner.om_uavs,0,sizeof(prepared_owner.om_uavs));
    memset(prepared_owner.cs_uavs,0,sizeof(prepared_owner.cs_uavs));
    memset(prepared_owner.buffer_input,0,sizeof(prepared_owner.buffer_input));
    prepared_owner.buffer_inputs=prepared_owner.non_graphics_inputs=0;
}
static void prepared_clear_facts(void)
{
    for(unsigned i=0;i<PREPARED_FACTS;++i)prepared_drop(prepared_owner.facts[i].key);
    for(unsigned i=0;i<PREPARED_STATE_FACTS;++i) {
        prepared_drop(prepared_owner.depths[i].key);prepared_drop(prepared_owner.blends[i].key);
    }
    memset(prepared_owner.facts,0,sizeof(prepared_owner.facts));
    memset(prepared_owner.depths,0,sizeof(prepared_owner.depths));
    memset(prepared_owner.blends,0,sizeof(prepared_owner.blends));
    prepared_owner.next_fact=prepared_owner.next_depth=prepared_owner.next_blend=0;
}
void prepared_state_invalidate(ID3D11DeviceContext *context)
{
    if(context!=prepared_owner.context)return;
    prepared_owner.known=0;prepared_clear_bindings();prepared_clear_facts();
}
void prepared_state_detach(ID3D11DeviceContext *context)
{
    if(context!=prepared_owner.context)return;
    prepared_state_invalidate(context);prepared_owner.context=NULL;prepared_drop(context);
}
static void prepared_default_depth(D3D11_DEPTH_STENCIL_DESC *d)
{
    memset(d,0,sizeof(*d));d->DepthEnable=TRUE;d->DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
    d->DepthFunc=D3D11_COMPARISON_LESS;d->StencilReadMask=d->StencilWriteMask=D3D11_DEFAULT_STENCIL_READ_MASK;
    d->FrontFace.StencilFailOp=d->FrontFace.StencilDepthFailOp=d->FrontFace.StencilPassOp=D3D11_STENCIL_OP_KEEP;
    d->FrontFace.StencilFunc=D3D11_COMPARISON_ALWAYS;d->BackFace=d->FrontFace;
}
static void prepared_default_blend(D3D11_BLEND_DESC *d)
{
    memset(d,0,sizeof(*d));
    for(unsigned i=0;i<8;++i) {
        D3D11_RENDER_TARGET_BLEND_DESC *r=&d->RenderTarget[i];r->SrcBlend=r->SrcBlendAlpha=D3D11_BLEND_ONE;
        r->DestBlend=r->DestBlendAlpha=D3D11_BLEND_ZERO;r->BlendOp=r->BlendOpAlpha=D3D11_BLEND_OP_ADD;
        r->RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    }
}
static void prepared_describe_depth(ID3D11DepthStencilState *state,D3D11_DEPTH_STENCIL_DESC *out)
{
    if(!state){prepared_default_depth(out);return;}
    for(unsigned i=0;i<PREPARED_STATE_FACTS;++i)if(prepared_owner.depths[i].key==state){*out=prepared_owner.depths[i].desc;return;}
    struct PreparedDepthFact *f=&prepared_owner.depths[prepared_owner.next_depth++%PREPARED_STATE_FACTS];
    prepared_add(state);prepared_drop(f->key);f->key=state;state->lpVtbl->GetDesc(state,&f->desc);*out=f->desc;
}
static void prepared_describe_blend(ID3D11BlendState *state,D3D11_BLEND_DESC *out)
{
    if(!state){prepared_default_blend(out);return;}
    for(unsigned i=0;i<PREPARED_STATE_FACTS;++i)if(prepared_owner.blends[i].key==state){*out=prepared_owner.blends[i].desc;return;}
    struct PreparedBlendFact *f=&prepared_owner.blends[prepared_owner.next_blend++%PREPARED_STATE_FACTS];
    prepared_add(state);prepared_drop(f->key);f->key=state;state->lpVtbl->GetDesc(state,&f->desc);*out=f->desc;
}
static void prepared_defaults(void)
{
    prepared_clear_bindings();
    for(unsigned i=0;i<4;++i)prepared_owner.state.factors[i]=1.0f;
    prepared_owner.state.sample_mask=~0u;
    prepared_default_depth(&prepared_owner.depth_desc);prepared_default_blend(&prepared_owner.blend_desc);
    prepared_owner.known=1;
}
int prepared_state_attach(ID3D11DeviceContext *context,int default_state)
{
    if(!context || context->lpVtbl->GetType(context)!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return 0;
    if(prepared_owner.context)prepared_state_detach(prepared_owner.context);
    prepared_add(context);prepared_owner.context=context;
    if(default_state)prepared_defaults();return 1;
}

int prepared_state_view_facts(ID3D11View *view,unsigned kind,struct PreparedViewFacts *out)
{
    if(!view || !out || kind<1 || kind>4)return 0;
    for(unsigned i=0;i<PREPARED_FACTS;++i)if(prepared_owner.facts[i].key==view && prepared_owner.facts[i].facts.kind==kind) {
        *out=prepared_owner.facts[i].facts;return 1;
    }
    struct PreparedViewFacts f;memset(&f,0,sizeof(f));f.kind=kind;
    view->lpVtbl->GetResource(view,&f.resource);if(!f.resource)return 0;
    f.resource->lpVtbl->GetType(f.resource,&f.dimension);
    if(f.dimension==D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        ID3D11Texture2D *image=NULL;
        if(SUCCEEDED(f.resource->lpVtbl->QueryInterface(f.resource,&IID_ID3D11Texture2D,(void**)&image))) {
            image->lpVtbl->GetDesc(image,&f.texture);prepared_drop(image);
        }
    }
    if(kind==1)((ID3D11ShaderResourceView*)view)->lpVtbl->GetDesc((ID3D11ShaderResourceView*)view,&f.desc.srv);
    if(kind==2)((ID3D11RenderTargetView*)view)->lpVtbl->GetDesc((ID3D11RenderTargetView*)view,&f.desc.rtv);
    if(kind==3)((ID3D11DepthStencilView*)view)->lpVtbl->GetDesc((ID3D11DepthStencilView*)view,&f.desc.dsv);
    if(kind==4)((ID3D11UnorderedAccessView*)view)->lpVtbl->GetDesc((ID3D11UnorderedAccessView*)view,&f.desc.uav);
    if(f.texture.ArraySize==1 && f.texture.MipLevels==1) {
        if(kind==1)f.simple_whole=(f.desc.srv.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D &&
            f.desc.srv.Texture2D.MostDetailedMip==0 && (f.desc.srv.Texture2D.MipLevels==1 || f.desc.srv.Texture2D.MipLevels==~0u)) ||
            f.desc.srv.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2DMS;
        if(kind==2)f.simple_whole=(f.desc.rtv.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2D &&
            f.desc.rtv.Texture2D.MipSlice==0) || f.desc.rtv.ViewDimension==D3D11_RTV_DIMENSION_TEXTURE2DMS;
        if(kind==3)f.simple_whole=f.desc.dsv.Flags==0 && ((f.desc.dsv.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2D &&
            f.desc.dsv.Texture2D.MipSlice==0) || f.desc.dsv.ViewDimension==D3D11_DSV_DIMENSION_TEXTURE2DMS);
        if(kind==4)f.simple_whole=f.desc.uav.ViewDimension==D3D11_UAV_DIMENSION_TEXTURE2D && f.desc.uav.Texture2D.MipSlice==0;
    }
    /* The cache key owns the view, which owns the resource identity. */
    struct PreparedFactEntry *entry=&prepared_owner.facts[prepared_owner.next_fact++%PREPARED_FACTS];
    prepared_add(view);prepared_drop(entry->key);entry->key=view;entry->facts=f;*out=f;prepared_drop(f.resource);
    return 1;
}
void prepared_state_forget_view(ID3D11View *view)
{
    if(!view)return;
    /* Drop metadata ownership before the caller's reference, preventing stale
     * pointer ABA and pinning destroyed/reset HD targets in the cold cache. */
    for(unsigned i=0;i<PREPARED_FACTS;++i)if(prepared_owner.facts[i].key==view) {
        prepared_owner.facts[i].key=NULL;prepared_drop(view);
    }
}
ULONG prepared_state_view_release(ID3D11View *view)
{
    if(!view)return 0;
    prepared_state_forget_view(view);
    return view->lpVtbl->Release(view);
}
/* Return -1 for ambiguous subresource/readonly relationships, 0 disjoint,
 * 1 exact whole-resource overlap. Never guess a rich view's hazard range. */
static int prepared_overlap(ID3D11View *a,unsigned ak,ID3D11View *b,unsigned bk)
{
    struct PreparedViewFacts af,bf;if(!a || !b)return 0;
    if(!prepared_state_view_facts(a,ak,&af) || !prepared_state_view_facts(b,bk,&bf))return -1;
    if(af.resource!=bf.resource)return 0;
    return af.simple_whole && bf.simple_whole?1:-1;
}
static int prepared_input_blocked(ID3D11ShaderResourceView *input)
{
    int overlap;
    for(unsigned i=0;i<D3D11_SO_BUFFER_SLOT_COUNT;++i)if(prepared_owner.so[i])return -1;
    for(unsigned i=0;i<8;++i) {
        overlap=prepared_overlap((ID3D11View*)input,1,(ID3D11View*)prepared_owner.state.outputs[i],2);if(overlap)return overlap;
        overlap=prepared_overlap((ID3D11View*)input,1,(ID3D11View*)prepared_owner.om_uavs[i],4);if(overlap)return overlap;
        overlap=prepared_overlap((ID3D11View*)input,1,(ID3D11View*)prepared_owner.cs_uavs[i],4);if(overlap)return overlap;
    }
    return prepared_overlap((ID3D11View*)input,1,(ID3D11View*)prepared_owner.state.target_depth,3);
}
static int prepared_record_srv(unsigned stage,unsigned slot,ID3D11ShaderResourceView *value)
{
    ID3D11ShaderResourceView **old=prepared_resources(stage);
    if(old[slot]==value)return 1;
    unsigned buffer=0;
    if(value) {
        struct PreparedViewFacts f;if(!prepared_state_view_facts((ID3D11View*)value,1,&f))return 0;
        buffer=f.dimension==D3D11_RESOURCE_DIMENSION_BUFFER;
    }
    if(stage<3)prepared_owner.buffer_inputs-=prepared_owner.buffer_input[stage][slot];
    else if(old[slot])--prepared_owner.non_graphics_inputs;
    prepared_owner.buffer_input[stage][slot]=buffer;
    if(stage<3)prepared_owner.buffer_inputs+=buffer;
    else if(value)++prepared_owner.non_graphics_inputs;
    prepared_assign(&old[slot],value);
    if(stage<PREPARED_STAGES)prepared_extent_slot(&prepared_owner.state.resource_count[stage],old,slot,value!=NULL);
    return 1;
}
static int prepared_unbind_inputs(ID3D11View *output,unsigned kind)
{
    if(!output)return 1;
    for(unsigned stage=0;stage<6;++stage) {
        ID3D11ShaderResourceView **inputs=prepared_resources(stage);
        for(unsigned i=0;i<128;++i)if(inputs[i]) {
            int overlap=prepared_overlap((ID3D11View*)inputs[i],1,output,kind);
            if(overlap<0)return 0;if(overlap && !prepared_record_srv(stage,i,NULL))return 0;
        }
    }
    return 1;
}
static unsigned prepared_unsupported(void)
{
    if(prepared_owner.state.predicate || prepared_owner.buffer_inputs || prepared_owner.non_graphics_inputs ||
        prepared_owner.extra[0].shader || prepared_owner.extra[1].shader)return 1;
    for(unsigned i=0;i<D3D11_SO_BUFFER_SLOT_COUNT;++i)if(prepared_owner.so[i])return 1;
    for(unsigned i=0;i<8;++i)if(prepared_owner.om_uavs[i] || prepared_owner.cs_uavs[i])return 1;
    return 0;
}

/* Explicit external/unknown-boundary repair. No normal setter invokes this. */
static int prepared_resync(ID3D11DeviceContext *c)
{
    if(!c || c!=prepared_owner.context)return 0;
    prepared_clear_bindings();struct PreparedState *s=&prepared_owner.state;
    /* Full bounds also make partial/failing raw captures safe to destroy. */
    s->vertex_count=D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
    for(unsigned stage=0;stage<PREPARED_STAGES;++stage) {
        s->constant_count[stage]=D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
        s->resource_count[stage]=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
        s->sampler_count[stage]=D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
    }
    c->lpVtbl->IAGetInputLayout(c,&s->layout);
    c->lpVtbl->IAGetVertexBuffers(c,0,32,s->vertices,s->strides,s->offsets);
    c->lpVtbl->IAGetIndexBuffer(c,&s->index,&s->index_format,&s->index_offset);
    c->lpVtbl->IAGetPrimitiveTopology(c,&s->topology);
        *prepared_class_count(0)=PREPARED_CLASSES;
    c->lpVtbl->VSGetShader(c,&s->vertex,prepared_classes(0),prepared_class_count(0));
    c->lpVtbl->VSGetConstantBuffers(c,0,14,prepared_constants(0));
    c->lpVtbl->VSGetShaderResources(c,0,128,prepared_resources(0));
    c->lpVtbl->VSGetSamplers(c,0,16,prepared_samplers(0));
    *prepared_class_count(1)=PREPARED_CLASSES;
    c->lpVtbl->GSGetShader(c,&s->geometry,prepared_classes(1),prepared_class_count(1));
    c->lpVtbl->GSGetConstantBuffers(c,0,14,prepared_constants(1));
    c->lpVtbl->GSGetShaderResources(c,0,128,prepared_resources(1));
    c->lpVtbl->GSGetSamplers(c,0,16,prepared_samplers(1));
    *prepared_class_count(2)=PREPARED_CLASSES;
    c->lpVtbl->PSGetShader(c,&s->pixel,prepared_classes(2),prepared_class_count(2));
    c->lpVtbl->PSGetConstantBuffers(c,0,14,prepared_constants(2));
    c->lpVtbl->PSGetShaderResources(c,0,128,prepared_resources(2));
    c->lpVtbl->PSGetSamplers(c,0,16,prepared_samplers(2));
    *prepared_class_count(3)=PREPARED_CLASSES;
    c->lpVtbl->HSGetShader(c,(ID3D11HullShader**)&prepared_owner.extra[0].shader,prepared_classes(3),prepared_class_count(3));
    c->lpVtbl->HSGetConstantBuffers(c,0,14,prepared_constants(3));
    c->lpVtbl->HSGetShaderResources(c,0,128,prepared_resources(3));
    c->lpVtbl->HSGetSamplers(c,0,16,prepared_samplers(3));
    *prepared_class_count(4)=PREPARED_CLASSES;
    c->lpVtbl->DSGetShader(c,(ID3D11DomainShader**)&prepared_owner.extra[1].shader,prepared_classes(4),prepared_class_count(4));
    c->lpVtbl->DSGetConstantBuffers(c,0,14,prepared_constants(4));
    c->lpVtbl->DSGetShaderResources(c,0,128,prepared_resources(4));
    c->lpVtbl->DSGetSamplers(c,0,16,prepared_samplers(4));
    *prepared_class_count(5)=PREPARED_CLASSES;
    c->lpVtbl->CSGetShader(c,(ID3D11ComputeShader**)&prepared_owner.extra[2].shader,prepared_classes(5),prepared_class_count(5));
    c->lpVtbl->CSGetConstantBuffers(c,0,14,prepared_constants(5));
    c->lpVtbl->CSGetShaderResources(c,0,128,prepared_resources(5));
    c->lpVtbl->CSGetSamplers(c,0,16,prepared_samplers(5));
    c->lpVtbl->RSGetState(c,&s->raster);
    s->viewport_count=16;c->lpVtbl->RSGetViewports(c,&s->viewport_count,s->viewports);
    s->scissor_count=16;c->lpVtbl->RSGetScissorRects(c,&s->scissor_count,s->scissors);
    c->lpVtbl->OMGetBlendState(c,&s->blend,s->factors,&s->sample_mask);
    c->lpVtbl->OMGetDepthStencilState(c,&s->depth,&s->stencil);
    c->lpVtbl->GetPredication(c,&s->predicate,&s->predicate_value);
    c->lpVtbl->OMGetRenderTargets(c,8,s->outputs,&s->target_depth);
    c->lpVtbl->OMGetRenderTargetsAndUnorderedAccessViews(c,0,NULL,NULL,0,8,prepared_owner.om_uavs);
    c->lpVtbl->CSGetUnorderedAccessViews(c,0,8,prepared_owner.cs_uavs);
    c->lpVtbl->SOGetTargets(c,D3D11_SO_BUFFER_SLOT_COUNT,prepared_owner.so);
    for(unsigned stage=0;stage<6;++stage)for(unsigned i=0;i<128;++i)if(prepared_resources(stage)[i]) {
        struct PreparedViewFacts f;
        if(!prepared_state_view_facts((ID3D11View*)prepared_resources(stage)[i],1,&f)) {
            prepared_clear_bindings();return 0;
        }
        unsigned buffer=f.dimension==D3D11_RESOURCE_DIMENSION_BUFFER;
        prepared_owner.buffer_input[stage][i]=buffer;
        if(stage<3)prepared_owner.buffer_inputs+=buffer;else ++prepared_owner.non_graphics_inputs;
    }
    prepared_state_derive_extents(s);
    prepared_describe_blend(s->blend,&prepared_owner.blend_desc);
    prepared_describe_depth(s->depth,&prepared_owner.depth_desc);
    prepared_owner.known=1;return 1;
}
int prepared_state_snapshot(ID3D11DeviceContext *context,struct PreparedState *out)
{
    if(!prepared_state_snapshot_borrow(context,out))return 0;
    prepared_state_retain(out);return 1;
}
void prepared_state_retain(struct PreparedState *s)
{prepared_state_refs(s,1);}
int prepared_state_snapshot_borrow(ID3D11DeviceContext *context,struct PreparedState *out)
{
    if(!out || context!=prepared_owner.context || (!prepared_owner.known && !prepared_resync(context)))return 0;
    if(prepared_unsupported())return 0;
    *out=prepared_owner.state;return 1;
}
int prepared_state_draw_subset(ID3D11DeviceContext *context,struct PreparedDrawSubset *out)
{
    if(!prepared_state_draw_subset_borrow(context,out))return 0;
    for(unsigned i=0;i<8;++i)prepared_add(out->outputs[i]);prepared_add(out->depth);
    for(unsigned i=0;i<5;++i)prepared_add(out->inputs[i]);
    prepared_add(out->blend);prepared_add(out->depth_state);out->borrowed=0;return 1;
}
int prepared_state_draw_subset_borrow(ID3D11DeviceContext *context,struct PreparedDrawSubset *out)
{
    if(!out || context!=prepared_owner.context || (!prepared_owner.known && !prepared_resync(context)))return 0;
    struct PreparedState *s=&prepared_owner.state;memset(out,0,sizeof(*out));
    for(unsigned i=0;i<8;++i)out->outputs[i]=s->outputs[i];
    out->depth=s->target_depth;
    for(unsigned i=0;i<5;++i)out->inputs[i]=s->resources[2][i];
    out->blend=s->blend;out->depth_state=s->depth;out->borrowed=1;
    out->blend_desc=prepared_owner.blend_desc;out->depth_desc=prepared_owner.depth_desc;
    memcpy(out->factors,s->factors,sizeof(out->factors));out->sample_mask=s->sample_mask;out->stencil=s->stencil;
    out->viewport_count=s->viewport_count;out->scissor_count=s->scissor_count;
    memcpy(out->viewports,s->viewports,sizeof(out->viewports));memcpy(out->scissors,s->scissors,sizeof(out->scissors));
    out->unsupported=prepared_unsupported();return 1;
}
void prepared_state_draw_release(struct PreparedDrawSubset *s)
{
    if(!s)return;
    if(s->borrowed){memset(s,0,sizeof(*s));return;}
    for(unsigned i=0;i<8;++i)prepared_drop(s->outputs[i]);prepared_drop(s->depth);
    for(unsigned i=0;i<5;++i)prepared_drop(s->inputs[i]);prepared_drop(s->blend);prepared_drop(s->depth_state);
    memset(s,0,sizeof(*s));
}

void prepared_IASetInputLayout(ID3D11DeviceContext *c,ID3D11InputLayout *v)
{ c->lpVtbl->IASetInputLayout(c,v);if(prepared_known(c))prepared_assign(&prepared_owner.state.layout,v); }
void prepared_IASetPrimitiveTopology(ID3D11DeviceContext *c,D3D11_PRIMITIVE_TOPOLOGY v)
{ c->lpVtbl->IASetPrimitiveTopology(c,v);if(prepared_known(c))prepared_owner.state.topology=v; }
void prepared_IASetIndexBuffer(ID3D11DeviceContext *c,ID3D11Buffer *v,DXGI_FORMAT f,UINT o)
{
    c->lpVtbl->IASetIndexBuffer(c,v,f,o);if(!prepared_known(c))return;
    for(unsigned i=0;i<D3D11_SO_BUFFER_SLOT_COUNT;++i)if(v && v==prepared_owner.so[i]){prepared_state_invalidate(c);return;}
    prepared_assign(&prepared_owner.state.index,v);prepared_owner.state.index_format=f;prepared_owner.state.index_offset=o;
}
void prepared_IASetVertexBuffers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11Buffer *const *v,const UINT *strides,const UINT *offsets)
{
    c->lpVtbl->IASetVertexBuffers(c,start,n,v,strides,offsets);if(!prepared_known(c))return;
    if(start>32 || n>32-start || (n && (!v || !strides || !offsets))){prepared_state_invalidate(c);return;}
    for(unsigned i=0;i<n;++i) {
        /* A buffer simultaneously bound for SO is implicitly NULL at IA. */
        ID3D11Buffer *actual=v[i];
        for(unsigned j=0;j<D3D11_SO_BUFFER_SLOT_COUNT;++j)if(actual && actual==prepared_owner.so[j]){prepared_state_invalidate(c);return;}
        prepared_assign(&prepared_owner.state.vertices[start+i],actual);
        prepared_extent_slot(&prepared_owner.state.vertex_count,prepared_owner.state.vertices,start+i,actual!=NULL);
        prepared_owner.state.strides[start+i]=strides[i];prepared_owner.state.offsets[start+i]=offsets[i];
    }
}
void prepared_OMSetBlendState(ID3D11DeviceContext *c,ID3D11BlendState *v,const FLOAT *f,UINT mask)
{
    c->lpVtbl->OMSetBlendState(c,v,f,mask);if(!prepared_known(c))return;
    prepared_assign(&prepared_owner.state.blend,v);prepared_describe_blend(v,&prepared_owner.blend_desc);
    for(unsigned i=0;i<4;++i)prepared_owner.state.factors[i]=f?f[i]:1.0f;prepared_owner.state.sample_mask=mask;
}
void prepared_OMSetDepthStencilState(ID3D11DeviceContext *c,ID3D11DepthStencilState *v,UINT ref)
{
    c->lpVtbl->OMSetDepthStencilState(c,v,ref);if(!prepared_known(c))return;
    prepared_assign(&prepared_owner.state.depth,v);prepared_owner.state.stencil=ref;prepared_describe_depth(v,&prepared_owner.depth_desc);
}
void prepared_RSSetState(ID3D11DeviceContext *c,ID3D11RasterizerState *v)
{ c->lpVtbl->RSSetState(c,v);if(prepared_known(c))prepared_assign(&prepared_owner.state.raster,v); }
void prepared_RSSetViewports(ID3D11DeviceContext *c,UINT n,const D3D11_VIEWPORT *v)
{
    c->lpVtbl->RSSetViewports(c,n,v);if(!prepared_known(c))return;
    if(n>16 || (n && !v)){prepared_state_invalidate(c);return;}
    memset(prepared_owner.state.viewports,0,sizeof(prepared_owner.state.viewports));
    if(n)memcpy(prepared_owner.state.viewports,v,n*sizeof(*v));prepared_owner.state.viewport_count=n;
}
void prepared_RSSetScissorRects(ID3D11DeviceContext *c,UINT n,const D3D11_RECT *v)
{
    c->lpVtbl->RSSetScissorRects(c,n,v);if(!prepared_known(c))return;
    if(n>16 || (n && !v)){prepared_state_invalidate(c);return;}
    memset(prepared_owner.state.scissors,0,sizeof(prepared_owner.state.scissors));
    if(n)memcpy(prepared_owner.state.scissors,v,n*sizeof(*v));prepared_owner.state.scissor_count=n;
}
void prepared_SetPredication(ID3D11DeviceContext *c,ID3D11Predicate *v,BOOL value)
{ c->lpVtbl->SetPredication(c,v,value);if(prepared_known(c)){prepared_assign(&prepared_owner.state.predicate,v);prepared_owner.state.predicate_value=value;} }
void prepared_SOSetTargets(ID3D11DeviceContext *c,UINT n,ID3D11Buffer *const *v,const UINT *offsets)
{
    c->lpVtbl->SOSetTargets(c,n,v,offsets);
    /* SO can implicitly unbind IA/SRVs and has hidden append offsets. This is
     * the private VS-probe boundary, not a supported HD draw route. */
    if(c==prepared_owner.context)prepared_state_invalidate(c);
}
void prepared_ClearState(ID3D11DeviceContext *c)
{ c->lpVtbl->ClearState(c);if(c==prepared_owner.context)prepared_defaults(); }
void prepared_ExecuteCommandList(ID3D11DeviceContext *c,ID3D11CommandList *list,BOOL restore)
{
    c->lpVtbl->ExecuteCommandList(c,list,restore);
    /* TRUE restores the exact old state. FALSE sets default state, per D3D11.
     * Keep the boundary conservative: no inference about foreign commandlists. */
    if(c==prepared_owner.context)prepared_state_invalidate(c);
}

void prepared_VSSetShader(ID3D11DeviceContext *c,ID3D11VertexShader *v,ID3D11ClassInstance *const *classes,UINT count)
{
    c->lpVtbl->VSSetShader(c,v,classes,count);if(!prepared_known(c))return;
    if(count>PREPARED_CLASSES || (v && count && !classes)){prepared_state_invalidate(c);return;}
    prepared_assign(prepared_shader_slot(0),v);
    ID3D11ClassInstance **old=prepared_classes(0);UINT before=*prepared_class_count(0);
    UINT after=v?count:0;
    for(unsigned i=0;i<after || i<before;++i)prepared_assign(&old[i],i<after?classes[i]:NULL);
    *prepared_class_count(0)=after;
}
void prepared_VSSetConstantBuffers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11Buffer *const *v)
{
    c->lpVtbl->VSSetConstantBuffers(c,start,n,v);if(!prepared_known(c))return;
    if(start>14 || n>14-start){prepared_state_invalidate(c);return;}
    ID3D11Buffer **old=prepared_constants(0);
    for(unsigned i=0;i<n;++i) {
        prepared_assign(&old[start+i],v?v[i]:NULL);
        prepared_extent_slot(&prepared_owner.state.constant_count[0],old,start+i,old[start+i]!=NULL);
    }
}
void prepared_VSSetShaderResources(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11ShaderResourceView *const *v)
{
    c->lpVtbl->VSSetShaderResources(c,start,n,v);if(!prepared_known(c))return;
    if(start>128 || n>128-start){prepared_state_invalidate(c);return;}
    ID3D11ShaderResourceView **old=prepared_resources(0);
    for(unsigned i=0;i<n;++i) {
        ID3D11ShaderResourceView *actual=v?v[i]:NULL;
        /* A redundant known bind is already hazard-resolved. Output setters
         * keep conflicting slots in the mirror NULL. */
        if(old[start+i]==actual)continue;
        int blocked=actual?prepared_input_blocked(actual):0;
        if(blocked<0){prepared_state_invalidate(c);return;}if(blocked)actual=NULL;
        if(!prepared_record_srv(0,start+i,actual)){prepared_state_invalidate(c);return;}
    }
}
void prepared_VSSetSamplers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11SamplerState *const *v)
{
    c->lpVtbl->VSSetSamplers(c,start,n,v);if(!prepared_known(c))return;
    if(start>16 || n>16-start){prepared_state_invalidate(c);return;}
    ID3D11SamplerState **old=prepared_samplers(0);
    for(unsigned i=0;i<n;++i) {
        prepared_assign(&old[start+i],v?v[i]:NULL);
        prepared_extent_slot(&prepared_owner.state.sampler_count[0],old,start+i,old[start+i]!=NULL);
    }
}

void prepared_GSSetShader(ID3D11DeviceContext *c,ID3D11GeometryShader *v,ID3D11ClassInstance *const *classes,UINT count)
{
    c->lpVtbl->GSSetShader(c,v,classes,count);if(!prepared_known(c))return;
    if(count>PREPARED_CLASSES || (v && count && !classes)){prepared_state_invalidate(c);return;}
    prepared_assign(prepared_shader_slot(1),v);
    ID3D11ClassInstance **old=prepared_classes(1);UINT before=*prepared_class_count(1);
    UINT after=v?count:0;
    for(unsigned i=0;i<after || i<before;++i)prepared_assign(&old[i],i<after?classes[i]:NULL);
    *prepared_class_count(1)=after;
}
void prepared_GSSetConstantBuffers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11Buffer *const *v)
{
    c->lpVtbl->GSSetConstantBuffers(c,start,n,v);if(!prepared_known(c))return;
    if(start>14 || n>14-start){prepared_state_invalidate(c);return;}
    ID3D11Buffer **old=prepared_constants(1);
    for(unsigned i=0;i<n;++i) {
        prepared_assign(&old[start+i],v?v[i]:NULL);
        prepared_extent_slot(&prepared_owner.state.constant_count[1],old,start+i,old[start+i]!=NULL);
    }
}
void prepared_GSSetShaderResources(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11ShaderResourceView *const *v)
{
    c->lpVtbl->GSSetShaderResources(c,start,n,v);if(!prepared_known(c))return;
    if(start>128 || n>128-start){prepared_state_invalidate(c);return;}
    ID3D11ShaderResourceView **old=prepared_resources(1);
    for(unsigned i=0;i<n;++i) {
        ID3D11ShaderResourceView *actual=v?v[i]:NULL;
        /* A redundant known bind is already hazard-resolved. Output setters
         * keep conflicting slots in the mirror NULL. */
        if(old[start+i]==actual)continue;
        int blocked=actual?prepared_input_blocked(actual):0;
        if(blocked<0){prepared_state_invalidate(c);return;}if(blocked)actual=NULL;
        if(!prepared_record_srv(1,start+i,actual)){prepared_state_invalidate(c);return;}
    }
}
void prepared_GSSetSamplers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11SamplerState *const *v)
{
    c->lpVtbl->GSSetSamplers(c,start,n,v);if(!prepared_known(c))return;
    if(start>16 || n>16-start){prepared_state_invalidate(c);return;}
    ID3D11SamplerState **old=prepared_samplers(1);
    for(unsigned i=0;i<n;++i) {
        prepared_assign(&old[start+i],v?v[i]:NULL);
        prepared_extent_slot(&prepared_owner.state.sampler_count[1],old,start+i,old[start+i]!=NULL);
    }
}

void prepared_PSSetShader(ID3D11DeviceContext *c,ID3D11PixelShader *v,ID3D11ClassInstance *const *classes,UINT count)
{
    c->lpVtbl->PSSetShader(c,v,classes,count);if(!prepared_known(c))return;
    if(count>PREPARED_CLASSES || (v && count && !classes)){prepared_state_invalidate(c);return;}
    prepared_assign(prepared_shader_slot(2),v);
    ID3D11ClassInstance **old=prepared_classes(2);UINT before=*prepared_class_count(2);
    UINT after=v?count:0;
    for(unsigned i=0;i<after || i<before;++i)prepared_assign(&old[i],i<after?classes[i]:NULL);
    *prepared_class_count(2)=after;
}
void prepared_PSSetConstantBuffers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11Buffer *const *v)
{
    c->lpVtbl->PSSetConstantBuffers(c,start,n,v);if(!prepared_known(c))return;
    if(start>14 || n>14-start){prepared_state_invalidate(c);return;}
    ID3D11Buffer **old=prepared_constants(2);
    for(unsigned i=0;i<n;++i) {
        prepared_assign(&old[start+i],v?v[i]:NULL);
        prepared_extent_slot(&prepared_owner.state.constant_count[2],old,start+i,old[start+i]!=NULL);
    }
}
void prepared_PSSetShaderResources(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11ShaderResourceView *const *v)
{
    c->lpVtbl->PSSetShaderResources(c,start,n,v);if(!prepared_known(c))return;
    if(start>128 || n>128-start){prepared_state_invalidate(c);return;}
    ID3D11ShaderResourceView **old=prepared_resources(2);
    for(unsigned i=0;i<n;++i) {
        ID3D11ShaderResourceView *actual=v?v[i]:NULL;
        /* A redundant known bind is already hazard-resolved. Output setters
         * keep conflicting slots in the mirror NULL. */
        if(old[start+i]==actual)continue;
        int blocked=actual?prepared_input_blocked(actual):0;
        if(blocked<0){prepared_state_invalidate(c);return;}if(blocked)actual=NULL;
        if(!prepared_record_srv(2,start+i,actual)){prepared_state_invalidate(c);return;}
    }
}
void prepared_PSSetSamplers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11SamplerState *const *v)
{
    c->lpVtbl->PSSetSamplers(c,start,n,v);if(!prepared_known(c))return;
    if(start>16 || n>16-start){prepared_state_invalidate(c);return;}
    ID3D11SamplerState **old=prepared_samplers(2);
    for(unsigned i=0;i<n;++i) {
        prepared_assign(&old[start+i],v?v[i]:NULL);
        prepared_extent_slot(&prepared_owner.state.sampler_count[2],old,start+i,old[start+i]!=NULL);
    }
}

void prepared_HSSetShader(ID3D11DeviceContext *c,ID3D11HullShader *v,ID3D11ClassInstance *const *classes,UINT count)
{
    c->lpVtbl->HSSetShader(c,v,classes,count);if(!prepared_known(c))return;
    if(count>PREPARED_CLASSES || (v && count && !classes)){prepared_state_invalidate(c);return;}
    prepared_assign(prepared_shader_slot(3),v);
    ID3D11ClassInstance **old=prepared_classes(3);UINT before=*prepared_class_count(3);
    UINT after=v?count:0;
    for(unsigned i=0;i<after || i<before;++i)prepared_assign(&old[i],i<after?classes[i]:NULL);
    *prepared_class_count(3)=after;
}
void prepared_HSSetConstantBuffers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11Buffer *const *v)
{
    c->lpVtbl->HSSetConstantBuffers(c,start,n,v);if(!prepared_known(c))return;
    if(start>14 || n>14-start){prepared_state_invalidate(c);return;}
    ID3D11Buffer **old=prepared_constants(3);
    for(unsigned i=0;i<n;++i)prepared_assign(&old[start+i],v?v[i]:NULL);
}
void prepared_HSSetShaderResources(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11ShaderResourceView *const *v)
{
    c->lpVtbl->HSSetShaderResources(c,start,n,v);if(!prepared_known(c))return;
    if(start>128 || n>128-start){prepared_state_invalidate(c);return;}
    ID3D11ShaderResourceView **old=prepared_resources(3);
    for(unsigned i=0;i<n;++i) {
        ID3D11ShaderResourceView *actual=v?v[i]:NULL;
        /* A redundant known bind is already hazard-resolved. Output setters
         * keep conflicting slots in the mirror NULL. */
        if(old[start+i]==actual)continue;
        int blocked=actual?prepared_input_blocked(actual):0;
        if(blocked<0){prepared_state_invalidate(c);return;}if(blocked)actual=NULL;
        if(!prepared_record_srv(3,start+i,actual)){prepared_state_invalidate(c);return;}
    }
}
void prepared_HSSetSamplers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11SamplerState *const *v)
{
    c->lpVtbl->HSSetSamplers(c,start,n,v);if(!prepared_known(c))return;
    if(start>16 || n>16-start){prepared_state_invalidate(c);return;}
    ID3D11SamplerState **old=prepared_samplers(3);
    for(unsigned i=0;i<n;++i)prepared_assign(&old[start+i],v?v[i]:NULL);
}

void prepared_DSSetShader(ID3D11DeviceContext *c,ID3D11DomainShader *v,ID3D11ClassInstance *const *classes,UINT count)
{
    c->lpVtbl->DSSetShader(c,v,classes,count);if(!prepared_known(c))return;
    if(count>PREPARED_CLASSES || (v && count && !classes)){prepared_state_invalidate(c);return;}
    prepared_assign(prepared_shader_slot(4),v);
    ID3D11ClassInstance **old=prepared_classes(4);UINT before=*prepared_class_count(4);
    UINT after=v?count:0;
    for(unsigned i=0;i<after || i<before;++i)prepared_assign(&old[i],i<after?classes[i]:NULL);
    *prepared_class_count(4)=after;
}
void prepared_DSSetConstantBuffers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11Buffer *const *v)
{
    c->lpVtbl->DSSetConstantBuffers(c,start,n,v);if(!prepared_known(c))return;
    if(start>14 || n>14-start){prepared_state_invalidate(c);return;}
    ID3D11Buffer **old=prepared_constants(4);
    for(unsigned i=0;i<n;++i)prepared_assign(&old[start+i],v?v[i]:NULL);
}
void prepared_DSSetShaderResources(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11ShaderResourceView *const *v)
{
    c->lpVtbl->DSSetShaderResources(c,start,n,v);if(!prepared_known(c))return;
    if(start>128 || n>128-start){prepared_state_invalidate(c);return;}
    ID3D11ShaderResourceView **old=prepared_resources(4);
    for(unsigned i=0;i<n;++i) {
        ID3D11ShaderResourceView *actual=v?v[i]:NULL;
        /* A redundant known bind is already hazard-resolved. Output setters
         * keep conflicting slots in the mirror NULL. */
        if(old[start+i]==actual)continue;
        int blocked=actual?prepared_input_blocked(actual):0;
        if(blocked<0){prepared_state_invalidate(c);return;}if(blocked)actual=NULL;
        if(!prepared_record_srv(4,start+i,actual)){prepared_state_invalidate(c);return;}
    }
}
void prepared_DSSetSamplers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11SamplerState *const *v)
{
    c->lpVtbl->DSSetSamplers(c,start,n,v);if(!prepared_known(c))return;
    if(start>16 || n>16-start){prepared_state_invalidate(c);return;}
    ID3D11SamplerState **old=prepared_samplers(4);
    for(unsigned i=0;i<n;++i)prepared_assign(&old[start+i],v?v[i]:NULL);
}

void prepared_CSSetShader(ID3D11DeviceContext *c,ID3D11ComputeShader *v,ID3D11ClassInstance *const *classes,UINT count)
{
    c->lpVtbl->CSSetShader(c,v,classes,count);if(!prepared_known(c))return;
    if(count>PREPARED_CLASSES || (v && count && !classes)){prepared_state_invalidate(c);return;}
    prepared_assign(prepared_shader_slot(5),v);
    ID3D11ClassInstance **old=prepared_classes(5);UINT before=*prepared_class_count(5);
    UINT after=v?count:0;
    for(unsigned i=0;i<after || i<before;++i)prepared_assign(&old[i],i<after?classes[i]:NULL);
    *prepared_class_count(5)=after;
}
void prepared_CSSetConstantBuffers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11Buffer *const *v)
{
    c->lpVtbl->CSSetConstantBuffers(c,start,n,v);if(!prepared_known(c))return;
    if(start>14 || n>14-start){prepared_state_invalidate(c);return;}
    ID3D11Buffer **old=prepared_constants(5);
    for(unsigned i=0;i<n;++i)prepared_assign(&old[start+i],v?v[i]:NULL);
}
void prepared_CSSetShaderResources(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11ShaderResourceView *const *v)
{
    c->lpVtbl->CSSetShaderResources(c,start,n,v);if(!prepared_known(c))return;
    if(start>128 || n>128-start){prepared_state_invalidate(c);return;}
    ID3D11ShaderResourceView **old=prepared_resources(5);
    for(unsigned i=0;i<n;++i) {
        ID3D11ShaderResourceView *actual=v?v[i]:NULL;
        /* A redundant known bind is already hazard-resolved. Output setters
         * keep conflicting slots in the mirror NULL. */
        if(old[start+i]==actual)continue;
        int blocked=actual?prepared_input_blocked(actual):0;
        if(blocked<0){prepared_state_invalidate(c);return;}if(blocked)actual=NULL;
        if(!prepared_record_srv(5,start+i,actual)){prepared_state_invalidate(c);return;}
    }
}
void prepared_CSSetSamplers(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11SamplerState *const *v)
{
    c->lpVtbl->CSSetSamplers(c,start,n,v);if(!prepared_known(c))return;
    if(start>16 || n>16-start){prepared_state_invalidate(c);return;}
    ID3D11SamplerState **old=prepared_samplers(5);
    for(unsigned i=0;i<n;++i)prepared_assign(&old[start+i],v?v[i]:NULL);
}

/* OM+UAV transitions have more implicit write/write rules than the closed
 * single-color path. Forward them then invalidate once at that boundary. */
void prepared_OMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext *c,UINT n,ID3D11RenderTargetView *const *v,
    ID3D11DepthStencilView *d,UINT start,UINT count,ID3D11UnorderedAccessView *const *u,const UINT *initial)
{
    c->lpVtbl->OMSetRenderTargetsAndUnorderedAccessViews(c,n,v,d,start,count,u,initial);
    if(c==prepared_owner.context)prepared_state_invalidate(c);
}
void prepared_CSSetUnorderedAccessViews(ID3D11DeviceContext *c,UINT start,UINT n,ID3D11UnorderedAccessView *const *v,const UINT *initial)
{
    c->lpVtbl->CSSetUnorderedAccessViews(c,start,n,v,initial);
    /* Resource conversion dispatch is an explicit ambiguity boundary. */
    if(c==prepared_owner.context)prepared_state_invalidate(c);
}
void prepared_OMSetRenderTargets(ID3D11DeviceContext *c,UINT n,ID3D11RenderTargetView *const *v,ID3D11DepthStencilView *d)
{
    c->lpVtbl->OMSetRenderTargets(c,n,v,d);if(!prepared_known(c))return;
    if(n>8 || (n && !v)){prepared_state_invalidate(c);return;}
    /* Resolve all output/output ambiguities before publishing any change. */
    for(unsigned i=0;i<n;++i)if(v[i]) {
        for(unsigned j=0;j<i;++j)if(prepared_overlap((ID3D11View*)v[i],2,(ID3D11View*)v[j],2))goto ambiguous;
        if(prepared_overlap((ID3D11View*)v[i],2,(ID3D11View*)d,3))goto ambiguous;
    }
    for(unsigned i=0;i<8;++i)if(prepared_owner.om_uavs[i] || prepared_owner.cs_uavs[i])goto ambiguous;
    for(unsigned i=0;i<n;++i)if(!prepared_unbind_inputs((ID3D11View*)v[i],2))goto ambiguous;
    if(!prepared_unbind_inputs((ID3D11View*)d,3))goto ambiguous;
    for(unsigned i=0;i<8;++i)prepared_assign(&prepared_owner.state.outputs[i],i<n?v[i]:NULL);
    prepared_assign(&prepared_owner.state.target_depth,d);return;
ambiguous: prepared_state_invalidate(c);
}

int prepared_state_rebind(ID3D11DeviceContext *c,UINT bind,ID3D11Buffer *(*resolve)(ID3D11Buffer *))
{
    if(!prepared_known(c) || !resolve)return 0;
    for(unsigned i=0;i<D3D11_SO_BUFFER_SLOT_COUNT;++i)if(prepared_owner.so[i])return 0;
    for(unsigned i=0;i<8;++i)if(prepared_owner.om_uavs[i] || prepared_owner.cs_uavs[i])return 0;
    /* Resolve every requested binding before changing anything. A failed
     * resolver leaves the context and mirror untouched for caller fallback. */
    ID3D11Buffer *values[6][D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT]={{0}};
    if(bind==D3D11_BIND_CONSTANT_BUFFER) {
        for(unsigned stage=0;stage<6;++stage)for(unsigned i=0;i<14;++i) {
            ID3D11Buffer *old=prepared_constants(stage)[i];values[stage][i]=old?resolve(old):NULL;
            if(old && !values[stage][i])return 0;
        }
                for(unsigned i=0;i<14;++i)if(values[0][i]!=prepared_constants(0)[i])prepared_VSSetConstantBuffers(c,i,1,&values[0][i]);
        for(unsigned i=0;i<14;++i)if(values[1][i]!=prepared_constants(1)[i])prepared_GSSetConstantBuffers(c,i,1,&values[1][i]);
        for(unsigned i=0;i<14;++i)if(values[2][i]!=prepared_constants(2)[i])prepared_PSSetConstantBuffers(c,i,1,&values[2][i]);
        for(unsigned i=0;i<14;++i)if(values[3][i]!=prepared_constants(3)[i])prepared_HSSetConstantBuffers(c,i,1,&values[3][i]);
        for(unsigned i=0;i<14;++i)if(values[4][i]!=prepared_constants(4)[i])prepared_DSSetConstantBuffers(c,i,1,&values[4][i]);
        for(unsigned i=0;i<14;++i)if(values[5][i]!=prepared_constants(5)[i])prepared_CSSetConstantBuffers(c,i,1,&values[5][i]);
        return 1;
    }
    if(bind==D3D11_BIND_VERTEX_BUFFER) {
        for(unsigned i=0;i<32;++i) {
            ID3D11Buffer *old=prepared_owner.state.vertices[i];values[0][i]=old?resolve(old):NULL;
            if(old && !values[0][i])return 0;
        }
        for(unsigned i=0;i<32;++i)if(values[0][i]!=prepared_owner.state.vertices[i])
            prepared_IASetVertexBuffers(c,i,1,&values[0][i],&prepared_owner.state.strides[i],&prepared_owner.state.offsets[i]);
        return 1;
    }
    if(bind==D3D11_BIND_INDEX_BUFFER) {
        ID3D11Buffer *old=prepared_owner.state.index,*value=old?resolve(old):NULL;
        if(old && !value)return 0;
        if(value!=old)prepared_IASetIndexBuffer(c,value,prepared_owner.state.index_format,prepared_owner.state.index_offset);
        return 1;
    }
    return 0;
}
#endif
