/* Exact original parameter layout and deferred/apply low-bit contract.
 * Unknown defaults remain unknown. This phase admits inactive voice storage
 * and setters, never active spatial playback without its rendering owner. */
#ifdef PILOT_AUDIO_SPATIAL_CACHE_VERSION
#if PILOT_AUDIO_SPATIAL_CACHE_VERSION != 1 || !defined(PILOT_AUDIO_LISTENER_VERSION)
#error Unsupported native spatial cache contract
#endif
static void audio_spatial_commit(struct PilotAudioBuffer *b)
{
    if(b->host) {
        uint32_t playing=0;
        if(pilot_audio_buffer_status(b->host,&playing)<0 || (playing&1))
            stop("spatial cache commit requires inactive output",b->base);
    }
    memcpy(b->spatial_cache.applied,b->spatial_cache.pending,sizeof(b->spatial_cache.pending));
    b->spatial_cache.applied_known=b->spatial_cache.known;b->spatial_cache.dirty=0;
}
static void audio_spatial_commit_all(void)
{
    for(unsigned i=0;i<256;++i)if(audio_buffers[i].base && audio_buffers[i].spatial)
        audio_spatial_commit(&audio_buffers[i]);
}
static int audio_spatial_dispatch(const char *name,const uint32_t *a)
{
    unsigned first=0,count=0,dirty=0,apply=0;const uint32_t *values=a+1;
    if(!strcmp(name,"IDirectSoundBuffer_SetAllParameters")){first=1;count=18;dirty=0xff;apply=a[2];}
    else if(!strcmp(name,"IDirectSoundBuffer_SetConeAngles")){first=7;count=2;dirty=0x10;apply=a[3];}
    else if(!strcmp(name,"IDirectSoundBuffer_SetConeOrientation")){first=9;count=3;dirty=0x18;apply=a[4];}
    else if(!strcmp(name,"IDirectSoundBuffer_SetConeOutsideVolume")){first=12;count=1;dirty=0x10;apply=a[2];}
    else if(!strcmp(name,"IDirectSoundBuffer_SetMinDistance")){first=13;count=1;dirty=4;apply=a[2];}
    else if(!strcmp(name,"IDirectSoundBuffer_SetMaxDistance")){first=14;count=1;dirty=4;apply=a[2];}
    else if(!strcmp(name,"IDirectSoundBuffer_SetMode")){first=15;count=1;apply=a[2];}
    else if(!strcmp(name,"IDirectSoundBuffer_SetPosition")){first=1;count=3;dirty=0xff;apply=a[4];}
    else if(!strcmp(name,"IDirectSoundBuffer_SetVelocity")){first=4;count=3;dirty=0x40;apply=a[4];}
    else if(!strcmp(name,"IDirectSoundBuffer_SetDistanceFactor")){first=16;count=1;dirty=0x60;apply=a[2];}
    else if(!strcmp(name,"IDirectSoundBuffer_SetRolloffFactor")){first=17;count=1;dirty=4;apply=a[2];}
    else if(!strcmp(name,"IDirectSoundBuffer_SetDopplerFactor")){first=18;count=1;dirty=0x40;apply=a[2];}
    else if(!strcmp(name,"IDirectSoundBuffer_SetI3DL2Source")){first=19;count=9;dirty=0x80;apply=a[2];}
    else return 0;
    struct PilotAudioBuffer *b=audio_buffer(a[0]);
    if(!b->spatial)stop("spatial parameter setter requires a spatial voice",a[0]);
    if(first==1 && count==18)values=(const uint32_t*)audio_pointer(a[1],76)+1;
    if(first==19)values=audio_pointer(a[1],36);
    memcpy(b->spatial_cache.pending+first,values,count*4);
    b->spatial_cache.known|=((1u<<count)-1)<<first;b->spatial_cache.dirty|=dirty;
    if(!(apply&1))audio_spatial_commit(b);
    return 1;
}
#endif
