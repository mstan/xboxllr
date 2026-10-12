#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "audio_reference.h"
#include "reference_clock.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct Input {uint64_t ticks=0;unsigned advances=0;bool fail_after_audio=false;};
static const char *observe(void *context,uint64_t *ticks)
{*ticks=static_cast<Input*>(context)->ticks;return nullptr;}
static const char *drive(void *context,uint64_t blocks)
{(void)context;(void)blocks;return nullptr;}
static const char *advance(void *context,uint32_t frames)
{
    auto *input=static_cast<Input*>(context);++input->advances;
    CHECK(!reference_advance_frames(frames));
    return input->fail_after_audio?"expected partial consumer failure":nullptr;
}
int main()
{
    Input input;PilotReferenceClock clock{};
    uint64_t counter=0x1122334455667788ull;
    CHECK(pilot_reference_clock_tsc(&clock,&counter) && counter==0x1122334455667788ull);
    CHECK(!pilot_reference_clock_init(&clock,observe,drive,&input));
    CHECK(!pilot_audio_open());
    CHECK(!reference_capture_epoch(0));
    CHECK(!pilot_reference_clock_audio(&clock,advance,&input));
    void *buffer=nullptr;CHECK(!pilot_audio_buffer_create(4096,2,48000,16,&buffer));
    int16_t pcm[2048];for(unsigned i=0;i<1024;++i){pcm[i*2]=16384;pcm[i*2+1]=-8192;}
    CHECK(!pilot_audio_buffer_copy(buffer,0,pcm,sizeof(pcm),0));
    CHECK(!pilot_audio_buffer_volume(buffer,-600));
    uint32_t bins[]={0,1};int32_t levels[]={0,0};
    CHECK(!pilot_audio_buffer_mixbins(buffer,2,bins,levels));
    CHECK(!pilot_audio_buffer_play(buffer,1));
    float mix[48*32];CHECK(!pilot_audio_capture_begin(mix,48,0));
    input.ticks=1125;CHECK(!pilot_reference_clock_step(&clock,0));
    CHECK(!pilot_reference_clock_tsc(&clock,&counter) && counter==733333);
    CHECK(reference_output_frames()==48 && clock.frames==48 && input.advances==1);
    CHECK(pilot_audio_capture_end()==48);
    float gain=static_cast<float>(std::pow(10.,-.3));
    for(unsigned f=0;f<48;++f)for(unsigned c=0;c<32;++c) {
        float expected=c==0?.5f*gain:c==1?-.25f*gain:0;
        CHECK(std::fabs(mix[f*32+c]-expected)<1e-7);
    }
    ReferenceAudioSourceState before{},after{};CHECK(!reference_source_state(buffer,&before));
    for(unsigned i=0;i<1000;++i){uint64_t ticks=0;CHECK(!pilot_reference_clock_observe(&clock,&ticks) && ticks==1125);}
    Sleep(20);CHECK(!reference_source_state(buffer,&after));
    CHECK(after.cbo_frames==before.cbo_frames && after.source_frames_fetched==before.source_frames_fetched && reference_output_frames()==48);
    input.ticks=2250;CHECK(!pilot_reference_clock_step(&clock,0));
    uint32_t read=0,write=0;CHECK(!pilot_audio_buffer_position(buffer,&read,&write));
    CHECK(read==96*4 && write==(96+32)*4);
    CHECK(!pilot_audio_buffer_stop(buffer));pilot_audio_buffer_destroy(buffer);
    CHECK(!pilot_reference_clock_audio(&clock,nullptr,nullptr));pilot_audio_close();
    input.ticks=3375;CHECK(!pilot_reference_clock_step(&clock,0) && clock.frames==144);
    CHECK(!pilot_audio_open());CHECK(!reference_capture_epoch(3375));CHECK(!pilot_reference_clock_audio(&clock,advance,&input));
    input.ticks=4500;CHECK(!pilot_reference_clock_step(&clock,0));
    CHECK(reference_output_frames()==48 && clock.frames==192);
    input.fail_after_audio=true;input.ticks=5625;
    CHECK(pilot_reference_clock_step(&clock,0));
    CHECK(clock.ticks==4500 && clock.frames==192 && reference_output_frames()==96);
    unsigned calls=input.advances;CHECK(pilot_reference_clock_step(&clock,0) && input.advances==calls);
    counter=0x1122334455667788ull;
    CHECK(pilot_reference_clock_tsc(&clock,&counter) && counter==0x1122334455667788ull);
    pilot_audio_close();
    Input offset;offset.ticks=1125001;PilotReferenceClock epoch{};
    CHECK(!pilot_reference_clock_init(&epoch,observe,drive,&offset));
    CHECK(!pilot_reference_clock_tsc(&epoch,&counter) && counter==733333984ull);
    offset.ticks=UINT64_MAX;CHECK(!pilot_reference_clock_step(&epoch,0));
    counter=0x1122334455667788ull;
    CHECK(pilot_reference_clock_tsc(&epoch,&counter) && counter==0x1122334455667788ull);
    puts("PASS actual APU/headroom/routing/cursors; pure clock reads; detach/reopen epoch; failed consumer stops before time publication");
}
