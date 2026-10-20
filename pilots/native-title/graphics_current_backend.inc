/* A disabled vertex array reads its current input value. Reuse the prepared
 * renderer's current-value owner; our NV2A shaders consume a small GPU cbuffer
 * instead of requesting a nonexistent vertex stream. No CPU vertex expansion.
 * The alternate renderer retains the same native D3D default/update contract. */
#ifdef PILOT_D3D_PHOBOS
#include "d3d8_vsh.h"
#endif
static ID3D11Buffer *native_current_buffer;
static float native_current_uploaded[16][4];
static unsigned native_current_uploaded_valid;
#ifndef PILOT_D3D_PHOBOS
static float native_current_compat[16][4];
static unsigned native_current_initialized;
static void native_current_initialize(void)
{
    if(native_current_initialized)return;
    for(unsigned i=0;i<16;++i)native_current_compat[i][3]=1;
    for(unsigned i=0;i<3;++i)native_current_compat[3][i]=1;
    native_current_initialized=1;
}
#endif
int pilot_gfx_vertex_current_get(unsigned reg,float value[4])
{
    if(reg>=16 || !value || !d3d8_GetD3D11Device())return 0;
#ifdef PILOT_D3D_PHOBOS
    d3d8_vsh_get_vertex_data((int)reg,value);
#else
    native_current_initialize();memcpy(value,native_current_compat[reg],16);
#endif
    return 1;
}
int pilot_gfx_vertex_current_set(unsigned reg,const float value[4])
{
    if(reg>=16 || !value || !d3d8_GetD3D11Device())return 0;
#ifdef PILOT_D3D_PHOBOS
    d3d8_vsh_set_vertex_data((int)reg,value);
#else
    native_current_initialize();memcpy(native_current_compat[reg],value,16);
#endif
    return 1;
}
static HRESULT native_current_snapshot(float values[16][4])
{
    for(unsigned i=0;i<16;++i)if(!pilot_gfx_vertex_current_get(i,values[i]))return E_FAIL;
    return S_OK;
}
static HRESULT native_current_flush(void)
{
    float values[16][4];HRESULT hr=native_current_snapshot(values);if(FAILED(hr))return hr;
    ID3D11Device *device=d3d8_GetD3D11Device();ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
    if(!native_current_buffer) {
        D3D11_BUFFER_DESC desc={0};desc.ByteWidth=sizeof(values);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial={values,0,0};hr=ID3D11Device_CreateBuffer(device,&desc,&initial,&native_current_buffer);
        if(FAILED(hr))return hr;
        memcpy(native_current_uploaded,values,sizeof(values));native_current_uploaded_valid=1;
    } else if(!native_current_uploaded_valid || memcmp(values,native_current_uploaded,sizeof(values))) {
        ID3D11DeviceContext_UpdateSubresource(ctx,(ID3D11Resource*)native_current_buffer,0,NULL,values,0,0);
        memcpy(native_current_uploaded,values,sizeof(values));native_current_uploaded_valid=1;
    }
    ID3D11DeviceContext_VSSetConstantBuffers(ctx,3,1,&native_current_buffer);return S_OK;
}
static void native_current_close(void)
{
    if(native_current_buffer) {
        ID3D11Buffer *none=NULL;ID3D11DeviceContext_VSSetConstantBuffers(d3d8_GetD3D11Context(),3,1,&none);
        ID3D11Buffer_Release(native_current_buffer);native_current_buffer=NULL;
    }
    memset(native_current_uploaded,0,sizeof(native_current_uploaded));native_current_uploaded_valid=0;
#ifndef PILOT_D3D_PHOBOS
    memset(native_current_compat,0,sizeof(native_current_compat));native_current_initialized=0;
#endif
}
/* Non-position attributes only. Position setters can emit immediate vertices
 * and need their Begin/End owner; they are not admitted as silent state writes.
 * Numeric packet semantics: xemu/nxdk NV097 4F, 2F and 4UB methods. */
int pilot_gfx_vertex_current_method(uint32_t method,uint32_t word,int apply)
{
    unsigned reg,lane,kind;
    if(method>=0x1a00 && method<0x1b00 && !(method&3)) {reg=(method-0x1a00)/16;lane=(method&15)/4;kind=4;}
    else if(method>=0x1880 && method<0x1900 && !(method&3)) {reg=(method-0x1880)/8;lane=(method&7)/4;kind=2;}
    else if(method>=0x1940 && method<0x1980 && !(method&3)) {reg=(method-0x1940)/4;lane=0;kind=1;}
    else return 0;
    if(!reg || !d3d8_GetD3D11Device())return 0;
    if(!apply)return 1;
    float values[4];if(!pilot_gfx_vertex_current_get(reg,values))return 0;
    if(kind==1)for(unsigned i=0;i<4;++i)values[i]=((word>>(i*8))&255)/255.0f;
    else {
        memcpy(values+lane,&word,4);
        if(kind==2){values[2]=0;values[3]=1;}
    }
    return pilot_gfx_vertex_current_set(reg,values);
}
