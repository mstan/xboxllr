/* Authored R09 immediate-context binding authority. Include AFTER d3d11.h,
 * with COBJMACROS defined, in every closed C setter translation unit.
 * Implementation is linked once. Other backends keep their existing path. */
#ifndef GRAPHICS_PREPARED_STATE_OWNER_H
#define GRAPHICS_PREPARED_STATE_OWNER_H
#if (defined(_WIN32) || defined(PILOT_DXVK_NATIVE)) && defined(COBJMACROS)
#include <d3d11.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { PREPARED_STAGES=3, PREPARED_CLASSES=256, PREPARED_DRAW_SRVS=5 };
/* Stage order is VS, GS, PS, matching the current epoch consumer. */
struct PreparedState {
    ID3D11InputLayout *layout;
    ID3D11Buffer *vertices[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT],*index;
    UINT strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT],offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT index_offset; DXGI_FORMAT index_format; D3D11_PRIMITIVE_TOPOLOGY topology;
    ID3D11VertexShader *vertex; ID3D11GeometryShader *geometry; ID3D11PixelShader *pixel;
    ID3D11ClassInstance *classes[PREPARED_STAGES][PREPARED_CLASSES];
    UINT class_count[PREPARED_STAGES];
    ID3D11Buffer *constants[PREPARED_STAGES][D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    ID3D11ShaderResourceView *resources[PREPARED_STAGES][D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ID3D11SamplerState *samplers[PREPARED_STAGES][D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
    ID3D11RasterizerState *raster; ID3D11BlendState *blend; ID3D11DepthStencilState *depth;
    FLOAT factors[4]; UINT sample_mask,stencil;
    ID3D11Predicate *predicate; BOOL predicate_value;
    ID3D11RenderTargetView *outputs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    ID3D11DepthStencilView *target_depth;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT viewport_count,scissor_count;
    /* Highest live binding plus one; holes and the complete D3D slot domain
     * remain valid. These bounds are owned alongside the retained pointers. */
    UINT vertex_count,constant_count[PREPARED_STAGES];
    UINT resource_count[PREPARED_STAGES],sampler_count[PREPARED_STAGES];
};
/* Resource pointer is BORROWED from the accompanying retained view. Metadata
 * is immutable. simple_whole admits only one-mip/one-slice 2D views, no readonly
 * DSV flags. Exact descriptors remain available for existing admission gates. */
struct PreparedViewFacts {
    ID3D11Resource *resource; D3D11_RESOURCE_DIMENSION dimension;
    D3D11_TEXTURE2D_DESC texture;
    unsigned kind,simple_whole; /* kind: SRV=1 RTV=2 DSV=3 UAV=4 */
    union { D3D11_SHADER_RESOURCE_VIEW_DESC srv; D3D11_RENDER_TARGET_VIEW_DESC rtv;
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv; D3D11_UNORDERED_ACCESS_VIEW_DESC uav; } desc;
};
struct PreparedDrawSubset {
    ID3D11RenderTargetView *outputs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    ID3D11DepthStencilView *depth;
    ID3D11ShaderResourceView *inputs[PREPARED_DRAW_SRVS];
    ID3D11BlendState *blend; ID3D11DepthStencilState *depth_state;
    D3D11_BLEND_DESC blend_desc; D3D11_DEPTH_STENCIL_DESC depth_desc;
    FLOAT factors[4]; UINT sample_mask,stencil;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT viewport_count,scissor_count;
    unsigned unsupported;
};
/* One registered immediate context, under the existing graphics lock. Attach
 * with default_state=1 ONLY immediately after device creation/ClearState.
 * Otherwise first snapshot resynchronizes once. Unwrapped/external mutations
 * MUST invalidate before use. All snapshots own references; zero-initialize
 * destinations and release before reuse. No setter/repair resyncs an unknown
 * mirror; only a consumer snapshot may perform the boundary resync. */
int prepared_state_attach(ID3D11DeviceContext *,int default_state);
void prepared_state_invalidate(ID3D11DeviceContext *);
void prepared_state_detach(ID3D11DeviceContext *);
int prepared_state_snapshot(ID3D11DeviceContext *,struct PreparedState *);
void prepared_state_release(struct PreparedState *);
/* Raw unknown-boundary capture derives bounds from complete slot arrays. */
void prepared_state_derive_extents(struct PreparedState *);
int prepared_state_draw_subset(ID3D11DeviceContext *,struct PreparedDrawSubset *);
void prepared_state_draw_release(struct PreparedDrawSubset *);
int prepared_state_view_facts(ID3D11View *,unsigned kind,struct PreparedViewFacts *);
void prepared_state_forget_view(ID3D11View *);
/* Optional REAL owner-destruction hook, NOT a replacement for every borrowed
 * View_Release. Ordinary releases must stay ordinary to retain hot facts. */
ULONG prepared_state_view_release(ID3D11View *);
int prepared_state_rebind(ID3D11DeviceContext *,UINT required_bind_flags,
    ID3D11Buffer *(*resolve_current)(ID3D11Buffer *));

/* Setter declarations and C macro replacements follow. Raw vtable forwarding
 * in the implementation prevents recursion. Setters on other contexts remain
 * ordinary D3D calls and never update this authority. */
void prepared_IASetInputLayout(ID3D11DeviceContext *,ID3D11InputLayout *);
void prepared_IASetVertexBuffers(ID3D11DeviceContext *,UINT,UINT,ID3D11Buffer *const *,const UINT *,const UINT *);
void prepared_IASetIndexBuffer(ID3D11DeviceContext *,ID3D11Buffer *,DXGI_FORMAT,UINT);
void prepared_IASetPrimitiveTopology(ID3D11DeviceContext *,D3D11_PRIMITIVE_TOPOLOGY);
void prepared_OMSetRenderTargets(ID3D11DeviceContext *,UINT,ID3D11RenderTargetView *const *,ID3D11DepthStencilView *);
void prepared_OMSetRenderTargetsAndUnorderedAccessViews(ID3D11DeviceContext *,UINT,ID3D11RenderTargetView *const *,ID3D11DepthStencilView *,UINT,UINT,ID3D11UnorderedAccessView *const *,const UINT *);
void prepared_OMSetBlendState(ID3D11DeviceContext *,ID3D11BlendState *,const FLOAT *,UINT);
void prepared_OMSetDepthStencilState(ID3D11DeviceContext *,ID3D11DepthStencilState *,UINT);
void prepared_RSSetState(ID3D11DeviceContext *,ID3D11RasterizerState *);
void prepared_RSSetViewports(ID3D11DeviceContext *,UINT,const D3D11_VIEWPORT *);
void prepared_RSSetScissorRects(ID3D11DeviceContext *,UINT,const D3D11_RECT *);
void prepared_SOSetTargets(ID3D11DeviceContext *,UINT,ID3D11Buffer *const *,const UINT *);
void prepared_SetPredication(ID3D11DeviceContext *,ID3D11Predicate *,BOOL);
void prepared_CSSetUnorderedAccessViews(ID3D11DeviceContext *,UINT,UINT,ID3D11UnorderedAccessView *const *,const UINT *);
void prepared_ClearState(ID3D11DeviceContext *);
void prepared_ExecuteCommandList(ID3D11DeviceContext *,ID3D11CommandList *,BOOL);
#define PREPARED_STAGE_API(stage,shader) \
 void prepared_##stage##SetShader(ID3D11DeviceContext *,shader *,ID3D11ClassInstance *const *,UINT); \
 void prepared_##stage##SetConstantBuffers(ID3D11DeviceContext *,UINT,UINT,ID3D11Buffer *const *); \
 void prepared_##stage##SetShaderResources(ID3D11DeviceContext *,UINT,UINT,ID3D11ShaderResourceView *const *); \
 void prepared_##stage##SetSamplers(ID3D11DeviceContext *,UINT,UINT,ID3D11SamplerState *const *);
PREPARED_STAGE_API(VS,ID3D11VertexShader)
PREPARED_STAGE_API(GS,ID3D11GeometryShader)
PREPARED_STAGE_API(PS,ID3D11PixelShader)
PREPARED_STAGE_API(HS,ID3D11HullShader)
PREPARED_STAGE_API(DS,ID3D11DomainShader)
PREPARED_STAGE_API(CS,ID3D11ComputeShader)
#undef PREPARED_STAGE_API
#ifdef __cplusplus
}
#endif
/* Macro overrides are appended below; implementation may define
 * PREPARED_STATE_NO_MACROS to access original SDK C helpers. */
#ifndef PREPARED_STATE_NO_MACROS
#undef ID3D11DeviceContext_IASetInputLayout
#define ID3D11DeviceContext_IASetInputLayout prepared_IASetInputLayout
#undef ID3D11DeviceContext_IASetVertexBuffers
#define ID3D11DeviceContext_IASetVertexBuffers prepared_IASetVertexBuffers
#undef ID3D11DeviceContext_IASetIndexBuffer
#define ID3D11DeviceContext_IASetIndexBuffer prepared_IASetIndexBuffer
#undef ID3D11DeviceContext_IASetPrimitiveTopology
#define ID3D11DeviceContext_IASetPrimitiveTopology prepared_IASetPrimitiveTopology
#undef ID3D11DeviceContext_OMSetRenderTargets
#define ID3D11DeviceContext_OMSetRenderTargets prepared_OMSetRenderTargets
#undef ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews
#define ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews prepared_OMSetRenderTargetsAndUnorderedAccessViews
#undef ID3D11DeviceContext_OMSetBlendState
#define ID3D11DeviceContext_OMSetBlendState prepared_OMSetBlendState
#undef ID3D11DeviceContext_OMSetDepthStencilState
#define ID3D11DeviceContext_OMSetDepthStencilState prepared_OMSetDepthStencilState
#undef ID3D11DeviceContext_RSSetState
#define ID3D11DeviceContext_RSSetState prepared_RSSetState
#undef ID3D11DeviceContext_RSSetViewports
#define ID3D11DeviceContext_RSSetViewports prepared_RSSetViewports
#undef ID3D11DeviceContext_RSSetScissorRects
#define ID3D11DeviceContext_RSSetScissorRects prepared_RSSetScissorRects
#undef ID3D11DeviceContext_SOSetTargets
#define ID3D11DeviceContext_SOSetTargets prepared_SOSetTargets
#undef ID3D11DeviceContext_SetPredication
#define ID3D11DeviceContext_SetPredication prepared_SetPredication
#undef ID3D11DeviceContext_CSSetUnorderedAccessViews
#define ID3D11DeviceContext_CSSetUnorderedAccessViews prepared_CSSetUnorderedAccessViews
#undef ID3D11DeviceContext_ClearState
#define ID3D11DeviceContext_ClearState prepared_ClearState
#undef ID3D11DeviceContext_ExecuteCommandList
#define ID3D11DeviceContext_ExecuteCommandList prepared_ExecuteCommandList
#undef ID3D11DeviceContext_VSSetShader
#define ID3D11DeviceContext_VSSetShader prepared_VSSetShader
#undef ID3D11DeviceContext_VSSetConstantBuffers
#define ID3D11DeviceContext_VSSetConstantBuffers prepared_VSSetConstantBuffers
#undef ID3D11DeviceContext_VSSetShaderResources
#define ID3D11DeviceContext_VSSetShaderResources prepared_VSSetShaderResources
#undef ID3D11DeviceContext_VSSetSamplers
#define ID3D11DeviceContext_VSSetSamplers prepared_VSSetSamplers
#undef ID3D11DeviceContext_GSSetShader
#define ID3D11DeviceContext_GSSetShader prepared_GSSetShader
#undef ID3D11DeviceContext_GSSetConstantBuffers
#define ID3D11DeviceContext_GSSetConstantBuffers prepared_GSSetConstantBuffers
#undef ID3D11DeviceContext_GSSetShaderResources
#define ID3D11DeviceContext_GSSetShaderResources prepared_GSSetShaderResources
#undef ID3D11DeviceContext_GSSetSamplers
#define ID3D11DeviceContext_GSSetSamplers prepared_GSSetSamplers
#undef ID3D11DeviceContext_PSSetShader
#define ID3D11DeviceContext_PSSetShader prepared_PSSetShader
#undef ID3D11DeviceContext_PSSetConstantBuffers
#define ID3D11DeviceContext_PSSetConstantBuffers prepared_PSSetConstantBuffers
#undef ID3D11DeviceContext_PSSetShaderResources
#define ID3D11DeviceContext_PSSetShaderResources prepared_PSSetShaderResources
#undef ID3D11DeviceContext_PSSetSamplers
#define ID3D11DeviceContext_PSSetSamplers prepared_PSSetSamplers
#undef ID3D11DeviceContext_HSSetShader
#define ID3D11DeviceContext_HSSetShader prepared_HSSetShader
#undef ID3D11DeviceContext_HSSetConstantBuffers
#define ID3D11DeviceContext_HSSetConstantBuffers prepared_HSSetConstantBuffers
#undef ID3D11DeviceContext_HSSetShaderResources
#define ID3D11DeviceContext_HSSetShaderResources prepared_HSSetShaderResources
#undef ID3D11DeviceContext_HSSetSamplers
#define ID3D11DeviceContext_HSSetSamplers prepared_HSSetSamplers
#undef ID3D11DeviceContext_DSSetShader
#define ID3D11DeviceContext_DSSetShader prepared_DSSetShader
#undef ID3D11DeviceContext_DSSetConstantBuffers
#define ID3D11DeviceContext_DSSetConstantBuffers prepared_DSSetConstantBuffers
#undef ID3D11DeviceContext_DSSetShaderResources
#define ID3D11DeviceContext_DSSetShaderResources prepared_DSSetShaderResources
#undef ID3D11DeviceContext_DSSetSamplers
#define ID3D11DeviceContext_DSSetSamplers prepared_DSSetSamplers
#undef ID3D11DeviceContext_CSSetShader
#define ID3D11DeviceContext_CSSetShader prepared_CSSetShader
#undef ID3D11DeviceContext_CSSetConstantBuffers
#define ID3D11DeviceContext_CSSetConstantBuffers prepared_CSSetConstantBuffers
#undef ID3D11DeviceContext_CSSetShaderResources
#define ID3D11DeviceContext_CSSetShaderResources prepared_CSSetShaderResources
#undef ID3D11DeviceContext_CSSetSamplers
#define ID3D11DeviceContext_CSSetSamplers prepared_CSSetSamplers
#endif
#endif /* Windows C interface */
#endif
