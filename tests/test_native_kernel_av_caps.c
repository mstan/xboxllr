#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>
#include "kernel_av_caps.h"
static uint32_t words[64],g_eax,g_esp;
static unsigned char eeprom[256],settings_cache[256];
static int settings_cache_valid,have_eeprom=1,pilot_verbose,eeprom_lock;
static jmp_buf stopped;
static int expecting_stop;
#define MEM32(at) words[(at)/4]
#define AcquireSRWLockExclusive(lock) ((void)(lock))
#define ReleaseSRWLockExclusive(lock) ((void)(lock))
static void stop(const char *why,uint32_t value)
{
    (void)value;if(!expecting_stop){fprintf(stderr,"unexpected guard: %s\n",why);exit(1);}longjmp(stopped,1);
}
#include "kernel_av_caps.inc"
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);return 1;}}while(0)
#define GUARD(action) do{expecting_stop=1;if(!setjmp(stopped)){action;fprintf(stderr,"missing guard %d\n",__LINE__);return 1;}expecting_stop=0;}while(0)
static void put(unsigned char *p,uint32_t v)
{for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(8*i));}
static void checksum(unsigned char *image,unsigned first,unsigned end)
{
    put(image+first,0);uint64_t sum=0;
    for(unsigned i=first;i<end;i+=4){sum+=pilot_av_word(image+i);sum=(sum&UINT32_MAX)+(sum>>32);}
    put(image+first,~(uint32_t)sum);
}
int main(void)
{
    unsigned char image[256]={0};uint32_t value=0xdeadbeef;
    put(image+0x58,0x00400100);put(image+0x94,0x000e0000);
    checksum(image,0x30,0x60);checksum(image,0x60,0xc0);
    CHECK(pilot_av_capabilities(image,&value) && value==0x004e0101);
    /* Authored alternate preferences: user standard/pack cannot replace
     * factory standard or the selected native composite device. */
    put(image+0x58,0x00800300);put(image+0x94,0x00110f06);
    checksum(image,0x30,0x60);checksum(image,0x60,0xc0);
    CHECK(pilot_av_capabilities(image,&value) && value==0x00910301);
    value=0xdeadbeef;image[0x40]^=1;
    CHECK(!pilot_av_capabilities(image,&value) && value==0xdeadbeef);
    image[0x40]^=1;memcpy(eeprom,image,256);
    memset(words,0xa5,sizeof(words));g_eax=0xdeadbeef;g_esp=16;
    MEM32(20)=0;MEM32(24)=6;MEM32(28)=0;MEM32(32)=128;
    kernel_av_query();
    CHECK(g_eax==0xdeadbeef && g_esp==36 && MEM32(128)==0x00910301);
    CHECK(MEM32(124)==0xa5a5a5a5 && MEM32(132)==0xa5a5a5a5);
    CHECK(settings_cache_valid);
    /* The original settings first-read cache owns subsequent queries. */
    memset(eeprom,0,256);g_esp=16;kernel_av_query();CHECK(MEM32(128)==0x00910301);
    g_esp=16;MEM32(24)=1;GUARD(kernel_av_query());CHECK(g_esp==16 && g_eax==0xdeadbeef);
    MEM32(24)=6;MEM32(32)=0;GUARD(kernel_av_query());
    MEM32(32)=128;MEM32(20)=0xfd000000;GUARD(kernel_av_query());
    MEM32(20)=0;settings_cache[0x90]^=1;GUARD(kernel_av_query());
    image[0x90]^=1;
    CHECK(!pilot_av_capabilities(image,&value) && value==0xdeadbeef);
    puts("PASS native AV capabilities: original reference, preference masks, invalid checksums");return 0;
}
