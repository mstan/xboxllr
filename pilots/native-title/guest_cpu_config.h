#ifndef PILOT_GUEST_CPU_CONFIG_H
#define PILOT_GUEST_CPU_CONFIG_H
#include <stdint.h>
/* One configured Xbox CPU identity shared by native and original execution.
 * Cache-descriptor and unknown leaves remain unsupported. */
static inline int pilot_guest_cpuid(uint32_t leaf,uint32_t subleaf,uint32_t out[4])
{
    (void)subleaf;
    if(leaf==0){out[0]=2;out[1]=0x756e6547;out[2]=0x6c65746e;out[3]=0x49656e69;}
    else if(leaf==1){out[0]=0x68a;out[1]=0;out[2]=0;out[3]=0x0383f9ff;}
    else return 1;
    return 0;
}
#endif
