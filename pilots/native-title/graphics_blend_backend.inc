#ifdef PILOT_D3D_PHOBOS
static int pilot_blend_profile(void)
{
    if(!pilot_blend_default_shape || pilot_color_target || pilot_logic_copy || pilot_blend_noise || pilot_gfx_feedback_mask())return 0;
    /* feedback_mask compares underlying color/depth allocations, including
     * distinct wrappers. A snapshot correction cannot sample either output. */
    if(!pilot_depth_target)return 0;
    if(pilot_depth_target==UINT32_MAX) {if(!pilot_blend_default_depth_shape)return 0;}
    else {
        D3D8Texture *depth=pilot_texture(pilot_depth_target);
        if(!pilot_textures[pilot_depth_target-1].blend_depth_shape || depth->width<640 || depth->height<480)return 0;
    }
    const DWORD *rs=d3d8_GetRenderStates();
    if(rs[D3DRS_ALPHABLENDENABLE]!=1 || rs[D3DRS_ALPHATESTENABLE] ||
       rs[D3DRS_SRCBLEND]!=D3DBLEND_SRCALPHA || rs[D3DRS_DESTBLEND]!=D3DBLEND_INVSRCALPHA ||
       rs[D3DRS_BLENDOP]!=1 || rs[D3DRS_COLORWRITEENABLE]!=15 || rs[D3DRS_SHADEMODE]!=2 ||
       rs[D3DRS_FILLMODE]!=D3DFILL_SOLID || rs[D3DRS_CULLMODE]!=D3DCULL_NONE ||
       rs[D3DRS_FOGENABLE] || rs[D3DRS_EDGEANTIALIAS] || rs[D3DRS_ZENABLE] || rs[D3DRS_ZWRITEENABLE] ||
       rs[D3DRS_STENCILENABLE]!=1 || rs[D3DRS_STENCILFUNC]!=D3DCMP_ALWAYS ||
       rs[D3DRS_STENCILFAIL]!=1 || rs[D3DRS_STENCILZFAIL]!=1 || rs[D3DRS_STENCILPASS]!=3 ||
       (rs[D3DRS_STENCILREF]&255) || (rs[D3DRS_STENCILMASK]&255)!=0xf0 ||
       (rs[D3DRS_STENCILWRITEMASK]&255)!=0xf0)return 0;
    ID3D11Texture2D *storage=NULL;
    for(unsigned stage=0;stage<4;++stage) {
        uint32_t handle=pilot_bound_textures[stage];
        if(!handle || pilot_sampling_formats[stage]!=0x12)return 0;
        D3D8Texture *texture=pilot_texture(handle);
        if(texture->levels!=1 ||
           (texture->d3d8_format==0x2e ? !pilot_textures[handle-1].blend_depth_shape :
            texture->d3d8_format!=0x12 || !pilot_textures[handle-1].blend_color_shape))return 0;
        ID3D11Texture2D *image=pilot_donor_texture(texture);
        if(stage && image!=storage)return 0;
        storage=image;
        const DWORD *tss=d3d8_GetTSS(stage);float bias=-1.2f;uint32_t bias_bits;memcpy(&bias_bits,&bias,4);
        /* Extension zero maps to WRAP in the donor; the 2D shader consumes no
         * W address. Original retained UP replays leave this extension zero. */
        if(tss[D3DTSS_ADDRESSU]!=3 || tss[D3DTSS_ADDRESSV]!=3 || tss[31]>1 ||
           tss[D3DTSS_MAGFILTER]!=2 || tss[D3DTSS_MINFILTER]!=2 ||
           tss[D3DTSS_MIPFILTER]!=(stage<2?2u:0u) ||
           tss[D3DTSS_MIPMAPLODBIAS]!=(stage<2?bias_bits:0u) ||
           tss[D3DTSS_MAXMIPLEVEL] || tss[D3DTSS_MAXANISOTROPY]!=1)return 0;
        /* The proved programmable VS supplies each PROJECT2D coordinate
         * directly. TEXCOORDINDEX is consumed only by the fixed-function VS. */
    }
    return 1;
}
static int pilot_blend_geometry(unsigned primitive,unsigned first,unsigned count)
{
    /* UP overrides persistent stream bookkeeping even when its proof fails. */
    if(pilot_blend_transient)return pilot_blend_transient_proof;
    uint32_t handle=pilot_bound_vertices[0],stride=pilot_bound_strides[0];
    if(!handle || !stride)return 0;
    D3D8VertexBuffer *vertex=pilot_vertex(handle);
    if(!pilot_vertices[handle-1].uploaded)return 0;
    uint64_t offset=(uint64_t)first*stride,bytes=(uint64_t)count*stride;
    if(!vertex->sys_mem || offset>vertex->size || bytes>vertex->size-offset)return 0;
    return pilot_gfx_shader_blend_rectangle(vertex->sys_mem+(size_t)offset,
        vertex->size-(uint32_t)offset,stride,primitive,count);
}
#endif
int pilot_gfx_blend_candidate_geometry(unsigned primitive,unsigned first,unsigned count)
{
#ifdef PILOT_D3D_PHOBOS
    return pilot_blend_profile() && pilot_blend_geometry(primitive,first,count);
#else
    (void)primitive;(void)first;(void)count;return 0;
#endif
}
void pilot_gfx_blend_stats(struct PilotBlendStats *stats)
{
    memset(stats,0,sizeof(*stats));
#ifdef PILOT_D3D_PHOBOS
    stats->draws=pilot_blend_draws;
    pilot_donor_blend_destination_stats(&stats->copies,&stats->allocations);
    pilot_donor_blend_shader_stats(&stats->shader_compilations,&stats->original_compilations);
#endif
}
