/* Retain the prepared screen/RHW decoder and pixel fog consumer. Xbox fixed
 * processing chooses projected Z for an affine projection, clip W otherwise;
 * NONE uses supplied specular alpha. This follows Cxbx's fixed-function depth
 * selection, with per-vertex factors and exceptional distance behavior from
 * xemu pgraph/glsl/vsh.c. Exact NV2A intermediate precision remains deferred.
 * Shared renderer attribution and retained terms are in THIRD_PARTY_ATTRIBUTION.
 */
static ID3D11Buffer *native_pretransformed_fog_buffer;
static float native_pretransformed_fog_values[8];
static void native_pretransformed_fog_close(void)
{
    if(native_pretransformed_fog_buffer) {
        ID3D11Buffer *none=NULL;
        ID3D11DeviceContext_VSSetConstantBuffers(d3d8_GetD3D11Context(),5,1,&none);
        ID3D11Buffer_Release(native_pretransformed_fog_buffer);native_pretransformed_fog_buffer=NULL;
    }
    memset(native_pretransformed_fog_values,0,sizeof(native_pretransformed_fog_values));
}
int32_t pilot_gfx_fvf_fog(uint32_t fvf,const float projection[16],int immediate)
{
#ifdef PILOT_D3D_PHOBOS
    if((fvf&15)!=4 || !projection || (immediate!=0 && immediate!=1) || !d3d8_GetD3D11Device())return E_INVALIDARG;
    const DWORD *rs=d3d8_GetRenderStates();
    if(!rs[D3DRS_FOGENABLE])return E_INVALIDARG;
    unsigned mode=rs[D3DRS_FOGTABLEMODE];
    if(mode>3 || rs[D3DRS_RANGEFOGENABLE])return E_NOTIMPL;
    float values[8]={(float)mode};
    memcpy(values+1,rs+D3DRS_FOGSTART,4);memcpy(values+2,rs+D3DRS_FOGEND,4);
    memcpy(values+3,rs+D3DRS_FOGDENSITY,4);
    values[4]=projection[3]==0 && projection[7]==0 && projection[11]==0 && projection[15]==1;
    if(!native_lighting_finite(values,8) || !native_lighting_finite(projection,16))return E_INVALIDARG;
    struct PilotVertexElement original[16],elements[16];pilot_gfx_fvf_layout(fvf,original);
    memcpy(elements,original,sizeof(elements));
    if(immediate)for(unsigned i=0;i<16;++i)if(elements[i].type)
        elements[i]=(struct PilotVertexElement){0x42,0,i*16};
    unsigned slot;
    for(slot=0;slot<256;++slot)if(native_shaders[slot].object && native_shaders[slot].fog_fvf==fvf &&
        native_shaders[slot].fog_immediate==(unsigned)immediate)break;
    if(slot==256) {
        for(slot=0;slot<256 && native_shaders[slot].object;++slot){}
        if(slot==256)return E_OUTOFMEMORY;
        const char *pilot_d3d_fixed_vertex_source(void);
        const char *core=pilot_d3d_fixed_vertex_source();if(!core)return E_FAIL;
        struct ShaderText text={0};text.capacity=strlen(core)+8192;text.data=malloc(text.capacity);
        if(!text.data)return E_OUTOFMEMORY;
        shader_text(&text,"#define main xlle_fixed_main\n%s\n#undef main\n"
            "cbuffer ScreenFog:register(b5){float4 ScreenFogParams;float4 ScreenFogBasis;};\n"
            "cbuffer CurrentInputs:register(b3){float4 CurrentInput[16];};\n"
            "struct RAW_IN {\n",core);
        unsigned used=0;
        for(unsigned i=0;i<16;++i)if(elements[i].type) {
            used|=1u<<i;shader_text(&text,"%s v%u:ATTR%u;\n",shader_format(elements[i].type)->hlsl,i,i);
        }
        shader_text(&text,"};\nVS_OUT raster_main(RAW_IN r){VS_IN v=(VS_IN)0;v.pos=r.v0;"
            "v.tex0=v.tex1=v.tex2=v.tex3=float4(0,0,0,1);\n");
        if(elements[3].type)shader_text(&text,"v.diffuse=r.v3;\n");
        if(elements[4].type)shader_text(&text,"v.specular=r.v4;\n");
        for(unsigned stage=0;stage<4;++stage)if(elements[stage+9].type) {
            unsigned lanes=original[stage+9].type>>4;
            const char *swizzle=lanes==1?"x":lanes==2?"xy":lanes==3?"xyz":"xyzw";
            shader_text(&text,"v.tex%u.%s=r.v%u%s%s;\n",stage,swizzle,stage+9,immediate?".":"",immediate?swizzle:"");
        }
        shader_text(&text,"VS_OUT o=xlle_fixed_main(v);uint mode=(uint)ScreenFogParams.x;"
            "float d=ScreenFogBasis.x!=0?v.pos.z:1/v.pos.w;float f;\n"
            "if(mode==0)f=%s;else if(isinf(d))f=1;"
            "else if(mode==3)f=(ScreenFogParams.z-d)/(ScreenFogParams.z-ScreenFogParams.y);"
            "else {float q=ScreenFogParams.w*d;f=exp(mode==2?-q*q:-q);}\n"
            "o.fog=isnan(f)?1:f;return o;}\n",elements[4].type?"v.specular.a":"CurrentInput[4].w");
        struct NativeShader shader={0};shader.used=used;shader.fixed_fvf=fvf;shader.fog_fvf=fvf;shader.fog_immediate=immediate;
        HRESULT result=native_shader_compile(&shader,text.data,elements);if(FAILED(result))return result;
        native_shaders[slot]=shader;
    }
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();HRESULT result;
    if(!native_pretransformed_fog_buffer) {
        D3D11_BUFFER_DESC desc={0};desc.ByteWidth=sizeof(values);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data={values,0,0};
        result=ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,&data,&native_pretransformed_fog_buffer);
        if(FAILED(result))return result;
    } else if(memcmp(values,native_pretransformed_fog_values,sizeof(values)))
        ID3D11DeviceContext_UpdateSubresource(context,(ID3D11Resource*)native_pretransformed_fog_buffer,0,NULL,values,0,0);
    memcpy(native_pretransformed_fog_values,values,sizeof(values));
    if(!mode && !elements[4].type){result=native_current_flush();if(FAILED(result))return result;}
    ID3D11DeviceContext_VSSetConstantBuffers(context,5,1,&native_pretransformed_fog_buffer);
    pilot_d3d_program_fog_mode(1);
    ID3D11DeviceContext_VSSetShader(context,native_shaders[slot].object,NULL,0);
    ID3D11DeviceContext_IASetInputLayout(context,native_shaders[slot].layout);
    native_shader_bound=slot+1;return S_OK;
#else
    (void)fvf;(void)projection;(void)immediate;return E_NOTIMPL;
#endif
}
