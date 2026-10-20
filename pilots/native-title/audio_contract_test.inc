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
static uint32_t audio_test_buffer_format_flags(uint32_t device,uint32_t bytes,unsigned channels,unsigned bits,unsigned rate,uint32_t mixbins,uint32_t flags)
{
    uint32_t desc=0x19100,fmt=0x19200,out=0x19010;
    memset(memory+desc,0,24);MEM32(desc)=24;MEM32(desc+4)=flags;MEM32(desc+8)=bytes;MEM32(desc+12)=fmt;
    MEM32(desc+16)=mixbins;
    memset(memory+fmt,0,18);MEM16(fmt)=1;MEM16(fmt+2)=(uint16_t)channels;MEM32(fmt+4)=rate;
    MEM32(fmt+8)=rate*channels*(bits/8);MEM16(fmt+12)=(uint16_t)(channels*(bits/8));MEM16(fmt+14)=(uint16_t)bits;
    MEM32(out-4)=0xaabbccdd;MEM32(out)=0;MEM32(out+4)=0x11223344;
    uint32_t args[]={device,desc,out,0};
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSound_CreateSoundBuffer,args)==0);
    CHECK(MEM32(out) && MEM32(out-4)==0xaabbccdd && MEM32(out+4)==0x11223344);
    return MEM32(out);
}
static uint32_t audio_test_buffer_format(uint32_t device,uint32_t bytes,unsigned channels,unsigned bits,unsigned rate,uint32_t mixbins)
{return audio_test_buffer_format_flags(device,bytes,channels,bits,rate,mixbins,0xa0);}
static uint32_t audio_test_buffer(uint32_t device,uint32_t bytes)
{return audio_test_buffer_format(device,bytes,1,16,48000,0);}
static void test_audio(void)
{
    vm_ensure();uint64_t committed=vm_committed(0,0x100000000ull);
    uint32_t device=audio_test_device(),buffer=audio_test_buffer(device,9600);
    struct PilotAudioBuffer *b=audio_buffer(buffer);
    CHECK(b->owned && b->bytes==9600 && b->headroom==600 && MEM32(audio_device+4)==2);
    MEM32(0x194fc)=0x11223344;MEM32(0x19510)=0x55667788;
    audio_caps(0x19500);
    CHECK(MEM32(0x19500)==255 && !MEM32(0x19504) && MEM32(0x19508)==16381 && MEM32(0x1950c)==9680);
    CHECK(MEM32(0x194fc)==0x11223344 && MEM32(0x19510)==0x55667788);
    uint32_t ref_args[]={b->base};CHECK(audio_test_call(PILOT_AUDIO_ObjectAddRef,ref_args)==2);
    ref_args[0]=buffer;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,ref_args)==1);
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
    a[1]=0x19304;a[2]=0x19308;
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition,a)==0);
    uint32_t stopped_cursor=MEM32(a[1]),stopped_write=MEM32(a[2]);
    Sleep(40);
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition,a)==0);
    CHECK(MEM32(a[1])==stopped_cursor && MEM32(a[2])==stopped_write);
#ifdef PILOT_AUDIO_DSP
    if(PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion<PILOT_AUDIO_COUNT) {
    a[1]=2400;a[2]=2400;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion,a)==0);
    a[1]=1;CHECK((int32_t)audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion,a)<0);
    a[1]=0;a[2]=0;a[3]=3;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a)==0);
    /* Multiple real device wraps must stay in the requested middle quarter. */
    Sleep(160);
    for(unsigned i=0;i<12;++i) {
        a[1]=0x19304;a[2]=0x19308;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition,a)==0);
        CHECK(MEM32(a[1])>=2400 && MEM32(a[1])<4800 && MEM32(a[2])>=2400 && MEM32(a[2])<4800);
        Sleep(7);
    }
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Stop,a)==0);
    a[1]=0;a[2]=0;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion,a)==0);
    }
