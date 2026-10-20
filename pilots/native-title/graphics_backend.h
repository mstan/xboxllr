#ifndef PILOT_GRAPHICS_BACKEND_H
#define PILOT_GRAPHICS_BACKEND_H
#include "visual_mods.h"
/* Trusted resolved mod plan, before device creation. Host size is diagnostic;
 * ordinary width/height/readback remain guest-sized. */
int pilot_gfx_visual_mods(const struct PilotVisualMods *options);
int pilot_gfx_render_size(uint32_t size[2]);
struct PilotInterpolationStats {
    uint64_t sources,presents,previous,current;
    unsigned width,height,history;float alpha;
};
void pilot_gfx_frame_scope(uint32_t scope); /* 0: original presentation; new key: clear history */
unsigned pilot_gfx_interpolation_rate(void);
int32_t pilot_gfx_interpolate(uint64_t now); /* 100ns QPC units; 0: sample clock */
void pilot_gfx_interpolation_stats(struct PilotInterpolationStats *out);
#include <stdint.h>
#include "graphics_presentation.h"
#include "graphics_initializer.h"
int pilot_gfx_initializer_set(uint32_t state,uint32_t value);
int pilot_gfx_initializer_admits(uint32_t state,uint32_t value);
int pilot_gfx_command_method(uint32_t method,uint32_t value,int apply);
int pilot_gfx_vertex_cache_break(int apply);
int pilot_gfx_command_submit(void);
int pilot_gfx_initializer_snapshot(struct PilotInitializerState *out);
int pilot_gfx_initializer_consumer(unsigned program,uint32_t primitive,uint32_t fvf);
int pilot_gfx_multisample(unsigned enable,uint32_t mask);
int pilot_gfx_edge_antialias(unsigned enabled);
uint32_t pilot_gfx_feedback_mask(void);
int32_t pilot_gfx_create(const uint32_t parameters[17]);
int32_t pilot_gfx_parameters(const uint32_t parameters[17]);
int32_t pilot_gfx_reset(const uint32_t parameters[17]);
void pilot_gfx_close(void);
int32_t pilot_gfx_clear(uint32_t flags,uint32_t color,float depth,uint32_t stencil);
int32_t pilot_gfx_viewport(const void *viewport);
int32_t pilot_gfx_get_viewport(uint32_t viewport[6]);
int32_t pilot_gfx_transform(unsigned state,const float matrix[16]);
void pilot_gfx_matrix_multiply(float out[16],const float a[16],const float b[16]);
int32_t pilot_gfx_present(void);
/* Host presentation only: guest render targets, viewports and resource sizes
 * stay unchanged. Zero numerator/denominator restores the donor's shape. */
int pilot_gfx_presentation(uint32_t numerator,uint32_t denominator,uint32_t height);
int pilot_gfx_client_size(uint32_t size[2]);
int pilot_gfx_frame_shape(uint32_t numerator,uint32_t denominator);
int32_t pilot_gfx_fvf_placement(uint32_t fvf,float screen_x,float screen_y,float x_scale,float x_anchor);
int32_t pilot_gfx_persist_display(const unsigned char *bgra,uint32_t width,uint32_t height,uint32_t pitch);
int32_t pilot_gfx_draw_up(uint32_t primitive,uint32_t count,const void *data,uint32_t stride,uint32_t fvf);
int32_t pilot_gfx_readback(unsigned char *rgba,uint32_t capacity);
void pilot_gfx_set_state(uint32_t state,uint32_t value);
int pilot_gfx_logic_op(uint32_t value);
void pilot_gfx_set_texture_state(uint32_t stage,uint32_t state,uint32_t value);
int pilot_gfx_xbox_render_state(uint32_t state,uint32_t value);
int pilot_gfx_xbox_texture_state(uint32_t stage,uint32_t state,uint32_t value);
uint32_t pilot_gfx_width(void);
uint32_t pilot_gfx_height(void);
int32_t pilot_gfx_surface_ref(unsigned surface,int acquire);
enum PilotResourceKind {PILOT_GFX_DEVICE,PILOT_GFX_SURFACE,PILOT_GFX_TEXTURE,PILOT_GFX_VERTEX};
int pilot_gfx_resource_busy(unsigned kind,uint32_t handle);
/* Read-only selected use serial; does not poll, submit or wait for GPU work. */
uint64_t pilot_gfx_resource_serial(unsigned kind,uint32_t handle);
struct PilotGpuProgress {uint64_t submitted,completed;unsigned pending_queries,created_queries;};
void pilot_gfx_completion_stats(struct PilotGpuProgress *progress);
uint64_t pilot_gfx_checkpoint(void);
int pilot_gfx_checkpoint_busy(uint64_t serial);
/* Owned native event handle, or NULL for completed work/polling-only backend.
 * Caller closes its duplicate after leaving any wait. Never a guest handle. */
