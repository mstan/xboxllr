#ifdef PILOT_D3D_PHOBOS
#include "d3d8_display.h"
#include "graphics_visual_state.inc"
#include "graphics_guest_resolve.inc"
static unsigned pilot_gamma_active;
static float pilot_gamma_table[256][4];
static void pilot_visual_gamma_reset(void){pilot_gamma_active=0;}
int32_t pilot_gfx_gamma(const uint8_t ramp[768])
{
    if(!pilot_device || !ramp)return E_INVALIDARG;
    unsigned active=0;
    for(unsigned i=0;i<256;++i)for(unsigned channel=0;channel<3;++channel) {
        unsigned value=ramp[channel*256+i];active|=value!=i;
        pilot_gamma_table[i][channel]=value/255.0f;
    }
    pilot_gamma_active=active;return S_OK;
}
#define PILOT_GAMMA_CONSTANTS "float gamma_enabled;float4 gamma_table[256];"
#define PILOT_GAMMA_PIXEL \
    "float4 mapped(float4 c){if(gamma_enabled<.5)return c;uint3 v=(uint3)round(saturate(c.rgb)*255);" \
    "return float4(gamma_table[v.r].r,gamma_table[v.g].g,gamma_table[v.b].b,c.a);}\n"
/* Independently written approximate AA, using local contrast, curvature,
 * bounded edge-end search and subpixel filtering. Algorithm reference:
 * Timothy Lottes, NVIDIA FXAA white paper, 2011:
 * https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf
 * This is a presentation filter, including the composed HUD. It claims no
 * official preset/bit identity. Guest readbacks never see this pass. */
static const char pilot_visual_aa_source[]=
    "cbuffer Frame:register(b7){float2 texel;float antialiasing;" PILOT_GAMMA_CONSTANTS "};\n"
    "Texture2D<float4> image:register(t9);SamplerState linear_clamp:register(s9);\n"
    "struct V{float4 position:SV_Position;};\n"
    "V vertex(uint id:SV_VertexID){V o;float2 q=float2((id<<1)&2,id&2);o.position=float4(q*float2(2,-2)+float2(-1,1),0,1);return o;}\n"
    "float4 color(float2 uv){return image.SampleLevel(linear_clamp,uv,0);}\n"
    "float light(float3 c){return dot(c,float3(.299,.587,.114));}\n"
    PILOT_GAMMA_PIXEL
    "float4 pixel(V input):SV_Target{\n"
    " float2 uv=input.position.xy*texel;float4 m=color(uv);\n"
    " if(antialiasing<.5)return mapped(m);\n"
    " float3 n=color(uv-float2(0,texel.y)).rgb,s=color(uv+float2(0,texel.y)).rgb;\n"
    " float3 w=color(uv-float2(texel.x,0)).rgb,e=color(uv+float2(texel.x,0)).rgb;\n"
    " float lm=light(m.rgb),ln=light(n),ls=light(s),lw=light(w),le=light(e);\n"
    " float lo=min(lm,min(min(ln,ls),min(lw,le))),hi=max(lm,max(max(ln,ls),max(lw,le)));\n"
    " float contrast=hi-lo;if(contrast<max(.03125,hi*.125))return mapped(m);\n"
    " float3 nw=color(uv-texel).rgb,ne=color(uv+float2(texel.x,-texel.y)).rgb;\n"
    " float3 sw=color(uv+float2(-texel.x,texel.y)).rgb,se=color(uv+texel).rgb;\n"
    " float a=light(nw),b=light(ne),c=light(sw),d=light(se);\n"
    " float horizontal=abs(a+c-2*lw)+2*abs(ln+ls-2*lm)+abs(b+d-2*le);\n"
    " float vertical=abs(a+b-2*ln)+2*abs(lw+le-2*lm)+abs(c+d-2*ls);\n"
    " bool along_x=horizontal>=vertical;float low_side=along_x?ln:lw,high_side=along_x?ls:le;\n"
    " float negative=abs(low_side-lm),positive=abs(high_side-lm);\n"
    " float direction=negative>positive?-1:1;float neighbor=negative>positive?low_side:high_side;\n"
    " float2 normal=along_x?float2(0,direction*texel.y):float2(direction*texel.x,0);\n"
    " float2 tangent=along_x?float2(texel.x,0):float2(0,texel.y);\n"
    " float mean=(lm+neighbor)*.5,threshold=max(negative,positive)*.25;\n"
    " float2 boundary=uv+normal*.5;float left_distance=8.5,right_distance=8.5,left_sign=0,right_sign=0;\n"
    " bool left_done=false,right_done=false;\n"
    " [loop]for(int step=1;step<=8;++step){\n"
    "  if(!left_done){float v=light(color(boundary-tangent*step).rgb)-mean;if(abs(v)>=threshold){left_distance=step;left_sign=v;left_done=true;}}\n"
    "  if(!right_done){float v=light(color(boundary+tangent*step).rgb)-mean;if(abs(v)>=threshold){right_distance=step;right_sign=v;right_done=true;}}\n"
    "  if(left_done&&right_done)break;\n"
    " }\n"
    " bool left=left_distance<right_distance;float endpoint=left?left_sign:right_sign;\n"
    " float offset=.5-min(left_distance,right_distance)/(left_distance+right_distance);\n"
    " if((endpoint>0)==(lm>mean))offset=0;\n"
    " float3 edge=color(uv+normal*offset).rgb;\n"
    " float subpixel=saturate((abs((ln+ls+lw+le)*.25-lm)/contrast-.25)*(4.0/3));\n"
    " subpixel=subpixel*subpixel*(3-2*subpixel)*.75;\n"
    " return mapped(float4(lerp(edge,(m.rgb+n+s+w+e+nw+ne+sw+se)/9,subpixel),m.a));\n"
    "}\n";
