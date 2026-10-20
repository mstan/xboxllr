#ifndef PILOT_GRAPHICS_IMMEDIATE_H
#define PILOT_GRAPHICS_IMMEDIATE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Native storage for NV097's latched current attributes. The current-value
 * registers retain their existing renderer owner; this batch only snapshots
 * them when a completed position write emits a vertex. No guest allocation or
 * fixed vertex-count cutoff is introduced by host staging. */
typedef float PilotImmediateVertex[16][4];
struct PilotImmediateBatch {
    PilotImmediateVertex *vertices;
    uint32_t count,capacity,primitive;
};
#ifndef PILOT_IMMEDIATE_REALLOC
#define PILOT_IMMEDIATE_REALLOC realloc
#endif
static inline int pilot_immediate_begin(struct PilotImmediateBatch *batch,uint32_t primitive)
{
    if(!batch || batch->primitive || primitive<5 || primitive>8)return 0;
    batch->count=0;batch->primitive=primitive;return 1;
}
static inline int pilot_immediate_append(struct PilotImmediateBatch *batch,const float values[16][4])
{
    if(!batch || !batch->primitive || !values)return 0;
    /* The renderer's byte-span interface uses uint32_t. Allocation failure or
     * exhaustion is explicit and leaves the complete preceding batch intact. */
    const uint32_t maximum=UINT32_MAX/(uint32_t)sizeof(PilotImmediateVertex);
    if(batch->count==maximum)return 0;
    if(batch->count==batch->capacity) {
        uint32_t capacity=batch->capacity?batch->capacity:16;
        if(capacity<=batch->count)capacity=capacity>maximum/2?maximum:capacity*2;
        if(capacity>maximum)capacity=maximum;
        void *next=PILOT_IMMEDIATE_REALLOC(batch->vertices,(size_t)capacity*sizeof(PilotImmediateVertex));
        if(!next)return 0;
        batch->vertices=(PilotImmediateVertex*)next;batch->capacity=capacity;
    }
    memcpy(batch->vertices[batch->count++],values,sizeof(PilotImmediateVertex));return 1;
}
static inline void pilot_immediate_end(struct PilotImmediateBatch *batch)
{batch->count=0;batch->primitive=0;}
static inline void pilot_immediate_close(struct PilotImmediateBatch *batch)
{free(batch->vertices);memset(batch,0,sizeof(*batch));}
#endif
