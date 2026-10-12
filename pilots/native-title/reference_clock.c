#include "reference_clock.h"
#include <string.h>

const char *pilot_reference_clock_init(PilotReferenceClock *clock,PilotIoClock input,
    PilotIoClockStep drive,void *context)
{
    if(!clock || !input || !drive)return "reference clock requires explicit input and driver";
    if(clock->initialized || clock->busy)return "reference clock already owned";
    uint64_t initial=0;const char *error=input(context,&initial);
    if(error)return error;
    memset(clock,0,sizeof(*clock));
    clock->input=input;clock->drive=drive;clock->input_context=context;
    clock->origin=clock->ticks=initial;clock->initialized=1;return NULL;
}
const char *pilot_reference_clock_audio(PilotReferenceClock *clock,
    PilotReferenceAudioAdvance advance,void *context)
{
    if(!clock || !clock->initialized || clock->busy)return "reference audio binding outside stopped owner";
    if(clock->failure)return clock->failure;
    if(advance && clock->audio)return "reference audio consumer already owned";
    clock->audio=advance;clock->audio_context=advance?context:NULL;
    /* A fresh engine starts here in the common epoch. Detached time remains
     * real device time; no old PCM or DSP state belongs to the fresh engine. */
    clock->audio_frame=clock->frames;return NULL;
}
const char *pilot_reference_clock_observe(void *context,uint64_t *ticks)
{
    PilotReferenceClock *clock=context;
    if(!clock || !clock->initialized || !ticks)return "reference clock observation outside owner";
    if(clock->failure)return clock->failure;
    *ticks=clock->ticks;return NULL;
}
const char *pilot_reference_clock_tsc(void *context,uint64_t *counter)
{
    uint64_t ticks=0;const char *error=pilot_reference_clock_observe(context,&ticks);
    if(error)return error;
    if(!counter)return "reference TSC observation without output";
    uint64_t seconds=ticks/1125000;
    uint64_t fraction=(ticks%1125000)*733333333ull/1125000;
    if(seconds>(UINT64_MAX-fraction)/733333333ull)return "reference TSC overflow";
    *counter=seconds*733333333ull+fraction;return NULL;
}
const char *pilot_reference_clock_step(void *context,uint64_t block_entries)
{
    PilotReferenceClock *clock=context;
    if(!clock || !clock->initialized || clock->busy)return "reference clock step outside stopped owner";
    if(clock->failure)return clock->failure;
    clock->busy=1;
    const char *error=clock->drive(clock->input_context,block_entries);
    uint64_t proposed=0,target=clock->frames;
    if(!error)error=clock->input(clock->input_context,&proposed);
    if(!error && proposed<clock->ticks)error="reference clock input went backwards";
    if(!error) {
        uint64_t elapsed=proposed-clock->origin,seconds=elapsed/1125000;
        if(seconds>(UINT64_MAX-47999)/48000)error="reference audio frame clock overflow";
        else target=seconds*48000+(elapsed%1125000)*48000/1125000;
    }
    while(!error && clock->audio && clock->audio_frame<target) {
        uint64_t remaining=target-clock->audio_frame;
        uint32_t frames=(uint32_t)(remaining>65536?65536:remaining);
        error=clock->audio(clock->audio_context,frames);
        if(!error)clock->audio_frame+=frames;
    }
    if(!error) {clock->ticks=proposed;clock->frames=target;}
    else clock->failure=error; /* Never publish a failed partial audio advance. */
    clock->busy=0;return error;
}
