/* Existing native writers remain untouched. Refresh only reads their resident
 * texels and expands the optional companion with integer center addressing.
 * No CPU texture allocation or upload occurs in this helper. */
#ifdef PILOT_D3D_PHOBOS
static const char pilot_native_upload_source[]=
    "cbuffer Size:register(b7){uint2 source_size;uint2 target_size;};\n"
    "Texture2D<float4> source_image:register(t9);\n"
    "struct V{float4 position:SV_Position;};\n"
    "V vertex(uint id:SV_VertexID){V o;uint2 p=uint2((id<<1)&2,id&2);"
    "o.position=float4(float2(p)*float2(2,-2)+float2(-1,1),0,1);return o;}\n"
    "float4 pixel(V i):SV_Target{uint2 p=(uint2)i.position.xy;"
    "uint2 q=((p*2+1)*source_size)/(target_size*2);"
    "return source_image.Load(int3(q,0));}\n";
static struct {
    RhiShader *vertex,*pixel;
    RhiBuffer *constants;
    RhiBlendState *blend;
    RhiDepthState *depth;
    RhiRasterState *raster;
    unsigned unavailable;
} pilot_native_upload;
static void pilot_native_upload_close(void)
{
    rhi_shader_destroy(pilot_native_upload.vertex);
    rhi_shader_destroy(pilot_native_upload.pixel);
    rhi_buffer_destroy(pilot_native_upload.constants);
    rhi_blend_state_destroy(pilot_native_upload.blend);
    rhi_depth_state_destroy(pilot_native_upload.depth);
    rhi_raster_state_destroy(pilot_native_upload.raster);
    memset(&pilot_native_upload,0,sizeof(pilot_native_upload));
}
static RhiShader *pilot_native_upload_shader(unsigned stage,const char *entry,const char *target)
{
    RhiShaderSource source={0};char error[2048]={0};
    source.hlsl=pilot_native_upload_source;source.len=sizeof(pilot_native_upload_source)-1;
    source.name="xboxllr-canonical-color-refresh";source.entry=entry;source.target=target;
    RhiShader *shader=rhi_shader_create(stage,&source,error,sizeof(error));
    if(!shader)fprintf(stderr,"[NATIVE UPLOAD] %s\n",error);
    return shader;
}
static int pilot_native_upload_prepare(void)
{
    if(pilot_native_upload.unavailable)return 0;
    if(pilot_native_upload.vertex && pilot_native_upload.pixel && pilot_native_upload.constants &&
       pilot_native_upload.blend && pilot_native_upload.depth && pilot_native_upload.raster)return 1;
    pilot_native_upload_close();
    pilot_native_upload.vertex=pilot_native_upload_shader(RHI_STAGE_VERTEX,"vertex","vs_4_0");
    pilot_native_upload.pixel=pilot_native_upload_shader(RHI_STAGE_PIXEL,"pixel","ps_4_0");
    RhiBufferDesc buffer={0};buffer.size=16;buffer.usage=RHI_USAGE_DYNAMIC;
    buffer.bind=RHI_BIND_UNIFORM;buffer.cpu_access=RHI_CPU_WRITE;
    pilot_native_upload.constants=rhi_buffer_create(&buffer,NULL);
    RhiBlendDesc blend={0};blend.write_mask=RHI_WRITE_ALL;
    pilot_native_upload.blend=rhi_blend_state_create(&blend);
    RhiDepthDesc depth={0};pilot_native_upload.depth=rhi_depth_state_create(&depth);
    RhiRasterDesc raster={0};raster.fill=RHI_FILL_SOLID;raster.cull=RHI_CULL_NONE;raster.depth_clip=1;
    pilot_native_upload.raster=rhi_raster_state_create(&raster);
    if(pilot_native_upload.vertex && pilot_native_upload.pixel && pilot_native_upload.constants &&
       pilot_native_upload.blend && pilot_native_upload.depth && pilot_native_upload.raster)return 1;
    pilot_native_upload_close();pilot_native_upload.unavailable=1;
    fprintf(stderr,"[NATIVE UPLOAD] expansion unavailable; canonical source retained, enhanced frame degraded\n");
    return 0;
}

/* OM target changes can implicitly clear an aliased SRV in any stage. Retain
 * all such bindings, including guest PS0..3 and the private t9/t10 slots. */
