/* Xbox Z24S8 bytes may subsequently be sampled as linear A8R8G8B8.
 * D3D11's depth-only SRV instead returns (D,0,0,1). Decode the unfiltered
 * depth/stencil texels into one cached RGBA32_FLOAT GPU image, then retain the
 * renderer's ordinary sampler/filter path. No CPU readback or upload occurs.
 * A content serial, distinct from last-use/completion, avoids reconversion
 * when the same source is sampled by several stages or subsequent draws.
 * FLOAT storage retains normalized compute output through float filtering.
 * An authored combiner MSB threshold regression fails for the old UNORM cache.
 * The cache uses16 bytes/texel; bounded GPU conversion/filter timings and
 * independently authored threshold controls accompany this default fix.
 *
 * D3D11.3 functional spec 3.2.3.2/5 requires nearest-rounded UNORM loads.
 * Recover D24 with integer IEEE-754 fields: for z<.5 floor(z*2^24)=D;
 * for z>=.5 it is D+1. Integer shifts avoid shader multiply precision.
 * Defaults for missing depth-SRV channels: spec 19.1.3.3.
 * https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm
 */
static ID3D11ComputeShader *pilot_depth_texture_shader;
static struct PilotDepthTextureStats pilot_depth_texture_counts;
static const char pilot_depth_texture_source[]=
    "Texture2D<float4> depth_image:register(t0);\n"
    "Texture2D<uint4> stencil_image:register(t1);\n"
    "RWTexture2D<float4> argb_image:register(u0);\n"
    "[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {\n"
    " uint w,h; argb_image.GetDimensions(w,h); if(id.x>=w || id.y>=h)return;\n"
    " uint bits=asuint(depth_image.Load(int3(id.xy,0)).r); uint e=(bits>>23)&255;\n"
    " uint m=(bits&0x7fffff)|0x800000;\n"
    " uint d=e>=127?0xffffff:(e<103?0:((m>>(126-e))-(e>=126?1:0)));\n"
    " uint s=stencil_image.Load(int3(id.xy,0)).g;\n"
    " argb_image[id.xy]=float4((d>>8)&255,d&255,s,(d>>16)&255)/255.0;\n"
    "}\n";
