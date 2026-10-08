/* Independently implemented color broadcast after the borrowed vertex stage.
 * Original Xbox XDK initialization selects FIRST provoking vertex. Triangles
 * and strips use their leading vertex; NV2A quads use their fourth vertex.
 * Texture coordinates, fog, depth and perspective W retain interpolation.
 * Cached shaders run only for flat draws; no CPU vertex expansion/readback. */
#include <d3dcompiler.h>
static ID3D11GeometryShader *pilot_flat_shaders[2];
static const char pilot_flat_source[]=
    "struct V { float4 pos:SV_POSITION; float4 diffuse:COLOR0; float4 specular:COLOR1;"
    "float4 tex0:TEXCOORD0; float4 tex1:TEXCOORD1; float4 tex2:TEXCOORD2; float4 tex3:TEXCOORD3;"
    "float fog:TEXCOORD4; float4 viewpos:TEXCOORD5; };\n"
    "[maxvertexcount(3)] void triangle_main(triangle V v[3],inout TriangleStream<V> stream) {"
    "[unroll] for(uint i=0;i<3;++i) { V o=v[i]; o.diffuse=v[0].diffuse; o.specular=v[0].specular; stream.Append(o); } }\n"
    "[maxvertexcount(4)] void quad_main(lineadj V v[4],inout TriangleStream<V> stream) {"
    "uint order[4]={1,2,0,3}; [unroll] for(uint i=0;i<4;++i) { V o=v[order[i]];"
    "o.diffuse=v[3].diffuse; o.specular=v[3].specular; stream.Append(o); } }\n";
static void pilot_flat_close(void)
{
    if(d3d8_GetD3D11Context())ID3D11DeviceContext_GSSetShader(d3d8_GetD3D11Context(),NULL,NULL,0);
    for(unsigned i=0;i<2;++i)if(pilot_flat_shaders[i]) {
        ID3D11GeometryShader_Release(pilot_flat_shaders[i]);pilot_flat_shaders[i]=NULL;
    }
}
static HRESULT pilot_flat_prepare(unsigned primitive)
{
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    if(d3d8_GetRenderStates()[D3DRS_SHADEMODE]!=1) {
        ID3D11DeviceContext_GSSetShader(context,NULL,NULL,0);return S_OK;
    }
    if(primitive!=5 && primitive!=6 && primitive!=8)return E_NOTIMPL;
    unsigned index=primitive==8;
    if(!pilot_flat_shaders[index]) {
        ID3DBlob *code=NULL,*errors=NULL;
        HRESULT hr=D3DCompile(pilot_flat_source,sizeof(pilot_flat_source)-1,"xlle-flat-colors",NULL,NULL,
            index?"quad_main":"triangle_main","gs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(FAILED(hr))fprintf(stderr,"[FLAT COMPILE] %s\n",errors?(const char*)ID3D10Blob_GetBufferPointer(errors):"no diagnostic");
        if(errors)ID3D10Blob_Release(errors);
        if(FAILED(hr))return hr;
        hr=ID3D11Device_CreateGeometryShader(d3d8_GetD3D11Device(),ID3D10Blob_GetBufferPointer(code),
            ID3D10Blob_GetBufferSize(code),NULL,&pilot_flat_shaders[index]);
        ID3D10Blob_Release(code);if(FAILED(hr))return hr;
    }
    ID3D11DeviceContext_GSSetShader(context,pilot_flat_shaders[index],NULL,0);
    return S_OK;
}