#endif
    a[1]=(uint32_t)-1800;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetVolume,a)==0 && b->volume==-1800);
    a[1]=300;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetHeadroom,a)==0 && b->headroom==300);
    a[1]=1;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetVolume,a)==0x88780032 && b->volume==-1800);
    a[1]=24000;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetFrequency,a)==0 && b->frequency==24000);
    uint32_t caller=0,bytes=9600;CHECK(!vm_allocate(&caller,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    memcpy(memory+caller,memory+b->data,9600);uint32_t old=b->data;
    a[1]=caller;a[2]=9600;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a)==0);
    CHECK(!b->owned && b->data==caller && b->frequency==24000 && test_write_faults(old));
    audio_caps(0x19500);
    CHECK(MEM32(0x19500)==255 && MEM32(0x19508)==16381 && MEM32(0x1950c)==80);
    CHECK(pilot_audio_buffer_copy(b->host,0,samples,9600,1)==0 && !memcmp(samples,memory+caller,9600));
    if(PILOT_AUDIO_IDirectSoundBuffer_SetPitch<PILOT_AUDIO_COUNT) {
        a[1]=0;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetPitch,a)==0 && b->frequency==48000);
    }
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
#ifdef PILOT_AUDIO_DSP
static void test_audio_effects(const char *path)
{
    vm_ensure();uint64_t before=vm_committed(0,0x100000000ull);
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0 && n<1024*1024);rewind(f);
    uint32_t source=0,bytes=(uint32_t)n;CHECK(!vm_allocate(&source,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(fread(memory+source,1,n,f)==(size_t)n);fclose(f);
    uint8_t *original=malloc(n);CHECK(original);memcpy(original,memory+source,n);
    uint32_t device=audio_test_device(),buffer=audio_test_buffer(device,9600),a[8]={buffer,0,0,1};
    struct PilotAudioBuffer *voice=audio_buffer(buffer);
    for(unsigned i=0;i<4800;++i)MEM16(voice->data+i*2)=(uint16_t)(int16_t)(2000.0*sin(6.283185307179586*440*i/48000));
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a)==0);
    uint64_t installed=0;
    for(unsigned iteration=0;iteration<4;++iteration) {
        MEM32(0x19320)=0x653470;MEM32(0x19324)=0x100;MEM32(0x19330)=0xcccccccc;
        a[0]=device;a[1]=source;a[2]=(uint32_t)n;a[3]=iteration==3?0:0x19320;a[4]=iteration==2?0:0x19330;
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSound_DownloadEffectsImage,a)==0);
        CHECK(!memcmp(original,memory+source,n));
        CHECK(audio_effects && audio_effects_storage);
        CHECK(audio_effects_location[0]==(iteration==3?UINT32_MAX:0x653470) && audio_effects_location[1]==(iteration==3?UINT32_MAX:0x100));
        if(a[4]){uint32_t desc=MEM32(a[4]);CHECK(desc>=audio_effects_storage && MEM32(desc)==3);}
        uint64_t now=vm_committed(0,0x100000000ull);if(iteration)CHECK(now==installed);installed=now;
        float *samples=calloc(4096*6,sizeof(float));CHECK(samples);
        CHECK(pilot_audio_capture_begin(samples,4096,1)==0);Sleep(180);
        unsigned frames=pilot_audio_capture_end();CHECK(frames==4096);
        double energy=0;for(unsigned i=0;i<frames*6;++i){CHECK(isfinite(samples[i]));energy+=samples[i]*samples[i];}
        CHECK(energy>0.01);free(samples);
        fprintf(stderr,"[AUDIO EFFECTS OUTPUT] replacement=%u real output frames=%u energy=%.6f\n",iteration,frames,energy);
    }
    a[0]=buffer;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,a)==0);
    a[0]=device;CHECK(audio_test_call(PILOT_AUDIO_IDirectSound_Release,a)==0);
    CHECK(!audio_effects && !audio_effects_storage);bytes=0;CHECK(!vm_free(&source,&bytes,MEM_RELEASE));free(original);
    CHECK(vm_committed(0,0x100000000ull)==before);
    puts("actual effects ABI: input preserved, opaque locations, nullable output, concurrent processing/replacement and complete ownership release passed");
}
#endif
static void test_audio_failure(const char *kind)
{
    uint32_t device=audio_test_device(),a[8]={device,0x19300,1,0,0x19310};
    if(!strcmp(kind,"effects"))audio_test_call(PILOT_AUDIO_IDirectSound_DownloadEffectsImage,a);
    else {
        uint32_t b=audio_test_buffer(device,9600);a[0]=b;
        if(!strcmp(kind,"routing"))audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetMixBins,a);
        else if(!strcmp(kind,"loop")){a[1]=2;a[2]=100;audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion,a);}
    }
    ExitProcess(0);
}
