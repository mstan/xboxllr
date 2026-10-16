#include "audio_device_timeline.h"
#include "reference_clock.h"
#include "usb_lle.h"
#include <stddef.h>

/* App-lifetime input owner. Device open/close never resets the shared CPU,
 * input, PIT or TSC epoch. Actual GP/EP/ACI work remains in its registered
 * device callback after this timeline publishes each stopped input step. */
static struct Timeline {
    PilotReferenceClock clock;
    uint64_t input,blocks;
    unsigned blocks_per_ms;
    PilotAudioDeviceInputTime input_time;
    const char *failure;
} timeline;

static const char *input_observe(void *context,uint64_t *ticks)
{
    struct Timeline *t=context;*ticks=t->input;return NULL;
}
static const char *input_drive(void *context,uint64_t blocks)
{
    struct Timeline *t=context;
    if(blocks<t->blocks)return "original audio reference block counter moved backwards";
    if(blocks-t->blocks>=t->blocks_per_ms) {
        if(t->input>UINT64_MAX-1125)return "original audio reference PIT input overflow";
        t->input+=1125;t->blocks=blocks;
    }
    return NULL;
}
static const char *timeline_observe(void *context,uint64_t *ticks)
{
    struct Timeline *t=context;
    return t->failure?t->failure:pilot_reference_clock_observe(&t->clock,ticks);
}
static const char *timeline_step(void *context,uint64_t blocks)
{
    struct Timeline *t=context;
    if(t->failure)return t->failure;
    const char *error=pilot_reference_clock_step(&t->clock,blocks);
    if(!error && t->input_time((t->clock.ticks-t->clock.origin)/1125))
        error="original audio reference controller time rejected";
    if(error)t->failure=error;
    return error;
}
static const char *timeline_counter(void *context,uint64_t *counter)
{
    struct Timeline *t=context;
    return t->failure?t->failure:pilot_reference_clock_tsc(&t->clock,counter);
}
const char *pilot_audio_device_timeline_start(unsigned blocks_per_ms,
    PilotAudioDeviceInputTime input_time)
{
    if(timeline.clock.initialized)return "original audio reference timeline already owned";
    if(!blocks_per_ms || blocks_per_ms>2000000 || !input_time)
        return "original audio reference timeline requires reviewed cadence and input observer";
    uint64_t next=0;
    const char *error=pilot_usb_pit_clock(&timeline.input,&next);
    if(!error && !next)error="original audio reference timeline requires original programmed PIT";
    if(error)return error;
    timeline.blocks_per_ms=blocks_per_ms;timeline.input_time=input_time;
    error=pilot_reference_clock_init(&timeline.clock,input_observe,input_drive,&timeline);
    if(!error && input_time(0))error="original audio reference initial controller time rejected";
    int bound=0;
    if(!error) {
        error=pilot_io_device_clock_bind_step(timeline_observe,&timeline,timeline_step);
        bound=!error;
    }
    if(!error)error=pilot_io_device_tsc_bind(timeline_counter,&timeline);
    if(error) {
        timeline.failure=error;
        if(bound)pilot_io_device_clock_bind(NULL,NULL);
    }
    return error;
}
const char *pilot_audio_device_timeline_counter(uint64_t *counter)
{return timeline_counter(&timeline,counter);}
const char *pilot_audio_device_timeline_quantum(uint32_t max_ticks)
{return pilot_reference_clock_quantum(&timeline.clock,max_ticks);}
const char *pilot_audio_device_timeline_work(uint64_t initial_blocks)
{return pilot_reference_clock_work(&timeline.clock,timeline.blocks_per_ms,initial_blocks);}
