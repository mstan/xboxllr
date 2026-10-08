/* Real output and ownership across detachment, with a retained guest object. */
#ifdef PILOT_AUDIO_DSP
static void test_audio_detach(void)
{
    vm_ensure();uint64_t initial=vm_committed(0,0x100000000ull);
    uint32_t device=audio_test_device(),iface=audio_test_buffer_format(device,0,2,16,48000,0);
    struct PilotAudioBuffer *b=audio_buffer(iface);
    uint32_t caller=0,allocated=19200;
    CHECK(!vm_allocate(&caller,0,&allocated,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    uint32_t a[8]={iface,0},desc=0x19500,pairs=0x19510;
    MEM32(desc)=1;MEM32(desc+4)=pairs;MEM32(pairs)=5;MEM32(pairs+4)=(uint32_t)-300;
    a[1]=desc;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetMixBins,a)==0);
    a[1]=(uint32_t)-602;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetVolume,a)==0);
    a[1]=300;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetHeadroom,a)==0);
    a[1]=24000;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetFrequency,a)==0);
    for(unsigned iteration=0;iteration<8;++iteration) {
        a[1]=iteration&1?caller:0;a[2]=19200;
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a)==0);
        CHECK(b->volume==-602 && b->headroom==300 && b->frequency==24000 && b->mix_count==1 && b->mix_bins[0]==5);
        a[1]=0x19300;a[2]=0x19304;
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition,a)==0 && MEM32(a[1])==0 && MEM32(a[2])==0);
        audio_test_rewrite(iface,1000,-2000);
        a[1]=4800;a[2]=4800;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion,a)==0);
        a[1]=a[2]=0;a[3]=3;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a)==0);
        float average[32];audio_test_observe(average,0);
        CHECK(fabs(average[5]-(1000.0/32768)*pow(10.0,-1202.0/2000))<0.00002);
        for(unsigned c=0;c<32;++c)if(c!=5)CHECK(fabs(average[c])<0.000001);
        uint32_t old=b->data,owned=b->owned,base=b->base;
        a[1]=a[2]=0;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a)==0);
        CHECK(!b->host && !b->data && !b->bytes && !b->owned && b->base==base && MEM32(base+4)==1);
        CHECK(MEM32(audio_device+4)==2 && b->channels==2 && b->rate==48000);
        CHECK(owned?test_write_faults(old):!test_write_faults(old));
        // Destruction must retire queued readers before caller storage can change.
        memset(memory+caller,0xa5,19200);
        audio_test_observe(average,0);
        for(unsigned c=0;c<32;++c)CHECK(fabs(average[c])<0.000001);
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a)==0); // idempotent detach
    }
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,a)==0);
    a[0]=device;CHECK(audio_test_call(PILOT_AUDIO_IDirectSound_Release,a)==0);
    CHECK(!test_write_faults(caller));allocated=0;CHECK(!vm_free(&caller,&allocated,MEM_RELEASE));
    CHECK(vm_committed(0,0x100000000ull)==initial);
    puts("audio detach: eight active real-output lifetimes, silence after detach, retained settings, caller/owned storage and rebinding passed");
}
#endif
