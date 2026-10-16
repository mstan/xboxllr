#include "audio_reference.h"
#include "reference_clock.h"
#include "input_host.h"
static PilotReferenceClock audio_reference_clock;
static uint64_t audio_reference_input,audio_reference_blocks;
static uint64_t audio_reference_blocked_at;
static int audio_reference_blocked;
static uint64_t audio_reference_counter(void)
{
    uint64_t counter=0;const char *error=pilot_reference_clock_tsc(&audio_reference_clock,&counter);
    if(error)stop(error,0);
    return counter;
}
static const char *audio_reference_observe(void *context,uint64_t *ticks)
{(void)context;*ticks=audio_reference_input;return NULL;}
static const char *audio_reference_drive(void *context,uint64_t blocks)
{
    (void)context;
    if(blocks<audio_reference_blocks)return "reference input block counter went backwards";
    if(blocks-audio_reference_blocks>=PILOT_REFERENCE_BLOCKS_PER_MS) {
        if(audio_reference_input>UINT64_MAX-1125)return "reference PIT input overflow";
        audio_reference_input+=1125;audio_reference_blocks=blocks;
    }
    return NULL;
}
static const char *audio_reference_advance(void *context,uint32_t frames)
{(void)context;return reference_advance_frames(frames)<0?"reference APU consumer failed":NULL;}
static const char *audio_reference_step(void *context,uint64_t blocks)
{
    /* A suspended native service may still hold its audio owner lock while
     * the original kernel runs. Admit the next external input only after that
     * storage/configuration operation has reached a consistent boundary. */
    if(!TryAcquireSRWLockExclusive(&audio_lock)) {
        if(!audio_reference_blocked){audio_reference_blocked=1;audio_reference_blocked_at=blocks;}
        /* Original VM calls can block on the original MM critical section.
         * Clock-dependent contention is not qualified. Stop after a bounded
         * amount of actual engine work, rather than silently freeze forever. */
        if(blocks-audio_reference_blocked_at>2000000)
            return "reference input blocked by retained audio owner; contended original service unqualified";
        return NULL;
    }
    audio_reference_blocked=0;
    const char *error=pilot_reference_clock_step(context,blocks);
#ifdef PILOT_INPUT_REFERENCE_CLOCK
    if(!error && pilot_input_host_reference_time((audio_reference_clock.ticks-audio_reference_clock.origin)/1125))
        error="reference controller input time went backwards";
#endif
    ReleaseSRWLockExclusive(&audio_lock);return error;
}
static void audio_reference_clock_start(void)
{
    uint64_t next=0;
    const char *error=pilot_usb_pit_clock(&audio_reference_input,&next);
    if(!error && !next)error="reference clock requires the original programmed PIT";
    if(!error)error=pilot_reference_clock_init(&audio_reference_clock,audio_reference_observe,audio_reference_drive,NULL);
#ifdef PILOT_GPU_LLE
    if(!error)error=pilot_reference_clock_quantum(&audio_reference_clock,32);
    if(!error)error=pilot_reference_clock_work(&audio_reference_clock,PILOT_REFERENCE_BLOCKS_PER_MS,0);
#endif
    if(!error)error=pilot_io_device_clock_bind_step(pilot_reference_clock_observe,&audio_reference_clock,audio_reference_step);
    if(!error)error=pilot_io_device_tsc_bind(pilot_reference_clock_tsc,&audio_reference_clock);
    if(error)stop(error,0);
    fprintf(stderr,"[REFERENCE TIME] stopped input: %u observed blocks per 1ms PIT opportunity; caller-driven APU/input/nominal TSC; no hardware CPU-cycle claim\n",PILOT_REFERENCE_BLOCKS_PER_MS);
#ifdef PILOT_GPU_LLE
    fprintf(stderr,"[GPU REFERENCE TIME] input intervals split at <=32 PIT ticks per observed-work opportunity; common CPU/APU/USB/display epoch\n");
#endif
}
static void audio_reference_clock_open(void)
{
#ifdef PILOT_AUDIO_REFERENCE_UNIT_OWNER
    /* Individual ABI/owner fixtures have no complete runtime input timeline. */
    if(!audio_reference_clock.initialized)return;
#endif
    if(reference_capture_epoch(audio_reference_clock.ticks)<0)stop("reference capture epoch binding failed",0);
    const char *error=pilot_reference_clock_audio(&audio_reference_clock,audio_reference_advance,NULL);
    if(error)stop(error,0);
}
static void audio_reference_clock_close(void)
{
#ifdef PILOT_AUDIO_REFERENCE_UNIT_OWNER
    if(!audio_reference_clock.initialized)return;
#endif
    const char *error=pilot_reference_clock_audio(&audio_reference_clock,NULL,NULL);
    if(error)stop(error,0);
}
