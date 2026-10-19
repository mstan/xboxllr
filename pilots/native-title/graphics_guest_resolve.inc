/* Real guest-visible Gaussian resolve, before optional window enhancements.
 * Kernel reference: local xemu NV2A Gaussian3 convolution (1,2,1 tensor /16).
 * Uses linear clamp samples at the projected pixel center plus texel offsets.
 * Exact silicon interpolation/UNORM rounding is not claimed. */
typedef struct PilotGuestResolved {
    RhiImage *image;RhiView *target,*sample;
    RhiShader *vertex,*pixel;RhiBuffer *constants;
    RhiBlendState *blend;RhiDepthState *depth;RhiRasterState *raster;RhiSampler *sampler;
    unsigned width,height;
} PilotGuestResolved;
static PilotGuestResolved pilot_guest_resolved;
static const char pilot_guest_gaussian_source[]=
    "cbuffer Frame:register(b7){float2 source_size;float2 target_size;};\n"
    "Texture2D<float4> image:register(t9);SamplerState linear_clamp:register(s9);\n"
    "struct V{float4 position:SV_Position;};\n"
    "V vertex(uint id:SV_VertexID){V o;float2 q=float2((id<<1)&2,id&2);o.position=float4(q*float2(2,-2)+float2(-1,1),0,1);return o;}\n"
    "float4 pixel(V p):SV_Target{float2 uv=p.position.xy/target_size;float4 color=0;\n"
    " [unroll]for(int y=-1;y<=1;y++)[unroll]for(int x=-1;x<=1;x++){\n"
    "  float weight=(x==0?2:1)*(y==0?2:1)*(1.0/16.0);\n"
    "  color+=image.SampleLevel(linear_clamp,uv+float2(x,y)/source_size,0)*weight;}return color;}\n";
