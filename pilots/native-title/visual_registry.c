/* Shared data-only quality/presentation registry for titles which need no
 * game-specific camera or HUD transforms. Original guest code is unchanged. */
#include "title_mod.h"
#include "visual_mod_plan.h"
#include <stdlib.h>
#include <string.h>

static const struct PilotTitleModHost *visual_host;
static unsigned visual_frames;
static void fail(const char *reason)
{
    fprintf(stderr,"[VISUAL MOD ERROR] %s\n",reason);
    exit(2);
}
static void present(void)
{
    visual_host->frame_scope(visual_frames?1:0);
}
const struct PilotTitleMod *pilot_title_mod_open(const struct PilotTitleModHost *host)
{
    const char *path=getenv("XLLE_PILOT_MOD_PLAN");
    if(!path || !*path)return NULL;
    if(!host || host->abi!=PILOT_TITLE_MOD_ABI || !host->xbe_sha256)
        fail("invalid visual host");
    FILE *file=fopen(path,"rb");
    if(!file)fail("resolved visual mod plan is unavailable");
    struct PilotVisualPlan plan;
    int valid=pilot_visual_plan_read(file,&plan);fclose(file);
    if(!valid || strcmp(plan.identity,host->xbe_sha256))fail("visual plan identity mismatch");
    if(plan.operations&PILOT_MOD_ASPECT)fail("this title has no camera/HUD aspect registry");
    if(plan.options.frame_mode &&
       (plan.options.frame_mode!=PILOT_FRAMES_INTERPOLATED || !host->frame_scope))
        fail("actual faster simulation is not supported");
    if(!host->visual_mods || !host->visual_mods(&plan.options))fail("unsupported visual options");
    visual_host=host;visual_frames=plan.options.frame_mode;
    fprintf(stderr,"[MOD] visual-plan=2 operations=%u height=%u ssaa=%u aa=%u aniso=%u frames=%u/%u original-image=unchanged\n",
        plan.operations,plan.options.internal_height,plan.options.supersample,
        plan.options.antialiasing,plan.options.anisotropy,visual_frames,plan.options.frame_rate);
    static struct PilotTitleMod result;
    result=(struct PilotTitleMod){PILOT_TITLE_MOD_ABI,NULL,NULL,visual_frames?present:NULL};
    return &result;
}
