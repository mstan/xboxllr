#ifdef PILOT_AUDIO_LISTENER_VERSION
static void audio_test_listener_call(const char *name,const uint32_t *args)
{
    unsigned entry=0;
    while(entry<PILOT_AUDIO_COUNT && strcmp(pilot_audio_calls[entry].name,name))++entry;
    CHECK(entry<PILOT_AUDIO_COUNT && !audio_test_call(entry,args));
}
static void test_audio_listener(uint32_t device,struct PilotAudioBuffer *voice)
{
    struct PilotAudioBuffer before=*voice;
    CHECK(!audio_listener.known && !audio_listener.applied_known);
    uint32_t a[8]={device,0,0,0x3f800000,0,0x3f800000,0,1};
    audio_test_listener_call("IDirectSound_SetOrientation",a);
    CHECK(audio_listener.known==0x1f80 && audio_listener.dirty==0x26 && !audio_listener.applied_known);
    CHECK(!memcmp(audio_listener.pending+7,a+1,24));
    a[1]=0x3f800000;a[2]=0x40000000;a[3]=0x40400000;a[4]=1;
    audio_test_listener_call("IDirectSound_SetPosition",a);
    CHECK(audio_listener.known==0x1f8e && audio_listener.dirty==0xff && !audio_listener.applied_known);
    audio_test_listener_call("IDirectSound_CommitDeferredSettings",a);
    CHECK(audio_listener.applied_known==0x1f8e && !audio_listener.dirty);
    CHECK(!memcmp(audio_listener.pending,audio_listener.applied,sizeof(audio_listener.pending)));
    a[1]=0x40800000;a[2]=0x40a00000;a[3]=0x40c00000;a[4]=1;
    audio_test_listener_call("IDirectSound_SetVelocity",a);
    CHECK(audio_listener.dirty==0x40 && !(audio_listener.applied_known&0x70));
    a[1]=0x3f000000;a[2]=2; /* Original tests only apply's low bit. */
    audio_test_listener_call("IDirectSound_SetDistanceFactor",a);
    CHECK(!audio_listener.dirty && (audio_listener.applied_known&0x2070)==0x2070);
    CHECK(audio_listener.applied[13]==0x3f000000 && audio_listener.applied[4]==0x40800000);
    a[1]=0x3f800000;a[2]=1;
    audio_test_listener_call("IDirectSound_SetRolloffFactor",a);CHECK(audio_listener.dirty==4);
    audio_test_listener_call("IDirectSound_SetDopplerFactor",a);CHECK(audio_listener.dirty==0x44);
    uint32_t original[16];original[0]=7; /* dwSize is not validated/copied by original. */
    for(unsigned i=1;i<16;++i)original[i]=0x3e800000+i*0x10000;
    memcpy(memory+0x19600,original,sizeof(original));a[1]=0x19600;a[2]=5;
    audio_test_listener_call("IDirectSound_SetAllParameters",a);
    CHECK(audio_listener.known==0xfffe && audio_listener.dirty==0xff);
    CHECK(!memcmp(audio_listener.pending+1,original+1,60));
    CHECK(!memcmp(memory+0x19600,original,sizeof(original)) && !audio_listener.pending[0]);
    audio_test_listener_call("IDirectSound_CommitDeferredSettings",a);
    CHECK(audio_listener.applied_known==0xfffe && !audio_listener.dirty);
    CHECK(!memcmp(audio_listener.applied+1,original+1,60));
    CHECK(!memcmp(voice,&before,sizeof(before)));
    fprintf(stderr,"PASS listener: source scalar/dirty/apply semantics, whole deferred commit, original dwSize behavior, no2D voice mutation\n");
}
#endif
