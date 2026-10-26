/* The built-in depth surface is guest-sized storage, just like an explicit
 * depth surface. Small effect targets can borrow it, then resume the enhanced
 * main scene. Keep both host rasters and migrate their full logical contents
 * on the GPU. No CPU readback, per-bind allocation, depth disable, or clear.
 * D3D11 baseline has no pixel-shader stencil export: copy depth with ZERO
 * stencil, then eight masked stencil passes. This needs no optional feature.
 * Calls share the graphics lock and run after flushing preceding guest draws. */
#ifdef PILOT_D3D_PHOBOS
static struct {
    RhiImage *image;RhiView *view;
    ID3D11Texture2D *source;
    ID3D11ShaderResourceView *base[2],*sample[2];
    ID3D11VertexShader *vertex;ID3D11PixelShader *depth,*stencil;
    ID3D11Buffer *constants;ID3D11RasterizerState *raster;
    ID3D11DepthStencilState *states[9];
    unsigned width,height,active;
} pilot_default_grid;
static const char pilot_default_grid_shader[]=
    "cbuffer Grid:register(b7){uint2 source_size;uint2 target_size;uint bit;};\n"
    "Texture2D<float4> z:register(t9);Texture2D<uint4> s:register(t10);\n"
    "float4 vertex(uint id:SV_VertexID):SV_Position {uint2 q=uint2((id<<1)&2,id&2);return float4(float2(q)*float2(2,-2)+float2(-1,1),0,1);}\n"
    "uint2 texel(float4 p){return min(((uint2(p.xy)*2+1)*source_size)/(target_size*2),source_size-1);}\n"
    "float depth(float4 p:SV_Position):SV_Depth{return z.Load(int3(texel(p),0)).r;}\n"
    "void stencil(float4 p:SV_Position){if(!(s.Load(int3(texel(p),0)).g&bit))discard;}\n";
