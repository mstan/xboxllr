/* Own packed YUY2/UYVY -> exact integer BT.601 GPU conversion.
 * Numerical basis: Microsoft's Recommended 8-Bit YUV Formats for Video
 * Rendering, Converting 8-bit YUV to RGB888 (integer equations):
 * https://learn.microsoft.com/en-us/windows/win32/medfound/recommended-8-bit-yuv-formats-for-video-rendering
 * The independent old-path fixture checks all 256^3 triples in both orders.
 * Raw CPU owner bytes stay unchanged; scratch is native-size, never scaled. */
static const char pilot_movie_gpu_source[]=
    "cbuffer Size:register(b7){uint width;uint height;uint format;uint unused;};\n"
    "Texture2D<uint> packed:register(t9);\n"
    "struct V{float4 position:SV_Position;};\n"
    "V vertex(uint id:SV_VertexID){V o;float2 q=float2((id<<1)&2,id&2);"
    "o.position=float4(q*float2(2,-2)+float2(-1,1),0,1);return o;}\n"
    "float4 pixel(V i):SV_Target{uint2 p=(uint2)i.position.xy;"
    "uint v=packed.Load(int3(p.x/2,p.y,0));int y,u,w;"
    "if(format==0x24){y=(v>>((p.x&1)*16))&255;u=(v>>8)&255;w=(v>>24)&255;}"
    "else{y=(v>>(8+(p.x&1)*16))&255;u=v&255;w=(v>>16)&255;}"
    "int c=y-16,d=u-128,e=w-128;"
    "int3 rgb=int3((298*c+409*e+128)>>8,(298*c-100*d-208*e+128)>>8,(298*c+516*d+128)>>8);"
    "return float4((float3)clamp(rgb,0,255)/255.0,1);}\n";
static struct {
    ID3D11Texture2D *packed,*converted;
    ID3D11ShaderResourceView *sample;
    ID3D11RenderTargetView *target;
    RhiShader *vertex,*pixel;
    RhiBuffer *constants;
    RhiBlendState *blend;
    RhiDepthState *depth;
    RhiRasterState *raster;
    unsigned width,height,format;
    int unavailable;
} pilot_movie_gpu;
static struct {uint64_t uploads,packed_bytes,expanded_bytes;} pilot_movie_gpu_stats;
uint64_t pilot_gfx_movie_gpu_upload_count(void){return pilot_movie_gpu_stats.uploads;}
static void pilot_movie_gpu_images_close(void)
{
    if(pilot_movie_gpu.sample)ID3D11ShaderResourceView_Release(pilot_movie_gpu.sample);
    if(pilot_movie_gpu.target)ID3D11RenderTargetView_Release(pilot_movie_gpu.target);
    if(pilot_movie_gpu.packed)ID3D11Texture2D_Release(pilot_movie_gpu.packed);
    if(pilot_movie_gpu.converted)ID3D11Texture2D_Release(pilot_movie_gpu.converted);
    pilot_movie_gpu.sample=NULL;pilot_movie_gpu.target=NULL;
    pilot_movie_gpu.packed=pilot_movie_gpu.converted=NULL;
    pilot_movie_gpu.width=pilot_movie_gpu.height=pilot_movie_gpu.format=0;
}
static void pilot_movie_gpu_close(void)
{
    pilot_movie_gpu_images_close();
    rhi_shader_destroy(pilot_movie_gpu.vertex);rhi_shader_destroy(pilot_movie_gpu.pixel);
    rhi_buffer_destroy(pilot_movie_gpu.constants);rhi_blend_state_destroy(pilot_movie_gpu.blend);
    rhi_depth_state_destroy(pilot_movie_gpu.depth);rhi_raster_state_destroy(pilot_movie_gpu.raster);
    memset(&pilot_movie_gpu,0,sizeof(pilot_movie_gpu));
}
static RhiShader *pilot_movie_gpu_shader(unsigned stage,const char *entry,const char *target)
{
    RhiShaderSource source={0};char error[2048]={0};source.hlsl=pilot_movie_gpu_source;
    source.len=sizeof(pilot_movie_gpu_source)-1;source.name="xboxllr-packed-movie";
    source.entry=entry;source.target=target;
    RhiShader *shader=rhi_shader_create(stage,&source,error,sizeof(error));
    if(!shader)fprintf(stderr,"[MOVIE GPU] shader unavailable: %s; original conversion retained\n",error);
    return shader;
}
static int pilot_movie_gpu_prepare(unsigned width,unsigned height,DXGI_FORMAT format)
{
    if(pilot_movie_gpu.unavailable)return 0;
    ID3D11Device *device=d3d8_GetD3D11Device();
    if(width!=pilot_movie_gpu.width || height!=pilot_movie_gpu.height || (unsigned)format!=pilot_movie_gpu.format) {
        pilot_movie_gpu_images_close();
        D3D11_TEXTURE2D_DESC desc={0};desc.Width=width/2;desc.Height=height;
        desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R32_UINT;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(ID3D11Device_CreateTexture2D(device,&desc,NULL,&pilot_movie_gpu.packed)) ||
           FAILED(ID3D11Device_CreateShaderResourceView(device,(ID3D11Resource*)pilot_movie_gpu.packed,NULL,&pilot_movie_gpu.sample)))goto unavailable;
        desc.Width=width;desc.Format=format;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        if(FAILED(ID3D11Device_CreateTexture2D(device,&desc,NULL,&pilot_movie_gpu.converted)) ||
           FAILED(ID3D11Device_CreateRenderTargetView(device,(ID3D11Resource*)pilot_movie_gpu.converted,NULL,&pilot_movie_gpu.target)))goto unavailable;
        pilot_movie_gpu.width=width;pilot_movie_gpu.height=height;pilot_movie_gpu.format=(unsigned)format;
    }
    if(pilot_movie_gpu.vertex)return 1;
    pilot_movie_gpu.vertex=pilot_movie_gpu_shader(RHI_STAGE_VERTEX,"vertex","vs_4_0");
    pilot_movie_gpu.pixel=pilot_movie_gpu_shader(RHI_STAGE_PIXEL,"pixel","ps_4_0");
    RhiBufferDesc cb={0};cb.size=16;cb.usage=RHI_USAGE_DYNAMIC;cb.bind=RHI_BIND_UNIFORM;cb.cpu_access=RHI_CPU_WRITE;
    pilot_movie_gpu.constants=rhi_buffer_create(&cb,NULL);
    RhiBlendDesc blend={0};blend.write_mask=RHI_WRITE_ALL;pilot_movie_gpu.blend=rhi_blend_state_create(&blend);
    RhiDepthDesc depth={0};pilot_movie_gpu.depth=rhi_depth_state_create(&depth);
    RhiRasterDesc raster={0};raster.fill=RHI_FILL_SOLID;raster.cull=RHI_CULL_NONE;raster.depth_clip=1;
    pilot_movie_gpu.raster=rhi_raster_state_create(&raster);
    if(!pilot_movie_gpu.vertex || !pilot_movie_gpu.pixel || !pilot_movie_gpu.constants ||
       !pilot_movie_gpu.blend || !pilot_movie_gpu.depth || !pilot_movie_gpu.raster)goto unavailable;
    return 1;
