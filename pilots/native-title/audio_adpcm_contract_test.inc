/* Real device, compressed guest storage and PCM host samples. The separate
 * codec fixture compares the borrowed decoder to the full APU reference. */
#if defined(PILOT_AUDIO_ADPCM) && defined(PILOT_AUDIO_DSP)
static uint32_t adpcm_test_buffer_flags(uint32_t device,unsigned channels,unsigned extension,uint32_t flags)
{
    uint32_t desc=0x19100,fmt=0x19200,out=0x19010;
    memset(memory+desc,0,24);MEM32(desc)=24;MEM32(desc+4)=flags;MEM32(desc+12)=fmt;
    memset(memory+fmt,0,20);MEM16(fmt)=0x69;MEM16(fmt+2)=(uint16_t)channels;
    MEM32(fmt+4)=44100;MEM32(fmt+8)=44100*36*channels/64;
    MEM16(fmt+12)=(uint16_t)(36*channels);MEM16(fmt+14)=4;MEM16(fmt+16)=2;MEM16(fmt+18)=(uint16_t)extension;
    uint32_t a[]={device,desc,out,0};
    CHECK(audio_test_call(PILOT_AUDIO_IDirectSound_CreateSoundBuffer,a)==0);
    return MEM32(out);
}
static uint32_t adpcm_test_buffer(uint32_t device,unsigned channels,unsigned extension)
{return adpcm_test_buffer_flags(device,channels,extension,0);}
static void adpcm_test_readback(struct PilotAudioBuffer *b)
{
    int16_t actual[8*64*2],expected[8*64*2];
    for(unsigned i=0;i<8;++i)
        CHECK(xbox_adpcm_decode_block(memory+b->data+i*b->block_align,b->block_align,b->channels,
                                     expected+i*64*b->channels,64*b->channels));
    CHECK(pilot_audio_buffer_copy(b->host,0,actual,8*64*b->channels*2,1)==0);
    CHECK(!memcmp(actual,expected,8*64*b->channels*2));
}
static void test_audio_adpcm(void)
{
    vm_ensure();uint64_t initial=vm_committed(0,0x100000000ull);
    for(unsigned channels=1;channels<=2;++channels) {
        uint32_t device=audio_test_device(),iface=adpcm_test_buffer(device,channels,channels==1?0:64);
        struct PilotAudioBuffer *b=audio_buffer(iface);uint32_t align=36*channels,bytes=8*align;
        CHECK(!b->host && b->format==0x69 && b->block_align==align);
        uint32_t a[8]={iface,0,bytes};
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a)==0);
        CHECK(b->owned && b->bytes==bytes);
        a[1]=0;a[2]=bytes;a[3]=0x19300;a[4]=0x19304;a[5]=0x19308;a[6]=0x1930c;a[7]=0;
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Lock,a)==0);
        CHECK(MEM32(a[3])==b->data && MEM32(a[4])==bytes && !MEM32(a[5]) && !MEM32(a[6]));
        /* Index0, zero nibbles => exactly constant predictor for all64frames.
         * Reserved bytes and the last non-played nibble must not reject a block. */
        for(unsigned i=0;i<8;++i)for(unsigned c=0;c<channels;++c) {
            uint32_t header=b->data+i*align+4*c;
            MEM16(header)=(uint16_t)(int16_t)(c?-2000:1000);MEM8(header+3)=255;
            MEM8(b->data+(i+1)*align-1)=0xf0;
        }
        a[1]=b->data;a[2]=bytes;a[3]=a[4]=0;
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Unlock,a)==0);adpcm_test_readback(b);
        /* A one-byte edit must refresh the entire dependent decoded block. */
        a[1]=align;a[2]=1;a[3]=0x19300;a[4]=0x19304;a[5]=a[6]=a[7]=0;
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Lock,a)==0);MEM8(b->data+align)=0;
        a[1]=b->data+align;a[2]=1;a[3]=a[4]=0;
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Unlock,a)==0);adpcm_test_readback(b);
        /* Wrapped compressed writes refresh both boundary blocks. */
        a[1]=bytes-1;a[2]=3;a[3]=0x19300;a[4]=0x19304;a[5]=0x19308;a[6]=0x1930c;
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Lock,a)==0);
        CHECK(MEM32(a[4])==1 && MEM32(a[6])==2);
        MEM8(b->data+bytes-1)=0;MEM16(b->data)=1000;
        a[1]=b->data+bytes-1;a[2]=1;a[3]=b->data;a[4]=2;
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Unlock,a)==0);adpcm_test_readback(b);
        for(unsigned i=0;i<8;++i) {
            a[1]=i*align;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetCurrentPosition,a)==0);
            a[1]=0x19300;a[2]=0x19304;
            CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition,a)==0 && MEM32(a[1])==i*align);
        }
        a[1]=bytes;CHECK((int32_t)audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetCurrentPosition,a)<0);
        int partial_loop=PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion<PILOT_AUDIO_COUNT;
        if(partial_loop) {
            a[1]=2*align;a[2]=4*align;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion,a)==0);
            a[1]=bytes;a[2]=1;CHECK((int32_t)audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion,a)<0);
        } else {
            // Full-ring playback includes the predictor edited in the earlier
            // partial-lock check; restore its constant value before mixing.
            MEM16(b->data+align)=1000;
        }
        a[1]=0;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetHeadroom,a)==0);
        a[1]=a[2]=0;a[3]=3;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a)==0);
        float average[32];audio_test_observe(average,0);
        CHECK(fabs(average[0]-1000.0/32768)<0.00003);
        CHECK(fabs(average[1]-(channels==1?1000.0:-2000.0)/32768)<0.00003);
        for(unsigned c=2;c<32;++c)CHECK(fabs(average[c])<0.000001);
        for(unsigned i=0;i<10;++i) {
            a[1]=0x19300;a[2]=0x19304;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetCurrentPosition,a)==0);
            CHECK(MEM32(a[1])>=(partial_loop?2*align:0) && MEM32(a[1])<(partial_loop?6*align:bytes) && !(MEM32(a[1])%align));
            CHECK(MEM32(a[2])>=(partial_loop?2*align:0) && MEM32(a[2])<(partial_loop?6*align:bytes) && !(MEM32(a[2])%align));Sleep(7);
        }
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Stop,a)==0);
        /* Caller-owned compressed storage remains alive after buffer/device release. */
        uint32_t source=0,allocated=bytes,old=b->data;
        CHECK(!vm_allocate(&source,0,&allocated,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        memcpy(memory+source,memory+b->data,bytes);a[1]=source;a[2]=bytes;
        CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a)==0);
        CHECK(!b->owned && b->data==source && test_write_faults(old));adpcm_test_readback(b);
        if(partial_loop) {
            a[1]=a[2]=0;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetLoopRegion,a)==0);
        }
        a[1]=a[2]=0;a[3]=2;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a)==0);
        Sleep(100);a[1]=0x19300;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_GetStatus,a)==0 && !MEM32(a[1]));
        a[1]=a[2]=0;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a)==0);
        CHECK(!b->host && !b->bytes && !b->data && !test_write_faults(source));
        a[1]=source;a[2]=bytes;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a)==0);
        CHECK(b->format==0x69 && b->block_align==align);adpcm_test_readback(b);
        /* Restore constant predictors after the writable-memory probe (block0)
         * and earlier partial-block edit (block1), before full-buffer playback. */
        MEM16(source)=MEM16(source+align)=1000;
        a[1]=a[2]=0;a[3]=3;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Play,a)==0);
        audio_test_observe(average,0);
        CHECK(fabs(average[0]-1000.0/32768)<0.00003);
        a[1]=a[2]=0;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,a)==0);
        a[0]=device;CHECK(audio_test_call(PILOT_AUDIO_IDirectSound_Release,a)==1);
        a[0]=iface;CHECK(audio_test_call(PILOT_AUDIO_IDirectSoundBuffer_Release,a)==0 && !audio_device);
        CHECK(!test_write_faults(source));allocated=0;CHECK(!vm_free(&source,&allocated,MEM_RELEASE));
        CHECK(vm_committed(0,0x100000000ull)==initial);
    }
    puts("ADPCM: mono/stereo actual output, compressed storage, partial/wrapped locks, byte cursors, seek/loop/end and ownership passed");
}
#endif
