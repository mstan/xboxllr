#include "graphics_commands.h"
#include <string.h>

static int fail(struct PilotCommandArena *s,unsigned error,uint32_t detail)
{s->fault=error;s->detail=detail;return (int)error;}
int pilot_command_open(struct PilotCommandArena *s,uint32_t base,uint32_t bytes,uint32_t margin)
{
    if(!s || !base || (base&3) || !bytes || (bytes&3) || (margin&3) ||
       margin>bytes || bytes-margin<4 || base>UINT32_MAX-bytes)return PILOT_COMMAND_RANGE;
    memset(s,0,sizeof(*s));s->base=base;s->bytes=bytes;s->margin=margin;return 0;
}
int pilot_command_flush(struct PilotCommandArena *s,const uint32_t *pending,uint32_t current,
                        PilotCommandMethod method,void *context)
{
    if(!s || !s->base)return PILOT_COMMAND_RANGE;
    if(s->fault)return PILOT_COMMAND_FAULTED;
    if(current<s->base || current-s->base<s->consumed || current-s->base>s->bytes ||
       (current&3))return fail(s,PILOT_COMMAND_RANGE,current);
    uint32_t bytes=current-s->base-s->consumed,words=bytes/4;
    if(!words)return 0;
    if(!pending || !method)return fail(s,PILOT_COMMAND_RANGE,current);
    uint64_t methods=0;
    for(unsigned apply=0;apply<2;++apply) {
        uint32_t at=0;
        while(at<words) {
            uint32_t header=pending[at++];
            if(!header)continue; // FIFO zero-count NOP
            // Aligned method, subchannel0, normal or non-incrementing packet.
            // Jumps/calls/returns and object rebinding need separate owners.
            uint32_t count=(header>>18)&0x7ff,offset=header&0x1ffc;
            if((header&~0x5ffc1ffcu) || !count || !offset ||
               (!(header&0x40000000u) && count>(0x2000-offset)/4))
                return fail(s,PILOT_COMMAND_HEADER,header);
            if(count>words-at)return fail(s,PILOT_COMMAND_INCOMPLETE,header);
            for(uint32_t n=0;n<count;++n) {
                if(!method(context,offset,pending[at++],(int)apply))
                    return fail(s,apply?PILOT_COMMAND_BACKEND:PILOT_COMMAND_METHOD,offset);
                if(apply)++methods;
                if(!(header&0x40000000u))offset+=4;
            }
        }
    }
    s->consumed=current-s->base;s->methods+=methods;++s->submissions;return 0;
}
int pilot_command_reserve(struct PilotCommandArena *s,const uint32_t *pending,uint32_t current,
                          uint32_t minimum,uint32_t window,PilotCommandMethod method,void *context,
                          uint32_t *next,uint32_t *limit)
{
    if(!s || !s->base)return PILOT_COMMAND_RANGE;
    if(s->fault)return PILOT_COMMAND_FAULTED;
    if(!next || !limit || !minimum || (minimum&3) || (window&3) || minimum>window ||
       window>s->bytes || window<=s->margin)
        return fail(s,PILOT_COMMAND_RANGE,window);
    int error=pilot_command_flush(s,pending,current,method,context);if(error)return error;
    // All preceding commands now belong to the native queue. Reusing CPU
    // staging storage does not signal GPU completion or retire a fence.
    s->consumed=0;*next=s->base;*limit=s->base+window-s->margin;return 0;
}
