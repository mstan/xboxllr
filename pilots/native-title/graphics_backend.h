#ifndef PILOT_GRAPHICS_BACKEND_H
#define PILOT_GRAPHICS_BACKEND_H
#include <stdint.h>
int32_t pilot_gfx_create(const uint32_t parameters[17]);
void pilot_gfx_close(void);
int32_t pilot_gfx_clear(uint32_t flags,uint32_t color,float depth,uint32_t stencil);
int32_t pilot_gfx_viewport(const void *viewport);
int32_t pilot_gfx_get_viewport(uint32_t viewport[6]);
int32_t pilot_gfx_present(void);
int32_t pilot_gfx_draw_up(uint32_t primitive,uint32_t count,const void *data,uint32_t stride,uint32_t fvf);
int32_t pilot_gfx_readback(unsigned char *rgba,uint32_t capacity);
void pilot_gfx_set_state(uint32_t state,uint32_t value);
void pilot_gfx_set_texture_state(uint32_t stage,uint32_t state,uint32_t value);
int pilot_gfx_xbox_render_state(uint32_t state,uint32_t value);
int pilot_gfx_xbox_texture_state(uint32_t stage,uint32_t state,uint32_t value);
uint32_t pilot_gfx_width(void);
uint32_t pilot_gfx_height(void);
int32_t pilot_gfx_surface_ref(unsigned surface,int acquire);
int32_t pilot_gfx_surface_readback(unsigned surface,unsigned char *rgba,uint32_t capacity);
int32_t pilot_gfx_texture_create(uint32_t width,uint32_t height,uint32_t levels,uint32_t format,uint32_t *handle);
void pilot_gfx_texture_destroy(uint32_t handle);
void pilot_gfx_texture_upload(uint32_t handle,uint32_t level,const void *data,uint32_t bytes);
int32_t pilot_gfx_texture_readback(uint32_t handle,uint32_t level,void *data,uint32_t capacity);
#endif
