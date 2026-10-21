#include <math.h>
/* Fixed-function point rasterization after the real vertex transform. The
 * original SDK converts POINTSIZE/attenuation and viewport height to NV2A
 * point parameters; invert that algebra directly, without expanding vertices
 * on the CPU. Xbox sprite coordinates replace texture set 3 (not sets 0-2).
 * Per-vertex size and programmable point outputs remain separately guarded. */
static struct {
    float size,minimum,maximum,a,b,c;
    unsigned sprite,scale,screen,expanded;
} pilot_point;
static ID3D11GeometryShader *pilot_point_shader;
static ID3D11Buffer *pilot_point_buffer;
static ID3D11RasterizerState *pilot_point_raster;
static D3D11_RASTERIZER_DESC pilot_point_raster_desc;
static float pilot_point_constants[12];
struct PilotPointDraw {ID3D11RasterizerState *previous;unsigned expanded;};
static const char pilot_point_source[]=
    "cbuffer PointRaster:register(b0){float4 Shape;float4 Size;float4 Attenuation;};\n"
    "struct V {float4 pos:SV_POSITION;float4 diffuse:COLOR0;float4 specular:COLOR1;"
    "float4 tex0:TEXCOORD0;float4 tex1:TEXCOORD1;float4 tex2:TEXCOORD2;float4 tex3:TEXCOORD3;"
    "float fog:TEXCOORD4;float4 viewpos:TEXCOORD5;};\n"
    "[maxvertexcount(4)] void main(point V v[1],inout TriangleStream<V> stream){"
    "float4 p=v[0].pos;"
    /* Point clipping uses its center, before the square is rasterized. */
    "if(p.w<=0 || abs(p.x)>p.w || abs(p.y)>p.w || p.z<0 || p.z>p.w)return;"
    "float size=Size.x;"
    "if(Shape.w!=0){float d=length(v[0].viewpos.xyz);"
    "size*=Shape.z*rsqrt(dot(Attenuation.xyz,float3(1,d,d*d)));}"
    "size=max(1,clamp(size,Size.y,Size.z));"
    "float2 delta=size*Shape.xy*p.w;"
    "float2 corners[4]={float2(-1,1),float2(1,1),float2(-1,-1),float2(1,-1)};"
    "float2 uv[4]={float2(0,0),float2(1,0),float2(0,1),float2(1,1)};"
    "[unroll]for(uint i=0;i<4;++i){V o=v[0];o.pos.xy+=corners[i]*delta;"
    "if(Size.w!=0)o.tex3=float4(uv[i],1,1);stream.Append(o);}}\n";
