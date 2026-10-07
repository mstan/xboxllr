#pragma once
#include "d3d8_internal.h"
#ifdef PILOT_D3D_PHOBOS
#include <d3d11.h>
/* Borrowed references owned by the selected renderer. These accessors are
 * added to the pinned private preparation; the guest never sees RHI/COM. */
ID3D11Device *d3d8_GetD3D11Device(void);
ID3D11DeviceContext *d3d8_GetD3D11Context(void);
void d3d8_shaders_prepare_pixel(void);
static ID3D11RenderTargetView *d3d8_GetDefaultRTV(void)
{ return (ID3D11RenderTargetView*)rhi_d3d11_native_view(d3d8_GetDefaultTargetView()); }
static ID3D11Texture2D *pilot_donor_texture(D3D8Texture *t)
{ return (ID3D11Texture2D*)rhi_d3d11_native_image(t->image); }
static ID3D11Buffer *pilot_donor_vertex(D3D8VertexBuffer *v)
{ return (ID3D11Buffer*)rhi_d3d11_native_buffer(v->buffer); }
#else
static ID3D11Texture2D *pilot_donor_texture(D3D8Texture *t) { return t->d3d11_texture; }
static ID3D11Buffer *pilot_donor_vertex(D3D8VertexBuffer *v) { return v->d3d11_buffer; }
#endif
