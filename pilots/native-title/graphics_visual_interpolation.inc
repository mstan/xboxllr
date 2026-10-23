/* Optional blended presentation interpolation. Two owned output-size images,
 * one source-frame of latency, no guest execution, IRQs or completion serials.
 * This is a blend, not optical flow; motion can show double edges. The host
 * pacer calls only under the existing graphics lock. No context is shared
 * concurrently and all touched native state is restored before returning. */
static const char pilot_visual_blend_source[]=
    "cbuffer Frame:register(b7){float2 texel;float alpha;" PILOT_GAMMA_CONSTANTS "};\n"
    "Texture2D<float4> previous:register(t9),current:register(t10);SamplerState linear_clamp:register(s9);\n"
    PILOT_GAMMA_PIXEL
    "float4 pixel(float4 position:SV_Position):SV_Target{float2 uv=position.xy*texel;return mapped(lerp(previous.SampleLevel(linear_clamp,uv,0),current.SampleLevel(linear_clamp,uv,0),alpha));}\n";
static struct {
    RhiImage *image[2];RhiView *target[2],*sample[2];RhiShader *pixel;
    unsigned width,height,index,count,scope;
    uint64_t previous,current,sources,presents;float alpha;
} pilot_visual_blend;
static unsigned pilot_visual_scope=1,pilot_visual_real_present;
static uint64_t pilot_visual_now(void)
{
    LARGE_INTEGER ticks,frequency;QueryPerformanceCounter(&ticks);QueryPerformanceFrequency(&frequency);
    return (uint64_t)ticks.QuadPart/frequency.QuadPart*10000000ull+
        ((uint64_t)ticks.QuadPart%frequency.QuadPart)*10000000ull/frequency.QuadPart;
}
static void pilot_visual_blend_close(void)
{
    for(unsigned i=0;i<2;++i) {
        rhi_view_destroy(pilot_visual_blend.sample[i]);rhi_view_destroy(pilot_visual_blend.target[i]);rhi_image_destroy(pilot_visual_blend.image[i]);
    }
    rhi_shader_destroy(pilot_visual_blend.pixel);memset(&pilot_visual_blend,0,sizeof(pilot_visual_blend));
}
static void pilot_visual_blend_prepare(unsigned width,unsigned height)
{
    if(pilot_visual_blend.width!=width || pilot_visual_blend.height!=height) {
        pilot_visual_blend_close();RhiImageDesc desc={0};desc.type=RHI_IMAGE_2D;desc.width=width;desc.height=height;
        desc.depth=desc.mip_levels=desc.samples=1;desc.format=RHI_FORMAT_R8G8B8A8_UNORM;
        desc.usage=RHI_USAGE_DEFAULT;desc.bind=RHI_BIND_RENDER_TARGET|RHI_BIND_SAMPLED;
        for(unsigned i=0;i<2;++i) {
            pilot_visual_blend.image[i]=rhi_image_create(&desc,NULL);
            if(pilot_visual_blend.image[i]) {
                pilot_visual_blend.target[i]=rhi_view_create(pilot_visual_blend.image[i],RHI_VIEW_RENDER_TARGET,NULL);
                pilot_visual_blend.sample[i]=rhi_view_create(pilot_visual_blend.image[i],RHI_VIEW_SAMPLED,NULL);
            }
            if(!pilot_visual_blend.target[i]||!pilot_visual_blend.sample[i])pilot_d3d_backend_stop("interpolation image allocation failed");
        }
        pilot_visual_blend.width=width;pilot_visual_blend.height=height;
    }
    if(!pilot_visual_blend.pixel) {
        RhiShaderSource source={0};char error[1024]={0};source.hlsl=pilot_visual_blend_source;source.len=sizeof(pilot_visual_blend_source)-1;
        source.name="xboxlle-blended-interpolation";source.entry="pixel";source.target="ps_4_0";
        pilot_visual_blend.pixel=rhi_shader_create(RHI_STAGE_PIXEL,&source,error,sizeof(error));
        if(!pilot_visual_blend.pixel){fprintf(stderr,"[INTERPOLATION] %s\n",error);pilot_d3d_backend_stop("interpolation shader creation failed");}
    }
}
void pilot_gfx_frame_scope(uint32_t scope)
{
    if(pilot_visual_scope!=scope)pilot_visual_blend.count=0;
    pilot_visual_scope=scope;
}
unsigned pilot_gfx_interpolation_rate(void)
{return pilot_visual.frame_mode==PILOT_FRAMES_INTERPOLATED?pilot_visual.frame_rate:0;}
int pilot_gfx_interpolation_ready(void)
{return pilot_device && pilot_gfx_interpolation_rate() && pilot_visual_scope && pilot_visual_blend.count && !pilot_visibility_active();}
void pilot_gfx_interpolation_stats(struct PilotInterpolationStats *out)
{
    if(!out)return;memset(out,0,sizeof(*out));out->sources=pilot_visual_blend.sources;out->presents=pilot_visual_blend.presents;
    out->previous=pilot_visual_blend.previous;out->current=pilot_visual_blend.current;
    out->width=pilot_visual_blend.width;out->height=pilot_visual_blend.height;out->history=pilot_visual_blend.count;out->alpha=pilot_visual_blend.alpha;
}
static HRESULT pilot_visual_capture_frame(RhiView *scene,unsigned width,unsigned height,D3D8DisplayFit fit,unsigned output_width,unsigned output_height)
{
    pilot_visual_blend_prepare(output_width,output_height);
    uint64_t now=pilot_visual_now();
    if(pilot_visual_blend.current && (now<=pilot_visual_blend.current || now-pilot_visual_blend.current>2500000ull))pilot_visual_blend.count=0;
    unsigned slot=pilot_visual_blend.index^1;
    HRESULT result=pilot_visual_resolve_image(scene,width,height,pilot_visual_blend.target[slot],fit,output_width,output_height,0);
    if(SUCCEEDED(result)) {
        pilot_visual_blend.index=slot;pilot_visual_blend.previous=pilot_visual_blend.current;pilot_visual_blend.current=now;
        if(pilot_visual_blend.count<2)++pilot_visual_blend.count;++pilot_visual_blend.sources;
    }
    return result;
}
int32_t pilot_d3d_visual_present(void)
{
    if(pilot_visual.frame_mode==PILOT_FRAMES_INTERPOLATED && pilot_visual_scope && pilot_visual_real_present && pilot_visual_blend.count)return S_OK;
    return rhi_present(0);
}
HRESULT pilot_d3d_interpolate_image(uint64_t now,RhiView *output)
{
    /* Host fullscreen draws must never contribute to a guest's live sample
     * counter. A missed presentation can resume after its visibility End. */
    if(!output || !pilot_gfx_interpolation_ready())return S_FALSE;
    if(!now)now=pilot_visual_now();
    float alpha=1;
    if(pilot_visual_blend.count>1 && pilot_visual_blend.current>pilot_visual_blend.previous && now>=pilot_visual_blend.current) {
        double elapsed=(double)(now-pilot_visual_blend.current)/(double)(pilot_visual_blend.current-pilot_visual_blend.previous);
        alpha=elapsed<1?(float)elapsed:1;
    }
    pilot_visual_blend.alpha=alpha;
    unsigned w=pilot_visual_blend.width,h=pilot_visual_blend.height,index=pilot_visual_blend.index;
    struct PilotVisualState saved;pilot_visual_state_save(&saved);pilot_visual_aa_prepare(w,h);
    pilot_visual_constants(w,h,alpha,1);
    RhiViewport viewport={0,0,(float)w,(float)h,0,1};float factor[]={1,1,1,1};
    rhi_set_render_target(output,NULL);rhi_set_viewports(1,&viewport);
    rhi_set_vertex_layout(NULL);rhi_set_topology(RHI_TOPOLOGY_TRIANGLES);rhi_set_shader(RHI_STAGE_VERTEX,pilot_visual_aa.vertex);
    rhi_set_shader(RHI_STAGE_PIXEL,pilot_visual_blend.pixel);rhi_set_uniform_buffers(RHI_STAGE_PIXEL,7,1,&pilot_visual_aa.constants);
    RhiView *images[]={pilot_visual_blend.sample[pilot_visual_blend.count>1?index^1:index],pilot_visual_blend.sample[index]};
    rhi_set_textures(9,2,images);rhi_set_samplers(9,1,&pilot_visual_aa.sampler);
    rhi_set_blend_state(pilot_visual_aa.blend,factor,0xffffffff);rhi_set_depth_state(pilot_visual_aa.depth,0);rhi_set_raster_state(pilot_visual_aa.raster);
    rhi_draw(3,0);pilot_visual_state_restore(&saved);return S_OK;
}
int32_t pilot_gfx_interpolate(uint64_t now)
{
    int32_t result=pilot_d3d_interpolate_image(now,rhi_swapchain_view());
    if(result!=S_OK)return result;
    result=rhi_present(0);if(result>=0)++pilot_visual_blend.presents;return result;
}
