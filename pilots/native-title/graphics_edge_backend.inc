/* Native coverage approximation for Xbox line/polygon smoothing. NV2A's
 * exact alpha coverage is a separate precision question. For the currently
 * admitted triangle draws, use real 4-sample coverage and resolve into the
 * original color image. Existing destination pixels are replicated before
 * drawing, so blending, color masks and uncovered pixels remain meaningful.
 * This is guest rendering, before the guest's optional Gaussian resolve;
 * it is independent of the elective presentation AA mod. */
static unsigned pilot_edge_requested;
#ifdef PILOT_D3D_PHOBOS
static struct {
    ID3D11Texture2D *copy,*samples;
    ID3D11ShaderResourceView *source;
    ID3D11RenderTargetView *target;
    ID3D11VertexShader *vertex;
    ID3D11PixelShader *pixel;
    ID3D11DepthStencilState *depth;
    ID3D11RasterizerState *fill;
    unsigned width,height,observed;DXGI_FORMAT format;
} pilot_edge;
struct PilotEdgeDraw {
    ID3D11RenderTargetView *target;ID3D11Texture2D *image;
    ID3D11RasterizerState *previous,*coverage;
};
static void pilot_edge_images_close(void)
{
    if(pilot_edge.target)ID3D11RenderTargetView_Release(pilot_edge.target);
    if(pilot_edge.source)ID3D11ShaderResourceView_Release(pilot_edge.source);
    if(pilot_edge.samples)ID3D11Texture2D_Release(pilot_edge.samples);
    if(pilot_edge.copy)ID3D11Texture2D_Release(pilot_edge.copy);
    pilot_edge.target=NULL;pilot_edge.source=NULL;pilot_edge.samples=pilot_edge.copy=NULL;
    pilot_edge.width=pilot_edge.height=0;
}
static void pilot_edge_close(void)
{
    pilot_edge_images_close();
    if(pilot_edge.vertex)ID3D11VertexShader_Release(pilot_edge.vertex);
    if(pilot_edge.pixel)ID3D11PixelShader_Release(pilot_edge.pixel);
    if(pilot_edge.depth)ID3D11DepthStencilState_Release(pilot_edge.depth);
    if(pilot_edge.fill)ID3D11RasterizerState_Release(pilot_edge.fill);
    memset(&pilot_edge,0,sizeof(pilot_edge));pilot_edge_requested=0;
}
static HRESULT pilot_edge_prepare(const D3D11_TEXTURE2D_DESC *shape)
{
    ID3D11Device *device=d3d8_GetD3D11Device();HRESULT hr;
    if(!pilot_edge.vertex || !pilot_edge.pixel || !pilot_edge.depth || !pilot_edge.fill) {
        unsigned requested=pilot_edge_requested;pilot_edge_close();pilot_edge_requested=requested;
        static const char source[]=
            "Texture2D<float4> image:register(t9);\n"
            "float4 vertex(uint id:SV_VertexID):SV_Position {float2 q=float2((id<<1)&2,id&2);return float4(q*float2(2,-2)+float2(-1,1),0,1);}\n"
            "float4 pixel(float4 p:SV_Position):SV_Target{return image.Load(int3(uint2(p.xy),0));}\n";
        ID3DBlob *code=NULL,*errors=NULL;
        hr=D3DCompile(source,sizeof(source)-1,"native-edge-copy",NULL,NULL,"vertex","vs_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(errors)ID3D10Blob_Release(errors);if(FAILED(hr))return hr;
        hr=ID3D11Device_CreateVertexShader(device,ID3D10Blob_GetBufferPointer(code),ID3D10Blob_GetBufferSize(code),NULL,&pilot_edge.vertex);
        ID3D10Blob_Release(code);if(FAILED(hr))return hr;
        code=NULL;errors=NULL;
        hr=D3DCompile(source,sizeof(source)-1,"native-edge-copy",NULL,NULL,"pixel","ps_4_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(errors)ID3D10Blob_Release(errors);if(FAILED(hr))return hr;
        hr=ID3D11Device_CreatePixelShader(device,ID3D10Blob_GetBufferPointer(code),ID3D10Blob_GetBufferSize(code),NULL,&pilot_edge.pixel);
        ID3D10Blob_Release(code);if(FAILED(hr))return hr;
        D3D11_DEPTH_STENCIL_DESC depth={0};depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
        hr=ID3D11Device_CreateDepthStencilState(device,&depth,&pilot_edge.depth);if(FAILED(hr))return hr;
        D3D11_RASTERIZER_DESC raster={0};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;
        raster.DepthClipEnable=TRUE;raster.MultisampleEnable=TRUE;
        hr=ID3D11Device_CreateRasterizerState(device,&raster,&pilot_edge.fill);if(FAILED(hr))return hr;
    }
    if(pilot_edge.width==shape->Width && pilot_edge.height==shape->Height && pilot_edge.format==shape->Format)return S_OK;
    UINT quality=0;hr=ID3D11Device_CheckMultisampleQualityLevels(device,shape->Format,4,&quality);
    if(FAILED(hr) || !quality)return E_NOTIMPL;
    pilot_edge_images_close();D3D11_TEXTURE2D_DESC desc=*shape;
    desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=desc.MiscFlags=0;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    hr=ID3D11Device_CreateTexture2D(device,&desc,NULL,&pilot_edge.copy);if(FAILED(hr))goto failed;
    hr=ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)pilot_edge.copy,NULL,&pilot_edge.source);if(FAILED(hr))goto failed;
    desc.SampleDesc.Count=4;desc.SampleDesc.Quality=0;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    hr=ID3D11Device_CreateTexture2D(device,&desc,NULL,&pilot_edge.samples);if(FAILED(hr))goto failed;
    hr=ID3D11Device_CreateRenderTargetView(device,(ID3D11Resource*)pilot_edge.samples,NULL,&pilot_edge.target);if(FAILED(hr))goto failed;
    pilot_edge.width=desc.Width;pilot_edge.height=desc.Height;pilot_edge.format=desc.Format;return S_OK;
failed:pilot_edge_images_close();return hr;
}
static void pilot_edge_draw_release(struct PilotEdgeDraw *draw)
{
    if(draw->target)ID3D11RenderTargetView_Release(draw->target);
    if(draw->image)ID3D11Texture2D_Release(draw->image);
    if(draw->previous)ID3D11RasterizerState_Release(draw->previous);
    if(draw->coverage)ID3D11RasterizerState_Release(draw->coverage);
    memset(draw,0,sizeof(*draw));
}
static HRESULT pilot_edge_begin(struct PilotEdgeDraw *draw)
{
    memset(draw,0,sizeof(*draw));if(!pilot_edge_requested)return S_OK;
    /* The replication pass and extra samples must not inflate a guest
     * occlusion report. Qualify their combined counter owner separately. */
    if(pilot_visibility_current && !pilot_visibility_current->ended)return E_NOTIMPL;
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();ID3D11DepthStencilView *depth=NULL;
    ID3D11DeviceContext_OMGetRenderTargets(ctx,1,&draw->target,&depth);
    if(depth){ID3D11DepthStencilView_Release(depth);pilot_edge_draw_release(draw);return E_NOTIMPL;}
    HRESULT hr=E_NOTIMPL;if(!draw->target)goto failed;
    ID3D11Resource *resource=NULL;ID3D11RenderTargetView_GetResource(draw->target,&resource);
    hr=ID3D11Resource_QueryInterface(resource,&IID_ID3D11Texture2D,(void**)&draw->image);ID3D11Resource_Release(resource);if(FAILED(hr))goto failed;
    D3D11_TEXTURE2D_DESC shape;ID3D11Texture2D_GetDesc(draw->image,&shape);
    D3D11_RENDER_TARGET_VIEW_DESC view;ID3D11RenderTargetView_GetDesc(draw->target,&view);
    if(shape.SampleDesc.Count!=1 || shape.SampleDesc.Quality || shape.ArraySize!=1 || shape.MipLevels!=1 ||
       (shape.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && shape.Format!=DXGI_FORMAT_B8G8R8A8_UNORM) ||
       view.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D || view.Texture2D.MipSlice){hr=E_NOTIMPL;goto failed;}
    hr=pilot_edge_prepare(&shape);if(FAILED(hr))goto failed;
    D3D11_RASTERIZER_DESC raster={0};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_BACK;raster.DepthClipEnable=TRUE;
    ID3D11DeviceContext_RSGetState(ctx,&draw->previous);
    if(draw->previous)ID3D11RasterizerState_GetDesc(draw->previous,&raster);
    raster.MultisampleEnable=TRUE;
    hr=ID3D11Device_CreateRasterizerState(d3d8_GetD3D11Device(),&raster,&draw->coverage);if(FAILED(hr))goto failed;
    struct PilotVisualState saved;pilot_visual_state_save(&saved);
    ID3D11DeviceContext_OMSetRenderTargets(ctx,0,NULL,NULL);
    ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource*)pilot_edge.copy,(ID3D11Resource*)draw->image);
    ID3D11DeviceContext_OMSetRenderTargets(ctx,1,&pilot_edge.target,NULL);
    D3D11_VIEWPORT viewport={0,0,(float)shape.Width,(float)shape.Height,0,1};ID3D11DeviceContext_RSSetViewports(ctx,1,&viewport);
    ID3D11DeviceContext_RSSetState(ctx,pilot_edge.fill);ID3D11DeviceContext_OMSetDepthStencilState(ctx,pilot_edge.depth,0);
    ID3D11DeviceContext_OMSetBlendState(ctx,NULL,NULL,UINT32_MAX);
    ID3D11DeviceContext_VSSetShader(ctx,pilot_edge.vertex,NULL,0);ID3D11DeviceContext_PSSetShader(ctx,pilot_edge.pixel,NULL,0);
    ID3D11DeviceContext_IASetInputLayout(ctx,NULL);ID3D11DeviceContext_IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_PSSetShaderResources(ctx,9,1,&pilot_edge.source);ID3D11DeviceContext_Draw(ctx,3,0);
    pilot_visual_state_restore(&saved);
    ID3D11DeviceContext_OMSetRenderTargets(ctx,1,&pilot_edge.target,NULL);ID3D11DeviceContext_RSSetState(ctx,draw->coverage);
    return S_OK;
