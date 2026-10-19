/* Independently written presentation-buffer adapter, appended to the private
 * pinned Phobos device translation unit by native_phobos_d3d.py. Uses its real
 * RHI allocation helpers; does not enable the donor's unimplemented Reset.
 * Original Xbox Reset retains the device and non-presentation resources. */
static void pilot_donor_display_free(D3D8DeviceState *s)
{
    if(s->depth_view)rhi_view_destroy(s->depth_view);
    if(s->depth_image)rhi_image_destroy(s->depth_image);
    if(s->rhi_scene_srv)rhi_view_destroy(s->rhi_scene_srv);
    if(s->rhi_default_rtv)rhi_view_destroy(s->rhi_default_rtv);
    if(s->rhi_scene_image)rhi_image_destroy(s->rhi_scene_image);
}
HRESULT pilot_donor_reset_display(UINT width,UINT height,DWORD format)
{
    D3D8DeviceState *staged=calloc(1,sizeof(*staged));
    if(!staged)return E_OUTOFMEMORY;
    d3d8_display_scene_size(width,height,&staged->width,&staged->height);
    HRESULT hr=create_render_targets(staged);
    D3D8Surface *depth=NULL;
    if(SUCCEEDED(hr) && pilot_d3d_has_auto_depth()) {
        depth=(D3D8Surface*)d3d8_surface_create(staged->depth_image,0,0,
            staged->width,staged->height,D3DFMT_D24S8,D3DPOOL_DEFAULT,
            D3DUSAGE_DEPTHSTENCIL,D3DMULTISAMPLE_NONE,NULL,0);
        if(!depth || !depth->dsv)hr=E_OUTOFMEMORY;
    }
    if(FAILED(hr)) {
        if(depth)IDirect3DSurface8_Release(&depth->iface);
        pilot_donor_display_free(staged);free(staged);
        /* The caller restores its exact previous native target bindings. */
        return hr;
    }
    if(g_cur_rt)IDirect3DSurface8_Release(&g_cur_rt->iface);
    if(g_cur_ds)IDirect3DSurface8_Release(&g_cur_ds->iface);
    if(g_auto_ds)IDirect3DSurface8_Release(&g_auto_ds->iface);
    g_cur_rt=NULL;g_auto_ds=g_cur_ds=depth;
    if(depth)IDirect3DSurface8_AddRef(&depth->iface);
    pilot_donor_display_free(&g_device_state);
    g_device_state.depth_image=staged->depth_image;
    g_device_state.depth_view=staged->depth_view;
    g_device_state.rhi_scene_image=staged->rhi_scene_image;
    g_device_state.rhi_default_rtv=staged->rhi_default_rtv;
    g_device_state.rhi_scene_srv=staged->rhi_scene_srv;
    g_device_state.width=staged->width;g_device_state.height=staged->height;
    g_device_state.present_width=width;g_device_state.present_height=height;
    g_device_state.backbuffer_format=(D3DFORMAT)format;
    g_guest_width=width;g_guest_height=height;
    rhi_set_render_target(g_device_state.rhi_default_rtv,depth?depth->dsv:NULL);
    free(staged);
    return S_OK;
}
