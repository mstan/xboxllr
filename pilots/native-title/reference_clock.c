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
    clock->origin=clock->ticks=clock->pending=initial;clock->initialized=1;return NULL;
}
const char *pilot_reference_clock_quantum(PilotReferenceClock *clock,uint32_t max_ticks)
{
    if(!clock || !clock->initialized || clock->busy || clock->pending!=clock->ticks)
        return "reference clock quantum outside stopped drained owner";
    if(clock->failure)return clock->failure;
    if(!max_ticks && clock->work_blocks_per_ms)
        return "reference work limit requires a bounded quantum";
    clock->max_step=max_ticks;return NULL;
}
const char *pilot_reference_clock_work(PilotReferenceClock *clock,
    uint32_t blocks_per_ms,uint64_t initial_blocks)
{
    if(!clock || !clock->initialized || clock->busy || clock->pending!=clock->ticks)
        return "reference work configuration outside stopped drained owner";
    if(clock->failure)return clock->failure;
    if(blocks_per_ms>2000000 || (blocks_per_ms && !clock->max_step))
        return "reference work limit requires bounded quantum and reviewed cadence";
    clock->work_blocks_per_ms=blocks_per_ms;clock->work_blocks=initial_blocks;
    clock->work_fraction=0;return NULL;
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
    const char *error=NULL;
    uint64_t proposed=clock->pending,target=clock->frames;
    uint64_t allowance=clock->max_step;
    uint32_t fraction=clock->work_fraction;
    if(clock->work_blocks_per_ms) {
        if(block_entries<clock->work_blocks)error="reference work counter went backwards";
        else {
            uint64_t delta=block_entries-clock->work_blocks;
            uint64_t whole=delta/clock->work_blocks_per_ms;
            uint64_t residual=(delta%clock->work_blocks_per_ms)*1125+fraction;
            uint64_t partial=residual/clock->work_blocks_per_ms;
            fraction=(uint32_t)(residual%clock->work_blocks_per_ms);
            /* Saturate before multiplying: even UINT64_MAX observed blocks
             * cannot overflow or bank time for later zero-work calls. */
            allowance=whole>clock->max_step/1125?clock->max_step:whole*1125;
            allowance=partial>clock->max_step-allowance?clock->max_step:allowance+partial;
        }
    }
    if(!error && proposed==clock->ticks) {
        error=clock->drive(clock->input_context,block_entries);
        if(!error)error=clock->input(clock->input_context,&proposed);
        if(!error && proposed<clock->ticks)error="reference clock input went backwards";
        if(!error)clock->pending=proposed;
    }
    if(!error && clock->max_step && proposed-clock->ticks>allowance)
        proposed=clock->ticks+allowance;
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
    if(!error) {
        clock->ticks=proposed;clock->frames=target;
        if(clock->work_blocks_per_ms) {
            clock->work_blocks=block_entries;clock->work_fraction=fraction;
        }
    }
    else clock->failure=error; /* Never publish a failed partial audio advance. */
    clock->busy=0;return error;
}
