/* Authored portable input-admission tests; no Windows/audio backend needed.
 * Frames use the independent reduced ratio16/375; no device is an oracle. */
#include "reference_clock.h"
#include <stdio.h>
#include <string.h>

static unsigned failures;
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);++failures; } } while(0)
struct Input {
    PilotReferenceClock *clock;
    uint64_t current,next,frames,last_blocks,expected_published;
    unsigned drives,reads,advances;
    int drive_failure,input_failure,audio_failure,check_busy;
};
static const char *input(void *context,uint64_t *ticks)
{
    struct Input *in=(struct Input*)context;++in->reads;
    if(in->input_failure)return "authored input failure";
    *ticks=in->current;return NULL;
}
static const char *drive(void *context,uint64_t blocks)
{
    struct Input *in=(struct Input*)context;++in->drives;in->last_blocks=blocks;
    if(in->drive_failure)return "authored drive failure";
    in->current=in->next;return NULL;
}
static const char *advance(void *context,uint32_t frames)
{
    struct Input *in=(struct Input*)context;
    uint64_t ticks=0;
    CHECK(frames>0 && frames<=65536);
    CHECK(!pilot_reference_clock_observe(in->clock,&ticks));
    CHECK(ticks==in->expected_published); /* Input is not published early. */
    ++in->advances;in->frames+=frames;
    if(in->check_busy) {
        CHECK(pilot_reference_clock_quantum(in->clock,7)!=NULL);
        CHECK(pilot_reference_clock_step(in->clock,123)!=NULL);
        CHECK(pilot_reference_clock_audio(in->clock,NULL,NULL)!=NULL);
    }
    return in->audio_failure?"authored consumer failure":NULL;
}
static const char *step(PilotReferenceClock *clock,uint64_t blocks)
{
    struct Input *in=(struct Input*)clock->input_context;
    in->expected_published=clock->ticks;
    return pilot_reference_clock_step(clock,blocks);
}
static void setup(PilotReferenceClock *clock,struct Input *in,uint64_t origin,uint32_t quantum)
{
    memset(clock,0,sizeof(*clock));memset(in,0,sizeof(*in));
    in->clock=clock;in->current=in->next=origin;
    CHECK(!pilot_reference_clock_init(clock,input,drive,in));
    if(quantum)CHECK(!pilot_reference_clock_quantum(clock,quantum));
    CHECK(!pilot_reference_clock_audio(clock,advance,in));
}
static void observed(PilotReferenceClock *clock,uint64_t expected,uint64_t frames)
{
    uint64_t ticks=UINT64_MAX,tsc=UINT64_MAX;
    CHECK(!pilot_reference_clock_observe(clock,&ticks) && ticks==expected);
    CHECK(clock->frames==frames);
    CHECK(!pilot_reference_clock_tsc(clock,&tsc));
    /* These examples are small enough for independent direct multiplication. */
    CHECK(tsc==expected*733333333ull/1125000);
}
static void split_target(void)
{
    PilotReferenceClock clock;struct Input in;
    setup(&clock,&in,0,32);in.next=1125;in.check_busy=1;
    for(unsigned i=1;i<=36;++i) {
        unsigned reads=in.reads,drives=in.drives,advances=in.advances;
        uint64_t prior=clock.ticks;
        for(unsigned repeat=0;repeat<7;++repeat) {
            uint64_t tsc=0,ticks=0;
            CHECK(!pilot_reference_clock_observe(&clock,&ticks) && ticks==prior);
            CHECK(!pilot_reference_clock_tsc(&clock,&tsc));
        }
        CHECK(in.reads==reads && in.drives==drives && in.advances==advances);
        CHECK(!step(&clock,1000+i));
        uint64_t expected=i==36?1125:(uint64_t)i*32;
        observed(&clock,expected,expected*16/375);
        CHECK(in.frames==expected*16/375);
        CHECK(in.drives==1 && in.reads==2 && in.last_blocks==1001);
        if(i==1) {
            CHECK(pilot_reference_clock_quantum(&clock,7)!=NULL);
            CHECK(pilot_reference_clock_quantum(&clock,0)!=NULL);
        }
        /* The producer may offer another target while its first is draining;
         * no new input/driver call may see it before the pending target ends. */
        in.next=2250;
    }
    CHECK(in.frames==48);
    CHECK(!step(&clock,2000));
    observed(&clock,1157,1157*16/375);
    CHECK(in.drives==2 && in.reads==3 && in.last_blocks==2000);
    for(unsigned i=0;i<35;++i)CHECK(!step(&clock,3000));
    observed(&clock,2250,96);CHECK(in.frames==96 && in.drives==2);
    /* A repeated absolute target is stopped time, including the audio owner. */
    unsigned advances=in.advances;
    CHECK(!step(&clock,4000));observed(&clock,2250,96);
    CHECK(in.advances==advances && in.drives==3);
}
static void unlimited_and_lifecycle(void)
{
    PilotReferenceClock clock;struct Input in;
    setup(&clock,&in,0,0);in.next=1125;
    CHECK(!step(&clock,1));observed(&clock,1125,48);
    CHECK(in.frames==48 && in.drives==1 && in.advances==1);
    CHECK(!pilot_reference_clock_quantum(&clock,32));in.next=2250;
    CHECK(!step(&clock,2));observed(&clock,1157,1157*16/375);
    CHECK(!pilot_reference_clock_audio(&clock,NULL,NULL));
    CHECK(!step(&clock,3));observed(&clock,1189,1189*16/375);
    /* Fresh audio attaches at the current admitted epoch, not the old pending
     * proposal or the old engine's history. Future input is still preserved. */
    in.frames=0;
    CHECK(!pilot_reference_clock_audio(&clock,advance,&in));
    CHECK(!step(&clock,4));observed(&clock,1221,1221*16/375);
    CHECK(in.frames==1221*16/375-1189*16/375 && in.drives==2);
    for(unsigned i=0;i<33;++i)CHECK(!step(&clock,5));
    CHECK(in.frames==96-1189*16/375 && in.drives==2);
    CHECK(!pilot_reference_clock_quantum(&clock,0));in.next=3375;
    CHECK(!step(&clock,6));observed(&clock,3375,144);
}
static void errors(void)
{
    PilotReferenceClock clock={0};struct Input in;
    CHECK(pilot_reference_clock_quantum(NULL,32)!=NULL);
    CHECK(pilot_reference_clock_quantum(&clock,32)!=NULL);
    for(unsigned kind=0;kind<4;++kind) {
        setup(&clock,&in,0,32);in.next=1125;
        CHECK(!step(&clock,1));
        if(kind==0) {
            /* Consumer failure with pending debt: no new external input. */
            in.audio_failure=1;
        } else {
            for(unsigned i=0;i<35;++i)CHECK(!step(&clock,2));
            in.next=2250;
            if(kind==1)in.drive_failure=1;
            if(kind==2)in.input_failure=1;
            if(kind==3)in.next=1124; /* Backwards absolute external input. */
        }
        uint64_t prior_ticks=clock.ticks,prior_frames=clock.frames;
        unsigned prior_drives=in.drives;
        CHECK(step(&clock,3)!=NULL);
        CHECK(clock.ticks==prior_ticks && clock.frames==prior_frames);
        if(kind==0)CHECK(in.drives==prior_drives);
        unsigned calls=in.drives,reads=in.reads,advances=in.advances;
        CHECK(step(&clock,4)!=NULL);
        CHECK(pilot_reference_clock_quantum(&clock,0)!=NULL);
        uint64_t untouched=0x123456789abcdef0ull;
        CHECK(pilot_reference_clock_observe(&clock,&untouched)!=NULL);
        CHECK(untouched==0x123456789abcdef0ull);
        CHECK(pilot_reference_clock_tsc(&clock,&untouched)!=NULL);
        CHECK(untouched==0x123456789abcdef0ull);
        CHECK(in.drives==calls && in.reads==reads && in.advances==advances);
    }
    /* min(remaining,quantum) must not overflow near the absolute tick limit. */
    setup(&clock,&in,UINT64_MAX-40,32);in.next=UINT64_MAX;
    CHECK(!step(&clock,1));CHECK(clock.ticks==UINT64_MAX-8 && clock.frames==1);
    CHECK(!step(&clock,2));CHECK(clock.ticks==UINT64_MAX && clock.frames==1);
    CHECK(in.drives==1 && in.frames==1);
}
int main(void)
{
    split_target();unlimited_and_lifecycle();errors();
    if(failures)return 1;
    puts("PASS bounded reference admission: exact target/frames/TSC, pure reads, input debt, attach/detach, failures and overflow");
    return 0;
}
