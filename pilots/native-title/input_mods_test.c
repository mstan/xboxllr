#include "input_mods.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
    for(unsigned flags=0;flags<4;++flags)for(unsigned bits=0;bits<65536;++bits) {
        uint8_t original[18],pad[18];
        for(unsigned i=0;i<18;++i)original[i]=(uint8_t)(i*11+3);
        unsigned other=65535-bits;
        original[14]=(uint8_t)bits;original[15]=(uint8_t)(bits>>8);
        original[16]=(uint8_t)other;original[17]=(uint8_t)(other>>8);
        memcpy(pad,original,sizeof(pad));pilot_input_camera_invert(pad,flags&1,flags&2);
        if(memcmp(pad,original,14))return 1;
        for(unsigned axis=0;axis<2;++axis) {
            unsigned start=axis?other:bits,offset=14+2*axis;
            int value=start<32768?(int)start:(int)start-65536;
            int expected=(flags&(1u<<axis))?(value==-32768?32767:-value):value;
            unsigned actual=pad[offset]|((unsigned)pad[offset+1]<<8);
            if(actual!=(uint16_t)expected) {
                fprintf(stderr,"FAIL flags=%u axis=%u input=%d expected=%d actual_bits=%u\n",flags,axis,value,expected,actual);
                return 1;
            }
        }
    }
    puts("PASS independent camera axes, all signed values, neutral/endpoints and movement/button preservation");
    return 0;
}
