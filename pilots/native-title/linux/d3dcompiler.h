/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include_next <d3dcompiler.h>
#define PILOT_NATIVE_HLSL_COMPILER_ID "linux-vkd3d21-ieee-v1"
#ifdef __cplusplus
extern "C" {
#endif
HRESULT pilot_linux_D3DCompile(const void*,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,
    LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
HRESULT pilot_linux_D3DCreateBlob(SIZE_T,ID3DBlob**);
#ifdef __cplusplus
}
#endif
#define D3DCompile pilot_linux_D3DCompile
#define D3DCreateBlob pilot_linux_D3DCreateBlob