struct PilotNativeUploadBindings {
    ID3D11ShaderResourceView *vs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ID3D11ShaderResourceView *ps[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ID3D11ShaderResourceView *gs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ID3D11ShaderResourceView *hs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ID3D11ShaderResourceView *ds[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ID3D11ShaderResourceView *cs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    ID3D11HullShader *hull;
    ID3D11DomainShader *domain;
    ID3D11Predicate *predicate;
    BOOL predicate_value;
};
static void pilot_native_upload_bindings_save(struct PilotNativeUploadBindings *saved)
{
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    memset(saved,0,sizeof(*saved));
#define NATIVE_UPLOAD_GET(stage,field) ID3D11DeviceContext_##stage##GetShaderResources(context,0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,saved->field)
    NATIVE_UPLOAD_GET(VS,vs);NATIVE_UPLOAD_GET(PS,ps);NATIVE_UPLOAD_GET(GS,gs);
    NATIVE_UPLOAD_GET(HS,hs);NATIVE_UPLOAD_GET(DS,ds);NATIVE_UPLOAD_GET(CS,cs);
#undef NATIVE_UPLOAD_GET
    ID3D11DeviceContext_HSGetShader(context,&saved->hull,NULL,NULL);
    ID3D11DeviceContext_DSGetShader(context,&saved->domain,NULL,NULL);
    ID3D11DeviceContext_GetPredication(context,&saved->predicate,&saved->predicate_value);
}
static void pilot_native_upload_bindings_restore(struct PilotNativeUploadBindings *saved)
{
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
#define NATIVE_UPLOAD_RESTORE(stage,field) \
    ID3D11DeviceContext_##stage##SetShaderResources(context,0,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT,saved->field); \
    for(unsigned i=0;i<D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;++i)if(saved->field[i])ID3D11ShaderResourceView_Release(saved->field[i])
    NATIVE_UPLOAD_RESTORE(VS,vs);NATIVE_UPLOAD_RESTORE(PS,ps);NATIVE_UPLOAD_RESTORE(GS,gs);
    NATIVE_UPLOAD_RESTORE(HS,hs);NATIVE_UPLOAD_RESTORE(DS,ds);NATIVE_UPLOAD_RESTORE(CS,cs);
#undef NATIVE_UPLOAD_RESTORE
    ID3D11DeviceContext_HSSetShader(context,saved->hull,NULL,0);
    ID3D11DeviceContext_DSSetShader(context,saved->domain,NULL,0);
    ID3D11DeviceContext_SetPredication(context,saved->predicate,saved->predicate_value);
    if(saved->hull)ID3D11HullShader_Release(saved->hull);
    if(saved->domain)ID3D11DomainShader_Release(saved->domain);
    if(saved->predicate)ID3D11Predicate_Release(saved->predicate);
}
static int pilot_native_upload_uav_active(void)
{
    /* PilotVisualState preserves RTV/DSV outputs, not pixel UAV bindings.
     * Do not let the expansion's RTV implicitly discard an existing UAV. */
    ID3D11UnorderedAccessView *views[D3D11_1_UAV_SLOT_COUNT]={0};
    UINT count=ID3D11Device_GetFeatureLevel(d3d8_GetD3D11Device())>=D3D_FEATURE_LEVEL_11_1?
        D3D11_1_UAV_SLOT_COUNT:D3D11_PS_CS_UAV_REGISTER_COUNT;
    ID3D11DeviceContext_OMGetRenderTargetsAndUnorderedAccessViews(d3d8_GetD3D11Context(),0,NULL,NULL,0,count,views);
    int active=0;
    for(unsigned i=0;i<count;++i)if(views[i]) {
        active=1;ID3D11UnorderedAccessView_Release(views[i]);
    }
    return active;
}
static int pilot_native_refresh_color(struct PilotNativePlane *plane)
{
    if(!plane)return 0;
    plane->coherent=plane->valid_aspects=0;
    if(!plane->image || !plane->enhanced || plane->image==plane->enhanced)goto degraded;
    D3D11_TEXTURE2D_DESC target,native;
    ID3D11Texture2D_GetDesc(plane->enhanced,&target);ID3D11Texture2D_GetDesc(plane->image,&native);
    DXGI_FORMAT format=native.Format;
    if((format!=DXGI_FORMAT_R8G8B8A8_UNORM && format!=DXGI_FORMAT_B8G8R8A8_UNORM) || target.Format!=format ||
       !target.Width || !target.Height || target.Width>16384 || target.Height>16384 ||
       !native.Width || !native.Height || native.Width>16384 || native.Height>16384 ||
       target.Width!=plane->width || target.Height!=plane->height ||
       native.Width!=plane->native_width || native.Height!=plane->native_height || target.MipLevels!=1 || native.MipLevels!=1 ||
       target.ArraySize!=1 || native.ArraySize!=1 || target.SampleDesc.Count!=1 || native.SampleDesc.Count!=1 ||
       target.SampleDesc.Quality || native.SampleDesc.Quality ||
       target.Usage!=D3D11_USAGE_DEFAULT || native.Usage!=D3D11_USAGE_DEFAULT ||
       !(target.BindFlags&D3D11_BIND_RENDER_TARGET) ||
       (target.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) || (native.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE))goto degraded;
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    /* Guest-visible writes already happened through the original donor path.
     * Optional refresh must neither modify that image nor add query samples. */
    if(((native.BindFlags|target.BindFlags)&D3D11_BIND_UNORDERED_ACCESS) ||
       pilot_native_upload_uav_active() || !pilot_native_upload_prepare())goto degraded;
    uint32_t sizes[]={native.Width,native.Height,target.Width,target.Height};
    void *mapped=rhi_buffer_map(pilot_native_upload.constants,RHI_MAP_WRITE_DISCARD);
    if(!mapped)goto degraded;
    memcpy(mapped,sizes,sizeof(sizes));rhi_buffer_unmap(pilot_native_upload.constants);
    ID3D11Device *device=d3d8_GetD3D11Device();
    ID3D11ShaderResourceView *source=NULL;ID3D11RenderTargetView *output=NULL;
    D3D11_SHADER_RESOURCE_VIEW_DESC sample={0};sample.Format=format;
    sample.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sample.Texture2D.MipLevels=1;
    HRESULT result=ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)plane->image,&sample,&source);
    if(SUCCEEDED(result))result=ID3D11Device_CreateRenderTargetView(device,(ID3D11Resource*)plane->enhanced,NULL,&output);
    if(FAILED(result)) {
        if(source)ID3D11ShaderResourceView_Release(source);
        if(output)ID3D11RenderTargetView_Release(output);
        goto degraded;
    }
    ID3D11Query *excluded=NULL;
    if(!pilot_visibility_internal_try_begin(&excluded)) {
        ID3D11ShaderResourceView_Release(source);ID3D11RenderTargetView_Release(output);goto degraded;
    }
    struct PilotNativeUploadBindings bindings;pilot_native_upload_bindings_save(&bindings);
    struct PilotVisualState saved;pilot_visual_state_save(&saved);
    ID3D11DeviceContext_HSSetShader(context,NULL,NULL,0);
    ID3D11DeviceContext_DSSetShader(context,NULL,NULL,0);
    ID3D11DeviceContext_SetPredication(context,NULL,FALSE);
    ID3D11DeviceContext_OMSetRenderTargets(context,1,&output,NULL);
    RhiViewport viewport={0,0,(float)target.Width,(float)target.Height,0,1};float factor[]={1,1,1,1};
    rhi_set_viewports(1,&viewport);rhi_set_vertex_layout(NULL);rhi_set_topology(RHI_TOPOLOGY_TRIANGLES);
    rhi_set_shader(RHI_STAGE_VERTEX,pilot_native_upload.vertex);rhi_set_shader(RHI_STAGE_PIXEL,pilot_native_upload.pixel);
    rhi_set_uniform_buffers(RHI_STAGE_PIXEL,7,1,&pilot_native_upload.constants);
    ID3D11DeviceContext_PSSetShaderResources(context,9,1,&source);
    rhi_set_blend_state(pilot_native_upload.blend,factor,0xffffffff);
    rhi_set_depth_state(pilot_native_upload.depth,0);rhi_set_raster_state(pilot_native_upload.raster);
    rhi_draw(3,0);
    pilot_visibility_internal_end(excluded);
    ID3D11ShaderResourceView *none=NULL;ID3D11DeviceContext_PSSetShaderResources(context,9,1,&none);
    /* Outputs must be restored before their original aliased SRVs. */
    pilot_visual_state_restore(&saved);
    pilot_native_upload_bindings_restore(&bindings);
    ID3D11ShaderResourceView_Release(source);ID3D11RenderTargetView_Release(output);
    plane->coherent=1;plane->valid_aspects=1;
    return 1;
degraded:
    pilot_native_degrade();
    return 0;
}
#endif