void *pilot_gfx_checkpoint_event(uint64_t serial);
int pilot_gfx_completion_events(void);
int32_t pilot_gfx_surface_readback(unsigned surface,unsigned char *rgba,uint32_t capacity);
/* Diagnostic raw bytes of the actual bound color/depth target, with DXGI
 * format metadata. NULL/zero queries shape only; S_FALSE means no target. */
int32_t pilot_gfx_reference_target_readback(unsigned depth,void *data,uint32_t capacity,uint32_t metadata[3]);
/* Capture-only native context observation. available bits: viewport=1,
 * scissor rectangle=2, rasterizer state=4, stage-0 sampled image extent=8.
 * Missing state remains explicitly unavailable; this never binds resources. */
struct PilotRasterObservation {
    uint32_t available,scissor_enabled,source_width,source_height;
    float viewport[6];int32_t scissor[4];
};
int32_t pilot_gfx_reference_raster(struct PilotRasterObservation *out);
int32_t pilot_gfx_texture_create(uint32_t width,uint32_t height,uint32_t levels,uint32_t format,uint32_t *handle);
int32_t pilot_gfx_texture_alias(unsigned kind,uint32_t source,uint32_t format,uint32_t *handle);
int32_t pilot_gfx_texture_create_usage(uint32_t width,uint32_t height,uint32_t levels,uint32_t format,uint32_t usage,uint32_t *handle);
int32_t pilot_gfx_texture_clear(uint32_t handle,uint32_t color);
int32_t pilot_gfx_texture_clear_depth(uint32_t handle,uint32_t flags,float depth,uint32_t stencil);
/* Color 0 = default back buffer; depth UINT32_MAX = default depth, 0 = none. */
int32_t pilot_gfx_targets(uint32_t color,uint32_t depth);
void pilot_gfx_texture_download(uint32_t handle,void *data,uint32_t bytes);
void pilot_gfx_texture_destroy(uint32_t handle);
void pilot_gfx_texture_upload(uint32_t handle,uint32_t level,const void *data,uint32_t bytes);
void pilot_gfx_texture_prepare_sample(uint32_t handle,uint32_t level,const void *data,uint32_t bytes,int cpu_exposed);
void pilot_gfx_texture_bind(unsigned stage,uint32_t handle);
/* Allocation format and the live guest sampling format may differ. */
void pilot_gfx_texture_sampling_format(unsigned stage,uint32_t format);
struct PilotDepthTextureStats {uint32_t allocations,conversions,shader_compilations;};
void pilot_gfx_depth_texture_stats(struct PilotDepthTextureStats *stats);
int32_t pilot_gfx_texture_readback(uint32_t handle,uint32_t level,void *data,uint32_t capacity);
/* Diagnostic luma grid from the actual uploaded packed-YUV CPU mirror. */
int32_t pilot_gfx_movie_luma(uint32_t handle,unsigned step,unsigned char *samples,unsigned bytes);
int32_t pilot_gfx_vertex_create(uint32_t bytes,uint32_t *handle);
void pilot_gfx_vertex_destroy(uint32_t handle);
void pilot_gfx_vertex_upload(uint32_t handle,const void *data,uint32_t bytes);
/* data addresses the whole guest resource; synchronize only this draw's span. */
void pilot_gfx_vertex_upload_range(uint32_t handle,const void *data,uint32_t offset,uint32_t bytes);
void pilot_gfx_vertex_bind(unsigned stream,uint32_t handle,uint32_t stride);
int32_t pilot_gfx_vertex_readback(uint32_t handle,void *data,uint32_t capacity);
struct PilotVertexElement { uint32_t type,stream,offset; };
int pilot_gfx_vertex_current_get(unsigned reg,float value[4]);
int pilot_gfx_vertex_current_set(unsigned reg,const float value[4]);
int pilot_gfx_vertex_current_method(uint32_t method,uint32_t word,int apply);
unsigned pilot_gfx_shader_current_inputs(uint32_t handle);
uint32_t pilot_gfx_vertex_type_size(uint32_t type);
/* Private ownership handles, never desktop FVF/donor shader selectors. */
int32_t pilot_gfx_shader_create(const uint32_t *code,unsigned count,const struct PilotVertexElement elements[16],uint32_t *handle);
/* Declaration-only 3D inputs use the shared fixed-function transform/lighting
 * shader. Typed stream decoding remains on the GPU, including packed normals. */