static struct {
    RhiImage *image;RhiView *target,*sample;
    RhiShader *vertex,*pixel;RhiBuffer *constants;
    RhiBlendState *blend;RhiDepthState *depth;RhiRasterState *raster;RhiSampler *sampler;
    unsigned width,height;
} pilot_visual_aa;
static void pilot_visual_blend_close(void);
static void pilot_visual_close(void)
{
    pilot_visual_blend_close();
    if(!pilot_visual_aa.image && !pilot_visual_aa.vertex)return;
    rhi_view_destroy(pilot_visual_aa.sample);rhi_view_destroy(pilot_visual_aa.target);rhi_image_destroy(pilot_visual_aa.image);
    rhi_shader_destroy(pilot_visual_aa.vertex);rhi_shader_destroy(pilot_visual_aa.pixel);
    rhi_buffer_destroy(pilot_visual_aa.constants);rhi_blend_state_destroy(pilot_visual_aa.blend);
    rhi_depth_state_destroy(pilot_visual_aa.depth);rhi_raster_state_destroy(pilot_visual_aa.raster);
    rhi_sampler_destroy(pilot_visual_aa.sampler);memset(&pilot_visual_aa,0,sizeof(pilot_visual_aa));
}
static RhiShader *pilot_visual_shader(unsigned stage,const char *entry,const char *target)
{
    RhiShaderSource source={0};char error[2048]={0};source.hlsl=pilot_visual_aa_source;
    source.len=sizeof(pilot_visual_aa_source)-1;source.name="xboxlle-visual-aa";source.entry=entry;source.target=target;
    RhiShader *shader=rhi_shader_create(stage,&source,error,sizeof(error));
    if(!shader){fprintf(stderr,"[VISUAL AA] %s\n",error);pilot_d3d_backend_stop("visual AA shader creation failed");}return shader;
}
static void pilot_visual_aa_prepare(unsigned width,unsigned height)
{
    if(pilot_visual_aa.width!=width || pilot_visual_aa.height!=height) {
        rhi_view_destroy(pilot_visual_aa.sample);rhi_view_destroy(pilot_visual_aa.target);rhi_image_destroy(pilot_visual_aa.image);
        pilot_visual_aa.image=NULL;pilot_visual_aa.target=pilot_visual_aa.sample=NULL;
        RhiImageDesc desc={0};desc.type=RHI_IMAGE_2D;desc.width=width;desc.height=height;desc.depth=1;
        desc.mip_levels=desc.samples=1;desc.format=RHI_FORMAT_R8G8B8A8_UNORM;desc.usage=RHI_USAGE_DEFAULT;
        desc.bind=RHI_BIND_RENDER_TARGET|RHI_BIND_SAMPLED;
        pilot_visual_aa.image=rhi_image_create(&desc,NULL);
        if(pilot_visual_aa.image) {
            pilot_visual_aa.target=rhi_view_create(pilot_visual_aa.image,RHI_VIEW_RENDER_TARGET,NULL);
            pilot_visual_aa.sample=rhi_view_create(pilot_visual_aa.image,RHI_VIEW_SAMPLED,NULL);
        }
        if(!pilot_visual_aa.target || !pilot_visual_aa.sample)pilot_d3d_backend_stop("visual AA target allocation failed");
        pilot_visual_aa.width=width;pilot_visual_aa.height=height;
    }
    if(pilot_visual_aa.vertex)return;
    pilot_visual_aa.vertex=pilot_visual_shader(RHI_STAGE_VERTEX,"vertex","vs_4_0");
    pilot_visual_aa.pixel=pilot_visual_shader(RHI_STAGE_PIXEL,"pixel","ps_4_0");
    RhiBufferDesc cb={0};cb.size=16+sizeof(pilot_gamma_table);cb.usage=RHI_USAGE_DYNAMIC;cb.bind=RHI_BIND_UNIFORM;cb.cpu_access=RHI_CPU_WRITE;
    pilot_visual_aa.constants=rhi_buffer_create(&cb,NULL);
    RhiBlendDesc blend={0};blend.write_mask=RHI_WRITE_ALL;pilot_visual_aa.blend=rhi_blend_state_create(&blend);
    RhiDepthDesc depth={0};pilot_visual_aa.depth=rhi_depth_state_create(&depth);
    RhiRasterDesc raster={0};raster.fill=RHI_FILL_SOLID;raster.cull=RHI_CULL_NONE;raster.depth_clip=1;
    pilot_visual_aa.raster=rhi_raster_state_create(&raster);
    RhiSamplerDesc sampler={0};sampler.filter=RHI_FILTER_LINEAR;
    sampler.address_u=sampler.address_v=sampler.address_w=RHI_ADDRESS_CLAMP;
    sampler.compare=RHI_CMP_NEVER;sampler.max_lod=3.402823466e+38f;
    pilot_visual_aa.sampler=rhi_sampler_create(&sampler);
    if(!pilot_visual_aa.constants || !pilot_visual_aa.blend || !pilot_visual_aa.depth ||
       !pilot_visual_aa.raster || !pilot_visual_aa.sampler)pilot_d3d_backend_stop("visual AA pipeline allocation failed");
}
static void pilot_visual_constants(unsigned width,unsigned height,float filter,unsigned apply_gamma)
{
    float constants[]={1.0f/width,1.0f/height,filter,(float)(apply_gamma && pilot_gamma_active)};
    void *mapped=rhi_buffer_map(pilot_visual_aa.constants,RHI_MAP_WRITE_DISCARD);
    if(!mapped)pilot_d3d_backend_stop("presentation constants upload failed");
    memcpy(mapped,constants,sizeof(constants));
    memcpy((uint8_t*)mapped+16,pilot_gamma_table,sizeof(pilot_gamma_table));
    rhi_buffer_unmap(pilot_visual_aa.constants);
}
static HRESULT pilot_visual_resolve_image(RhiView *scene,unsigned width,unsigned height,RhiView *output,D3D8DisplayFit fit,unsigned output_width,unsigned output_height,unsigned apply_gamma)
{
    if(!scene || !output || !fit.w || !fit.h || !output_width || !output_height)return E_INVALIDARG;
    if(!pilot_visual.antialiasing && !(apply_gamma && pilot_gamma_active))return d3d8_display_resolve(scene,width,height,output,fit);
    pilot_visual_aa_prepare(output_width,output_height);
    HRESULT result=d3d8_display_resolve(scene,width,height,pilot_visual_aa.target,fit);
    if(FAILED(result))return result;
    pilot_visual_constants(output_width,output_height,(float)pilot_visual.antialiasing,apply_gamma);
    RhiViewport viewport={0,0,(float)output_width,(float)output_height,0,1};float factor[]={1,1,1,1};
    rhi_set_render_target(output,NULL);rhi_set_viewports(1,&viewport);rhi_set_vertex_layout(NULL);rhi_set_topology(RHI_TOPOLOGY_TRIANGLES);
    rhi_set_shader(RHI_STAGE_VERTEX,pilot_visual_aa.vertex);rhi_set_shader(RHI_STAGE_PIXEL,pilot_visual_aa.pixel);
    rhi_set_uniform_buffers(RHI_STAGE_PIXEL,7,1,&pilot_visual_aa.constants);
    rhi_set_textures(9,1,&pilot_visual_aa.sample);rhi_set_samplers(9,1,&pilot_visual_aa.sampler);
    rhi_set_blend_state(pilot_visual_aa.blend,factor,0xffffffff);rhi_set_depth_state(pilot_visual_aa.depth,0);rhi_set_raster_state(pilot_visual_aa.raster);
    rhi_draw(3,0);RhiView *none=NULL;rhi_set_textures(9,1,&none);return S_OK;
}
#include "graphics_visual_interpolation.inc"
static void pilot_debug_capture_presented(void)
{
    uint64_t request=pilot_debug_capture_requested();
    if(!request || !pilot_visual_real_present)return;
    uint32_t width=0,height=0;unsigned char *rgba=NULL;
    int status=rhi_swapchain_readback(NULL,0,&width,&height);
    uint64_t bytes=(uint64_t)width*height*4;
    if(!status && width && height && bytes<=64*1024*1024) {
        rgba=malloc((size_t)bytes);
        status=rgba?rhi_swapchain_readback(rgba,width*4,&width,&height):E_OUTOFMEMORY;
    } else status=E_INVALIDARG;
    pilot_debug_capture_complete(request,rgba,width,height,status);free(rgba);
}
HRESULT pilot_d3d_visual_resolve(RhiView *scene,unsigned width,unsigned height,RhiView *output,D3D8DisplayFit fit,unsigned output_width,unsigned output_height)
{
    if(!scene || !output || !fit.w || !fit.h || !output_width || !output_height)return E_INVALIDARG;
    if(pilot_presentation.sample_type==0x2222) {
        scene=pilot_guest_resolved.sample;width=pilot_presentation.width;height=pilot_presentation.height;
    }
    int interpolate=pilot_visual.frame_mode==PILOT_FRAMES_INTERPOLATED && pilot_visual_scope && pilot_visual_real_present;
    if(!pilot_visual.antialiasing && !interpolate && !pilot_gamma_active) {
        HRESULT result=d3d8_display_resolve(scene,width,height,output,fit);
        if(SUCCEEDED(result))pilot_debug_capture_presented();
        return result;
    }
    struct PilotVisualState saved;pilot_visual_state_save(&saved);
    HRESULT result=interpolate?pilot_visual_capture_frame(scene,width,height,fit,output_width,output_height):
        pilot_visual_resolve_image(scene,width,height,output,fit,output_width,output_height,1);
    pilot_visual_state_restore(&saved);
    if(SUCCEEDED(result))pilot_debug_capture_presented();
    return result;
}
#undef PILOT_GAMMA_CONSTANTS
#undef PILOT_GAMMA_PIXEL
#else
static void pilot_visual_gamma_reset(void){}
int32_t pilot_gfx_gamma(const uint8_t ramp[768])
{
    if(!pilot_device || !ramp)return E_INVALIDARG;
    for(unsigned i=0;i<256;++i)for(unsigned channel=0;channel<3;++channel)
        if(ramp[channel*256+i]!=i)return E_NOTIMPL;
    return S_OK;
}
static void pilot_visual_close(void){}
void pilot_gfx_frame_scope(uint32_t scope){(void)scope;}
unsigned pilot_gfx_interpolation_rate(void){return 0;}
int32_t pilot_gfx_interpolate(uint64_t now){(void)now;return 1;}
void pilot_gfx_interpolation_stats(struct PilotInterpolationStats *out){if(out)memset(out,0,sizeof(*out));}
#endif
