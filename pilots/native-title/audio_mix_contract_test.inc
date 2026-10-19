/* Observe actual device-driven samples, not just cached routing fields. */
#ifdef PILOT_AUDIO_DSP
static void audio_test_observe(float average[32],int output)
{
    unsigned channels=output?6:32;
    float *samples=calloc(4096*channels,sizeof(float));CHECK(samples);
    CHECK(pilot_audio_capture_begin(samples,4096,output)==0);
#ifdef PILOT_AUDIO_REFERENCE
    Sleep(180);
#else
    CHECK(pilot_audio_capture_wait(5000)==1);
#endif
    unsigned frames=pilot_audio_capture_end();CHECK(frames==4096);
    memset(average,0,32*sizeof(float));
    for(unsigned n=frames-512;n<frames;++n)
        for(unsigned c=0;c<channels;++c)average[c]+=samples[n*channels+c]/512.0f;
    free(samples);
}
static void audio_test_rewrite(uint32_t iface,int16_t left,int16_t right)
{
    struct PilotAudioBuffer *b=audio_buffer(iface);
    uint32_t a[8]={iface,0,b->bytes,0x19300,0x19304,0x19308,0x1930c,0};
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Lock,a)==0);
    for(unsigned i=0;i<b->bytes/4;++i) {
        MEM16(b->data+i*4)=(uint16_t)left;MEM16(b->data+i*4+2)=(uint16_t)right;
    }
    a[1]=b->data;a[2]=b->bytes;a[3]=a[4]=0;
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Unlock,a)==0);
}
static void test_audio_mix(void)
{
    vm_ensure();uint64_t initial=vm_committed(0,0x100000000ull);
    for(unsigned iteration=0;iteration<3;++iteration) {
        uint32_t device=audio_test_device();
        uint32_t desc=0x19500,pairs=0x19510;
        MEM32(desc)=3;MEM32(desc+4)=pairs;
        MEM32(pairs)=5;MEM32(pairs+4)=0;
        MEM32(pairs+8)=4;MEM32(pairs+12)=0;
        MEM32(pairs+16)=7;MEM32(pairs+20)=(uint32_t)-602;
        // Route before storage allocation; SetBufferData must retain the sends.
        uint32_t buffer=audio_test_buffer_format(device,0,2,16,48000,desc);
        uint32_t a[8]={buffer,0,19200};
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a)==0);
        a[1]=0;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetHeadroom,a)==0);
        audio_test_rewrite(buffer,1000,-2000);
        a[1]=a[2]=0;a[3]=3;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a)==0);
        float average[32];audio_test_observe(average,0);
        CHECK(fabs(average[5]-1000.0/32768)<0.00002 && fabs(average[4]+2000.0/32768)<0.00002);
        CHECK(fabs(average[7]-(1000.0/32768)*pow(10.0,-602.0/2000))<0.00002);
        for(unsigned c=0;c<32;++c)if(c!=4 && c!=5 && c!=7)CHECK(fabs(average[c])<0.000001);
        MEM32(desc)=1;MEM32(pairs)=5;MEM32(pairs+4)=(uint32_t)-1204;
        a[1]=desc;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetMixBinVolumes_8,a)==0);
        audio_test_observe(average,0);
        CHECK(fabs(average[5]-(1000.0/32768)*pow(10.0,-1204.0/2000))<0.00002);
        CHECK(fabs(average[4]+2000.0/32768)<0.00002); // unrelated send survives
        MEM32(pairs)=32;
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetMixBins,a)==0x88780032);
        CHECK(audio_buffer(buffer)->mix_count==3 && audio_buffer(buffer)->mix_bins[0]==5);
        // Sustained updates of a playing ring reach real host output; old queued
        // chunks own their bytes until consumed, and no completion is invented.
        a[1]=0;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetMixBins,a)==0);
        audio_test_rewrite(buffer,-3000,4000);Sleep(60);
        audio_test_observe(average,1);
        CHECK(fabs(average[0]+3000.0/32768)<0.00003 && fabs(average[1]-4000.0/32768)<0.00003);
        for(unsigned c=2;c<6;++c)CHECK(fabs(average[c])<0.000001);
        a[1]=(uint32_t)-10000;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetVolume,a)==0);
        audio_test_observe(average,1);
        for(unsigned c=0;c<6;++c)CHECK(fabs(average[c])<0.000001);
        a[1]=0;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetVolume,a)==0);
        MEM32(desc)=0;a[1]=desc;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetMixBins,a)==0);
        audio_test_observe(average,0);
        for(unsigned c=0;c<32;++c)CHECK(fabs(average[c])<0.000001);
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Stop,a)==0);
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,a)==0);
        a[0]=device;CHECK(audio_test_call(PILOT_AUDIO_IDirectSound_Release,a)==0);
        CHECK(vm_committed(0,0x100000000ull)==initial);
    }
    puts("audio mixer: actual stereo channel sends, attenuation, live ring updates, audible-path samples, mute and three complete lifetimes passed");
}
#endif
