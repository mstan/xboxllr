/* Beta slots remain part of the original stride when vertex blending is
 * explicitly disabled. The existing fixed transform then consumes position,
 * normal/colors/coordinates and leaves those weight bytes untouched. */
int32_t pilot_gfx_fvf_prepare_unblended(uint32_t fvf)
{
#ifdef PILOT_D3D_PHOBOS
    unsigned position=fvf&15;
    if(position<6 || position>12 || (position&1))return E_INVALIDARG;
    struct PilotInitializerState state;
    if(!pilot_gfx_initializer_snapshot(&state) || !(state.controls&4) || state.vertex_blend)return E_NOTIMPL;
    struct PilotVertexElement elements[16];pilot_gfx_fvf_float_beta_layout(fvf,elements);
    /* Removing this unused input preserves the following original offsets. */
    elements[1]=(struct PilotVertexElement){0};
    for(unsigned i=0;i<256;++i)if(native_shaders[i].object && native_shaders[i].source_fvf==fvf)
        return pilot_gfx_shader_prepare_fixed(i+1);
    uint32_t handle=0;HRESULT result=pilot_gfx_shader_create_fixed(elements,&handle);
    if(FAILED(result))return result;
    native_shader(handle)->source_fvf=fvf;
    return pilot_gfx_shader_prepare_fixed(handle);
#else
    (void)fvf;return E_NOTIMPL;
#endif
}
