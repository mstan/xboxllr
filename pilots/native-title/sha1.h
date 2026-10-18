#ifndef PILOT_SHA1_H
#define PILOT_SHA1_H
/* Portable SHA-1 with the original 116-byte XcSHA caller representation.
 * Bytes 0..23 are preserved. Count is a 64-bit BYTE count, high DWORD first
 * at44/48; this differs from the reviewed Phobos SHA context/bit count. */
#include <stdint.h>
#include <string.h>
static uint32_t pilot_sha_word(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void pilot_sha_put(unsigned char *p,uint32_t v)
{for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t pilot_sha_rol(uint32_t v,unsigned n){return (v<<n)|(v>>(32-n));}
static void pilot_sha_block(unsigned char *context,const unsigned char *input)
{
    uint32_t w[80],state[5];
    for(unsigned i=0;i<16;++i)w[i]=(uint32_t)input[i*4]<<24|(uint32_t)input[i*4+1]<<16|
        (uint32_t)input[i*4+2]<<8|input[i*4+3];
    for(unsigned i=16;i<80;++i)w[i]=pilot_sha_rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    for(unsigned i=0;i<5;++i)state[i]=pilot_sha_word(context+24+i*4);
    uint32_t a=state[0],b=state[1],c=state[2],d=state[3],e=state[4];
    for(unsigned i=0;i<80;++i) {
        uint32_t f,k;
        if(i<20){f=(b&c)|(~b&d);k=0x5a827999u;}
        else if(i<40){f=b^c^d;k=0x6ed9eba1u;}
        else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdcu;}
        else {f=b^c^d;k=0xca62c1d6u;}
        uint32_t next=pilot_sha_rol(a,5)+f+e+k+w[i];
        e=d;d=c;c=pilot_sha_rol(b,30);b=a;a=next;
    }
    uint32_t out[]={a,b,c,d,e};
    for(unsigned i=0;i<5;++i)pilot_sha_put(context+24+i*4,state[i]+out[i]);
}
static void pilot_sha_init(unsigned char *context)
{
    const uint32_t state[]={0x67452301u,0xefcdab89u,0x98badcfeu,0x10325476u,0xc3d2e1f0u};
    for(unsigned i=0;i<5;++i)pilot_sha_put(context+24+i*4,state[i]);
    pilot_sha_put(context+44,0);pilot_sha_put(context+48,0);
}
static void pilot_sha_update(unsigned char *context,const unsigned char *input,uint32_t length)
{
    uint64_t count=((uint64_t)pilot_sha_word(context+44)<<32)|pilot_sha_word(context+48);
    unsigned used=(unsigned)(count&63);count+=length;
    pilot_sha_put(context+44,(uint32_t)(count>>32));pilot_sha_put(context+48,(uint32_t)count);
    if(used && length>=64-used) {
        unsigned take=64-used;memcpy(context+52+used,input,take);
        pilot_sha_block(context,context+52);input+=take;length-=take;used=0;
    }
    while(length>=64) {
        /* Original Update stages unaligned blocks in the public buffer.
         * Preserve those observable bytes as well as the resulting digest. */
        if((uintptr_t)input&3){memcpy(context+52,input,64);pilot_sha_block(context,context+52);}
        else pilot_sha_block(context,input);
        input+=64;length-=64;
    }
    if(length)memcpy(context+52+used,input,length);
}
static void pilot_sha_final(unsigned char *context,unsigned char *digest)
{
    uint64_t bytes=((uint64_t)pilot_sha_word(context+44)<<32)|pilot_sha_word(context+48);
    union {uint32_t alignment;unsigned char bytes[72];} padding={0};
    unsigned char *pad=padding.bytes;memset(pad,0,sizeof(padding.bytes));pad[0]=0x80;
    unsigned length=64-(unsigned)(bytes&63);
    if(length<=8)length+=64;
    uint64_t bits=bytes<<3;
    for(unsigned i=0;i<8;++i)pad[length-8+i]=(unsigned char)(bits>>(56-i*8));
    pilot_sha_update(context,pad,length);
    for(unsigned i=0;i<5;++i) {
        uint32_t word=pilot_sha_word(context+24+i*4);
        for(unsigned j=0;j<4;++j)digest[i*4+j]=(unsigned char)(word>>(24-j*8));
    }
    pilot_sha_init(context);
}
#endif
