/* Presentation passes must leave the immediate context as the guest left it.
 * RhiOutputState deliberately saves outputs only. Save exactly the additional
 * slots touched by resolve/AA/interpolation; no GPU readback or state replay. */
struct PilotVisualState {
    RhiOutputState output;
    ID3D11VertexShader *vertex;ID3D11PixelShader *pixel;ID3D11GeometryShader *geometry;
    ID3D11InputLayout *layout;D3D11_PRIMITIVE_TOPOLOGY topology;
    ID3D11Buffer *constants;ID3D11ShaderResourceView *images[2];ID3D11SamplerState *sampler;
    ID3D11BlendState *blend;FLOAT factor[4];UINT mask;
    ID3D11DepthStencilState *depth;UINT stencil;ID3D11RasterizerState *raster;
};
static void pilot_visual_state_save(struct PilotVisualState *s)
{
    ID3D11DeviceContext *c=d3d8_GetD3D11Context();memset(s,0,sizeof(*s));rhi_output_save(&s->output);
    ID3D11DeviceContext_VSGetShader(c,&s->vertex,NULL,NULL);ID3D11DeviceContext_PSGetShader(c,&s->pixel,NULL,NULL);
    ID3D11DeviceContext_GSGetShader(c,&s->geometry,NULL,NULL);ID3D11DeviceContext_GSSetShader(c,NULL,NULL,0);
    ID3D11DeviceContext_IAGetInputLayout(c,&s->layout);ID3D11DeviceContext_IAGetPrimitiveTopology(c,&s->topology);
    ID3D11DeviceContext_PSGetConstantBuffers(c,7,1,&s->constants);ID3D11DeviceContext_PSGetShaderResources(c,9,2,s->images);
    ID3D11DeviceContext_PSGetSamplers(c,9,1,&s->sampler);ID3D11DeviceContext_OMGetBlendState(c,&s->blend,s->factor,&s->mask);
    ID3D11DeviceContext_OMGetDepthStencilState(c,&s->depth,&s->stencil);ID3D11DeviceContext_RSGetState(c,&s->raster);
}
static void pilot_visual_state_restore(struct PilotVisualState *s)
{
    ID3D11DeviceContext *c=d3d8_GetD3D11Context();ID3D11ShaderResourceView *none[2]={NULL,NULL};
    ID3D11DeviceContext_PSSetShaderResources(c,9,2,none);rhi_output_restore(&s->output);
    ID3D11DeviceContext_VSSetShader(c,s->vertex,NULL,0);ID3D11DeviceContext_PSSetShader(c,s->pixel,NULL,0);
    ID3D11DeviceContext_GSSetShader(c,s->geometry,NULL,0);ID3D11DeviceContext_IASetInputLayout(c,s->layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(c,s->topology);ID3D11DeviceContext_PSSetConstantBuffers(c,7,1,&s->constants);
    ID3D11DeviceContext_PSSetShaderResources(c,9,2,s->images);ID3D11DeviceContext_PSSetSamplers(c,9,1,&s->sampler);
    ID3D11DeviceContext_OMSetBlendState(c,s->blend,s->factor,s->mask);ID3D11DeviceContext_OMSetDepthStencilState(c,s->depth,s->stencil);
    ID3D11DeviceContext_RSSetState(c,s->raster);
#define VISUAL_RELEASE(type,field) if(s->field)type##_Release(s->field)
    VISUAL_RELEASE(ID3D11VertexShader,vertex);VISUAL_RELEASE(ID3D11PixelShader,pixel);VISUAL_RELEASE(ID3D11GeometryShader,geometry);
    VISUAL_RELEASE(ID3D11InputLayout,layout);VISUAL_RELEASE(ID3D11Buffer,constants);VISUAL_RELEASE(ID3D11SamplerState,sampler);
    VISUAL_RELEASE(ID3D11BlendState,blend);VISUAL_RELEASE(ID3D11DepthStencilState,depth);VISUAL_RELEASE(ID3D11RasterizerState,raster);
#undef VISUAL_RELEASE
    for(unsigned i=0;i<2;++i)if(s->images[i])ID3D11ShaderResourceView_Release(s->images[i]);
}
