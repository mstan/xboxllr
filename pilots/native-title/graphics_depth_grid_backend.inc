/* A standalone depth allocation has no inherent scene/lookup role. Match the
 * actual color target's raster grid at bind time, retaining guest dimensions.
 * Reuse the qualified D24S8 upload/download boundary on a grid transition;
 * ordinary rebinds neither allocate nor copy. Objects/aliases keep their identity.
 * All calls run under the graphics ownership lock, after flushing old draws. */
static HRESULT pilot_depth_match_color(uint32_t handle,ID3D11Texture2D *color,unsigned width,unsigned height)
{
    if(!pilot_textures[handle-1].standalone_depth)return S_OK;
    D3D8Texture *texture=pilot_texture(handle);
    D3D11_TEXTURE2D_DESC c,d;
    ID3D11Texture2D_GetDesc(color,&c);ID3D11Texture2D_GetDesc(pilot_donor_texture(texture),&d);
    uint64_t w=((uint64_t)c.Width*texture->width+width/2)/width;
    uint64_t h=((uint64_t)c.Height*texture->height+height/2)/height;
    if(!w || !h || w>16384 || h>16384)return E_INVALIDARG;
    if(d.Width==w && d.Height==h)return S_OK;
#ifdef PILOT_D3D_PHOBOS
    unsigned logical_width=texture->width,logical_height=texture->height;
    unsigned pitch=(logical_width*4+63)&~63u,bytes=pitch*logical_height;
    unsigned uploaded=pilot_textures[handle-1].uploaded,gpu_written=pilot_textures[handle-1].gpu_written;
    unsigned char *pixels=NULL;
    if(uploaded) {
        pixels=calloc(1,bytes);if(!pixels)return E_OUTOFMEMORY;
        for(unsigned y=0;y<logical_height;++y)
            memcpy(pixels+(size_t)y*pitch,texture->sys_mem+(size_t)y*logical_width*4,logical_width*4);
        if(gpu_written)pilot_gfx_texture_download(handle,pixels,bytes);
    }
    uint32_t size[]={(uint32_t)w,(uint32_t)h},temporary=0;
    HRESULT hr=pilot_texture_create_policy(logical_width,logical_height,1,0x2e,2,size,&temporary);
    if(FAILED(hr)){free(pixels);return hr;}
    if(uploaded)pilot_gfx_texture_upload(temporary,0,pixels,bytes);
    free(pixels);
    /* Creation may grow the slot array. Reacquire pointers; retain the original
     * D3D object, its reference count and shared completion owner. */
    struct PilotNativeTexture *owner=&pilot_textures[handle-1],*next=&pilot_textures[temporary-1];
    texture=pilot_texture(handle);D3D8Texture *replacement=pilot_texture(temporary);
    RhiImage *image=texture->image;texture->image=replacement->image;replacement->image=image;
    RhiView *view=texture->srv;texture->srv=replacement->srv;replacement->srv=view;
    BYTE *cpu=texture->sys_mem;texture->sys_mem=replacement->sys_mem;replacement->sys_mem=cpu;
    ID3D11DepthStencilView *dsv=owner->dsv;owner->dsv=next->dsv;next->dsv=dsv;
    uint64_t serial=next->use->serial;
    if(serial>owner->use->serial)owner->use->serial=serial;
    owner->use->write_serial=owner->content_serial=serial;
    /* A GPU-produced surface is still newer than guest RAM after this private
     * transfer. A later guest lock must download it, even though upload was
     * the implementation used to migrate it to its new raster. */
    owner->uploaded=uploaded;owner->gpu_written=gpu_written;
    for(unsigned i=0;i<pilot_texture_capacity;++i) {
        struct PilotNativeTexture *alias=&pilot_textures[i];
        if(!alias->object || alias->use!=owner->use)continue;
        D3D8Texture *object=(D3D8Texture*)alias->object;
        if(i!=handle-1) {
            RhiImage *shared=rhi_image_retain(texture->image);
            RhiView *sample=rhi_view_create(shared,RHI_VIEW_SAMPLED,NULL);
            if(!sample)pilot_d3d_backend_stop("depth raster alias view allocation failed");
            rhi_view_destroy(object->srv);rhi_image_destroy(object->image);
            object->image=shared;object->srv=sample;
            alias->uploaded=uploaded;alias->gpu_written=gpu_written;alias->content_serial=serial;
        }
        if(alias->base_srv){ID3D11ShaderResourceView_Release(alias->base_srv);alias->base_srv=NULL;}
        pilot_depth_texture_destroy(alias);
    }
    pilot_sampling_views_dirty=15;
    /* Clear the donor's old output pointers before releasing their owner. The
     * caller installs the new color/depth pair immediately after this return. */
    ID3D11DeviceContext_OMSetRenderTargets(d3d8_GetD3D11Context(),0,NULL,NULL);
    pilot_donor_output_images(NULL,NULL,1);
    pilot_gfx_texture_destroy(temporary);
    fprintf(stderr,"[DEPTH RASTER TRANSITION] handle=%08x guest=%ux%u old=%ux%u new=%ux%u preserved=%u\n",
        handle,logical_width,logical_height,d.Width,d.Height,(unsigned)w,(unsigned)h,uploaded!=0);
    return S_OK;
#else
    return E_NOTIMPL;
#endif
}
