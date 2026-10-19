#if PILOT_AUDIO_HAS_NOTIFICATIONS
static int32_t audio_set_notifications(struct PilotAudioBuffer *b,unsigned count,uint32_t positions)
{
#if defined(PILOT_AUDIO_DSP) && !defined(PILOT_AUDIO_REFERENCE)
    if(!b->notification_enabled || (b->format!=1 && b->format!=0x69) || !b->host || count>64 ||
       (count && !wait_range(positions,count*8,0)))
        stop("unqualified audio notification configuration",positions);
    uint32_t snapshot[128];struct PilotAudioNotification staged[64]={{0}};
    if(count)memcpy(snapshot,memory+positions,count*8);
    for(unsigned i=0;i<count;++i)
        if(snapshot[2*i]>=b->bytes || snapshot[2*i]%b->block_align)
            stop("audio notification offset outside qualified source range",snapshot[2*i]);
    unsigned retained=0;uint32_t status=0;
    for(;retained<count;++retained) {
        /* Retain decoded PCM positions, using the data owner's 64-frame blocks. */
        staged[retained].offset=audio_pcm_bytes(b,snapshot[2*retained]);
        staged[retained].event=event_duplicate(snapshot[2*retained+1],&status);
        if(!staged[retained].event)break;
    }
    if(retained!=count) {
        for(unsigned i=0;i<retained;++i)CloseHandle(staged[i].event);
        stop("audio notification requires owned native event",snapshot[2*retained+1]);
    }
    int32_t result=pilot_audio_buffer_notifications(b->host,count,staged);
    if(result>=0) {
        for(unsigned i=0;i<b->notification_count;++i)
            if(!CloseHandle(b->notifications[i].event))stop("audio notification replacement close failed",b->base);
        memcpy(b->notifications,staged,count*sizeof(*staged));b->notification_count=count;
    } else for(unsigned i=0;i<count;++i)CloseHandle(staged[i].event);
    return result;
#else
    (void)b;(void)count;stop("audio notification transport unqualified",positions);
#endif
}
#endif
