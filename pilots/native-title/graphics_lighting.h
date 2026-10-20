#ifndef PILOT_GRAPHICS_LIGHTING_H
#define PILOT_GRAPHICS_LIGHTING_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Value-only original D3D8 structures. No COM pointers or host enum mapping. */
struct PilotLightingMaterial {
    float diffuse[4],ambient[4],specular[4],emissive[4],power;
};
struct PilotLightingLight {
    uint32_t type;
    float diffuse[4],specular[4],ambient[4];
    float position[3],direction[3],range,falloff,attenuation[3],theta,phi;
};
struct PilotLightingSnapshot {
    struct PilotLightingMaterial material;
    uint32_t ambient,color_vertex,local_viewer,specular_enable,normalize;
    uint32_t sources[4],count; /* diffuse, specular, ambient, emissive */
    struct PilotLightingLight lights[8];
    float view[16];
};
struct PilotLightingLayout {
    uint32_t table,capacity,head,material,stride,flags,next;
};
enum PilotLightingCacheResult {
    PILOT_LIGHTING_CACHE_OK=1,PILOT_LIGHTING_CACHE_RANGE=-1,
    PILOT_LIGHTING_CACHE_NODE=-2,PILOT_LIGHTING_CACHE_CYCLE=-3,
    PILOT_LIGHTING_CACHE_UNDEFINED=-4
};
static inline uint32_t pilot_lighting_word(const unsigned char *memory,size_t address)
{uint32_t word;memcpy(&word,memory+address,4);return word;}
/* The original driver takes the first eight enabled records, in linked-list
 * order. Logical light indices need not be below eight. Flags bit4 is dirty,
 * not enable: only membership determines enable. Publish atomically on success.
 * The original setters retain allocation, defaults and dirty-bit ownership. */
static inline int pilot_lighting_cache(const unsigned char *memory,size_t bytes,
    uint32_t device,const struct PilotLightingLayout *layout,struct PilotLightingSnapshot *out)
{
    if(!memory || !layout || !out || sizeof(struct PilotLightingLight)!=104 ||
       sizeof(struct PilotLightingMaterial)!=68 || layout->stride<104 ||
       (uint64_t)layout->flags+4>layout->stride || (uint64_t)layout->next+4>layout->stride ||
       (uint64_t)device+layout->table+4>bytes || (uint64_t)device+layout->capacity+4>bytes ||
       (uint64_t)device+layout->head+4>bytes || (uint64_t)device+layout->material+68>bytes)
        return PILOT_LIGHTING_CACHE_RANGE;
    uint32_t table=pilot_lighting_word(memory,(size_t)device+layout->table);
    uint32_t capacity=pilot_lighting_word(memory,(size_t)device+layout->capacity);
    uint32_t node=pilot_lighting_word(memory,(size_t)device+layout->head);
    if((table&3) || (uint64_t)table+(uint64_t)capacity*layout->stride>bytes ||
       (!table && capacity) || (!capacity && node))return PILOT_LIGHTING_CACHE_RANGE;
    struct PilotLightingSnapshot candidate=*out;
    candidate.count=0;memset(candidate.lights,0,sizeof(candidate.lights));
    memcpy(&candidate.material,memory+(size_t)device+layout->material,68);
    uint32_t visited[8];
    while(node && candidate.count<8) {
        if(node<table || !capacity || (node-table)%layout->stride ||
           (uint64_t)node+layout->stride>(uint64_t)table+(uint64_t)capacity*layout->stride)
            return PILOT_LIGHTING_CACHE_NODE;
        for(unsigned i=0;i<candidate.count;++i)if(visited[i]==node)return PILOT_LIGHTING_CACHE_CYCLE;
        if(!(pilot_lighting_word(memory,(size_t)node+layout->flags)&1))return PILOT_LIGHTING_CACHE_UNDEFINED;
        visited[candidate.count]=node;
        memcpy(&candidate.lights[candidate.count++],memory+node,104);
        node=pilot_lighting_word(memory,(size_t)node+layout->next);
    }
    *out=candidate;return PILOT_LIGHTING_CACHE_OK;
}
#endif
