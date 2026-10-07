/* Real Windows audio device and buffers; no mocked clock/output provider. */
static uint32_t audio_test_call(unsigned entry,const uint32_t *args)
{
    unsigned count=pilot_audio_calls[entry].stack_bytes/4;
    g_esp=0x18000;MEM32(g_esp)=0x12340;
    for(unsigned i=0;i<count;++i)MEM32(g_esp+4+i*4)=args[i];
    g_ebx=0x12345678;g_esi=0x23456789;g_edi=0x3456789a;g_ebp=0x456789ab;
    pilot_audio_dispatch(entry);
    CHECK(g_esp==0x18004+count*4 && MEM32(0x18000)==0x12340);
    CHECK(g_ebx==0x12345678 && g_esi==0x23456789 && g_edi==0x3456789a && g_ebp==0x456789ab);
    return g_eax;
}
static uint32_t audio_test_device(void)
{
    vm_ensure();uint32_t args[]={0,0x19000,0};MEM32(0x19000)=0xcccccccc;
    CHECK(audio_test_call(PILOT_AUDIO_DirectSoundCreate,args)==0);
    uint32_t iface=MEM32(0x19000);CHECK(iface==audio_device+8 && MEM32(audio_device+4)==1);
    return iface;
}
static uint32_t audio_test_buffer(uint32_t device,uint32_t bytes)
{
    uint32_t desc=0x19100,fmt=0x19200,out=0x19010;
    memset(memory+desc,0,24);MEM32(desc)=24;MEM32(desc+4)=0xa0;MEM32(desc+8)=bytes;MEM32(desc+12)=fmt;
    memset(memory+fmt,0,18);MEM16(fmt)=1;MEM16(fmt+2)=1;MEM32(fmt+4)=48000;
    MEM32(fmt+8)=96000;MEM16(fmt+12)=2;MEM16(fmt+14)=16;
    MEM32(out-4)=0xaabbccdd;MEM32(out)=0;MEM32(out+4)=0x11223344;
    uint32_t args[]={device,desc,out,0};
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSound_CreateSoundBuffer,args)==0);
    CHECK(MEM32(out) && MEM32(out-4)==0xaabbccdd && MEM32(out+4)==0x11223344);
    return MEM32(out);
}
static void test_audio(void)
{
    vm_ensure();uint64_t committed=vm_committed(0,0x100000000ull);
    uint32_t device=audio_test_device(),buffer=audio_test_buffer(device,9600);
    struct PilotAudioBuffer *b=audio_buffer(buffer);
    CHECK(b->owned && b->bytes==9600 && b->headroom==600 && MEM32(audio_device+4)==2);
    for(unsigned i=0;i<4800;++i)MEM16(b->data+2*i)=(uint16_t)(int16_t)(1500.0*sin(6.283185307179586*440*i/48000));
    uint32_t a[8]={buffer,0,9600,0x19300,0x19304,0x19308,0x1930c,0};
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Lock,a)==0);
    CHECK(MEM32(0x19300)==b->data && MEM32(0x19304)==9600 && !MEM32(0x19308) && !MEM32(0x1930c));
    a[1]=b->data;a[2]=9600;a[3]=a[4]=0;
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Unlock,a)==0);
    uint8_t samples[9600];CHECK(pilot_audio_buffer_copy(b->host,0,samples,9600,1)==0);
    CHECK(!memcmp(samples,memory+b->data,9600));
    a[1]=9596;a[2]=8;a[3]=0x19300;a[4]=0x19304;a[5]=0x19308;a[6]=0x1930c;a[7]=0;
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Lock,a)==0);
    CHECK(MEM32(0x19300)==b->data+9596 && MEM32(0x19304)==4 && MEM32(0x19308)==b->data && MEM32(0x1930c)==4);
    MEM32(b->data)=0;MEM32(b->data+9596)=0;
    a[1]=b->data+9596;a[2]=4;a[3]=b->data;a[4]=4;
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Unlock,a)==0);
    CHECK(pilot_audio_buffer_copy(b->host,0,samples,9600,1)==0 && !memcmp(samples,memory+b->data,9600));
    a[1]=0;a[2]=0;a[3]=0;
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a)==0);
    DWORD start=GetTickCount();uint32_t saw_playing=0,saw_cursor=0,status=0;
    do {
        a[1]=0x19300;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetStatus,a)==0);status=MEM32(a[1]);
        saw_playing|=status&1;
        a[1]=0x19304;a[2]=0x19308;
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition,a)==0);
        CHECK(MEM32(a[1])<9600 && MEM32(a[2])<9600);saw_cursor|=MEM32(a[1])!=0;
        if(saw_playing && !(status&1))break;
        Sleep(5);
    } while(GetTickCount()-start<1500);
    CHECK(saw_playing && saw_cursor && !(status&1));
    fprintf(stderr,"[AUDIO TEST] real one-shot reached end in %lu ms, advancing native play cursor\n",GetTickCount()-start);
    a[1]=0;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetCurrentPosition,a)==0);
    a[1]=0;a[2]=0;a[3]=1;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a)==0);
    Sleep(250);a[1]=0x19300;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetStatus,a)==0 && MEM32(a[1])==5);
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Stop,a)==0);
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetStatus,a)==0 && !(MEM32(a[1])&1));
    a[1]=(uint32_t)-1800;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetVolume,a)==0 && b->volume==-1800);
    a[1]=300;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetHeadroom,a)==0 && b->headroom==300);
    a[1]=1;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetVolume,a)==0x88780032 && b->volume==-1800);
    a[1]=24000;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetFrequency,a)==0 && b->frequency==24000);
    uint32_t caller=0,bytes=9600;CHECK(!vm_allocate(&caller,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    memcpy(memory+caller,memory+b->data,9600);uint32_t old=b->data;
    a[1]=caller;a[2]=9600;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a)==0);
    CHECK(!b->owned && b->data==caller && b->frequency==24000 && test_write_faults(old));
    CHECK(pilot_audio_buffer_copy(b->host,0,samples,9600,1)==0 && !memcmp(samples,memory+caller,9600));
    a[1]=0;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetPitch,a)==0 && b->frequency==48000);
    a[0]=device;CHECK(audio_test_call(PILOT_AUDIO_IDirectSound_Release,a)==1); /* buffer retains device */
    a[0]=buffer;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,a)==0 && !audio_device);
    CHECK(!test_write_faults(caller));bytes=0;CHECK(!vm_free(&caller,&bytes,MEM_RELEASE));
    CHECK(vm_committed(0,0x100000000ull)==committed);
    /* Deferred storage retains properties and installs them on actual allocation. */
    device=audio_test_device();buffer=audio_test_buffer(device,0);b=audio_buffer(buffer);
    a[0]=buffer;a[1]=(uint32_t)-1000;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetVolume,a)==0);
    a[1]=24000;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetFrequency,a)==0);
    a[1]=0;a[2]=9600;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a)==0 && b->frequency==24000);
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,a)==0);
    a[0]=device;CHECK(audio_test_call(PILOT_AUDIO_IDirectSound_Release,a)==0);
    CHECK(vm_committed(0,0x100000000ull)==committed);
    puts("audio contract: actual PCM storage/playback/end/cursor/loop/stop, Xbox ABI, wrapped locks, settings and ownership passed");
}
static void test_audio_failure(const char *kind)
{
    uint32_t device=audio_test_device(),a[8]={device,0x19300,1,0,0};
    if(!strcmp(kind,"effects"))audio_test_call(PILOT_AUDIO_IDirectSound_DownloadEffectsImage,a);
    else {
        uint32_t b=audio_test_buffer(device,9600);a[0]=b;
        if(!strcmp(kind,"routing"))audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetMixBins,a);
        else if(!strcmp(kind,"loop")){a[1]=2;a[2]=100;audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion,a);}
    }
    ExitProcess(0);
}