int32_t pilot_gfx_shader_create_fixed(const struct PilotVertexElement elements[16],uint32_t *handle);
int32_t pilot_gfx_shader_prepare_fixed(uint32_t handle);
/* Source-decoded XYZ FVF reuses the typed fixed-function core and cache. */
int32_t pilot_gfx_fvf_prepare_fixed(uint32_t fvf);
void pilot_gfx_shader_destroy(uint32_t handle);
void pilot_gfx_shaders_close(void);
int32_t pilot_gfx_shader_bind(uint32_t handle);
void pilot_gfx_shader_constants(unsigned first,unsigned count,const void *data);
/* Constant writes coalesce until draw submission. */
int32_t pilot_gfx_shader_flush(void);
unsigned pilot_gfx_shader_uploads(void);
void pilot_gfx_shader_capture_constants(void *data);
/* Internal semantic admission; no GPU/CPU framebuffer access or draw override. */
void pilot_gfx_shader_fixed_binding(void);
/* Preserve the accepted Xbox noise state for bounded semantic admission. */
void pilot_gfx_noise_state(unsigned enabled);
int pilot_gfx_shader_blend_rectangle(const void *vertices,uint32_t bytes,unsigned stride,unsigned primitive,unsigned count);
/* Read-only partial admission hook; the donor separately proves PS semantics. */
int pilot_gfx_blend_candidate_geometry(unsigned primitive,unsigned first,unsigned count);
struct PilotBlendStats {uint32_t draws,copies,allocations,shader_compilations,original_compilations;};
void pilot_gfx_blend_stats(struct PilotBlendStats *stats);
int32_t pilot_gfx_shader_raster(const float scale[4],const float offset[4]);
int32_t pilot_gfx_draw_vertices(unsigned primitive,unsigned first,unsigned count);
/* Little-endian Xbox uint16 indices, including unaligned CPU storage. */
int32_t pilot_gfx_draw_indexed(unsigned primitive,unsigned count,const void *indices,int32_t base);
int32_t pilot_gfx_draw_memory(unsigned primitive,unsigned count,const void *vertices,unsigned stride);
uint32_t pilot_gfx_fvf_extent(uint32_t fvf);
uint32_t pilot_gfx_fvf_layout(uint32_t fvf,struct PilotVertexElement elements[16]);
int32_t pilot_gfx_fvf_prepare(uint32_t fvf,float screen_x,float screen_y);
unsigned pilot_gfx_shader_streams(uint32_t extents[16]);
void pilot_gfx_pixel_fixed(void);
int32_t pilot_gfx_pixel_program(const uint32_t states[57],uint32_t texture_modes);
/* Diagnostic raw vertex outputs, before rasterization/viewport conversion. */
struct PilotVertexProbeStream { const void *data; unsigned stride; };
int32_t pilot_gfx_shader_probe(uint32_t handle,const struct PilotVertexProbeStream streams[16],unsigned count,
                              const float constants[192][4],float *outputs);
/* Exact compiled raster_main stream output; diagnostic only, same 13*float4 layout. */
int32_t pilot_gfx_shader_raster_probe(uint32_t handle,const struct PilotVertexProbeStream streams[16],unsigned count,
                                     const float constants[192][4],float *outputs);
/* NULL constants probes the actually bound shader/layout/constant buffer. */
int32_t pilot_gfx_visibility_begin(void);
int32_t pilot_gfx_visibility_end(uint32_t index);
int32_t pilot_gfx_visibility_result(uint32_t index,uint32_t *count,uint64_t *stamp);
#endif
