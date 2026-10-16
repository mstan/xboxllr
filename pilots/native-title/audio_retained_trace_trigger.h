/* Passive original-call bookkeeping only. No guest/device work or time input. */
#pragma once
#include <stdint.h>
enum PilotRetainedTraceCall { PILOT_RETAINED_PLAY, PILOT_RETAINED_STOP,
    PILOT_RETAINED_REBIND, PILOT_RETAINED_FINAL_RELEASE };
typedef struct PilotRetainedTraceTrigger {
    uint64_t lifetime,prior_play,stop;
    uint32_t object;
    struct {uint32_t object;uint64_t play;} played[32];
} PilotRetainedTraceTrigger;
/* Completed successful calls only (final Release means return value zero).
 * 1 requests the first retained-object Stop trace; 0 means no request;
 * -1 is an exhausted/changed diagnostic domain, never an implicit trigger. */
static inline int pilot_retained_trace_call(PilotRetainedTraceTrigger *s,
    uint64_t lifetime,uint64_t id,uint32_t object,enum PilotRetainedTraceCall call)
{
    unsigned found=32,free_slot=32;
    if(!s || !lifetime || !id || !object)return -1;
    if(s->lifetime && s->lifetime!=lifetime)return -1;
    if(s->stop)return 0;
    for(unsigned i=0;i<32;++i) {
        if(s->played[i].object==object)found=i;
        if(!s->played[i].object && free_slot==32)free_slot=i;
    }
    if(call==PILOT_RETAINED_PLAY && found==32 && free_slot==32)return -1;
    s->lifetime=lifetime;
    if(call==PILOT_RETAINED_PLAY) {
        if(found==32)found=free_slot;
        s->played[found].object=object;s->played[found].play=id;
    } else if(call==PILOT_RETAINED_STOP && found!=32) {
        s->object=object;s->prior_play=s->played[found].play;s->stop=id;return 1;
    } else if((call==PILOT_RETAINED_REBIND || call==PILOT_RETAINED_FINAL_RELEASE) && found!=32) {
        s->played[found].object=0;s->played[found].play=0;
    }
    return 0;
}
