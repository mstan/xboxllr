#ifdef PILOT_AUDIO_I3DL2_VERSION
#if PILOT_AUDIO_I3DL2_VERSION != 1 || !defined(PILOT_AUDIO_LISTENER_VERSION)
#error Unsupported original native I3DL2 contract
#endif
#ifdef PILOT_AUDIO_DSP
static struct {uint32_t pending[12],applied[12];int dirty,known;} audio_i3dl2;
static void audio_listener_commit(void);
static void audio_i3dl2_commit(void)
{
    if(!audio_i3dl2.dirty)return;
    uint32_t effect=audio_effects_location[0],base=0,bytes=4096,tail;
    if(!audio_effects || !pilot_dsp_effect_read(audio_effects,effect,0x21c,&tail,4))
        stop("I3DL2 effect state layout is unqualified",effect);
    if(vm_allocate(&base,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))
        stop("native I3DL2 workspace allocation failed",bytes);
    MEM32(base)=base+0x224;memcpy(memory+base+0x224,audio_i3dl2.pending,48);
    if(!pilot_dsp_effect_read(audio_effects,effect,0,memory+base+4,0x118))
        stop("I3DL2 original state prefix unavailable",effect);
    native_cpu_pure_thiscall(PILOT_AUDIO_I3DL2_INITIALIZE,base);
    native_cpu_pure_thiscall(PILOT_AUDIO_I3DL2_CALCULATE,base);
    MEM32(base+0x14)|=4;
    struct PilotDspPatch patches[]={{0x10,4,memory+base+0x14},{0x118,0x108,memory+base+0x11c}};
    int32_t result=pilot_audio_effects_patch(audio_effects,effect,2,patches);
    uint32_t zero=0;if(vm_free(&base,&zero,MEM_RELEASE))stop("native I3DL2 workspace retirement failed",base);
    if(result<0)stop("native I3DL2 coefficient transaction failed",(uint32_t)result);
    memcpy(audio_i3dl2.applied,audio_i3dl2.pending,48);audio_i3dl2.dirty=0;audio_i3dl2.known=1;
    fprintf(stderr,"[AUDIO I3DL2] original-native calculation; effect=%u committed_control=4 coefficient_bytes=264\n",effect);
}
static int32_t audio_i3dl2_set(const uint32_t *a)
{
    audio_device_check(a[0]);
    if(!audio_effects || audio_effects_location[0]==UINT32_MAX)return (int32_t)0x88780032;
    memcpy(audio_i3dl2.pending,audio_pointer(a[1],48),48);audio_i3dl2.dirty=1;
    if(!(a[2]&1))audio_listener_commit();
    return 0;
}
#endif
#endif
