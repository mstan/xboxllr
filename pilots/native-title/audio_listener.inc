/* Device-owned original listener parameters. Only 2D voices are currently
 * admitted: these parameters do not transform their samples or mix-bin routes.
 * Keep pending/applied values for a future qualified spatial consumer, with
 * known masks so unobserved original defaults cannot masquerade as zeroes. */
#ifdef PILOT_AUDIO_LISTENER_VERSION
#if PILOT_AUDIO_LISTENER_VERSION != 1
#error Unsupported native listener contract
#endif
static struct {
    uint32_t pending[16],applied[16];
    uint32_t known,applied_known,dirty;
} audio_listener;
static void audio_listener_commit(void)
{
    memcpy(audio_listener.applied,audio_listener.pending,sizeof(audio_listener.pending));
    audio_listener.applied_known=audio_listener.known;
    audio_listener.dirty=0;
}
static int audio_listener_dispatch(const char *name,const uint32_t *a)
{
    unsigned first=0,count=0,dirty=0,apply=0;
    const uint32_t *values=a+1;
    if(!strcmp(name,"IDirectSound_SetOrientation")){first=7;count=6;dirty=0x26;apply=a[7];}
    else if(!strcmp(name,"IDirectSound_SetPosition")){first=1;count=3;dirty=0xff;apply=a[4];}
    else if(!strcmp(name,"IDirectSound_SetVelocity")){first=4;count=3;dirty=0x40;apply=a[4];}
    else if(!strcmp(name,"IDirectSound_SetDistanceFactor")){first=13;count=1;dirty=0x60;apply=a[2];}
    else if(!strcmp(name,"IDirectSound_SetRolloffFactor")){first=14;count=1;dirty=4;apply=a[2];}
    else if(!strcmp(name,"IDirectSound_SetDopplerFactor")){first=15;count=1;dirty=0x40;apply=a[2];}
    else if(!strcmp(name,"IDirectSound_SetAllParameters")) {
        /* The original ignores dwSize and copies the 15 scalar parameters. */
        first=1;count=15;dirty=0xff;apply=a[2];
    } else if(!strcmp(name,"IDirectSound_CommitDeferredSettings")) {
        audio_device_check(a[0]);audio_listener_commit();return 1;
    } else return 0;
    audio_device_check(a[0]);
    if(first==1 && count==15)values=(const uint32_t *)audio_pointer(a[1],64)+1;
    memcpy(audio_listener.pending+first,values,count*4);
    audio_listener.known|=((1u<<count)-1)<<first;
    audio_listener.dirty|=dirty;
    if(!(apply&1))audio_listener_commit();
    return 1;
}
#endif
