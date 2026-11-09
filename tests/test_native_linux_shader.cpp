/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <d3dcompiler.h>
#include <cassert>
#include <cstring>
int main()
{
    const char source[]="float4 main(float4 position : POSITION) : SV_Position { return position; }";
    ID3DBlob *shader=nullptr,*errors=nullptr;
    HRESULT hr=D3DCompile(source,sizeof(source)-1,"native-abi-check",nullptr,nullptr,"main","vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors);
    assert(SUCCEEDED(hr) && shader && shader->GetBufferSize()>4);
    assert(!std::memcmp(shader->GetBufferPointer(),"DXBC",4));
    ID3DBlob *cached=nullptr;assert(SUCCEEDED(D3DCreateBlob(shader->GetBufferSize(),&cached)));
    std::memcpy(cached->GetBufferPointer(),shader->GetBufferPointer(),shader->GetBufferSize());
    assert(cached->Release()==0 && shader->Release()==0);if(errors)errors->Release();
    const char nan_shader[]="float4 main(float4 value : TEXCOORD) : SV_Target { return float4(isnan(value))+float4(isinf(value))+float4(isfinite(value)); }";
    shader=errors=nullptr;
    hr=D3DCompile(nan_shader,sizeof(nan_shader)-1,"nan-compat",nullptr,nullptr,"main","ps_5_0",0,0,&shader,&errors);
    assert(SUCCEEDED(hr) && shader);shader->Release();if(errors)errors->Release();
    const char bad[]="this is not HLSL";shader=errors=nullptr;
    hr=D3DCompile(bad,sizeof(bad)-1,"invalid-hlsl",nullptr,nullptr,"main","ps_4_0",0,0,&shader,&errors);
    assert(FAILED(hr) && !shader && errors && errors->GetBufferSize()>0);errors->Release();
}
