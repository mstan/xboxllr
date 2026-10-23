#ifndef PILOT_INPUT_MODS_H
#define PILOT_INPUT_MODS_H
#include <stdint.h>
/* The original Xbox report is little endian, independently of host ABI.
 * Saturate the asymmetric negative endpoint; neutral must remain neutral. */
static void pilot_input_camera_invert(uint8_t pad[18],unsigned horizontal,unsigned vertical)
{
    const unsigned enabled[2]={horizontal,vertical};
    for(unsigned axis=0;axis<2;++axis)if(enabled[axis]) {
        unsigned offset=14+axis*2;
        uint32_t bits=(uint32_t)pad[offset]|((uint32_t)pad[offset+1]<<8);
        int32_t value=bits>=32768?(int32_t)bits-65536:(int32_t)bits;
        value=value==-32768?32767:-value;
        bits=(uint16_t)value;
        pad[offset]=(uint8_t)bits;pad[offset+1]=(uint8_t)(bits>>8);
    }
}
#endif
