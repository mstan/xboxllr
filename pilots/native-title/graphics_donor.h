#pragma once
#include "d3d8_internal.h"
void pilot_d3d_program_fog_mode(unsigned program);
#ifdef PILOT_D3D_PHOBOS
#include <d3d11.h>
/* Borrowed references owned by the selected renderer. These accessors are
 * added to the pinned private preparation; the guest never sees RHI/COM. */
ID3D11Device *d3d8_GetD3D11Device(void);
ID3D11DeviceContext *d3d8_GetD3D11Context(void);
void d3d8_shaders_prepare_pixel(void);
RhiImage *pilot_donor_default_depth(void);
void pilot_donor_output_images(RhiImage *color,RhiImage *depth,int force);
DWORD pilot_donor_feedback_begin(void);
void pilot_donor_feedback_end(DWORD mask);
void pilot_donor_feedback_stats(uint32_t *copies,uint32_t *allocations);
void pilot_donor_blend_candidate(int admitted);
int pilot_donor_blend_active(void);
RhiView *pilot_donor_blend_destination(void);
void pilot_donor_blend_destination_stats(uint32_t *copies,uint32_t *allocations);
void pilot_donor_blend_shader_stats(uint32_t *variants,uint32_t *originals);
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
