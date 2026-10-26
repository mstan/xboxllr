#ifndef PILOT_TITLE_MOD_H
#define PILOT_TITLE_MOD_H
#include <stdint.h>
#include "visual_mods.h"

/* Trusted, statically linked title plugins. Packages select registry IDs;
 * they never supply executable code. Bound guest bodies remain available.
 * Graphics callbacks run under the graphics lock; function callbacks run on
 * the guest CPU owner. The host lives for the process; pointers are borrowed. */
#define PILOT_TITLE_MOD_ABI 3u
struct PilotTitleModCall {
    uint32_t id,caller,phase; /* 0: before original; 1: after original */
    const uint32_t *entry; /* EAX ECX EDX EBX ESP EBP ESI EDI at original entry */
    uint32_t gpr[8]; /* actual original result; ESP must retain its ABI return */
    uint32_t arguments[16]; /* source-proved callee-cleaned arguments */
    uint64_t scratch; /* trusted callback state carried from entry to exit */
};
struct PilotTitleModHost {
    uint32_t abi;
    const char *xbe_sha256;
    int (*read)(uint32_t address,void *data,uint32_t bytes);
    int (*write_data)(uint32_t address,const void *data,uint32_t bytes);
    int (*presentation)(uint32_t numerator,uint32_t denominator,uint32_t height);
    int (*client_size)(uint32_t size[2]);
    int (*frame_shape)(uint32_t numerator,uint32_t denominator);
    int (*visual_mods)(const struct PilotVisualMods *options);
    void (*frame_scope)(uint32_t key); /* 0: original presentation; new key: reset history */
    int (*invoke)(uint32_t id,uint32_t ecx,const uint32_t *args,uint32_t count,uint32_t *result);
    int (*radial_blur)(float strength); /* graphics callbacks only; preserves guest pipeline */
    int (*resident_media)(uint32_t budget_mib,const char *const *stream_extensions,uint32_t count);
    int (*resident_ready)(void);
    uint64_t (*monotonic)(void); /* 100ns wall clock */

};
struct PilotTitleModDraw {
    uint32_t caller,shader,primitive,count,default_target;
    const unsigned char *positions;
    uint32_t stride,position_type;
    /* Identity leaves ordinary rasterization untouched. The plugin can map
     * screen X to scale*X + anchor*(1-scale), in guest viewport pixels.
     * A finite anchor may lie beyond the viewport for a clipped marker. */
    float x_scale,x_anchor;
    uint32_t depth_test,depth_write;
};
struct PilotTitleMod {
    uint32_t abi;
    void (*transform)(uint32_t state,uint32_t address,uint32_t caller);
    void (*draw)(struct PilotTitleModDraw *draw);
    void (*present)(void);
    int (*function)(struct PilotTitleModCall *call); /* pre: 1 supplies completed result; post: must return0 */
};
/* Defined by the one reviewed title registry source supplied by the game.
 * A NULL result means the resolved plan selects no plugin. */
const struct PilotTitleMod *pilot_title_mod_open(const struct PilotTitleModHost *host);
#endif
