/* One full-resolution scene pass per admitted native/enhanced plane. No temporal feedback, guest texture resize,
 * quantization, readback, depth conversion or HUD filtering. */
static int pilot_native_copy_completed(ID3D11Resource *,ID3D11Resource *);
static struct {
    ID3D11Texture2D *image;ID3D11ShaderResourceView *sample;
    RhiShader *pixel;unsigned width,height;DXGI_FORMAT format;
    struct PilotNativePlane *plane;
    struct PilotNativeMap *map;
} pilot_radial;
static void pilot_radial_native_close(void)
{
    pilot_native_map_release(pilot_radial.map);pilot_radial.map=NULL;
    pilot_native_plane_release(pilot_radial.plane);pilot_radial.plane=NULL;
}
static void pilot_radial_close(void)
{
    pilot_radial_native_close();
    if(pilot_radial.sample)ID3D11ShaderResourceView_Release(pilot_radial.sample);
    if(pilot_radial.image)ID3D11Texture2D_Release(pilot_radial.image);
    rhi_shader_destroy(pilot_radial.pixel);memset(&pilot_radial,0,sizeof(pilot_radial));
}
/* Called after the original native copy, while its source is still coherent.
 * The private companion receives the actual enhanced scene, never a resize of
 * native pixels. Its map follows the private image's existing lifetime. */
