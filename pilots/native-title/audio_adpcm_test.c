/* Compare independently structured HLE/APU donor decoders outside production. */
#include "xbox_adpcm.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL ADPCM line %d: %s\n",__LINE__,#c);return 1;}}while(0)
int pilot_adpcm_lle_reference(int16_t*,const uint8_t*,size_t,int);
int main(void)
{
    uint32_t random=0x41f036ab;unsigned cases=0;
    for(unsigned channels=1;channels<=2;++channels)for(unsigned index=0;index<256;++index) {
        uint8_t block[72];int16_t lle[130],hle[130],changed[128];
        for(unsigned i=0;i<36*channels;++i){random=random*1664525+1013904223;block[i]=(uint8_t)(random>>24);}
        for(unsigned c=0;c<channels;++c){block[c*4+2]=(uint8_t)index;block[c*4+3]=index&1?8:255;}
        memset(hle,0xa5,sizeof(hle));
        CHECK(pilot_adpcm_lle_reference(lle,block,36*channels,channels)==65);
        CHECK(xbox_adpcm_decode_block(block,36*channels,channels,hle+1,64*channels));
        CHECK(!memcmp(lle,hle+1,64*channels*2));
        CHECK((uint16_t)hle[0]==0xa5a5 && (uint16_t)hle[64*channels+1]==0xa5a5);
        for(unsigned c=0;c<channels;++c)block[32*channels+c*4+3]^=0xf0;
        CHECK(xbox_adpcm_decode_block(block,36*channels,channels,changed,64*channels));
        CHECK(!memcmp(changed,hle+1,64*channels*2)); /* final high nibble is not played */
        ++cases;
    }
    uint8_t block[72]={0};int16_t output[128],before[128];memset(output,0x57,sizeof(output));memcpy(before,output,sizeof(output));
    CHECK(!xbox_adpcm_decode_block(block,35,1,output,128));
    CHECK(!xbox_adpcm_decode_block(block,72,2,output,127));
    CHECK(!xbox_adpcm_decode_block(block,72,3,output,128));
    CHECK(!xbox_adpcm_decode_block(NULL,36,1,output,128));
    CHECK(!memcmp(output,before,sizeof(output)));
    fprintf(stderr,"PASS: %u HLE/APU mono/stereo blocks, saturation/index/reserved headers, output extent and 64-frame consumption\n",cases);
    return 0;
}