failed:pilot_edge_draw_release(draw);return hr;
}
static void pilot_edge_end(struct PilotEdgeDraw *draw)
{
    if(!draw->target)return;
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();ID3D11DeviceContext_OMSetRenderTargets(ctx,0,NULL,NULL);
    ID3D11DeviceContext_ResolveSubresource(ctx,(ID3D11Resource*)draw->image,0,(ID3D11Resource*)pilot_edge.samples,0,pilot_edge.format);
    ID3D11DeviceContext_OMSetRenderTargets(ctx,1,&draw->target,NULL);ID3D11DeviceContext_RSSetState(ctx,draw->previous);
    if(!pilot_edge.observed) {
        pilot_edge.observed=1;fprintf(stderr,"[EDGE COVERAGE] native=4x target=%ux%u format=%u\n",pilot_edge.width,pilot_edge.height,(unsigned)pilot_edge.format);
    }
    pilot_edge_draw_release(draw);
}
#else
struct PilotEdgeDraw {unsigned unused;};
static void pilot_edge_close(void){pilot_edge_requested=0;}
static HRESULT pilot_edge_begin(struct PilotEdgeDraw *draw){(void)draw;return pilot_edge_requested?E_NOTIMPL:S_OK;}
static void pilot_edge_end(struct PilotEdgeDraw *draw){(void)draw;}
#endif
int pilot_gfx_edge_antialias(unsigned enabled)
{
    if(!pilot_device || enabled>1)return 0;
#ifndef PILOT_D3D_PHOBOS
    if(enabled)return 0;
#endif
    pilot_edge_requested=enabled;return 1;
}