static void pilot_guest_resolve_close(void)
{
    rhi_view_destroy(pilot_guest_resolved.sample);rhi_view_destroy(pilot_guest_resolved.target);
    rhi_image_destroy(pilot_guest_resolved.image);
    rhi_shader_destroy(pilot_guest_resolved.vertex);rhi_shader_destroy(pilot_guest_resolved.pixel);
    rhi_buffer_destroy(pilot_guest_resolved.constants);rhi_blend_state_destroy(pilot_guest_resolved.blend);
    rhi_depth_state_destroy(pilot_guest_resolved.depth);rhi_raster_state_destroy(pilot_guest_resolved.raster);
    rhi_sampler_destroy(pilot_guest_resolved.sampler);memset(&pilot_guest_resolved,0,sizeof(pilot_guest_resolved));
}
static HRESULT pilot_guest_resolve_prepare(unsigned width,unsigned height)
{
    if(pilot_guest_resolved.image)return pilot_guest_resolved.width==width && pilot_guest_resolved.height==height?S_OK:E_INVALIDARG;
    RhiImageDesc image={0};image.type=RHI_IMAGE_2D;image.width=width;image.height=height;image.depth=1;
    image.mip_levels=image.samples=1;image.format=RHI_FORMAT_R8G8B8A8_UNORM;image.usage=RHI_USAGE_DEFAULT;
    image.bind=RHI_BIND_RENDER_TARGET|RHI_BIND_SAMPLED;
    pilot_guest_resolved.image=rhi_image_create(&image,NULL);
    if(pilot_guest_resolved.image) {
        pilot_guest_resolved.target=rhi_view_create(pilot_guest_resolved.image,RHI_VIEW_RENDER_TARGET,NULL);
        pilot_guest_resolved.sample=rhi_view_create(pilot_guest_resolved.image,RHI_VIEW_SAMPLED,NULL);
    }
    RhiShaderSource shader={0};char error[2048]={0};shader.hlsl=pilot_guest_gaussian_source;
    shader.len=sizeof(pilot_guest_gaussian_source)-1;shader.name="guest-gaussian-resolve";
    shader.entry="vertex";shader.target="vs_4_0";
    pilot_guest_resolved.vertex=rhi_shader_create(RHI_STAGE_VERTEX,&shader,error,sizeof(error));
    shader.entry="pixel";shader.target="ps_4_0";
    pilot_guest_resolved.pixel=rhi_shader_create(RHI_STAGE_PIXEL,&shader,error,sizeof(error));
    RhiBufferDesc cb={0};cb.size=16;cb.usage=RHI_USAGE_DYNAMIC;cb.bind=RHI_BIND_UNIFORM;cb.cpu_access=RHI_CPU_WRITE;
    pilot_guest_resolved.constants=rhi_buffer_create(&cb,NULL);
    RhiBlendDesc blend={0};blend.write_mask=RHI_WRITE_ALL;pilot_guest_resolved.blend=rhi_blend_state_create(&blend);
    RhiDepthDesc depth={0};pilot_guest_resolved.depth=rhi_depth_state_create(&depth);
    RhiRasterDesc raster={0};raster.fill=RHI_FILL_SOLID;raster.cull=RHI_CULL_NONE;raster.depth_clip=1;
    pilot_guest_resolved.raster=rhi_raster_state_create(&raster);
    RhiSamplerDesc sampler={0};sampler.filter=RHI_FILTER_LINEAR;
    sampler.address_u=sampler.address_v=sampler.address_w=RHI_ADDRESS_CLAMP;
    sampler.compare=RHI_CMP_NEVER;sampler.max_lod=3.402823466e+38f;
    pilot_guest_resolved.sampler=rhi_sampler_create(&sampler);
    if(!pilot_guest_resolved.target || !pilot_guest_resolved.sample || !pilot_guest_resolved.vertex ||
       !pilot_guest_resolved.pixel || !pilot_guest_resolved.constants || !pilot_guest_resolved.blend ||
       !pilot_guest_resolved.depth || !pilot_guest_resolved.raster || !pilot_guest_resolved.sampler) {
        fprintf(stderr,"[GUEST GAUSSIAN] %s\n",error);pilot_guest_resolve_close();return E_FAIL;
    }
    pilot_guest_resolved.width=width;pilot_guest_resolved.height=height;return S_OK;
}
static HRESULT pilot_guest_resolve(void)
{
    if(!pilot_guest_resolved.image)return E_FAIL;
    unsigned width=pilot_presentation.width*pilot_presentation_axis(&pilot_presentation);
    unsigned height=pilot_presentation.height*pilot_presentation_axis(&pilot_presentation);
    struct PilotVisualState saved;pilot_visual_state_save(&saved);
    /* The guest may sample an owned alias of its resolved front image. Binding
     * it as this pass's RTV would otherwise silently unbind those guest slots. */
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11ShaderResourceView *guest_samples[4]={0},*empty_samples[4]={0};
    ID3D11DeviceContext_PSGetShaderResources(context,0,4,guest_samples);
    void *mapped=rhi_buffer_map(pilot_guest_resolved.constants,RHI_MAP_WRITE_DISCARD);
    if(!mapped){
        for(unsigned i=0;i<4;++i)if(guest_samples[i])ID3D11ShaderResourceView_Release(guest_samples[i]);
        pilot_visual_state_restore(&saved);return E_FAIL;
    }
    float constants[]={(float)width,(float)height,(float)pilot_guest_resolved.width,(float)pilot_guest_resolved.height};
    memcpy(mapped,constants,sizeof(constants));rhi_buffer_unmap(pilot_guest_resolved.constants);
    RhiViewport viewport={0,0,(float)pilot_guest_resolved.width,(float)pilot_guest_resolved.height,0,1};
    float factor[]={1,1,1,1};RhiView *scene=d3d8_GetSceneView(),*none=NULL;
    ID3D11DeviceContext_PSSetShaderResources(context,0,4,empty_samples);
    rhi_set_render_target(pilot_guest_resolved.target,NULL);rhi_set_viewports(1,&viewport);
    rhi_set_vertex_layout(NULL);rhi_set_topology(RHI_TOPOLOGY_TRIANGLES);
    rhi_set_shader(RHI_STAGE_VERTEX,pilot_guest_resolved.vertex);rhi_set_shader(RHI_STAGE_PIXEL,pilot_guest_resolved.pixel);
    rhi_set_uniform_buffers(RHI_STAGE_PIXEL,7,1,&pilot_guest_resolved.constants);
    rhi_set_textures(9,1,&scene);rhi_set_samplers(9,1,&pilot_guest_resolved.sampler);
    rhi_set_blend_state(pilot_guest_resolved.blend,factor,0xffffffff);
    rhi_set_depth_state(pilot_guest_resolved.depth,0);rhi_set_raster_state(pilot_guest_resolved.raster);
    rhi_draw(3,0);rhi_set_textures(9,1,&none);pilot_visual_state_restore(&saved);
    ID3D11DeviceContext_PSSetShaderResources(context,0,4,guest_samples);
    for(unsigned i=0;i<4;++i)if(guest_samples[i])ID3D11ShaderResourceView_Release(guest_samples[i]);
    return S_OK;
}
