/* The original native CPU API retains light allocation/list/material behavior.
 * Inline stores are visible because every active fixed draw takes its snapshot
 * from that same authoritative cache. No guest dirty bits are consumed. */
static void graphics_lighting_sync(uint32_t shader)
{
#ifdef PILOT_D3D_LIGHTING_CACHE_VERSION
    if(graphics_render_read(141))stop("unqualified two-sided fixed-function lighting",shader);
    struct PilotLightingSnapshot snapshot={0};
    const struct PilotLightingLayout layout={PILOT_D3D_LIGHTING_TABLE,PILOT_D3D_LIGHTING_CAPACITY,
        PILOT_D3D_LIGHTING_HEAD,PILOT_D3D_LIGHTING_MATERIAL,PILOT_D3D_LIGHTING_STRIDE,
        PILOT_D3D_LIGHTING_FLAGS,PILOT_D3D_LIGHTING_NEXT};
    int result=pilot_lighting_cache(memory,UINT64_C(0x100000000),PILOT_D3D_DEVICE_OBJECT,&layout,&snapshot);
    if(result!=PILOT_LIGHTING_CACHE_OK)stop("invalid original fixed-function light cache",(uint32_t)result);
    snapshot.ambient=graphics_render_read(115);
    snapshot.color_vertex=graphics_render_read(PILOT_D3D_LIGHTING_COLOR_VERTEX_STATE);
    snapshot.local_viewer=graphics_render_read(PILOT_D3D_LIGHTING_LOCAL_VIEWER_STATE);
    snapshot.specular_enable=graphics_render_read(103);
    snapshot.normalize=graphics_render_read(142);
    /* Canonical semantic order: front diffuse/specular/ambient/emissive. */
    snapshot.sources[0]=graphics_render_read(111);snapshot.sources[1]=graphics_render_read(110);
    snapshot.sources[2]=graphics_render_read(112);snapshot.sources[3]=graphics_render_read(113);
    memcpy(snapshot.view,memory+TRANSFORM_STATE(TRANSFORMS),sizeof(snapshot.view));
    if(pilot_gfx_lighting_publish(&snapshot)<0)stop("unqualified original fixed-function lighting snapshot",shader);
#else
    stop("unqualified original fixed-function lighting owner",shader);
#endif
}
