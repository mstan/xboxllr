static uint32_t reference_test_device,reference_test_buffer;
static float *reference_test_pcm;
static void reference_test_open(void)
{
    reference_test_device=audio_test_device();
    reference_test_buffer=audio_test_buffer_format(reference_test_device,65536,2,16,48000,0);
    struct PilotAudioBuffer *b=audio_buffer(reference_test_buffer);
    CHECK(b->headroom==600 && b->volume==0);
    for(unsigned i=0;i<16384;++i){MEM16(b->data+i*4)=16384;MEM16(b->data+i*4+2)=(uint16_t)-8192;}
    uint32_t a[]={reference_test_buffer,0,65536,0x19300,0x19304,0x19308,0x1930c,0};
    CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Lock,a));
    a[1]=b->data;a[2]=65536;a[3]=a[4]=0;
    CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Unlock,a));
    reference_test_pcm=calloc(8192*32,sizeof(float));CHECK(reference_test_pcm);
    CHECK(!pilot_audio_capture_begin(reference_test_pcm,8192,0));
    a[1]=a[2]=0;a[3]=1;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a));
    CHECK(!reference_output_frames());
}
static void test_audio_reference_runtime(void)
{
    thread_idle_runtime_clock=4;thread_idle_reference_open=reference_test_open;
    test_thread_idle_clock_lle();thread_idle_reference_open=NULL;
    uint32_t frames=pilot_audio_capture_end();
    CHECK(frames>=1584 && frames<8192 && frames==reference_output_frames());
    float gain=(float)pow(10.,-.3);
    for(unsigned f=0;f<frames;++f)for(unsigned c=0;c<32;++c) {
        float expected=c==0?.5f*gain:c==1?-.25f*gain:0;
        CHECK(fabsf(reference_test_pcm[f*32+c]-expected)<1e-7f);
    }
    struct PilotAudioBuffer *b=audio_buffer(reference_test_buffer);
    uint32_t a[]={reference_test_buffer,0x19304,0x19308,0};
    CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition,a));
    CHECK(MEM32(a[1])==frames*4 && MEM32(a[2])==(frames+32)*4);
    uint32_t counter=0,frame=0;
    uint32_t control=0x80;CHECK(!pilot_usb_lle_access(0xfed00004,1,&control));
    CHECK(!pilot_usb_lle_access(0xfe80200c,0,&counter));
    CHECK(!pilot_usb_lle_access(0xfed0003c,0,&frame));
    uint64_t ticks=0,next=0;CHECK(!pilot_usb_pit_clock(&ticks,&next));
    CHECK(counter==(uint32_t)(ticks/1125000)*48000+(uint32_t)((ticks%1125000)*48000/1125000));
    Sleep(40);
    for(unsigned i=0;i<100;++i) {
        uint32_t observed=0;CHECK(!pilot_usb_lle_access(0xfe80200c,0,&observed) && observed==counter);
        CHECK(!pilot_usb_lle_access(0xfed0003c,0,&observed) && observed==frame);
    }
    CHECK(reference_output_frames()==frames);
    CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Stop,a));
    a[0]=reference_test_device;CHECK(audio_test_call(PILOT_AUDIO_IDirectSound_Release,a)==1);
    a[0]=reference_test_buffer;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,a));
    CHECK(!audio_device && !audio_reference_clock.audio && !reference_output_frames());
    CHECK(!pilot_reference_clock_step(&audio_reference_clock,audio_reference_blocks+PILOT_REFERENCE_BLOCKS_PER_MS));
    CHECK(!pilot_usb_pit_time(audio_reference_clock.ticks) && !pilot_usb_lle_tick());
    uint32_t observed=0;CHECK(!pilot_usb_lle_access(0xfe80200c,0,&observed) && observed==counter+48);
    CHECK(!pilot_usb_lle_access(0xfed0003c,0,&observed) && (uint16_t)observed==(uint16_t)(frame+1));
    control=0;CHECK(!pilot_usb_lle_access(0xfed00004,1,&control));
    free(reference_test_pcm);
    fprintf(stderr,"PASS reference runtime: original33ms PIT/IRQ/wait, actual Xbox PCM ABI/default headroom/lock/upload/play/cursor/Stop/release, %u actual frames; USB/SampleCounter frozen through host stall\n",frames);
}
