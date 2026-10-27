/* Xbox/D3D8 Clear(NULL,0) affects the current viewport, not the whole view.
 * D3D11's native clear ignores that rectangle. Retain its fast full-view path;
 * a cached baseline-D3D11 raster pass clears partial color/depth/stencil views.
 * No readback, per-clear allocation, optional ClearView, or donor state edits.
 * https://learn.microsoft.com/windows/win32/direct3d9/viewports-and-clipping */
static struct {
    ID3D11VertexShader *vertex;ID3D11PixelShader *pixel;
    ID3D11Buffer *constants;ID3D11RasterizerState *raster;
    ID3D11BlendState *blend[2];ID3D11DepthStencilState *depth[4];
} pilot_clear;
static void pilot_clear_close(void)
{
    if(pilot_clear.vertex)ID3D11VertexShader_Release(pilot_clear.vertex);
    if(pilot_clear.pixel)ID3D11PixelShader_Release(pilot_clear.pixel);
    if(pilot_clear.constants)ID3D11Buffer_Release(pilot_clear.constants);
    if(pilot_clear.raster)ID3D11RasterizerState_Release(pilot_clear.raster);
    for(unsigned i=0;i<2;++i)if(pilot_clear.blend[i])ID3D11BlendState_Release(pilot_clear.blend[i]);
    for(unsigned i=0;i<4;++i)if(pilot_clear.depth[i])ID3D11DepthStencilState_Release(pilot_clear.depth[i]);
    memset(&pilot_clear,0,sizeof(pilot_clear));
}
static void pilot_clear_prepare(void)
{
    if(pilot_clear.vertex)return;
    static const char source[]=
        "cbuffer Clear:register(b7){float4 color;float4 value;};\n"
        "float4 vertex(uint id:SV_VertexID):SV_Position {uint2 p=uint2((id<<1)&2,id&2);return float4(float2(p)*float2(2,-2)+float2(-1,1),0,1);}\n"
        "struct Out {float4 color:SV_Target;float depth:SV_Depth;};\n"
        "Out pixel(){Out o;o.color=color;o.depth=value.x;return o;}\n";
    ID3D11Device *device=d3d8_GetD3D11Device();
    for(unsigned i=0;i<2;++i) {
        ID3DBlob *code=NULL,*error=NULL;
        HRESULT hr=D3DCompile(source,sizeof(source)-1,"viewport-clear",NULL,NULL,i?"pixel":"vertex",i?"ps_4_0":"vs_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(error){fprintf(stderr,"[VIEWPORT CLEAR SHADER] %s\n",(const char*)ID3D10Blob_GetBufferPointer(error));ID3D10Blob_Release(error);}
        pilot_d3d_require(hr,__FILE__,__LINE__);
        hr=i?ID3D11Device_CreatePixelShader(device,ID3D10Blob_GetBufferPointer(code),ID3D10Blob_GetBufferSize(code),NULL,&pilot_clear.pixel):
            ID3D11Device_CreateVertexShader(device,ID3D10Blob_GetBufferPointer(code),ID3D10Blob_GetBufferSize(code),NULL,&pilot_clear.vertex);
        ID3D10Blob_Release(code);pilot_d3d_require(hr,__FILE__,__LINE__);
    }
    D3D11_BUFFER_DESC buffer={0};buffer.ByteWidth=32;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    pilot_d3d_require(ID3D11Device_CreateBuffer(device,&buffer,NULL,&pilot_clear.constants),__FILE__,__LINE__);
    D3D11_RASTERIZER_DESC raster={0};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    pilot_d3d_require(ID3D11Device_CreateRasterizerState(device,&raster,&pilot_clear.raster),__FILE__,__LINE__);
    for(unsigned i=0;i<2;++i) {
        D3D11_BLEND_DESC blend={0};blend.RenderTarget[0].RenderTargetWriteMask=i?15:0;
        pilot_d3d_require(ID3D11Device_CreateBlendState(device,&blend,&pilot_clear.blend[i]),__FILE__,__LINE__);
    }
    for(unsigned i=0;i<4;++i) {
        D3D11_DEPTH_STENCIL_DESC depth={0};depth.DepthEnable=(i&1)!=0;depth.DepthWriteMask=(i&1)?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc=D3D11_COMPARISON_ALWAYS;depth.StencilEnable=(i&2)!=0;depth.StencilReadMask=depth.StencilWriteMask=255;
        depth.FrontFace.StencilFunc=D3D11_COMPARISON_ALWAYS;
        depth.FrontFace.StencilFailOp=depth.FrontFace.StencilDepthFailOp=depth.FrontFace.StencilPassOp=D3D11_STENCIL_OP_REPLACE;
        depth.BackFace=depth.FrontFace;
        pilot_d3d_require(ID3D11Device_CreateDepthStencilState(device,&depth,&pilot_clear.depth[i]),__FILE__,__LINE__);
    }
}
static void pilot_clear_rectangle(uint32_t flags,uint32_t color,float depth,uint32_t stencil,const D3D11_RECT *rect)
{
    pilot_clear_prepare();ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
    ID3D11VertexShader *vs=NULL;ID3D11PixelShader *ps=NULL;ID3D11GeometryShader *gs=NULL;
    ID3D11InputLayout *layout=NULL;ID3D11Buffer *constants=NULL;
    ID3D11BlendState *blend=NULL;FLOAT factor[4];UINT mask,reference;
    ID3D11DepthStencilState *ds=NULL;ID3D11RasterizerState *raster=NULL;
    D3D11_PRIMITIVE_TOPOLOGY topology;D3D11_VIEWPORT views[16];UINT count=16;
    ID3D11DeviceContext_VSGetShader(ctx,&vs,NULL,NULL);ID3D11DeviceContext_PSGetShader(ctx,&ps,NULL,NULL);ID3D11DeviceContext_GSGetShader(ctx,&gs,NULL,NULL);
    ID3D11DeviceContext_IAGetInputLayout(ctx,&layout);ID3D11DeviceContext_IAGetPrimitiveTopology(ctx,&topology);
    ID3D11DeviceContext_PSGetConstantBuffers(ctx,7,1,&constants);ID3D11DeviceContext_OMGetBlendState(ctx,&blend,factor,&mask);
    ID3D11DeviceContext_OMGetDepthStencilState(ctx,&ds,&reference);ID3D11DeviceContext_RSGetState(ctx,&raster);ID3D11DeviceContext_RSGetViewports(ctx,&count,views);
    float data[]={((color>>16)&255)/255.0f,((color>>8)&255)/255.0f,(color&255)/255.0f,(color>>24)/255.0f,depth,0,0,0};
    ID3D11DeviceContext_UpdateSubresource(ctx,(ID3D11Resource*)pilot_clear.constants,0,NULL,data,0,0);
    ID3D11DeviceContext_VSSetShader(ctx,pilot_clear.vertex,NULL,0);ID3D11DeviceContext_PSSetShader(ctx,pilot_clear.pixel,NULL,0);ID3D11DeviceContext_GSSetShader(ctx,NULL,NULL,0);
    ID3D11DeviceContext_IASetInputLayout(ctx,NULL);ID3D11DeviceContext_IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx,7,1,&pilot_clear.constants);ID3D11DeviceContext_OMSetBlendState(ctx,pilot_clear.blend[!!(flags&1)],NULL,UINT32_MAX);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx,pilot_clear.depth[(flags>>1)&3],stencil);ID3D11DeviceContext_RSSetState(ctx,pilot_clear.raster);
    D3D11_VIEWPORT viewport={(FLOAT)rect->left,(FLOAT)rect->top,(FLOAT)(rect->right-rect->left),(FLOAT)(rect->bottom-rect->top),0,1};
    ID3D11DeviceContext_RSSetViewports(ctx,1,&viewport);
    ID3D11Query *excluded=pilot_visibility_internal_begin();ID3D11DeviceContext_Draw(ctx,3,0);pilot_visibility_internal_end(excluded);
    ID3D11DeviceContext_VSSetShader(ctx,vs,NULL,0);ID3D11DeviceContext_PSSetShader(ctx,ps,NULL,0);ID3D11DeviceContext_GSSetShader(ctx,gs,NULL,0);
    ID3D11DeviceContext_IASetInputLayout(ctx,layout);ID3D11DeviceContext_IASetPrimitiveTopology(ctx,topology);ID3D11DeviceContext_PSSetConstantBuffers(ctx,7,1,&constants);
    ID3D11DeviceContext_OMSetBlendState(ctx,blend,factor,mask);ID3D11DeviceContext_OMSetDepthStencilState(ctx,ds,reference);
    ID3D11DeviceContext_RSSetState(ctx,raster);ID3D11DeviceContext_RSSetViewports(ctx,count,views);
    if(vs)ID3D11VertexShader_Release(vs);if(ps)ID3D11PixelShader_Release(ps);if(gs)ID3D11GeometryShader_Release(gs);
    if(layout)ID3D11InputLayout_Release(layout);if(constants)ID3D11Buffer_Release(constants);if(blend)ID3D11BlendState_Release(blend);
    if(ds)ID3D11DepthStencilState_Release(ds);if(raster)ID3D11RasterizerState_Release(raster);
}
static void pilot_clear_shape(ID3D11View *view,D3D11_TEXTURE2D_DESC *shape)
{
    ID3D11Resource *resource=NULL;ID3D11View_GetResource(view,&resource);
    ID3D11Texture2D_GetDesc((ID3D11Texture2D*)resource,shape);ID3D11Resource_Release(resource);
}
