/* Decode latched float attributes through the same fixed-function vertex core
 * as ordinary FVF draws. Float colors stay float until the raster pipeline;
 * packing them into UBYTE4 here would change interpolation and rounding. */
int32_t pilot_gfx_immediate_fvf(uint32_t fvf)
{
#ifdef PILOT_D3D_PHOBOS
    if((fvf&15)!=4)return E_NOTIMPL;
    struct PilotVertexElement original[16],elements[16]={0};
    pilot_gfx_fvf_layout(fvf,original);
    unsigned slot;
    for(slot=0;slot<256;++slot)if(native_shaders[slot].object && native_shaders[slot].immediate_fvf==fvf)break;
    if(slot==256) {
        for(slot=0;slot<256 && native_shaders[slot].object;++slot){}
        if(slot==256)return E_OUTOFMEMORY;
        const char *pilot_d3d_fixed_vertex_source(void);
        const char *core=pilot_d3d_fixed_vertex_source();if(!core)return E_FAIL;
        struct ShaderText text={0};text.capacity=strlen(core)+8192;text.data=malloc(text.capacity);
        if(!text.data)return E_OUTOFMEMORY;
        unsigned used=0;
        shader_text(&text,"#define main xlle_fixed_main\n%s\n#undef main\nstruct RAW_IN {\n",core);
        for(unsigned reg=0;reg<16;++reg)if(original[reg].type) {
            used|=1u<<reg;elements[reg]=(struct PilotVertexElement){0x42,0,reg*16};
            shader_text(&text,"float4 v%u:ATTR%u;\n",reg,reg);
        }
        shader_text(&text,"};\nVS_OUT raster_main(RAW_IN r) { VS_IN v=(VS_IN)0;\n"
            "v.pos=r.v0;v.normal=float3(0,0,1);v.tex0=v.tex1=v.tex2=v.tex3=float4(0,0,0,1);\n");
        if(original[3].type)shader_text(&text,"v.diffuse=r.v3;\n");
        if(original[4].type)shader_text(&text,"v.specular=r.v4;\n");
        for(unsigned stage=0;stage<4;++stage)if(original[stage+9].type) {
            unsigned lanes=original[stage+9].type>>4;
            shader_text(&text,"v.tex%u.%s=r.v%u.%s;\n",stage,
                lanes==1?"x":lanes==2?"xy":lanes==3?"xyz":"xyzw",stage+9,
                lanes==1?"x":lanes==2?"xy":lanes==3?"xyz":"xyzw");
        }
        shader_text(&text,"return xlle_fixed_main(v);}\n");
        struct NativeShader shader={0};shader.used=used;shader.fixed_fvf=fvf;shader.immediate_fvf=fvf;
        HRESULT result=native_shader_compile(&shader,text.data,elements);if(FAILED(result))return result;
        native_shaders[slot]=shader;
    }
    struct NativeShader *shader=&native_shaders[slot];
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_VSSetShader(context,shader->object,NULL,0);
    ID3D11DeviceContext_IASetInputLayout(context,shader->layout);
    native_shader_bound=slot+1;return S_OK;
#else
    (void)fvf;return E_NOTIMPL;
#endif
}