static ID3D11ShaderResourceView *pilot_radial_native_snapshot(struct PilotNativePlane *scene,ID3D11Resource *source)
{
    if(!scene || !(scene->valid_aspects&1) || !scene->coherent || !scene->enhanced || !scene->rtv ||
       !scene->width || !scene->height || scene->native_width!=pilot_radial.width ||
       scene->native_height!=pilot_radial.height)return NULL;
    if(pilot_radial.plane && (pilot_radial.plane->width!=scene->width || pilot_radial.plane->height!=scene->height))
        pilot_radial_native_close();
    if(!pilot_radial.plane) {
        pilot_radial.plane=pilot_native_plane_create(pilot_radial.image,scene->width,scene->height);
        if(!pilot_radial.plane)return NULL;
        pilot_radial.map=pilot_native_map_create(pilot_radial.image,pilot_radial.plane);
        if(!pilot_radial.map){pilot_radial_native_close();return NULL;}
    }
    if(!pilot_native_copy_completed((ID3D11Resource*)pilot_radial.image,source))return NULL;
    ID3D11ShaderResourceView *sample=NULL;
    if(FAILED(pilot_native_plane_view(pilot_radial.plane,pilot_radial.sample,&sample)))return NULL;
    return sample;
}
int pilot_gfx_radial_blur(float strength)
{
    if(!pilot_device || pilot_color_target || !isfinite(strength) || strength<0 || strength>.06f)return 0;
    if(!strength)return 1;
    pilot_gfx_batch_flush();
    pilot_native_epoch_flush();
    ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
    ID3D11RenderTargetView *target=NULL;ID3D11Resource *source=NULL;
    ID3D11DeviceContext_OMGetRenderTargets(ctx,1,&target,NULL);
    if(!target)return 0;
    ID3D11RenderTargetView_GetResource(target,&source);
    if(source!=(ID3D11Resource*)pilot_surfaces[0]) {
        ID3D11Resource_Release(source);ID3D11RenderTargetView_Release(target);return 0;
    }
    D3D11_TEXTURE2D_DESC desc;ID3D11Texture2D_GetDesc(pilot_surfaces[0],&desc);
    if(desc.SampleDesc.Count!=1){ID3D11Resource_Release(source);ID3D11RenderTargetView_Release(target);return 0;}
    if(!pilot_radial.image || desc.Width!=pilot_radial.width || desc.Height!=pilot_radial.height || desc.Format!=pilot_radial.format) {
        pilot_radial_close();desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags=desc.MiscFlags=0;
        pilot_d3d_require(ID3D11Device_CreateTexture2D(d3d8_GetD3D11Device(),&desc,NULL,&pilot_radial.image),__FILE__,__LINE__);
        pilot_d3d_require(ID3D11Device_CreateShaderResourceView(d3d8_GetD3D11Device(),(ID3D11Resource*)pilot_radial.image,NULL,&pilot_radial.sample),__FILE__,__LINE__);
        pilot_radial.width=desc.Width;pilot_radial.height=desc.Height;pilot_radial.format=desc.Format;
    }
    if(!pilot_radial.pixel) {
        static const char code[]=
            "cbuffer Frame:register(b7){float2 texel;float strength;float unused;};"
            "Texture2D<float4> image:register(t9);SamplerState linear_clamp:register(s9);"
            "float4 pixel(float4 p:SV_Position):SV_Target{float2 uv=p.xy*texel,d=uv-.5;"
            /* Distance in units of image height; a circular clear center stays
             * circular at every aspect. Retain 50% sharp scene at the edges. */
            "float edge=smoothstep(.12,.65,length(d*float2(texel.y/texel.x,1)));"
            "float4 sharp=image.SampleLevel(linear_clamp,uv,0),sum=sharp;"
            "[unroll]for(int i=1;i<8;++i)sum+=image.SampleLevel(linear_clamp,uv-d*(strength*edge*i/7),0);"
            "return lerp(sharp,sum*.125,.5*edge);}";
        RhiShaderSource shader={0};shader.hlsl=code;shader.len=sizeof(code)-1;shader.name="native-radial-blur";
        shader.entry="pixel";shader.target="ps_4_0";char error[2048]={0};
        pilot_radial.pixel=rhi_shader_create(RHI_STAGE_PIXEL,&shader,error,sizeof(error));
        if(!pilot_radial.pixel){fprintf(stderr,"[RADIAL BLUR] %s\n",error);pilot_d3d_backend_stop("radial shader creation failed");}
    }
    pilot_visual_aa_prepare(desc.Width,desc.Height);
    struct PilotVisualState saved;pilot_visual_state_save(&saved);
    ID3D11DeviceContext_OMSetRenderTargets(ctx,0,NULL,NULL);
    ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource*)pilot_radial.image,source);
    struct PilotNativePlane *scene=pilot_native_enabled()?pilot_native_plane_find(source):NULL;
    ID3D11ShaderResourceView *enhanced=scene?pilot_radial_native_snapshot(scene,source):NULL;
    float constants[]={1.0f/desc.Width,1.0f/desc.Height,strength,0};
    void *mapped=rhi_buffer_map(pilot_visual_aa.constants,RHI_MAP_WRITE_DISCARD);
    if(!mapped)pilot_d3d_backend_stop("radial constants upload failed");
    memcpy(mapped,constants,sizeof(constants));rhi_buffer_unmap(pilot_visual_aa.constants);
    RhiViewport viewport={0,0,(float)desc.Width,(float)desc.Height,0,1};float factor[]={1,1,1,1};
    ID3D11DeviceContext_OMSetRenderTargets(ctx,1,&target,NULL);
    rhi_set_viewports(1,&viewport);rhi_set_vertex_layout(NULL);rhi_set_topology(RHI_TOPOLOGY_TRIANGLES);
    rhi_set_shader(RHI_STAGE_VERTEX,pilot_visual_aa.vertex);rhi_set_shader(RHI_STAGE_PIXEL,pilot_radial.pixel);
    rhi_set_uniform_buffers(RHI_STAGE_PIXEL,7,1,&pilot_visual_aa.constants);
    ID3D11DeviceContext_PSSetShaderResources(ctx,9,1,&pilot_radial.sample);
    rhi_set_samplers(9,1,&pilot_visual_aa.sampler);
    rhi_set_blend_state(pilot_visual_aa.blend,factor,0xffffffff);rhi_set_depth_state(pilot_visual_aa.depth,0);
    rhi_set_raster_state(pilot_visual_aa.raster);rhi_draw(3,0);
    if(pilot_native_enabled()) {
        /* Native pixels have changed even if an optional companion step fails.
         * Only the completed full HD pass can publish color coherence again. */
        if(scene){scene->valid_aspects&=~1u;scene->coherent=0;}
        ID3D11Query *excluded=NULL;int completed=0;
        if(enhanced) {
            float hd[]={1.0f/scene->width,1.0f/scene->height,strength,0};
            mapped=rhi_buffer_map(pilot_visual_aa.constants,RHI_MAP_WRITE_DISCARD);
            if(mapped) {
                memcpy(mapped,hd,sizeof(hd));rhi_buffer_unmap(pilot_visual_aa.constants);
                if(pilot_visibility_internal_try_begin(&excluded)) {
                    RhiViewport hd_viewport={0,0,(float)scene->width,(float)scene->height,0,1};
                    ID3D11DeviceContext_OMSetRenderTargets(ctx,1,&scene->rtv,NULL);
                    ID3D11DeviceContext_PSSetShaderResources(ctx,9,1,&enhanced);
                    rhi_set_viewports(1,&hd_viewport);rhi_draw(3,0);
                    pilot_visibility_internal_end(excluded);completed=1;
                }
            }
        }
        if(completed){scene->valid_aspects|=1;scene->coherent=1;}
        else pilot_native_degrade();
    }
    pilot_visual_state_restore(&saved);
    if(enhanced)ID3D11ShaderResourceView_Release(enhanced);
    ID3D11Resource_Release(source);ID3D11RenderTargetView_Release(target);return 1;
}
