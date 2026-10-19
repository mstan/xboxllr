#if defined(PILOT_AUDIO_DSP) && PILOT_AUDIO_HAS_NOTIFICATIONS && !defined(PILOT_AUDIO_REFERENCE)
static void test_audio_notifications(void)
{
    vm_ensure();uint64_t before=vm_committed(0,0x100000000ull);
    for(unsigned lifetime=0;lifetime<2;++lifetime) {
        uint32_t device=audio_test_device();
        uint32_t bytes=4096,iface;
#ifdef PILOT_AUDIO_ADPCM
        if(lifetime) {
            bytes=64*72;iface=adpcm_test_buffer_flags(device,2,64,0x1a0);
            uint32_t data[]={iface,0,bytes};CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,data));
        } else
#endif
        iface=audio_test_buffer_format_flags(device,bytes,2,16,48000,0,0x1a0);
        struct PilotAudioBuffer *buffer=audio_buffer(iface);CHECK(buffer->notification_enabled);
        uint32_t create[]={0x19700,0,1,0};call_kernel(189,create,4);CHECK(!g_eax);
        uint32_t first=MEM32(create[0]);create[0]+=4;call_kernel(189,create,4);CHECK(!g_eax);
        uint32_t second=MEM32(create[0]),status;
        HANDLE left=event_duplicate(first,&status);CHECK(left && !status);
        HANDLE right=event_duplicate(second,&status);CHECK(right && !status);
        MEM32(0x19720)=bytes/4-buffer->block_align;MEM32(0x19724)=first;
        MEM32(0x19728)=bytes*3/4-buffer->block_align;MEM32(0x1972c)=second;
        uint32_t a[8]={iface,2,0x19720};
        CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetNotificationPositions,a));
        CHECK(buffer->notifications[0].offset==(buffer->format==0x69?15*64*4:1020));
        CHECK(buffer->notification_count==2 && WaitForSingleObject(left,0)==WAIT_TIMEOUT && WaitForSingleObject(right,0)==WAIT_TIMEOUT);
        // A bad later entry must leave the existing list and handle count intact.
        DWORD handles_before,handles_after;CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles_before));
        struct PilotAudioNotification bad[]={{buffer->notifications[0].offset,left},{audio_pcm_bytes(buffer,bytes),right}};
        CHECK(pilot_audio_buffer_notifications(buffer->host,2,bad)<0);
        CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles_after) && handles_before==handles_after);
        // Guest close and storage replacement cannot revoke operation-owned refs.
        call_kernel(187,&first,1);CHECK(!g_eax);call_kernel(187,&second,1);CHECK(!g_eax);
        a[1]=a[2]=0;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a));
        CHECK(!buffer->host && buffer->notification_count==2);
        a[2]=bytes;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a));
        a[2]=0;a[3]=3;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a));
        CHECK(WaitForSingleObject(left,5000)==WAIT_OBJECT_0 && WaitForSingleObject(right,5000)==WAIT_OBJECT_0);
        uint32_t status_args[]={iface,0x19740};
        CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetStatus,status_args));
        CHECK(MEM32(0x19740)&1);
        // Removing all positions stops notifications while real playback continues.
        a[1]=a[2]=0;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetNotificationPositions,a));
        CHECK(!buffer->notification_count && ResetEvent(left) && ResetEvent(right));
        float average[32];audio_test_observe(average,0);
        CHECK(WaitForSingleObject(left,0)==WAIT_TIMEOUT && WaitForSingleObject(right,0)==WAIT_TIMEOUT);
        CHECK(CloseHandle(left) && CloseHandle(right));
        CHECK(!audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,a));
        a[0]=device;CHECK(!audio_test_call(PILOT_AUDIO_IDirectSound_Release,a));
        CHECK(!wait_handles && vm_committed(0,0x100000000ull)==before);
    }
    puts("PASS audio notifications: actual PCM/ADPCM progress, source-block conversion, typed retained events, invalid later entry rollback, guest-close/storage replacement lifetime, removal and two complete lifetimes");
}
#endif
