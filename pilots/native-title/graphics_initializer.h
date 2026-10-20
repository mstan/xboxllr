#ifndef PILOT_GRAPHICS_INITIALIZER_H
#define PILOT_GRAPHICS_INITIALIZER_H
#include <stdint.h>

/* Native output precision is deliberately observable. Retaining a dither
 * request does not pretend that an ignored desktop enum implements NV2A's
 * pixel matrix. NATIVE_RGBA8 uses the real native UNORM8 conversion; exact
 * console dither/rounding is a separately tracked precision deferral. */
enum PilotColorPrecisionPolicy { PILOT_COLOR_NATIVE_RGBA8=1 };
enum PilotRasterSchedulePolicy { PILOT_RASTER_HOST_AUTOMATIC=1 };
struct PilotInitializerState {
    uint32_t precision_policy,dither,normalize,vertex_blend,ambient_source,point_min,zero_bias,controls;
    uint32_t swath_width,raster_schedule_policy;
};
static inline int pilot_initializer_value(uint32_t state,uint32_t value)
{
    switch(state) {
    case 65:return value<=1;
    case 76:return value==4 || value==15;
    case 112:return value==0 || value==2;
    case 117:return value==0 || value==0x3f800000u;
    case 137:case 142:case 149:return value==0;
    default:return 0;
    }
}
static inline int pilot_initializer_format(uint32_t format)
{return format==6 || format==7 || format==0x11 || format==0x12 || format==0x1e;}
static inline int pilot_initializer_observe(struct PilotInitializerState *state,uint32_t index,uint32_t value)
{
    if(!state || !pilot_initializer_value(index,value))return 0;
    state->precision_policy=PILOT_COLOR_NATIVE_RGBA8;
    switch(index) {
    case 65:state->dither=value;break;
    case 76:state->swath_width=value;state->raster_schedule_policy=PILOT_RASTER_HOST_AUTOMATIC;break;
    case 112:state->ambient_source=value;state->controls|=1;break;
    case 117:state->point_min=value;state->controls|=2;break;
    case 137:state->vertex_blend=value;state->controls|=4;break;
    case 142:state->normalize=value;state->controls|=8;break;
    case 149:state->zero_bias=1;break;
    }
    return 1;
}
static inline int pilot_initializer_cache(uint32_t *cache,unsigned count,uint32_t state,uint32_t value)
{
    if(!cache || count<166 || !pilot_initializer_value(state,value))return 0;
    if(state==149) {
        /* Original FILD(0), FCHS produces negative zero for both offset
         * coefficients; all three polygon-offset enables are false. */
        cache[77]=cache[78]=0x80000000u;cache[79]=cache[80]=cache[81]=0;
    }
    cache[state]=value;return 1;
}
/* kind 0: screen-space FVF, 1: program, 2: declaration-only fixed function.
 * The fixed-function caller additionally establishes its qualified lighting
 * snapshot and generated coordinates; active weighted blending remains guarded. */
static inline int pilot_initializer_consumer(unsigned kind,uint32_t primitive,uint32_t fvf)
{
    if(kind>2 || ((primitive<5 || primitive>8) && !(kind==2 && primitive==1)))return 0;
    unsigned textures=(fvf>>8)&15;
    return kind || ((fvf&15)==4 && !(fvf&0xf030u) && textures<=4 && !((fvf>>16)>>(textures*2)));
}
static inline int pilot_graphics_simple_decode(uint32_t method,uint32_t *state)
{
    if(!state)return 0;
    switch(method) {
    case 0x40300:*state=60;break;case 0x40304:*state=59;break;
    case 0x40310:*state=65;break;
    case 0x4033c:*state=58;break;case 0x40340:*state=61;break;
    case 0x40344:*state=62;break;case 0x40348:*state=63;break;
    case 0x40350:*state=74;break;case 0x40354:*state=57;break;
    case 0x40358:*state=67;break;case 0x4035c:*state=64;break;
    case 0x40360:*state=73;break;case 0x40364:*state=70;break;
    case 0x40368:*state=71;break;case 0x4036c:*state=72;break;
    case 0x40370:*state=145;break;case 0x40374:*state=68;break;
    case 0x40378:*state=69;break;case 0x4037c:*state=66;break;
    case 0x409f8:*state=76;break;
    default:return 0;
    }
    return 1;
}
#endif
