#ifndef PILOT_KERNEL_AV_CAPS_H
#define PILOT_KERNEL_AV_CAPS_H
#include <stdint.h>
#include "settings_factory.h"
/* SMC code 6 is composite. It is not AV_PACK_SVIDEO (also numbered 6).
 * Keep the native boot export and read-only capability policy coherent. */
#define PILOT_NATIVE_SMC_VIDEO_MODE 6u
static inline uint32_t pilot_av_word(const unsigned char *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline int pilot_av_capabilities(const unsigned char image[256],uint32_t *value)
{
    if(pilot_factory_checksum(image)!=UINT32_MAX)return 0;
    uint64_t sum=0;
    for(unsigned i=0x60;i<0xc0;i+=4) {
        sum+=pilot_av_word(image+i);sum=(sum&UINT32_MAX)+(sum>>32);
    }
    if((uint32_t)sum!=UINT32_MAX)return 0;
    /* XC_FACTORY_AV_REGION + XC_VIDEO, excluding the pack/standard bits
     * from the user preference word. The native device advertises composite.
     * Bit roles and VOID query ABI: nxdk xboxkrnl; original query reference:
     * tools/native_av_reference.py (capture result 004e0101). */
    *value=(pilot_av_word(image+0x58)&0x00c00f00u)|
           (pilot_av_word(image+0x94)&~0x00000fffu)|1u;
    return 1;
}
#endif