unavailable:
    pilot_movie_gpu_close();pilot_movie_gpu.unavailable=1;
    fprintf(stderr,"[MOVIE GPU] unavailable; original conversion retained\n");return 0;
}
static int pilot_movie_gpu_upload(D3D8Texture *texture,unsigned level,unsigned width,unsigned height,const void *data,unsigned pitch)
{
    static int selection;
    if(!selection){const char *v=getenv("XLLE_PILOT_GPU_MOVIE_UPLOAD");selection=v && !strcmp(v,"0")?1:2;}
    if(selection!=2 || level || texture->levels!=1 || !width || !height || (width&1) ||
       width>4096 || height>4096 || !data || pitch<(uint64_t)width*2 || height>UINT32_MAX/pitch)return 0;
    /* An optimization must not add query allocations or failure modes inside
     * a guest query. Keep the original CPU upload for that whole lifetime. */
    if(pilot_visibility_active())return 0;
    ID3D11Texture2D *destination=pilot_donor_texture(texture);D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D_GetDesc(destination,&desc);
    if(desc.Width!=width || desc.Height!=height || desc.MipLevels!=1 || desc.ArraySize!=1 || desc.SampleDesc.Count!=1 ||
       (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM))return 0;
    if(!pilot_movie_gpu_prepare(width,height,desc.Format))return 0;
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_UpdateSubresource(context,(ID3D11Resource*)pilot_movie_gpu.packed,0,NULL,data,pitch,pitch*height);
    uint32_t values[]={width,height,(uint32_t)texture->d3d8_format,0};
    void *mapped=rhi_buffer_map(pilot_movie_gpu.constants,RHI_MAP_WRITE_DISCARD);
    if(!mapped)return 0;
    memcpy(mapped,values,sizeof(values));rhi_buffer_unmap(pilot_movie_gpu.constants);
    struct PilotVisualState saved;pilot_visual_state_save(&saved);
    ID3D11DeviceContext_OMSetRenderTargets(context,1,&pilot_movie_gpu.target,NULL);
    RhiViewport viewport={0,0,(float)width,(float)height,0,1};float factor[]={1,1,1,1};
    rhi_set_viewports(1,&viewport);rhi_set_vertex_layout(NULL);rhi_set_topology(RHI_TOPOLOGY_TRIANGLES);
    rhi_set_shader(RHI_STAGE_VERTEX,pilot_movie_gpu.vertex);rhi_set_shader(RHI_STAGE_PIXEL,pilot_movie_gpu.pixel);
    rhi_set_uniform_buffers(RHI_STAGE_PIXEL,7,1,&pilot_movie_gpu.constants);
    ID3D11DeviceContext_PSSetShaderResources(context,9,1,&pilot_movie_gpu.sample);
    rhi_set_blend_state(pilot_movie_gpu.blend,factor,0xffffffff);rhi_set_depth_state(pilot_movie_gpu.depth,0);
    rhi_set_raster_state(pilot_movie_gpu.raster);
    rhi_draw(3,0);
    ID3D11ShaderResourceView *none=NULL;ID3D11DeviceContext_PSSetShaderResources(context,9,1,&none);
    pilot_visual_state_restore(&saved);
    ID3D11DeviceContext_CopyResource(context,(ID3D11Resource*)destination,(ID3D11Resource*)pilot_movie_gpu.converted);
    if(!pilot_movie_gpu_stats.uploads)fprintf(stderr,"[MOVIE GPU] active packed YUV; original raw owner and exact integer color; %ux%u\n",width,height);
    ++pilot_movie_gpu_stats.uploads;pilot_movie_gpu_stats.packed_bytes+=(uint64_t)width*height*2;
    pilot_movie_gpu_stats.expanded_bytes+=(uint64_t)width*height*4;return 1;
}
