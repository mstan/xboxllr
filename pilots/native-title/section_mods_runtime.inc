/* A loader view never replaces the identity-checked source image. Its owned
 * bytes live for the process, including unload/reload of deferred sections. */
static unsigned char *image_loader_view;
static size_t image_loader_length;
static const unsigned char *image_load_bytes(void)
{return image_loader_view?image_loader_view:image_bytes;}
static size_t image_load_size(void)
{return image_loader_view?image_loader_length:image_size;}
#ifdef PILOT_SECTION_MODS
#include "section_mods.h"
#include "section_mods_hash.inc"
static int section_mod_contains_code(void *context,uint32_t address,uint32_t bytes)
{
    (void)context;
    for(uint64_t at=address;at<(uint64_t)address+bytes;++at)
        if(recomp_lookup((uint32_t)at))return 1;
    return 0;
}
static void image_mod_initialize(const char *verified_sha256)
{
    const struct PilotSectionModPlan *plan=pilot_title_section_mods();
    if(!plan)return;
    if(image_loader_view)stop("section mod initialized twice",0);
    const struct PilotSectionModChecks checks={section_mod_hash,section_mod_contains_code,NULL};
    const char *error=pilot_section_mods_create(image_bytes,image_size,verified_sha256,plan,
        &checks,&image_loader_view,&image_loader_length);
    if(error)stop(error,0);
    fprintf(stderr,"[SECTION MOD] id=%s count=%u original_sha256=%s loader_bytes=%zu\n",
        plan->id,plan->count,verified_sha256,image_loader_length);
}
#else
static void image_mod_initialize(const char *verified_sha256)
{(void)verified_sha256;}
#endif
