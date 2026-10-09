// Authored Xbox block adapter. dr_wav implementation remains unmodified;
// provenance and MIT-0 license: src/third_party/dr_wav/README.md and LICENSE.
#include "apu_adpcm.h"
#define DR_WAV_NO_STDIO
#define DR_WAV_IMPLEMENTATION
#include "../third_party/dr_wav/dr_wav.h"
#include <string.h>

static void put16(uint8_t* p, unsigned n) { p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); }
static void put32(uint8_t* p, unsigned n) { put16(p,n); put16(p+2,n>>16); }

int xlle_apu_decode_adpcm(const uint8_t* block, size_t bytes, unsigned channels,
                          int16_t* output, size_t samples) {
    if(!block || !output || channels<1 || channels>2 ||
       bytes!=36*channels || samples<64*channels)return 0;

    // Present a single block through the public memory WAV interface. Standard
    // IMA decodes 65 frames here; the Xbox VP consumes the first 64, including
    // the header predictor. The last high nibble has no played sample.
    uint8_t wav[48+72]={0};
    memcpy(wav,"RIFF",4);put32(wav+4,40+(unsigned)bytes);
    memcpy(wav+8,"WAVEfmt ",8);put32(wav+16,20);
    put16(wav+20,DR_WAVE_FORMAT_DVI_ADPCM);put16(wav+22,channels);
    put32(wav+24,48000);put32(wav+28,48000*(unsigned)bytes/65);
    put16(wav+32,(unsigned)bytes);put16(wav+34,4);put16(wav+36,2);put16(wav+38,65);
    memcpy(wav+40,"data",4);put32(wav+44,(unsigned)bytes);
    memcpy(wav+48,block,bytes);
    for(unsigned ch=0;ch<channels;++ch) {
        const unsigned index=block[ch*4+2];
        wav[48+ch*4+2]=(uint8_t)(index>=128?0:index>88?88:index);
    }
    drwav decoder;
    if(!drwav_init_memory(&decoder,wav,48+bytes,NULL))return 0;
    int16_t decoded[128];
    const drwav_uint64 frames=drwav_read_pcm_frames_s16(&decoder,64,decoded);
    const drwav_result result=drwav_uninit(&decoder);
    if(frames!=64 || result!=DRWAV_SUCCESS)return 0;
    memcpy(output,decoded,64*channels*sizeof(*output));
    return 1;
}
