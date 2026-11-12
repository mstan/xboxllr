/* Adapter-issued host calls only. No renderer calls are made by the query.
 * Map READ and Flush have direct elapsed and thread-cycle measurements. */
static void pilot_cost_flush(ID3D11DeviceContext *ctx)
{
    struct PilotCostSpan s=pilot_cost_wait_begin();
    ctx->lpVtbl->Flush(ctx);pilot_cost_wait_end(COST_FLUSH,s);
}
static HRESULT pilot_cost_map(ID3D11DeviceContext *ctx,ID3D11Resource *resource,
    UINT sub,D3D11_MAP type,UINT flags,D3D11_MAPPED_SUBRESOURCE *mapped)
{
    if(type!=D3D11_MAP_READ && type!=D3D11_MAP_READ_WRITE)
        return ctx->lpVtbl->Map(ctx,resource,sub,type,flags,mapped);
    pilot_cost_add(COST_READBACK,1);struct PilotCostSpan s=pilot_cost_wait_begin();
    HRESULT hr=ctx->lpVtbl->Map(ctx,resource,sub,type,flags,mapped);
    pilot_cost_wait_end(COST_MAP_READ,s);return hr;
}
#undef ID3D11DeviceContext_Flush
#define ID3D11DeviceContext_Flush(ctx) pilot_cost_flush(ctx)
#undef ID3D11DeviceContext_Map
#define ID3D11DeviceContext_Map(ctx,res,sub,type,flags,mapped) pilot_cost_map(ctx,res,sub,type,flags,mapped)
#undef ID3D11DeviceContext_Draw
#define ID3D11DeviceContext_Draw(ctx,n,first) (pilot_cost_add(COST_HOST_DRAWS,1),(ctx)->lpVtbl->Draw(ctx,n,first))
#undef ID3D11DeviceContext_DrawIndexed
#define ID3D11DeviceContext_DrawIndexed(ctx,n,first,base) (pilot_cost_add(COST_HOST_DRAWS,1),(ctx)->lpVtbl->DrawIndexed(ctx,n,first,base))
