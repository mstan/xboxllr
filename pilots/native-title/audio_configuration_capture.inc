/* Bounded per-object input census for an already planned title observation.
 * This observes accepted caller settings, never drives a device or clock. */
static int audio_configuration_enabled(void)
{
    static int enabled=-1;
    if(enabled<0){const char *path=getenv("XLLE_PILOT_AUDIO_CONFIGURATION_CAPTURE");enabled=path && *path;}
    return enabled;
}
static int audio_configuration_selected(unsigned entry)
{
    switch(entry) {
    case PILOT_AUDIO_IDirectSoundBuffer_Release:
    case PILOT_AUDIO_IDirectSoundBuffer_SetBufferData:
    case PILOT_AUDIO_IDirectSoundBuffer_SetMixBins:
    case PILOT_AUDIO_IDirectSoundBuffer_SetMixBinVolumes_8:
    case PILOT_AUDIO_IDirectSoundBuffer_Play:
    case PILOT_AUDIO_IDirectSoundBuffer_Stop:
    case PILOT_AUDIO_IDirectSoundBuffer_SetCurrentPosition:
    case PILOT_AUDIO_IDirectSoundBuffer_SetVolume:
    case PILOT_AUDIO_IDirectSoundBuffer_SetHeadroom:
    case PILOT_AUDIO_IDirectSoundBuffer_SetFrequency:
    case PILOT_AUDIO_IDirectSoundBuffer_SetPitch:
    case PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion:return 1;
    default:return 0;
    }
}
static void audio_configuration_event(const struct PilotAudioBuffer *b,unsigned slot,
    unsigned entry,const uint32_t *args,int32_t result,uint32_t creation_flags)
{
    static FILE *file;static uint64_t generations[256],next_generation;
    static uint32_t flags[256];static unsigned records;
    if(slot>=256 || result<0)stop("invalid audio configuration observation",slot);
    if(!file) {
        file=fopen(getenv("XLLE_PILOT_AUDIO_CONFIGURATION_CAPTURE"),"wbx");
        if(!file)stop("audio configuration capture create failed",0);
        fprintf(stderr,"[AUDIO CONFIGURATION CAPTURE] accepted settings, stable object generations; maximum32768records\n");
    }
    if(records>=32768)stop("audio configuration capture record limit",records);
    if(entry==PILOT_AUDIO_IDirectSound_CreateSoundBuffer) {
        generations[slot]=++next_generation;flags[slot]=creation_flags;
    }
    if(!generations[slot])stop("audio configuration missing object creation",slot);
#ifdef PILOT_AUDIO_REFERENCE
    uint64_t time=audio_reference_clock.ticks;
    const char *domain="pit_ticks";
#else
    uint64_t time=dispatch_monotonic();const char *domain="qpc_100ns";
#endif
    fprintf(file,"{\"version\":1,\"record\":%u,\"generation\":%llu,\"time_domain\":\"%s\",\"time\":%llu,\"entry\":\"%s\",\"result\":%u,\"base\":%u,\"data\":%u,\"bytes\":%u,\"flags\":%u,\"format\":%u,\"bits\":%u,\"channels\":%u,\"created_rate\":%u,\"frequency\":%u,\"volume\":%d,\"user_headroom\":%u,\"mix\":[",
        ++records,(unsigned long long)generations[slot],domain,(unsigned long long)time,
        pilot_audio_calls[entry].name,(uint32_t)result,b->base,b->data,b->bytes,flags[slot],
        b->format,b->bits,b->channels,b->rate,b->frequency,b->volume,b->headroom);
    for(unsigned i=0;i<b->mix_count;++i)
        fprintf(file,"%s[%u,%d]",i?",":"",b->mix_bins[i],b->mix_volumes[b->mix_bins[i]]);
    fprintf(file,"],\"arguments\":[");
    for(unsigned i=0;i<pilot_audio_calls[entry].stack_bytes/4;++i)fprintf(file,"%s%u",i?",":"",args[i]);
    if(fprintf(file,"]}\n")<0 || fflush(file) || ferror(file))stop("audio configuration capture write failed",records);
}
