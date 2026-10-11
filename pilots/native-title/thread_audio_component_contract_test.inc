/* Real original CPU calls enter the source-bound whole audio API. Muted host
 * PCM readback observes publication; no Play or synthesized completion. */
#define AUDIO_COMPONENT_CPU 0x16000u
#define AUDIO_COMPONENT_HELPER 0x16f80u
#define AUDIO_FIELD(n) (0xf0000000u+(n))
#define AUDIO_OUTPUT(n) (0xe0000000u+(n))
static unsigned audio_component_calls,audio_component_checks,audio_component_results;
static uint32_t audio_component_old_data;
static unsigned audio_component_pool_count(void)
{
    unsigned count=0;for(unsigned i=0;i<4096;++i)if(pool_blocks[i].span)++count;return count;
}
static void test_audio_component_sources(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));
    long length=ftell(file);CHECK(length>0x178 && length<64*1024*1024);rewind(file);
    unsigned char *blob=malloc((size_t)length);CHECK(blob);
    CHECK(fread(blob,1,(size_t)length,file)==(size_t)length);fclose(file);
    char identity[65];test_component_hash(blob,(uint32_t)length,identity);CHECK(!strcmp(identity,PILOT_XBE_SHA256));
    uint32_t base,sections,table;memcpy(&base,blob+0x104,4);memcpy(&sections,blob+0x11c,4);memcpy(&table,blob+0x120,4);
    CHECK(table>=base && (uint64_t)table-base+sections*56<=(uint64_t)length);
    unsigned guards=sizeof(pilot_audio_guards)/sizeof(pilot_audio_guards[0]);
    for(unsigned index=0;index<PILOT_AUDIO_COUNT+guards;++index) {
        uint32_t address=index<PILOT_AUDIO_COUNT?pilot_audio_calls[index].address:pilot_audio_guards[index-PILOT_AUDIO_COUNT].address;
        uint32_t bytes=index<PILOT_AUDIO_COUNT?pilot_audio_calls[index].bytes:pilot_audio_guards[index-PILOT_AUDIO_COUNT].bytes;
        int found=0;
        for(unsigned section=0;section<sections;++section) {
            uint32_t words[5];memcpy(words,blob+table-base+section*56,sizeof(words));
            if(address<words[1] || (uint64_t)address+bytes>(uint64_t)words[1]+words[4])continue;
            CHECK(words[0]&2u);CHECK((uint64_t)words[3]+words[4]<=(uint64_t)length);
            commit(address,bytes);memcpy(memory+address,blob+words[3]+address-words[1],bytes);found=1;break;
        }
        CHECK(found);
    }
    free(blob);native_audio_components_bind();
}
static const char *test_audio_component_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==AUDIO_COMPONENT_HELPER && regs[1]<=3 && regs[2]==0x1b000);
    CHECK(regs[3]==0x11223344 && regs[5]==0x44556677 && regs[6]==0x22334455 && regs[7]==regs[2]);
    unsigned phase=regs[1];uint32_t context=regs[2];
    const char *error=audio_original_enter();if(error)return error;
    AcquireSRWLockExclusive(&audio_lock);
    if(phase<3) {
        struct PilotAudioBuffer *b=audio_buffer(MEM32(context+4));
        CHECK(b->owned && b->host && b->bytes==64 && !b->locked);
        CHECK(MEM32(context)==audio_device+8 && MEM32(audio_device+4)==2);
        CHECK(test_pool_owned(b->base) && test_pool_owned(audio_device));
        uint32_t words[7];CHECK(!vm_query(b->data,words));
        CHECK(words[4]==MEM_COMMIT && words[5]==PAGE_READWRITE);
        uint8_t samples[64];CHECK(!pilot_audio_buffer_copy(b->host,0,samples,sizeof(samples),1));
        CHECK(!memcmp(samples,memory+b->data,sizeof(samples)));
        if(!phase) {
            CHECK(!MEM32(b->data) && !MEM32(b->data+60));
            audio_component_old_data=b->data;MEM32(context+100)=b->base;MEM32(context+104)=audio_device;
        } else {
            CHECK(MEM32(b->data)==(phase==1?0x13572468u:0x89abcdefu));
            CHECK(MEM32(b->data+60)==(phase==1?0x24681357u:0xfedcba98u));
            for(unsigned i=4;i<60;++i)CHECK(!samples[i]);
            if(phase==1) {
                CHECK(b->data==audio_component_old_data && MEM32(context+80)==b->data && MEM32(context+84)==64);
                CHECK(!MEM32(context+88) && !MEM32(context+92));
            } else {
                CHECK(MEM32(context+80)==b->data+60 && MEM32(context+84)==4);
                CHECK(MEM32(context+88)==b->data && MEM32(context+92)==4);
                CHECK(b->volume==-900 && b->frequency==24000 && b->headroom==600);
            }
        }
        MEM32(context+108)=b->data;
    } else {
        CHECK(!audio_device);
#ifdef PILOT_AUDIO_DSP
        CHECK(!audio_effects && !audio_effects_storage);
#endif
        for(unsigned i=0;i<256;++i)CHECK(!audio_buffers[i].base);
        CHECK(!test_pool_owned(MEM32(context+100)) && !test_pool_owned(MEM32(context+104)));
        MEMORY_BASIC_INFORMATION host;
        CHECK(VirtualQuery(memory+MEM32(context+108),&host,sizeof(host)) && host.State==MEM_RESERVE);
    }
    ++audio_component_checks;ReleaseSRWLockExclusive(&audio_lock);
    error=audio_original_leave();if(error)return error;
    regs[0]=0;regs[4]+=4;return NULL;
}
static void audio_cpu_bytes(uint32_t *cursor,const uint8_t *bytes,unsigned size)
{memcpy(memory+*cursor,bytes,size);*cursor+=size;}
#define AUDIO_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};audio_cpu_bytes(c,b,sizeof(b));} while(0)
static void audio_cpu_word(uint32_t *cursor,uint32_t value)
{audio_cpu_bytes(cursor,(const uint8_t*)&value,4);}
static void audio_cpu_push(uint32_t *cursor,uint32_t value)
{
    if((value&0xffff0000u)==AUDIO_FIELD(0)) {AUDIO_BYTES(cursor,0xff,0xb7);audio_cpu_word(cursor,value&0xffffu);}
    else if((value&0xffff0000u)==AUDIO_OUTPUT(0)) {AUDIO_BYTES(cursor,0x8d,0x87);audio_cpu_word(cursor,value&0xffffu);AUDIO_BYTES(cursor,0x50);}
    else {AUDIO_BYTES(cursor,0x68);audio_cpu_word(cursor,value);}
}
static void audio_cpu_result(uint32_t *cursor)
{
    AUDIO_BYTES(cursor,0x89,0x87);audio_cpu_word(cursor,128+audio_component_results++*4);
}
static void audio_cpu_call(uint32_t *cursor,unsigned entry,const uint32_t *args)
{
    unsigned count=pilot_audio_calls[entry].stack_bytes/4;
    for(unsigned i=count;i;--i)audio_cpu_push(cursor,args[i-1]);
    AUDIO_BYTES(cursor,0xb8);audio_cpu_word(cursor,pilot_audio_calls[entry].address);AUDIO_BYTES(cursor,0xff,0xd0);
    audio_cpu_result(cursor);++audio_component_calls;
}
static void audio_cpu_check(uint32_t *cursor,unsigned phase)
{
    AUDIO_BYTES(cursor,0xb9);audio_cpu_word(cursor,phase);AUDIO_BYTES(cursor,0x89,0xfa,0xb8);
    audio_cpu_word(cursor,AUDIO_COMPONENT_HELPER);AUDIO_BYTES(cursor,0xff,0xd0);audio_cpu_result(cursor);
}
static void test_thread_audio_component_lle(const char *path,int failure_case)
{
    CHECK(SetEnvironmentVariableA("XLLE_PILOT_AUDIO_OUTPUT_MUTE","1"));
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));vm_ensure();test_audio_component_sources(path);
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};
    memcpy(memory+AUDIO_COMPONENT_HELPER,helper,sizeof(helper));char identity[65];test_component_hash(helper,sizeof(helper),identity);
    CHECK(!pilot_io_device_thread_component_bind_abi(AUDIO_COMPONENT_HELPER,sizeof(helper),identity,0,test_audio_component_body));
    uint32_t desc=0x1c000,fmt=0x1c080;memset(memory+desc,0,24);MEM32(desc)=24;MEM32(desc+4)=0xa0;MEM32(desc+8)=64;MEM32(desc+12)=fmt;
    memset(memory+fmt,0,18);MEM16(fmt)=1;MEM16(fmt+2)=1;MEM32(fmt+4)=48000;MEM32(fmt+8)=96000;MEM16(fmt+12)=2;MEM16(fmt+14)=16;
    uint32_t cursor=AUDIO_COMPONENT_CPU;
    AUDIO_BYTES(&cursor,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8);
    if(failure_case) {
        if(failure_case==1) {const uint32_t args[5]={0};audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSound_DownloadEffectsImage,args);}
        else {
            CHECK(failure_case>=2 && failure_case<=5);audio_cpu_push(&cursor,0);
            AUDIO_BYTES(&cursor,0xb8);audio_cpu_word(&cursor,pilot_audio_guards[failure_case-2].address);AUDIO_BYTES(&cursor,0xff,0xd0);
        }
    } else {
        const uint32_t device[]={0,AUDIO_OUTPUT(0),0};audio_cpu_call(&cursor,PILOT_AUDIO_DirectSoundCreate,device);
        const uint32_t buffer[]={AUDIO_FIELD(0),desc,AUDIO_OUTPUT(4),0};audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSound_CreateSoundBuffer,buffer);
        audio_cpu_check(&cursor,0);
        const uint32_t lock[]={AUDIO_FIELD(4),0,64,AUDIO_OUTPUT(80),AUDIO_OUTPUT(84),AUDIO_OUTPUT(88),AUDIO_OUTPUT(92),0};
        audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSoundBuffer_Lock,lock);
        AUDIO_BYTES(&cursor,0x8b,0x47,80,0xc7,0x00,0x68,0x24,0x57,0x13,0xc7,0x40,60,0x57,0x13,0x68,0x24);
        const uint32_t unlock[]={AUDIO_FIELD(4),AUDIO_FIELD(80),AUDIO_FIELD(84),AUDIO_FIELD(88),AUDIO_FIELD(92)};
        audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSoundBuffer_Unlock,unlock);audio_cpu_check(&cursor,1);
        const uint32_t volume[]={AUDIO_FIELD(4),(uint32_t)-900};audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSoundBuffer_SetVolume,volume);
        const uint32_t frequency[]={AUDIO_FIELD(4),24000};audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSoundBuffer_SetFrequency,frequency);
        const uint32_t detach[]={AUDIO_FIELD(4),0,0};audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,detach);
        const uint32_t rebind[]={AUDIO_FIELD(4),0,64};audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSoundBuffer_SetBufferData,rebind);
        const uint32_t wrap[]={AUDIO_FIELD(4),60,8,AUDIO_OUTPUT(80),AUDIO_OUTPUT(84),AUDIO_OUTPUT(88),AUDIO_OUTPUT(92),0};
        audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSoundBuffer_Lock,wrap);
        AUDIO_BYTES(&cursor,0x8b,0x47,80,0xc7,0x00,0x98,0xba,0xdc,0xfe,0x8b,0x47,88,0xc7,0x00,0xef,0xcd,0xab,0x89);
        audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSoundBuffer_Unlock,unlock);audio_cpu_check(&cursor,2);
        const uint32_t release_buffer[]={AUDIO_FIELD(4)};audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSoundBuffer_Release,release_buffer);
        const uint32_t release_device[]={AUDIO_FIELD(0)};audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSound_Release,release_device);
        audio_cpu_check(&cursor,3);
    }
    AUDIO_BYTES(&cursor,0x9c,0x5a,0x89,0x57,12,0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x5f,112,0x89,0x6f,116,0x89,0x77,120,0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(cursor<AUDIO_COMPONENT_HELPER && audio_component_results<80);
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);unsigned baseline_pool=audio_component_pool_count();
    for(unsigned life=0;life<(failure_case?1u:3u);++life) {
        uint32_t context=0x1b000;memset(memory+context,0,512);MEM32(context+64)=AUDIO_COMPONENT_CPU;MEM32(context+124)=0xa5a55a5a;
        for(unsigned i=0;i<audio_component_results;++i)MEM32(context+128+i*4)=0xdeadbeef;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7+life);
        double expected=12345.6789+life;memcpy(memory+context+48,&expected,8);
        if(failure_case) {
            uint32_t out=0x19800,create[]={out,0,AUDIO_COMPONENT_CPU,context,0};CHECK(!event_lle_call(254,create));
            uint32_t handle=MEM32(out),ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
            uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
            uint32_t join[]={object,0,0,0,0},result=0;const char *error=pilot_io_device_lle(159,memory,join,&result);
            CHECK(error && !strcmp(error,failure_case==1?"original scheduling native audio effects ownership requires integration":
                "original audio inner object destruction requires its native lifetime owner"));
            CHECK(!audio_device && !MEM32(context+76));
#ifdef PILOT_AUDIO_DSP
            CHECK(!audio_effects && !audio_effects_storage);
#endif
            CHECK(audio_component_pool_count()==baseline_pool && MEM32(audio_original_gate+16)==UINT32_MAX);
            fprintf(stderr,"PASS: source-bound original CPU audio ownership guard before object/native mutation: %s\n",error);return;
        }
        thread_cpu_create_join(AUDIO_COMPONENT_CPU,context,life&1);
        CHECK(MEM32(context+76)==1 && MEM32(context+124)==0xa5a55a5a && audio_component_pool_count()==baseline_pool);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(MEM32(context+112)==0x11223344 && MEM32(context+116)==0x44556677 && MEM32(context+120)==0x22334455);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        for(unsigned i=0;i<audio_component_results;++i)CHECK(!MEM32(context+128+i*4));
        CHECK(MEM32(audio_original_gate+16)==UINT32_MAX && !MEM32(audio_original_gate+20) && !MEM32(audio_original_gate+24));
        CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    CHECK(audio_component_checks==12);
    uint32_t gate=audio_original_gate;audio_original_gate=native_component_original_gate=0;pool_free(gate);
    fprintf(stderr,"PASS: 21 actual-byte audio APIs + four vtable guards; three muted original CPU device/PCM Lock/Unlock/detach/rebind/release lifetimes, %u real API calls, exact host PCM, pool/thread/handle cleanup and GPR/ESP/flags/x87/XMM\n",audio_component_calls*3);
}
#undef AUDIO_BYTES
#undef AUDIO_FIELD
#undef AUDIO_OUTPUT
