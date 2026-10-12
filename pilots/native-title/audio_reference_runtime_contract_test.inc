static uint32_t reference_test_device,reference_test_buffer;
static float *reference_test_pcm;
static void reference_test_open(void)
{
    input_host_is_keyboard();input_host_keyboard=0;
    FILE *replay=tmpfile();CHECK(replay);
    CHECK(fputs("0 000000000000000000000000000000000000\n20 100000000000000000000000000000000000\n",replay)>=0);
    rewind(replay);CHECK(input_replay_load(replay) && !fclose(replay));
    uint8_t pad[18];CHECK(!pilot_input_host_state(0,pad) && !pad[0]);
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
    CHECK(input_host_reference_ms==(audio_reference_clock.ticks-audio_reference_clock.origin)/1125);
    uint8_t pad[18];CHECK(!pilot_input_host_state(0,pad) && pad[0]==0x10);
    uint64_t tsc=0;CHECK(!pilot_reference_clock_tsc(&audio_reference_clock,&tsc));
    CHECK(xbox_ReadTimeStampCounter()==tsc);
    Sleep(40);
    for(unsigned i=0;i<100;++i) {
        uint32_t observed=0;CHECK(!pilot_usb_lle_access(0xfe80200c,0,&observed) && observed==counter);
        CHECK(!pilot_usb_lle_access(0xfed0003c,0,&observed) && observed==frame);
        CHECK(!pilot_input_host_state(0,pad) && pad[0]==0x10);
        CHECK(xbox_ReadTimeStampCounter()==tsc);
    }
    CHECK(reference_output_frames()==frames);
    CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Stop,a));
    a[0]=reference_test_device;CHECK(audio_test_call(PILOT_AUDIO_IDirectSound_Release,a)==1);
    a[0]=reference_test_buffer;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,a));
    CHECK(!audio_device && !audio_reference_clock.audio && !reference_output_frames());
    uint64_t previous_input_ms=input_host_reference_ms;
    CHECK(!audio_reference_step(&audio_reference_clock,audio_reference_blocks+PILOT_REFERENCE_BLOCKS_PER_MS));
    CHECK(input_host_reference_ms==previous_input_ms+1);
    CHECK(xbox_ReadTimeStampCounter()>tsc);
    CHECK(!pilot_usb_pit_time(audio_reference_clock.ticks) && !pilot_usb_lle_tick());
    uint32_t observed=0;CHECK(!pilot_usb_lle_access(0xfe80200c,0,&observed) && observed==counter+48);
    CHECK(!pilot_usb_lle_access(0xfed0003c,0,&observed) && (uint16_t)observed==(uint16_t)(frame+1));
    control=0;CHECK(!pilot_usb_lle_access(0xfed00004,1,&control));
    free(reference_test_pcm);
    fprintf(stderr,"PASS reference runtime: original33ms PIT/IRQ/wait, actual Xbox PCM ABI/default headroom/lock/upload/play/cursor/Stop/release, %u actual frames; USB/SampleCounter frozen through host stall\n",frames);
}
static const char *reference_test_bad_tsc(void *context,uint64_t *counter)
{(void)context;(void)counter;return "expected reference TSC provider failure";}
static void test_reference_tsc(int mode)
{
    test_audio_reference_runtime();
    CHECK(!pilot_io_device_clock_bind(pilot_reference_clock_observe,&audio_reference_clock));
    if(mode!=1)CHECK(!pilot_io_device_tsc_bind(mode==2?reference_test_bad_tsc:pilot_reference_clock_tsc,&audio_reference_clock));
    /* Actual original CPU: preserve nonvolatile registers, set CF, save flags,
     * read RDTSC twice and store EDX:EAX, save flags/ECX/ESP, RET4. */
    const uint8_t body[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0xb9,0x78,0x56,0x34,0x12,0xf9,0x9c,0x5e,0x89,0x77,16,
        0x0f,0x31,0x89,0x07,0x89,0x57,4,
        0x0f,0x31,0x89,0x47,8,0x89,0x57,12,
        0x9c,0x5e,0x89,0x77,20,0x89,0x4f,24,0x89,0x67,28,
        0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+THREAD_CPU_ARITHMETIC,body,sizeof(body));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t initial_threads=MEM32(stats+4),initial_handles=MEM32(stats+8);
    for(unsigned life=0;life<3;++life) {
        uint32_t context=0x19900;memset(memory+context,0xcc,80);
        MEM32(context+64)=THREAD_CPU_ARITHMETIC;
        uint64_t expected=xbox_ReadTimeStampCounter();Sleep(40);
        CHECK(xbox_ReadTimeStampCounter()==expected);
        thread_cpu_create_join(THREAD_CPU_ARITHMETIC,context,life&1);
        fprintf(stderr,"[REFERENCE TSC] life=%u expected=%llu original=%llu/%llu flags=%08x/%08x\n",
            life,(unsigned long long)expected,(unsigned long long)MEM64(context),
            (unsigned long long)MEM64(context+8),MEM32(context+16),MEM32(context+20));
        CHECK(MEM64(context)==expected && MEM64(context+8)==expected);
        CHECK(MEM32(context+16)==MEM32(context+20) && (MEM32(context+20)&1));
        CHECK(MEM32(context+24)==0x12345678 && MEM32(context+60)==0xcccccccc);
        CHECK(MEM32(context+28)>0x60000000);
        MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==initial_threads && MEM32(stats+8)==initial_handles);
        CHECK(!audio_reference_step(&audio_reference_clock,audio_reference_blocks+PILOT_REFERENCE_BLOCKS_PER_MS));
        CHECK(xbox_ReadTimeStampCounter()>expected);
    }
    CHECK(thread_cpu_calls==3);
    CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    CHECK(pilot_io_device_tsc_bind(pilot_reference_clock_tsc,&audio_reference_clock));
    CHECK(!pilot_io_device_tsc_bind(NULL,NULL));
    fprintf(stderr,"PASS reference TSC: three real original CPU thread lives, repeated RDTSC equals compiled counter, flags/ECX/stack/canary/cleanup,120ms host stalls; explicit steps alone advance\n");
}
