#ifndef PILOT_SECTION_MODS_H
#define PILOT_SECTION_MODS_H
#include <stddef.h>
#include <stdint.h>

/* Reviewed, statically linked data mods. The original input and CPU identity
 * remain immutable. A selected section gets its own appended payload/pages;
 * all other bytes and section descriptors retain their original values.
 * Counter reservations are title-authored proof inputs, never inferred free
 * merely because header bytes happen to be zero. */
#define PILOT_SECTION_MOD_ABI 1u
struct PilotSectionMod {
    uint32_t index, counter_offset;
    unsigned char original_descriptor[56], payload_sha256[32];
    const unsigned char *payload;
    uint32_t bytes;
};
struct PilotSectionModPlan {
    uint32_t abi, count;
    const char *id, *image_sha256;
    const struct PilotSectionMod *sections;
};
enum PilotSectionHash { PILOT_SECTION_SHA256, PILOT_SECTION_LENGTH_SHA1 };
struct PilotSectionModChecks {
    /* LENGTH_SHA1 hashes LE32(bytes) followed by the exact payload. */
    int (*hash)(void *context,enum PilotSectionHash kind,const void *data,
                uint32_t bytes,unsigned char *out);
    /* Reject any generated CPU entry within the original section span. */
    int (*contains_code)(void *context,uint32_t address,uint32_t bytes);
    void *context;
};
/* On rejection *view/bytes remain NULL/zero. Success returns an owned loader
 * view, released by free() only after every deferred section load has ended.
 * There are no filesystem operations or writes into the source image. */
const char *pilot_section_mods_create(const unsigned char *original,size_t bytes,
    const char *image_sha256,const struct PilotSectionModPlan *plan,
    const struct PilotSectionModChecks *checks,unsigned char **view,size_t *view_bytes);
/* Defined only by the selected title-owned registry; NULL disables the mod. */
const struct PilotSectionModPlan *pilot_title_section_mods(void);
#endif
