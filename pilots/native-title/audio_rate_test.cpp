// Compare independently generated tones through maintained source/rate DSP and
// actual native XAudio2. Fits tolerate arbitrary capture phase, not wrong pitch.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"native rate FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#include "../../tests/apu_rate_fixture.hpp"
#include "audio_backend.h"

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    CHECK(pilot_audio_open()==0);unsigned cases=0;
    for(unsigned channels:{1u,2u})for(int pitch:{-8192,-4096,-501,0,4096,8191}) {
        ApuRateFixture reference(channels,4096,true);reference.pitch(pitch);
        const double original_rate=48000*std::exp2(double(pitch)/4096);
        const unsigned native_rate=static_cast<unsigned>(std::lround(original_rate));
        const auto source=reference.render(8192);
        CHECK(source.size()==16384 && reference.error.empty());
        // Compose the actual source/rate stage with the maintained final send
        // stage. Upstream amplitude/filter preconditions are unity/bypass.
        reference.reg(0x20300,(1<<5)|(2<<10)|(3<<16)|(4<<21)|(5<<26));
        reference.reg(0x20304,reference.word(reference.object+4)|6|(7<<5));
        reference.reg(0x20360,0x000f000f); // first two sends 0 dB; all others mute
        reference.reg(0x20364,0xffffffff);reference.reg(0x20368,0xffffffff);
        std::vector<float> mixed(4096*32);
        for(unsigned frame=0;frame<4096;frame+=32) {
            float bins[1024]={};
            CHECK(reference.apu.mix_2d_samples(reference.voice,source.data()+(4096+frame)*2,32,bins));
            for(unsigned n=0;n<32;++n)for(unsigned bin=0;bin<32;++bin)mixed[(frame+n)*32+bin]=bins[bin*32+n];
        }
        void* voice=nullptr;
        CHECK(pilot_audio_buffer_create(unsigned(reference.pcm.size()),channels,48000,16,&voice)==0);
        CHECK(pilot_audio_buffer_copy(voice,0,reference.pcm.data(),unsigned(reference.pcm.size()),0)==0);
        const uint32_t bins[]={0,1};const int32_t volumes[]={0,0};
        CHECK(pilot_audio_buffer_mixbins(voice,2,bins,volumes)==0);
        CHECK(pilot_audio_buffer_volume(voice,0)==0);
        CHECK(pilot_audio_buffer_frequency(voice,native_rate)==0);
        CHECK(pilot_audio_buffer_play(voice,1)==0);Sleep(100);
        std::vector<float> actual(4096*32);
        CHECK(pilot_audio_capture_begin(actual.data(),4096,0)==0);
        Sleep(180);CHECK(pilot_audio_capture_end()==4096);
        for(unsigned c=0;c<2;++c) {
            const auto lle=apu_tone_fit(mixed.data()+c,4096,32,original_rate/64);
            const auto native=apu_tone_fit(actual.data()+c,4096,32,double(native_rate)/64);
            const double expected=channels==2 && c?0.1:0.2;
            CHECK(std::fabs(lle.amplitude-expected)<0.001 && lle.error<0.0003);
            CHECK(std::fabs(native.amplitude-expected)<0.002 && native.error<0.001);
            CHECK(std::fabs(lle.amplitude-native.amplitude)<0.002);
        }
        for(unsigned n=0;n<4096;++n)for(unsigned bin=2;bin<32;++bin) {
            CHECK(mixed[n*32+bin]==0);
            CHECK(std::fabs(actual[n*32+bin])<0.00003f);
        }
        uint32_t play=0,write=0;CHECK(pilot_audio_buffer_position(voice,&play,&write)==0);
        CHECK(play<reference.pcm.size() && write<reference.pcm.size());
        CHECK(pilot_audio_buffer_stop(voice)==0);pilot_audio_buffer_destroy(voice);++cases;
    }
    pilot_audio_close();
    std::printf("PASS: %u real native versus maintained rate cases, nominal 12-192 kHz, measured tone frequency/amplitude/residual and real cursors\n",cases);
}
