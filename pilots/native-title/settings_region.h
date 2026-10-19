#ifndef PILOT_SETTINGS_REGION_H
#define PILOT_SETTINGS_REGION_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
enum {PILOT_CONSOLE_SETTINGS_BYTES=44};
static inline int pilot_region_parse(const unsigned char *blob,size_t size,const unsigned char identity[32],uint32_t *region)
{
    if(!blob || !identity || !region || size!=PILOT_CONSOLE_SETTINGS_BYTES || memcmp(blob,"XLLESET1",8))return 0;
    if(memcmp(blob+8,identity,32))return 0;
    uint32_t value=(uint32_t)blob[40] | ((uint32_t)blob[41]<<8) | ((uint32_t)blob[42]<<16) | ((uint32_t)blob[43]<<24);
    uint32_t low=value&UINT32_C(0x7fffffff);
    if(!value || (low&(low-1)))return 0;
    *region=value;return 1;
}
#endif
