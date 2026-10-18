#ifndef PILOT_VISUAL_MOD_PLAN_H
#define PILOT_VISUAL_MOD_PLAN_H
#include "visual_mods.h"
#include <stdio.h>
enum PilotVisualOperation {
    PILOT_MOD_ASPECT=1, PILOT_MOD_RESOLUTION=2, PILOT_MOD_AA=4,
    PILOT_MOD_SUPERSAMPLING=8, PILOT_MOD_FILTERING=16, PILOT_MOD_FRAME_RATE=32
};
struct PilotVisualPlan {
    char identity[65],aspect_plugin[96];
    uint32_t operations,adaptive;
    struct PilotVisualMods options;
};
/* Bounded data-only operations, no effects during parse. Failure leaves output
 * untouched. The title registry verifies identity/aspect plugin and its data
 * guards before passing the complete policy to the host. */
int pilot_visual_plan_read(FILE *file,struct PilotVisualPlan *output);
#endif
