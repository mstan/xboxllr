// Finite output and completion contract against the independent native engine.
// Sequential observations only; the production path never runs both engines.
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"native completion FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#include "../../tests/apu_completion_fixture.hpp"
#include "audio_backend.h"

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    CHECK(pilot_audio_open()==0);unsigned cases=0;
    for(unsigned channels:{1u,2u})for(int pitch:{-4096,0,4096}) {
        // Odd finite length and a distinct last 32 samples expose lost tails.
        ApuCompletionFixture reference(channels,1057);reference.pitch(pitch);
        std::vector<float> expected;bool completed=false;unsigned quanta=0;
        while(!completed) {
            CHECK(++quanta<=80);
            std::array<float,1024> bins{};uint32_t count=0;
            CHECK(reference.apu.process_2d_voice(reference.voice,bins.data(),count,completed));
            for(unsigned n=0;n<count;++n)for(unsigned c=0;c<2;++c)expected.push_back(bins[c*32+n]);
            CHECK(expected.size()<=1057*4);
        }
        CHECK(reference.completions==1 && reference.ram[reference.notify+15]==1);
        CHECK(!(reference.word(reference.object+0x54)&(1u<<21)));
        CHECK((reference.word(reference.object+0x58)&0xffffff)==1056);

        const unsigned rate=pitch<0?24000:pitch>0?96000:48000;
        void* voice=nullptr;
        CHECK(pilot_audio_buffer_create(unsigned(reference.pcm.size()),channels,rate,16,&voice)==0);
        CHECK(pilot_audio_buffer_copy(voice,0,reference.pcm.data(),unsigned(reference.pcm.size()),0)==0);
        const uint32_t bins[]={0,1};const int32_t volumes[]={0,0};
        CHECK(pilot_audio_buffer_mixbins(voice,2,bins,volumes)==0);
        std::vector<float> actual(8192*32);
        CHECK(pilot_audio_capture_begin(actual.data(),8192,0)==0);
        CHECK(pilot_audio_buffer_play(voice,0)==0);
        Sleep(220);CHECK(pilot_audio_capture_end()==8192);
        uint32_t status=99;CHECK(pilot_audio_buffer_status(voice,&status)==0);CHECK(status==0);
        for(unsigned c=0;c<2;++c) {
            double lle_area=0,native_area=0,lle_tail=0,native_tail=0;
            const double sign=channels==2 && c?-1:1;
            for(unsigned n=0;n<expected.size()/2;++n) {
                const double sample=expected[n*2+c]*sign;lle_area+=sample;
                lle_tail=std::max(lle_tail,sample);
            }
            for(unsigned n=0;n<8192;++n) {
                const double sample=actual[n*32+c]*sign;native_area+=sample;
                native_tail=std::max(native_tail,sample);
            }
            const double peak=channels==2 && c?0.5:0.75;
            // Different interpolation filters can differ at finite edges.
            // Bound area by three peak sample-frames plus 0.2% of the area;
            // the distinctive tail must also survive in both real outputs.
            const double tolerance=3*peak+0.002*std::fabs(lle_area);
            std::printf("finite ch=%u rate=%u channel=%u area %.6f/%.6f tolerance %.6f tail %.6f/%.6f\n",
                        channels,rate,c,lle_area,native_area,tolerance,lle_tail,native_tail);
            CHECK(std::fabs(lle_area-native_area)<tolerance);
            CHECK(lle_tail>peak*0.9 && native_tail>peak*0.9);
        }
        for(unsigned n=0;n<8192;++n)for(unsigned bin=2;bin<32;++bin)
            CHECK(std::fabs(actual[n*32+bin])<0.00003f);
        // Completion remains stable, and the actual engine emits no repeated
        // buffer/tail after the first capture window.
        std::vector<float> after(2048*32);
        CHECK(pilot_audio_capture_begin(after.data(),2048,0)==0);
        Sleep(80);CHECK(pilot_audio_capture_end()==2048);
        CHECK(pilot_audio_buffer_status(voice,&status)==0 && status==0);
        for(float sample:after)CHECK(std::fabs(sample)<0.00003f);
        pilot_audio_buffer_destroy(voice);++cases;
    }
    pilot_audio_close();
    std::printf("PASS: %u finite native/LLE output, final-tail, stopped-status and no-replay comparisons\n",cases);
}