static void pilot_depth_texture_destroy(struct PilotNativeTexture *texture)
{
    if(texture->argb_uav)ID3D11UnorderedAccessView_Release(texture->argb_uav);
    if(texture->argb_srv)ID3D11ShaderResourceView_Release(texture->argb_srv);
    if(texture->stencil_srv)ID3D11ShaderResourceView_Release(texture->stencil_srv);
    if(texture->argb_image)ID3D11Texture2D_Release(texture->argb_image);
    texture->argb_uav=NULL;texture->argb_srv=NULL;texture->stencil_srv=NULL;texture->argb_image=NULL;
    texture->argb_initialized=0;
}
static void pilot_depth_texture_close(void)
{
    if(pilot_depth_texture_shader)ID3D11ComputeShader_Release(pilot_depth_texture_shader);
    pilot_depth_texture_shader=NULL;
    memset(&pilot_depth_texture_counts,0,sizeof(pilot_depth_texture_counts));
}
void pilot_gfx_depth_texture_stats(struct PilotDepthTextureStats *stats)
{*stats=pilot_depth_texture_counts;}
static ID3D11ShaderResourceView *pilot_depth_texture_argb(uint32_t handle,ID3D11ShaderResourceView *depth_view)
{
    struct PilotNativeTexture *texture=&pilot_textures[handle-1];
    if(texture->argb_initialized && texture->argb_serial==texture->content_serial)return texture->argb_srv;
    ID3D11Device *device=d3d8_GetD3D11Device();
    ID3D11Texture2D *storage=pilot_donor_texture((D3D8Texture*)texture->object);
    D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(storage,&desc);
    if(desc.Format!=DXGI_FORMAT_R24G8_TYPELESS || desc.MipLevels!=1 ||
       desc.ArraySize!=1 || desc.SampleDesc.Count!=1)
        pilot_d3d_backend_stop("ARGB depth sampling requires single-level typeless D24S8");
    if(!texture->argb_image) {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd={0};
        sd.Format=DXGI_FORMAT_X24_TYPELESS_G8_UINT;sd.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels=1;
        pilot_d3d_require(ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)storage,
            &sd,&texture->stencil_srv),__FILE__,__LINE__);
        desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.Usage=D3D11_USAGE_DEFAULT;
        UINT support=0,required=D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_SHADER_SAMPLE|
            D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
        pilot_d3d_require(ID3D11Device_CheckFormatSupport(device,desc.Format,&support),__FILE__,__LINE__);
        if((support&required)!=required)pilot_d3d_backend_stop("FLOAT ARGB depth cache unsupported by native device");
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
        desc.CPUAccessFlags=desc.MiscFlags=0;
        pilot_d3d_require(ID3D11Device_CreateTexture2D(device,&desc,NULL,&texture->argb_image),__FILE__,__LINE__);
        pilot_d3d_require(ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)texture->argb_image,
            NULL,&texture->argb_srv),__FILE__,__LINE__);
        pilot_d3d_require(ID3D11Device_CreateUnorderedAccessView(device,(ID3D11Resource*)texture->argb_image,
            NULL,&texture->argb_uav),__FILE__,__LINE__);
        ++pilot_depth_texture_counts.allocations;
    }
    if(!pilot_depth_texture_shader) {
        ID3DBlob *code=NULL,*errors=NULL;
        HRESULT hr=D3DCompile(pilot_depth_texture_source,sizeof(pilot_depth_texture_source)-1,
            "pilot-depth-argb",NULL,NULL,"main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(errors){fprintf(stderr,"[DEPTH ARGB SHADER] %s\n",(const char*)ID3D10Blob_GetBufferPointer(errors));ID3D10Blob_Release(errors);}
        pilot_d3d_require(hr,__FILE__,__LINE__);
        hr=ID3D11Device_CreateComputeShader(device,ID3D10Blob_GetBufferPointer(code),ID3D10Blob_GetBufferSize(code),NULL,&pilot_depth_texture_shader);
        ID3D10Blob_Release(code);pilot_d3d_require(hr,__FILE__,__LINE__);
        ++pilot_depth_texture_counts.shader_compilations;
    }
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    /* A previous draw may still have this conversion bound for sampling.
     * Explicitly unbind it before UAV use, and invalidate our binding cache. */
    for(unsigned i=0;i<4;++i)if(pilot_sampling_views[i]==texture->argb_srv) {
        ID3D11ShaderResourceView *empty=NULL;
        ID3D11DeviceContext_PSSetShaderResources(context,i,1,&empty);
        pilot_sampling_views_dirty|=1u<<i;
    }
    ID3D11ComputeShader *previous=NULL;
    ID3D11ShaderResourceView *saved_views[2]={0},*views[]={depth_view,texture->stencil_srv};
    ID3D11UnorderedAccessView *saved_output=NULL,*empty_output=NULL;
    ID3D11DeviceContext_CSGetShader(context,&previous,NULL,NULL);
    ID3D11DeviceContext_CSGetShaderResources(context,0,2,saved_views);
    ID3D11DeviceContext_CSGetUnorderedAccessViews(context,0,1,&saved_output);
    ID3D11DeviceContext_CSSetShader(context,pilot_depth_texture_shader,NULL,0);
    ID3D11DeviceContext_CSSetShaderResources(context,0,2,views);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(context,0,1,&texture->argb_uav,NULL);
    ID3D11DeviceContext_Dispatch(context,(desc.Width+7)/8,(desc.Height+7)/8,1);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(context,0,1,&empty_output,NULL);
    ID3D11DeviceContext_CSSetShaderResources(context,0,2,saved_views);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(context,0,1,&saved_output,NULL);
    ID3D11DeviceContext_CSSetShader(context,previous,NULL,0);
    if(previous)ID3D11ComputeShader_Release(previous);
    if(saved_output)ID3D11UnorderedAccessView_Release(saved_output);
    for(unsigned i=0;i<2;++i)if(saved_views[i])ID3D11ShaderResourceView_Release(saved_views[i]);
    texture->argb_serial=texture->content_serial;texture->argb_initialized=1;
    texture->use->serial=pilot_mark_work();++pilot_depth_texture_counts.conversions;
    return texture->argb_srv;
}
