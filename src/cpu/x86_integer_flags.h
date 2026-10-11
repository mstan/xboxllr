#ifndef XLLE_X86_INTEGER_FLAGS_H
#define XLLE_X86_INTEGER_FLAGS_H
#include <stdint.h>
/* Materialize all six defined CMP r32 arithmetic flags. Retain the other
 * architectural flags from an established context, never a 2|IF substitute. */
static inline uint32_t xlle_x86_cmp32_flags(uint32_t left,uint32_t right,uint32_t previous)
{
    uint32_t result=left-right;
    uint32_t parity=result&255;parity^=parity>>4;parity^=parity>>2;parity^=parity>>1;
    return (previous&~0x8d5u)|(left<right?1:0)|((parity&1)?0:4)|
        ((left^right^result)&0x10)|(result?0:0x40)|(result&0x80000000u?0x80:0)|
        (((left^right)&(left^result)&0x80000000u)?0x800:0);
}
#endif
