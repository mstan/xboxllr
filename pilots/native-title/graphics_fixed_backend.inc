/* Feed typed original declarations into the same fixed-function VS core used
 * by the renderer. The wrapper changes input decoding, not transform/lighting
 * equations. No CPU vertex expansion or interpreter is involved. */
int32_t pilot_gfx_shader_create_fixed(const struct PilotVertexElement elements[16],uint32_t *handle)
{
#ifdef PILOT_D3D_PHOBOS
    if(!elements || !handle || !d3d8_GetD3D11Device())return E_INVALIDARG;
    if(elements[0].type!=0x32)return E_NOTIMPL;
    uint32_t fvf=2,used=0;unsigned textures=0;
    for(unsigned i=0;i<16;++i)if(elements[i].type) {
        unsigned type=elements[i].type;
        if(elements[i].stream>=16 || elements[i].offset>2048)return E_INVALIDARG;
        if(i==0) {}
        else if(i==2) {if(type!=0x16 && type!=0x32)return E_NOTIMPL;fvf|=0x10;}
        else if(i==3 || i==4) {if(type!=0x40)return E_NOTIMPL;fvf|=0x40u<<(i-3);}
        else if(i>=9 && i<=12) {
            if(type!=0x12 && type!=0x22 && type!=0x32 && type!=0x42)return E_NOTIMPL;
            unsigned stage=i-9,lanes=type>>4;
            if(stage!=textures)return E_NOTIMPL;
            ++textures;fvf|=((lanes+2)&3)<<(16+stage*2);
        } else return E_NOTIMPL;
        used|=1u<<i;
    }
    fvf|=textures<<8;
    unsigned slot;for(slot=0;slot<256 && native_shaders[slot].object;++slot){}
    if(slot==256)return E_OUTOFMEMORY;
    const char *pilot_d3d_fixed_vertex_source(void);
    const char *core=pilot_d3d_fixed_vertex_source();if(!core)return E_FAIL;
    struct ShaderText text={0};text.capacity=strlen(core)+32768;text.data=malloc(text.capacity);
    if(!text.data)return E_OUTOFMEMORY;
    shader_text(&text,"#define main xlle_fixed_main\n%s\n#undef main\nstruct RAW_IN {\n",core);
    for(unsigned i=0;i<16;++i)if(used&(1u<<i))
        shader_text(&text,"%s v%u:ATTR%u;\n",shader_format(elements[i].type)->hlsl,i,i);
    shader_text(&text,"};\nVS_OUT raster_main(RAW_IN r) { VS_IN v=(VS_IN)0;\n"
        "v.pos=float4(r.v0,1);v.normal=float3(0,0,1);"
        "v.tex0=v.tex1=v.tex2=v.tex3=float4(0,0,0,1);\n");
    if(elements[2].type==0x16)
        shader_text(&text,"v.normal=max(float3(float(int(r.v2<<21)>>21)/1023.0,"
            "float(int(r.v2<<10)>>21)/1023.0,float(int(r.v2)>>22)/511.0),-1.0);\n");
    else if(elements[2].type)shader_text(&text,"v.normal=r.v2;\n");
    if(elements[3].type)shader_text(&text,"v.diffuse=r.v3;\n");
    if(elements[4].type)shader_text(&text,"v.specular=r.v4;\n");
    for(unsigned i=0;i<textures;++i) {
        unsigned lanes=elements[i+9].type>>4;
        shader_text(&text,"v.tex%u.%s=r.v%u;\n",i,lanes==1?"x":lanes==2?"xy":lanes==3?"xyz":"xyzw",i+9);
    }
    shader_text(&text,"return xlle_fixed_main(v);}\n");
    struct NativeShader shader={0};shader.used=used;shader.fixed_fvf=fvf;
    HRESULT result=native_shader_compile(&shader,text.data,elements);if(FAILED(result))return result;
    native_shaders[slot]=shader;*handle=slot+1;
    fprintf(stderr,"[FIXED SHADER CREATE] native=%u fvf=%08x inputs=%04x\n",*handle,fvf,used);
    return S_OK;
#else
    (void)elements;(void)handle;return E_NOTIMPL;
#endif
}
int32_t pilot_gfx_shader_prepare_fixed(uint32_t handle)
{
    struct NativeShader *shader=native_shader(handle);if(!shader->fixed_fvf)return E_INVALIDARG;
    void pilot_d3d_fvf_prepare(UINT,const float[4],const float[4]);
    const float scale[4]={1,1,1,0},offset[4]={0};
    pilot_gfx_shader_fixed_binding();pilot_d3d_program_fog_mode(0);
    pilot_d3d_fvf_prepare(shader->fixed_fvf,scale,offset);
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_VSSetShader(context,shader->object,NULL,0);
    ID3D11DeviceContext_IASetInputLayout(context,shader->layout);
    native_shader_bound=handle;return S_OK;
}
