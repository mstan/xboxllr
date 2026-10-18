#include "visual_mods.h"
#include <string.h>

void pilot_visual_mods_default(struct PilotVisualMods *o)
{
    memset(o,0,sizeof(*o));o->version=PILOT_VISUAL_MOD_VERSION;
    o->supersample=o->anisotropy=1;o->aspect_numerator=4;o->aspect_denominator=3;
}
int pilot_visual_mods_validate(const struct PilotVisualMods *o)
{
    if(!o || o->version!=PILOT_VISUAL_MOD_VERSION ||
       (o->internal_height && o->internal_height!=1080 && o->internal_height!=1440 &&
        o->internal_height!=1800 && o->internal_height!=2160) ||
       o->supersample<1 || o->supersample>3 || o->antialiasing>PILOT_AA_FXAA ||
       (o->anisotropy!=1 && o->anisotropy!=2 && o->anisotropy!=4 &&
        o->anisotropy!=8 && o->anisotropy!=16) || o->frame_mode>PILOT_FRAMES_ACTUAL ||
       (!o->frame_mode && o->frame_rate) ||
       (o->frame_mode && o->frame_rate!=60 && o->frame_rate!=120) ||
       !o->aspect_numerator || !o->aspect_denominator ||
       o->aspect_numerator>65536 || o->aspect_denominator>65536 ||
       (uint64_t)o->aspect_numerator*3<(uint64_t)o->aspect_denominator*4 ||
       (uint64_t)o->aspect_numerator*9>(uint64_t)o->aspect_denominator*32)return 0;
    return 1;
}
int pilot_visual_mods_size(const struct PilotVisualMods *o,
    uint32_t gw,uint32_t gh,uint32_t tw,uint32_t th,uint32_t limit,uint32_t size[2])
{
    if(!size || !pilot_visual_mods_validate(o) || !gw || !gh || !tw || !th ||
       gw>limit || gh>limit || tw>limit || th>limit)return 0;
    uint64_t height=o->internal_height?o->internal_height:gh;
    uint64_t width=o->internal_height?
        (height*o->aspect_numerator+o->aspect_denominator-1)/o->aspect_denominator:gw;
    width*=o->supersample;height*=o->supersample;
    /* Each term is bounded before multiplying by a target dimension. */
    if(width>limit || height>limit)return 0;
    uint64_t w=(width*tw+gw-1)/gw,h=(height*th+gh-1)/gh;
    if(!w || !h || w>limit || h>limit)return 0;
    size[0]=(uint32_t)w;size[1]=(uint32_t)h;return 1;
}
int pilot_visual_mods_target_size(const struct PilotVisualMods *o,
    uint32_t gw,uint32_t gh,uint32_t tw,uint32_t th,uint32_t limit,uint32_t size[2])
{
    if(!size || !pilot_visual_mods_validate(o) || !gw || !gh || !tw || !th ||
       tw>limit || th>limit || gw>limit || gh>limit)return 0;
    if((uint64_t)tw*gh!=(uint64_t)th*gw) {size[0]=tw;size[1]=th;return 1;}
    return pilot_visual_mods_size(o,gw,gh,tw,th,limit,size);
}
