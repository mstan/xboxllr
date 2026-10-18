#ifndef PILOT_VISUAL_MODS_H
#define PILOT_VISUAL_MODS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Shared trusted mod options, fixed before device creation. Original is always
 * available. One frame_mode owns cadence: interpolation and actual extra game
 * updates can never be enabled together. Actual mode additionally requires a
 * title's qualified timing adapter; a higher present limit is not that adapter. */
enum PilotFrameMode { PILOT_FRAMES_ORIGINAL, PILOT_FRAMES_INTERPOLATED, PILOT_FRAMES_ACTUAL };
enum PilotAntialiasing { PILOT_AA_ORIGINAL, PILOT_AA_FXAA };
#define PILOT_VISUAL_MOD_VERSION 1u
struct PilotVisualMods {
    uint32_t version;
    uint32_t internal_height; /* 0 = original; otherwise 1080/1440/1800/2160 */
    uint32_t supersample;     /* per axis: 1, 2 or 3; 2 means four samples/pixel */
    uint32_t antialiasing;
    uint32_t anisotropy;      /* 1 = original; otherwise 2/4/8/16 */
    uint32_t frame_mode, frame_rate; /* Original requires 0; other modes 60/120 */
    uint32_t aspect_numerator, aspect_denominator; /* initial internal canvas */
};
void pilot_visual_mods_default(struct PilotVisualMods *options);
int pilot_visual_mods_validate(const struct PilotVisualMods *options);
/* Returns false before allocation on overflow, unsupported dimensions or an
 * invalid plan. Target resources share the scene's rational X/Y scale while
 * guest descriptors and CPU layout stay untouched. Integer ceil is deliberate. */
int pilot_visual_mods_size(const struct PilotVisualMods *options,
    uint32_t guest_width,uint32_t guest_height,uint32_t target_width,uint32_t target_height,
    uint32_t maximum_dimension,uint32_t size[2]);
/* Scale the presentation-aspect image family. Square lookup/data/shadow
 * targets retain their original sampling grid; RT usage alone is not proof
 * that a resource is a scene image. Keep this classification independent. */
int pilot_visual_mods_target_size(const struct PilotVisualMods *options,
    uint32_t guest_width,uint32_t guest_height,uint32_t target_width,uint32_t target_height,
    uint32_t maximum_dimension,uint32_t size[2]);
#ifdef __cplusplus
}
#endif
#endif
