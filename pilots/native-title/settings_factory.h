#ifndef PILOT_SETTINGS_FACTORY_H
#define PILOT_SETTINGS_FACTORY_H
#include <stdint.h>
typedef struct PilotFactoryField {unsigned offset,bytes,type;} PilotFactoryField;
static inline int pilot_factory_field(uint32_t index,PilotFactoryField *field)
{
    static const PilotFactoryField fields[]={{0x34,12,3},{0x40,6,3},{0x48,16,3},{0x58,4,4}};
    if(index<0x100 || index>0x103)return 0;
    *field=fields[index-0x100];return 1;
}
/* Factory section includes its checksum, at30..5F. Original software uses
 * DWORD little-endian addition with end-around carry; valid sum isFFFFFFFF. */
static inline uint32_t pilot_factory_checksum(const unsigned char image[256])
{
    uint64_t sum=0;
    for(unsigned offset=0x30;offset<0x60;offset+=4) {
        uint32_t value=(uint32_t)image[offset] | ((uint32_t)image[offset+1]<<8) |
            ((uint32_t)image[offset+2]<<16) | ((uint32_t)image[offset+3]<<24);
        sum+=value;sum=(sum&UINT32_MAX)+(sum>>32);
    }
    return (uint32_t)sum;
}
#endif
