#ifndef PILOT_KERNEL_HMAC_H
#define PILOT_KERNEL_HMAC_H
/* Portable XcHMAC data transform. nxdk declares two input spans and a 20-byte
 * digest. The owned kernel's default XcHMAC implementation (80033e9c) caps
 * key consumption at 64 bytes; unlike RFC 2104 it does not hash longer keys.
 * No console keys, donor implementation or host crypto provider is included.
 */
#include <stddef.h>
#include "sha1.h"

static void pilot_hmac_erase(void *data,size_t bytes)
{
    volatile unsigned char *p=(volatile unsigned char *)data;
    while(bytes--)*p++=0;
}

static int pilot_xc_hmac(const unsigned char *key,uint32_t key_length,
                         const unsigned char *first,uint32_t first_length,
                         const unsigned char *second,uint32_t second_length,
                         unsigned char *output)
{
    const uint32_t consumed=key_length>64?64:key_length;
    if((consumed && !key) || (first_length && !first) ||
       (second_length && !second) || !output)return 0;
    unsigned char context[116]={0},ipad[64],opad[64],inner[20],result[20];
    for(unsigned i=0;i<64;++i) {
        const unsigned char value=i<consumed?key[i]:0;
        ipad[i]=value^0x36;opad[i]=value^0x5c;
    }
    pilot_sha_init(context);pilot_sha_update(context,ipad,64);
    if(first_length)pilot_sha_update(context,first,first_length);
    if(second_length)pilot_sha_update(context,second,second_length);
    pilot_sha_final(context,inner);
    pilot_sha_init(context);pilot_sha_update(context,opad,64);
    pilot_sha_update(context,inner,20);pilot_sha_final(context,result);
    /* Both input spans and the key are fully consumed before output changes,
     * including when callers intentionally reuse their input as the digest. */
    memcpy(output,result,20);
    pilot_hmac_erase(context,sizeof(context));pilot_hmac_erase(ipad,sizeof(ipad));
    pilot_hmac_erase(opad,sizeof(opad));pilot_hmac_erase(inner,sizeof(inner));
    pilot_hmac_erase(result,sizeof(result));
    return 1;
}
#endif
