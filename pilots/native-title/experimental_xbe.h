/* Prerelease fingerprint gate only; native CPU/service guards stay active. */
#ifndef PILOT_EXPERIMENTAL_XBE_H
#define PILOT_EXPERIMENTAL_XBE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static uint32_t pilot_experimental_word(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static const char *pilot_experimental_xbe_check(const unsigned char *bytes,size_t size,
    const char *actual,const char *request,uint32_t expected_title)
{
    if(!request || strcmp(request,actual))return "XBE differs from generated artifact";
    if(size<0x178 || memcmp(bytes,"XBEH",4))return "experimental XBE header is invalid";
    uint32_t base=pilot_experimental_word(bytes+0x104),headers=pilot_experimental_word(bytes+0x108);
    uint32_t address=pilot_experimental_word(bytes+0x118);
    if(headers<0x178 || headers>size || address<base)return "experimental XBE certificate is invalid";
    uint64_t cert=(uint64_t)address-base;
    if(cert<0x178 || cert+0xb0>headers)return "experimental XBE certificate is invalid";
    uint32_t extent=pilot_experimental_word(bytes+(size_t)cert);
    if(extent<0xb0 || cert+extent>headers)return "experimental XBE certificate is invalid";
    if(pilot_experimental_word(bytes+(size_t)cert+8)!=expected_title)
        return "experimental XBE belongs to another game";
    return NULL;
}
#endif