int32_t pilot_gfx_points(const uint32_t states[8],unsigned screen)
{
    if(!states || screen>1)return E_INVALIDARG;
    float values[8];memcpy(values,states,sizeof(values));
    for(unsigned i=0;i<8;++i)if(i!=2 && i!=3 &&
        (!(values[i]>=0) || !isfinite(values[i])))return E_INVALIDARG;
    if(states[2]>1 || states[3]>1 || values[1]>values[7] ||
       (states[3] && !screen && !(values[4]>0 || values[5]>0 || values[6]>0)))return E_INVALIDARG;
#ifndef PILOT_D3D_PHOBOS
    /* The retained donor does not publish the qualified fixed VS signature. */
    if(values[0]!=1 || values[1]>1 || values[7]<1 || states[2] || (states[3] && !screen))return E_NOTIMPL;
#endif
    pilot_point.size=values[0];pilot_point.minimum=fminf(values[1],63.875f);
    pilot_point.maximum=fminf(values[7],63.875f);
    pilot_point.a=values[4];pilot_point.b=values[5];pilot_point.c=values[6];
    pilot_point.sprite=states[2];pilot_point.scale=states[3];pilot_point.screen=screen;
    pilot_point.expanded=values[0]!=1 || values[1]>1 || values[7]<1 || states[2] || (states[3] && !screen);
    return S_OK;
}
static void pilot_point_close(void)
{
    if(pilot_point_shader)ID3D11GeometryShader_Release(pilot_point_shader);
    if(pilot_point_buffer)ID3D11Buffer_Release(pilot_point_buffer);
    if(pilot_point_raster)ID3D11RasterizerState_Release(pilot_point_raster);
    pilot_point_shader=NULL;pilot_point_buffer=NULL;pilot_point_raster=NULL;
    memset(&pilot_point,0,sizeof(pilot_point));memset(pilot_point_constants,0,sizeof(pilot_point_constants));
}
static void pilot_point_end(struct PilotPointDraw *draw)
{
    if(draw->expanded)ID3D11DeviceContext_RSSetState(d3d8_GetD3D11Context(),draw->previous);
    if(draw->previous)ID3D11RasterizerState_Release(draw->previous);
}
static HRESULT pilot_point_prepare(struct PilotPointDraw *draw)
{
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();memset(draw,0,sizeof(*draw));
    if(!pilot_point.expanded){ID3D11DeviceContext_GSSetShader(context,NULL,NULL,0);return S_OK;}
    if(!pilot_guest_viewport[2] || !pilot_guest_viewport[3])return E_INVALIDARG;
    HRESULT hr;
    if(!pilot_point_shader){
        ID3DBlob *code=NULL,*errors=NULL;
        hr=D3DCompile(pilot_point_source,sizeof(pilot_point_source)-1,"xlle-point-raster",NULL,NULL,
            "main","gs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(FAILED(hr))fprintf(stderr,"[POINT COMPILE] %s\n",errors?(const char*)ID3D10Blob_GetBufferPointer(errors):"no diagnostic");
        if(errors)ID3D10Blob_Release(errors);if(FAILED(hr))return hr;
        hr=ID3D11Device_CreateGeometryShader(d3d8_GetD3D11Device(),ID3D10Blob_GetBufferPointer(code),
            ID3D10Blob_GetBufferSize(code),NULL,&pilot_point_shader);
        ID3D10Blob_Release(code);if(FAILED(hr))return hr;
    }
    float constants[12]={1.0f/pilot_guest_viewport[2],1.0f/pilot_guest_viewport[3],
        (float)pilot_guest_viewport[3],(float)(pilot_point.scale && !pilot_point.screen),
        pilot_point.size,pilot_point.minimum,pilot_point.maximum,(float)pilot_point.sprite,
        pilot_point.a,pilot_point.b,pilot_point.c,0};
    if(!pilot_point_buffer){
        D3D11_BUFFER_DESC desc={0};desc.ByteWidth=sizeof(constants);desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        hr=ID3D11Device_CreateBuffer(d3d8_GetD3D11Device(),&desc,NULL,&pilot_point_buffer);if(FAILED(hr))return hr;
        /* A newly allocated buffer must be initialized even if all values match. */
        memset(pilot_point_constants,0,sizeof(pilot_point_constants));
    }
    if(memcmp(constants,pilot_point_constants,sizeof(constants))){
        ID3D11DeviceContext_UpdateSubresource(context,(ID3D11Resource*)pilot_point_buffer,0,NULL,constants,0,0);
        memcpy(pilot_point_constants,constants,sizeof(constants));
    }
    D3D11_RASTERIZER_DESC desc={0};desc.FillMode=D3D11_FILL_SOLID;desc.CullMode=D3D11_CULL_BACK;desc.DepthClipEnable=TRUE;
    ID3D11DeviceContext_RSGetState(context,&draw->previous);
    if(draw->previous)ID3D11RasterizerState_GetDesc(draw->previous,&desc);
    /* Triangle fill/culling must not remove or outline the generated square. */
    desc.FillMode=D3D11_FILL_SOLID;desc.CullMode=D3D11_CULL_NONE;
    if(!pilot_point_raster || memcmp(&desc,&pilot_point_raster_desc,sizeof(desc))){
        ID3D11RasterizerState *raster=NULL;
        hr=ID3D11Device_CreateRasterizerState(d3d8_GetD3D11Device(),&desc,&raster);
        if(FAILED(hr)){if(draw->previous)ID3D11RasterizerState_Release(draw->previous);draw->previous=NULL;return hr;}
        if(pilot_point_raster)ID3D11RasterizerState_Release(pilot_point_raster);
        pilot_point_raster=raster;pilot_point_raster_desc=desc;
    }
    ID3D11DeviceContext_RSSetState(context,pilot_point_raster);draw->expanded=1;
    ID3D11DeviceContext_GSSetConstantBuffers(context,0,1,&pilot_point_buffer);
    ID3D11DeviceContext_GSSetShader(context,pilot_point_shader,NULL,0);return S_OK;
}
