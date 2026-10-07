/* Real XAudio2 consumption, separate from guest stream policy. Silent PCM
 * keeps the probe unobtrusive; success requires the device's sample count. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "audio_output.h"

int main()
{
    const uint32_t rate=48000,bytes=rate*4/10;
    uint8_t pcm[bytes]={};
    recomp_audio_output_initialize();
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        recomp_audio_output_reset_voice(0);
        if(!recomp_audio_output_submit(0,pcm,bytes,rate,2,16,0)) {
            fprintf(stderr,"FAIL: actual host submission rejected\n");return 1;
        }
        uint64_t played=0,previous=0;uint32_t queued=0;
        ULONGLONG deadline=GetTickCount64()+3000;
        do {
            if(!recomp_audio_output_position(0,&played,&queued) || played<previous) {
                fprintf(stderr,"FAIL: missing or decreasing device position\n");return 1;
            }
            previous=played;
            if(played>=bytes && !queued)break;
            Sleep(5);
        } while(GetTickCount64()<deadline);
        if(played!=bytes || queued) {
            fprintf(stderr,"FAIL: consumed %llu/%u bytes, %u queued\n",(unsigned long long)played,bytes,queued);return 1;
        }
        printf("PASS: lifetime %u consumed %u actual PCM bytes\n",lifetime+1,bytes);
    }
    recomp_audio_output_shutdown();
    return 0;
}
