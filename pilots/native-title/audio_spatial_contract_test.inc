#ifdef PILOT_AUDIO_SPATIAL_CACHE_VERSION
static void test_audio_spatial(int play_failure)
{
    vm_ensure();uint64_t committed=vm_committed(0,0x100000000ull);
    uint32_t device=audio_test_device();
    uint32_t iface=audio_test_buffer_format_flags(device,play_failure?9600:0,1,16,48000,0,0x40010);
    struct PilotAudioBuffer *b=audio_buffer(iface);
    CHECK(b->spatial && !b->spatial_cache.known && !b->spatial_cache.applied_known);
    if(play_failure) {
        uint32_t play[]={iface,0,0,0};audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,play);CHECK(0);
    }
    const struct {const char *name;unsigned first,count,dirty;} cases[]={
        {"SetPosition",1,3,0xff},{"SetVelocity",4,3,0x40},{"SetConeAngles",7,2,0x10},
        {"SetConeOrientation",9,3,0x18},{"SetConeOutsideVolume",12,1,0x10},
        {"SetMinDistance",13,1,4},{"SetMaxDistance",14,1,4},{"SetMode",15,1,0},
        {"SetDistanceFactor",16,1,0x60},{"SetRolloffFactor",17,1,4},{"SetDopplerFactor",18,1,0x40}};
    for(unsigned c=0;c<sizeof(cases)/sizeof(*cases);++c) {
        uint32_t previous[28];memcpy(previous,b->spatial_cache.applied,sizeof(previous));
        uint32_t a[]={iface,0x3f800000+c,0x40000000+c,0x40400000+c,1};
        a[cases[c].count+1]=1;
        char name[96];snprintf(name,sizeof(name),"IDirectSoundBuffer_%s",cases[c].name);
        audio_test_listener_call(name,a);
        CHECK(b->spatial_cache.dirty==cases[c].dirty);
        CHECK(!memcmp(b->spatial_cache.pending+cases[c].first,a+1,cases[c].count*4));
        CHECK(!memcmp(b->spatial_cache.applied,previous,sizeof(previous)));
        a[0]=device;audio_test_listener_call("IDirectSound_CommitDeferredSettings",a);
        CHECK(!b->spatial_cache.dirty && b->spatial_cache.applied_known==b->spatial_cache.known);
        CHECK(!memcmp(b->spatial_cache.pending,b->spatial_cache.applied,sizeof(previous)));
    }
    uint32_t values[19];values[0]=7;
    for(unsigned i=1;i<19;++i)values[i]=0x3f000000+i*0x10000;
    memcpy(memory+0x19600,values,sizeof(values));uint32_t a[]={iface,0x19600,5};
    audio_test_listener_call("IDirectSoundBuffer_SetAllParameters",a);
    CHECK(b->spatial_cache.dirty==0xff && !b->spatial_cache.pending[0]);
    CHECK(!memcmp(b->spatial_cache.pending+1,values+1,72));
    CHECK(!memcmp(memory+0x19600,values,sizeof(values)));
    for(unsigned i=0;i<9;++i)values[i]=0x12000000+i;
    memcpy(memory+0x19600,values,36);a[2]=2; /* Only the low bit means deferred. */
    audio_test_listener_call("IDirectSoundBuffer_SetI3DL2Source",a);
    CHECK(b->spatial_cache.known==0x0ffffffe && b->spatial_cache.applied_known==0x0ffffffe);
    CHECK(!memcmp(b->spatial_cache.applied+19,values,36) && !b->spatial_cache.dirty);
    a[0]=iface;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,a));
    a[0]=device;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSound_Release,a));
    CHECK(vm_committed(0,0x100000000ull)==committed);
    fprintf(stderr,"PASS spatial cache: 13 original public setter ABIs, dirty masks, deferred/device-wide apply, actual input bytes, unknown defaults and lifetime\n");
}
#endif
