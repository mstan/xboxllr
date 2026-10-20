/* Actual transport observations through the guest ABI: no synthetic cursor or
 * wall-time completion. The retained queued samples must survive a ring edit. */
#if defined(PILOT_AUDIO_DSP) && PILOT_AUDIO_HAS_PAUSE && !defined(PILOT_AUDIO_REFERENCE)
static void audio_pause_rewrite(uint32_t iface,int16_t value)
{
    struct PilotAudioBuffer *b=audio_buffer(iface);
    uint32_t a[]={iface,0,b->bytes,0x19300,0x19304,0x19308,0x1930c,0};
    CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Lock,a));
    memset(memory+b->data,0,b->bytes);
    if(b->format==0x69) {
        for(unsigned offset=0;offset<b->bytes;offset+=b->block_align)
            for(unsigned c=0;c<b->channels;++c)MEM16(b->data+offset+c*4)=(uint16_t)value;
    } else for(unsigned offset=0;offset<b->bytes;offset+=2)MEM16(b->data+offset)=(uint16_t)value;
    a[1]=b->data;a[2]=b->bytes;a[3]=a[4]=0;
    CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Unlock,a));
}
static void test_audio_pause(void)
{
    vm_ensure();uint64_t before=vm_committed(0,0x100000000ull);
    for(unsigned lifetime=0;lifetime<2;++lifetime) {
        uint32_t device=audio_test_device(),iface;
#ifdef PILOT_AUDIO_ADPCM
        if(lifetime) {
            iface=adpcm_test_buffer_flags(device,2,64,0x1a0);
            uint32_t data[]={iface,0,1024*72};CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,data));
        } else
#endif
        iface=audio_test_buffer_format_flags(device,192000,2,16,48000,0,0x1a0);
        struct PilotAudioBuffer *b=audio_buffer(iface);
        uint32_t a[8]={iface,1};
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Pause,a)==1); // inactive S_FALSE
        a[1]=0;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Pause,a));
        audio_pause_rewrite(iface,8192);
#if PILOT_AUDIO_HAS_NOTIFICATIONS
        uint32_t create[]={0x19700,0,1,0};call_kernel(189,create,4);CHECK(!g_eax);
        uint32_t event=MEM32(create[0]),status;HANDLE owned=event_duplicate(event,&status);CHECK(owned && !status);
        MEM32(0x19720)=b->bytes/2-b->block_align;MEM32(0x19724)=event;
        a[1]=1;a[2]=0x19720;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetNotificationPositions,a));
        call_kernel(187,&event,1);CHECK(!g_eax);
#endif
        a[1]=a[2]=0;a[3]=1;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a));
        Sleep(70);a[1]=1;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Pause,a));
        CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Pause,a)); // repeated pause
        Sleep(50); // retire the host's in-flight processing quantum
        a[1]=0x19300;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetStatus,a) && MEM32(a[1])==6);
        a[1]=0x19304;a[2]=0x19308;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition,a));
        uint32_t cursor=MEM32(a[1]),write=MEM32(a[2]);CHECK(cursor && cursor<b->bytes/2);
        CHECK(pilot_audio_buffer_pause(b->host,2)<0); // unsupported mode cannot disturb pause
        audio_pause_rewrite(iface,-8192);Sleep(70);
        CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition,a));
        CHECK(MEM32(a[1])==cursor && MEM32(a[2])==write);
#if PILOT_AUDIO_HAS_NOTIFICATIONS
        CHECK(WaitForSingleObject(owned,0)==WAIT_TIMEOUT);
#endif
        float *samples=calloc(19200*32,sizeof(float));CHECK(samples);
        CHECK(!pilot_audio_capture_begin(samples,19200,0));
        a[1]=0;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Pause,a));
        CHECK(pilot_audio_capture_wait(5000)==1 && pilot_audio_capture_end()==19200);
        int first=0,saw_new=0;
        for(unsigned frame=0;frame<19200;++frame) {
            float value=samples[frame*32];
            if(fabsf(value)>.05f){if(!first)first=value>0?1:-1;if(value<0)saw_new=1;}
        }
        free(samples);CHECK(first==1 && saw_new); // queued old PCM, then edited ring
#if PILOT_AUDIO_HAS_NOTIFICATIONS
        CHECK(WaitForSingleObject(owned,2500)==WAIT_OBJECT_0 && CloseHandle(owned));
#endif
        a[1]=1;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Pause,a));
        a[1]=0;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetCurrentPosition,a));
        Sleep(40);a[1]=0x19304;a[2]=0x19308;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition,a));
        CHECK(!MEM32(a[1]) && !MEM32(a[2]));
        CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Stop,a));
        a[1]=1;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Pause,a)==1);
        uint32_t capacity=b->bytes;
        a[1]=a[2]=0;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a));
        CHECK(!b->host);a[2]=capacity;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a));
        a[1]=0x19300;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetStatus,a) && !MEM32(a[1]));
        CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,a));
        a[0]=device;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSound_Release,a));
        CHECK(!wait_handles && vm_committed(0,0x100000000ull)==before);
    }
    puts("PASS audio pause: PCM/ADPCM ABI, inactive return, paused status/loop, frozen cursors, queued samples survive ring edits, notifications, paused seek/stop/detach, two lifetimes");
}
#endif
