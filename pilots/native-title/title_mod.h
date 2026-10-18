#ifndef PILOT_TITLE_MOD_H
#define PILOT_TITLE_MOD_H
#include <stdint.h>
#include "visual_mods.h"

/* Trusted, statically linked title plugins. Packages select registry IDs;
 * they never supply executable code. No generated guest body is instrumented.
 * Calls run on the graphics owner under its existing lock. The host lives for
 * the process; draw pointers are borrowed only for the callback. */
#define PILOT_TITLE_MOD_ABI 2u
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
};
struct PilotTitleModDraw {
    uint32_t caller,shader,primitive,count,default_target;
    const unsigned char *positions;
    uint32_t stride,position_type;
    /* Identity leaves ordinary rasterization untouched. The plugin can map
     * screen X to scale*X + anchor*(1-scale), in guest viewport pixels. */
    float x_scale,x_anchor;
    uint32_t depth_test,depth_write;
};
struct PilotTitleMod {
    uint32_t abi;
    void (*transform)(uint32_t state,uint32_t address,uint32_t caller);
    void (*draw)(struct PilotTitleModDraw *draw);
    void (*present)(void);
};
/* Defined by the one reviewed title registry source supplied by the game.
 * A NULL result means the resolved plan selects no plugin. */
const struct PilotTitleMod *pilot_title_mod_open(const struct PilotTitleModHost *host);
#endif