static void pilot_default_grid_views(ID3D11Texture2D *image,ID3D11ShaderResourceView *views[2])
{
    D3D11_SHADER_RESOURCE_VIEW_DESC d={0};d.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;
    d.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    pilot_d3d_require(ID3D11Device_CreateShaderResourceView(d3d8_GetD3D11Device(),(ID3D11Resource*)image,&d,&views[0]),__FILE__,__LINE__);
    d.Format=DXGI_FORMAT_X24_TYPELESS_G8_UINT;
    pilot_d3d_require(ID3D11Device_CreateShaderResourceView(d3d8_GetD3D11Device(),(ID3D11Resource*)image,&d,&views[1]),__FILE__,__LINE__);
}
static void pilot_default_grid_prepare(void)
{
    if(pilot_default_grid.vertex)return;
    ID3D11Device *device=d3d8_GetD3D11Device();
    const char *entries[]={"vertex","depth","stencil"};
    for(unsigned i=0;i<3;++i) {
        ID3DBlob *code=NULL,*error=NULL;
        HRESULT hr=D3DCompile(pilot_default_grid_shader,sizeof(pilot_default_grid_shader)-1,"depth-raster-copy",NULL,NULL,entries[i],i?"ps_4_0":"vs_4_0",
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(error){fprintf(stderr,"[DEPTH GRID SHADER] %s\n",(const char*)ID3D10Blob_GetBufferPointer(error));ID3D10Blob_Release(error);}
        pilot_d3d_require(hr,__FILE__,__LINE__);
        if(!i)hr=ID3D11Device_CreateVertexShader(device,ID3D10Blob_GetBufferPointer(code),ID3D10Blob_GetBufferSize(code),NULL,&pilot_default_grid.vertex);
        else hr=ID3D11Device_CreatePixelShader(device,ID3D10Blob_GetBufferPointer(code),ID3D10Blob_GetBufferSize(code),NULL,i==1?&pilot_default_grid.depth:&pilot_default_grid.stencil);
        ID3D10Blob_Release(code);pilot_d3d_require(hr,__FILE__,__LINE__);
    }
    D3D11_BUFFER_DESC buffer={0};buffer.ByteWidth=32;buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    pilot_d3d_require(ID3D11Device_CreateBuffer(device,&buffer,NULL,&pilot_default_grid.constants),__FILE__,__LINE__);
    D3D11_RASTERIZER_DESC raster={0};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    pilot_d3d_require(ID3D11Device_CreateRasterizerState(device,&raster,&pilot_default_grid.raster),__FILE__,__LINE__);
    for(unsigned i=0;i<9;++i) {
        D3D11_DEPTH_STENCIL_DESC d={0};d.DepthEnable=!i;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;d.DepthFunc=D3D11_COMPARISON_ALWAYS;
        d.StencilEnable=TRUE;d.StencilReadMask=255;d.StencilWriteMask=i?(1u<<(i-1)):255;
        d.FrontFace.StencilFunc=D3D11_COMPARISON_ALWAYS;d.FrontFace.StencilFailOp=d.FrontFace.StencilDepthFailOp=D3D11_STENCIL_OP_KEEP;
        d.FrontFace.StencilPassOp=i?D3D11_STENCIL_OP_REPLACE:D3D11_STENCIL_OP_ZERO;d.BackFace=d.FrontFace;
        pilot_d3d_require(ID3D11Device_CreateDepthStencilState(device,&d,&pilot_default_grid.states[i]),__FILE__,__LINE__);
    }
}
static void pilot_default_depth_close(void)
{
    for(unsigned i=0;i<2;++i) {
        if(pilot_default_grid.base[i])ID3D11ShaderResourceView_Release(pilot_default_grid.base[i]);
        if(pilot_default_grid.sample[i])ID3D11ShaderResourceView_Release(pilot_default_grid.sample[i]);
    }
    if(pilot_default_grid.source)ID3D11Texture2D_Release(pilot_default_grid.source);
    rhi_view_destroy(pilot_default_grid.view);rhi_image_destroy(pilot_default_grid.image);
    if(pilot_default_grid.vertex)ID3D11VertexShader_Release(pilot_default_grid.vertex);
    if(pilot_default_grid.depth)ID3D11PixelShader_Release(pilot_default_grid.depth);
    if(pilot_default_grid.stencil)ID3D11PixelShader_Release(pilot_default_grid.stencil);
    if(pilot_default_grid.constants)ID3D11Buffer_Release(pilot_default_grid.constants);
    if(pilot_default_grid.raster)ID3D11RasterizerState_Release(pilot_default_grid.raster);
    for(unsigned i=0;i<9;++i)if(pilot_default_grid.states[i])ID3D11DepthStencilState_Release(pilot_default_grid.states[i]);
    memset(&pilot_default_grid,0,sizeof(pilot_default_grid));
}
static RhiImage *pilot_default_depth_image(void)
{return pilot_default_grid.active?pilot_default_grid.image:pilot_donor_default_depth();}
static ID3D11Texture2D *pilot_default_depth_storage(void)
{return (ID3D11Texture2D*)rhi_d3d11_native_image(pilot_default_depth_image());}
static HRESULT pilot_default_depth_match_color(ID3D11Texture2D *color,unsigned width,unsigned height,ID3D11DepthStencilView **view)
{
    ID3D11Texture2D *original=(ID3D11Texture2D*)rhi_d3d11_native_image(pilot_donor_default_depth());
    D3D11_TEXTURE2D_DESC c,d;ID3D11Texture2D_GetDesc(color,&c);ID3D11Texture2D_GetDesc(original,&d);
    if(d.MipLevels!=1 || d.ArraySize!=1 || d.SampleDesc.Count!=1 ||
       (d.Format!=DXGI_FORMAT_D24_UNORM_S8_UINT && d.Format!=DXGI_FORMAT_R24G8_TYPELESS))return E_NOTIMPL;
    uint64_t w=((uint64_t)c.Width*pilot_gfx_width()+width/2)/width;
    uint64_t h=((uint64_t)c.Height*pilot_gfx_height()+height/2)/height;
    if(!w || !h || w>16384 || h>16384)return E_INVALIDARG;
    unsigned active=w!=d.Width || h!=d.Height;
    if(active && pilot_default_grid.image && (w!=pilot_default_grid.width || h!=pilot_default_grid.height)) {
        /* Retain contents through the base raster before replacing the cache. */
        ID3D11DepthStencilView *unused;
        HRESULT hr=pilot_default_depth_match_color(pilot_surfaces[0],pilot_gfx_width(),pilot_gfx_height(),&unused);
        if(FAILED(hr))return hr;
        ID3D11DeviceContext_OMSetRenderTargets(d3d8_GetD3D11Context(),0,NULL,NULL);
        pilot_donor_output_images(NULL,NULL,1);
        for(unsigned i=0;i<2;++i){ID3D11ShaderResourceView_Release(pilot_default_grid.sample[i]);pilot_default_grid.sample[i]=NULL;}
        rhi_view_destroy(pilot_default_grid.view);rhi_image_destroy(pilot_default_grid.image);pilot_default_grid.view=NULL;pilot_default_grid.image=NULL;
    }
    if(active && !pilot_default_grid.image) {
        RhiImageDesc next={0};next.type=RHI_IMAGE_2D;next.width=(unsigned)w;next.height=(unsigned)h;next.depth=next.mip_levels=next.samples=1;
        next.format=RHI_FORMAT_D24_UNORM_S8_UINT;next.usage=RHI_USAGE_DEFAULT;next.bind=RHI_BIND_DEPTH|RHI_BIND_SAMPLED;
        pilot_default_grid.image=rhi_image_create(&next,NULL);
        if(!pilot_default_grid.image)return E_OUTOFMEMORY;
        pilot_default_grid.view=rhi_view_create(pilot_default_grid.image,RHI_VIEW_DEPTH,NULL);
        if(!pilot_default_grid.view)pilot_d3d_backend_stop("default depth raster view allocation failed");
        pilot_default_grid_views((ID3D11Texture2D*)rhi_d3d11_native_image(pilot_default_grid.image),pilot_default_grid.sample);
        pilot_default_grid.width=(unsigned)w;pilot_default_grid.height=(unsigned)h;
    }
    if(active!=pilot_default_grid.active) {
        pilot_default_grid_prepare();
        if(!pilot_default_grid.source) {
            d.Format=DXGI_FORMAT_R24G8_TYPELESS;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            pilot_d3d_require(ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&d,NULL,&pilot_default_grid.source),__FILE__,__LINE__);
            pilot_default_grid_views(pilot_default_grid.source,pilot_default_grid.base);
        }
        ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();struct PilotVisualState saved;pilot_visual_state_save(&saved);
        ID3D11DeviceContext_OMSetRenderTargets(ctx,0,NULL,NULL);
        if(active)ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource*)pilot_default_grid.source,(ID3D11Resource*)original);
        ID3D11DepthStencilView *target=active?(ID3D11DepthStencilView*)rhi_d3d11_native_view(pilot_default_grid.view):pilot_default_depth;
        ID3D11DeviceContext_OMSetRenderTargets(ctx,0,NULL,target);
        ID3D11DeviceContext_PSSetShaderResources(ctx,9,2,active?pilot_default_grid.base:pilot_default_grid.sample);
        ID3D11DeviceContext_PSSetConstantBuffers(ctx,7,1,&pilot_default_grid.constants);
        ID3D11DeviceContext_VSSetShader(ctx,pilot_default_grid.vertex,NULL,0);ID3D11DeviceContext_IASetInputLayout(ctx,NULL);
        ID3D11DeviceContext_IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_RSSetState(ctx,pilot_default_grid.raster);ID3D11DeviceContext_OMSetBlendState(ctx,NULL,NULL,UINT32_MAX);
        D3D11_VIEWPORT viewport={0,0,(FLOAT)w,(FLOAT)h,0,1};ID3D11DeviceContext_RSSetViewports(ctx,1,&viewport);
        uint32_t constants[8]={active?d.Width:pilot_default_grid.width,active?d.Height:pilot_default_grid.height,(unsigned)w,(unsigned)h,0,0,0,0};
        for(unsigned i=0;i<9;++i) {
            constants[4]=i?1u<<(i-1):0;
            ID3D11DeviceContext_UpdateSubresource(ctx,(ID3D11Resource*)pilot_default_grid.constants,0,NULL,constants,0,0);
            ID3D11DeviceContext_PSSetShader(ctx,i?pilot_default_grid.stencil:pilot_default_grid.depth,NULL,0);
            ID3D11DeviceContext_OMSetDepthStencilState(ctx,pilot_default_grid.states[i],255);ID3D11DeviceContext_Draw(ctx,3,0);
        }
        pilot_visual_state_restore(&saved);
        pilot_default_grid.active=active;
        pilot_surface_use[2]->serial=pilot_surface_use[2]->write_serial=pilot_mark_work();
    }
    *view=active?(ID3D11DepthStencilView*)rhi_d3d11_native_view(pilot_default_grid.view):pilot_default_depth;
    return S_OK;
}
#else
static void pilot_default_depth_close(void){}
static ID3D11Texture2D *pilot_default_depth_storage(void){return pilot_surfaces[2];}
static HRESULT pilot_default_depth_match_color(ID3D11Texture2D *color,unsigned width,unsigned height,ID3D11DepthStencilView **view)
{(void)color;(void)width;(void)height;*view=pilot_default_depth;return S_OK;}
#endif
