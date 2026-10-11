/* One coherent whole-API original CPU effects lifecycle. Real engine install
 * waits for its current quantum; no Play or fabricated completion is used. */
#define AUDIO_EFFECTS_CPU 0x16800u
#define AUDIO_EFFECTS_HELPER 0x16f00u
#define AUDIO_EFFECTS_CONTEXT 0x1b000u
#define EFFECTS_FIELD(n) (0xf0000000u+(n))
#define EFFECTS_OUTPUT(n) (0xe0000000u+(n))
static uint8_t *audio_effects_input;
static uint32_t audio_effects_source,audio_effects_input_bytes,audio_effects_previous;
static unsigned audio_effects_checks;
static const char *test_audio_effects_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==AUDIO_EFFECTS_HELPER && regs[1]<=5 && regs[2]==AUDIO_EFFECTS_CONTEXT);
    CHECK(regs[3]==0x11223344 && regs[5]==0x44556677 && regs[6]==0x22334455 && regs[7]==regs[2]);
    unsigned phase=regs[1];uint32_t context=regs[2];
    const char *error=audio_original_enter();if(error)return error;
    AcquireSRWLockExclusive(&audio_lock);
    if(phase<4)CHECK(!memcmp(audio_effects_input,memory+audio_effects_source,audio_effects_input_bytes));
    if(phase<4) {
        CHECK(audio_device && MEM32(context)==audio_device+8 && MEM32(audio_device+4)==1);
        CHECK(audio_effects && audio_effects_storage);
        uint32_t words[7];CHECK(!vm_query(audio_effects_storage,words));
        CHECK(words[4]==MEM_COMMIT && words[5]==PAGE_READWRITE);
        CHECK(audio_effects_location[0]==(phase==3?UINT32_MAX:0x653470));
        CHECK(audio_effects_location[1]==(phase==3?UINT32_MAX:0x100));
        if(phase==2)CHECK(MEM32(context+84)==0xcccccccc);
        else {
            uint32_t descriptor=MEM32(context+84);
            CHECK(descriptor>=audio_effects_storage && descriptor+4<=audio_effects_storage+words[3]);
            CHECK(MEM32(descriptor)==3);
        }
        if(audio_effects_previous) {
            MEMORY_BASIC_INFORMATION host;
            CHECK(audio_effects_previous!=audio_effects_storage);
            CHECK(VirtualQuery(memory+audio_effects_previous,&host,sizeof(host)) && host.State==MEM_RESERVE);
        }
        audio_effects_previous=audio_effects_storage;
    } else if(phase==4) {
        /* The actual title caller releases its borrowed file image before
         * returning. Original199 below has already retired that publication. */
        MEMORY_BASIC_INFORMATION host;
        CHECK(VirtualQuery(memory+audio_effects_source,&host,sizeof(host)) && host.State==MEM_RESERVE);
        CHECK(audio_device && audio_effects && audio_effects_storage==audio_effects_previous);
        CHECK(MEM32(MEM32(context+84))==3);
    } else {
        CHECK(!audio_device && !audio_effects && !audio_effects_storage);
        MEMORY_BASIC_INFORMATION host;
        CHECK(audio_effects_previous && VirtualQuery(memory+audio_effects_previous,&host,sizeof(host)) && host.State==MEM_RESERVE);
        audio_effects_previous=0;
    }
    ++audio_effects_checks;ReleaseSRWLockExclusive(&audio_lock);
    error=audio_original_leave();if(error)return error;
    regs[0]=0;regs[4]+=4;return NULL;
}
#define EFFECTS_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};audio_cpu_bytes(c,b,sizeof(b));} while(0)
static void effects_cpu_check(uint32_t *cursor,unsigned phase)
{
    EFFECTS_BYTES(cursor,0xb9);audio_cpu_word(cursor,phase);
    EFFECTS_BYTES(cursor,0x89,0xfa,0xb8);audio_cpu_word(cursor,AUDIO_EFFECTS_HELPER);
    EFFECTS_BYTES(cursor,0xff,0xd0);audio_cpu_result(cursor);
}
static void test_thread_audio_effects_lle(const char *xbe,const char *image)
{
    CHECK(SetEnvironmentVariableA("XLLE_PILOT_AUDIO_OUTPUT_MUTE","1"));
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));vm_ensure();test_audio_component_sources(xbe);
    FILE *f=fopen(image,"rb");CHECK(f && !fseek(f,0,SEEK_END));long length=ftell(f);
    CHECK(length>0 && length<1024*1024);rewind(f);audio_effects_input_bytes=(uint32_t)length;
    audio_effects_input=malloc(length);CHECK(audio_effects_input && fread(audio_effects_input,1,length,f)==(size_t)length);fclose(f);
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};
    memcpy(memory+AUDIO_EFFECTS_HELPER,helper,sizeof(helper));char sha[65];test_component_hash(helper,sizeof(helper),sha);
    CHECK(!pilot_io_device_thread_component_bind_abi(AUDIO_EFFECTS_HELPER,sizeof(helper),sha,0,test_audio_effects_body));
    uint32_t cursor=AUDIO_EFFECTS_CPU;
    EFFECTS_BYTES(&cursor,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8);
    const uint32_t device[]={0,EFFECTS_OUTPUT(0),0};audio_cpu_call(&cursor,PILOT_AUDIO_DirectSoundCreate,device);
    for(unsigned phase=0;phase<4;++phase) {
        EFFECTS_BYTES(&cursor,0xc7,0x47,84);audio_cpu_word(&cursor,0xcccccccc);
        const uint32_t args[]={EFFECTS_FIELD(0),EFFECTS_FIELD(88),(uint32_t)length,
            phase==3?0:AUDIO_EFFECTS_CONTEXT+96,phase==2?0:AUDIO_EFFECTS_CONTEXT+84};
        audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSound_DownloadEffectsImage,args);effects_cpu_check(&cursor,phase);
    }
    audio_cpu_push(&cursor,MEM_RELEASE);audio_cpu_push(&cursor,EFFECTS_OUTPUT(108));audio_cpu_push(&cursor,EFFECTS_OUTPUT(88));
    EFFECTS_BYTES(&cursor,0xb8);audio_cpu_word(&cursor,0xfe000000u+199*4);
    EFFECTS_BYTES(&cursor,0xff,0xd0);audio_cpu_result(&cursor);
    /* The actual Nt routine may change volatile condition flags. Restore the
     * authored caller flags before measuring the remaining whole audio calls. */
    EFFECTS_BYTES(&cursor,0xff,0x77,8,0x9d);effects_cpu_check(&cursor,4);
    const uint32_t release[]={EFFECTS_FIELD(0)};audio_cpu_call(&cursor,PILOT_AUDIO_IDirectSound_Release,release);effects_cpu_check(&cursor,5);
    EFFECTS_BYTES(&cursor,0x9c,0x5a,0x89,0x57,12,0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x5f,112,0x89,0x6f,116,0x89,0x77,120,0xc7,0x47,76,1,0,0,0,
        0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(cursor<AUDIO_EFFECTS_HELPER && audio_component_results<80);
    CHECK(!pilot_io_device_resident(0x10000,0x10000));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t threads=MEM32(stats+4),handles=MEM32(stats+8),baseline[9],after[9];test_memory_snapshot(baseline);
    unsigned pool=audio_component_pool_count();
    for(unsigned life=0;life<3;++life) {
        uint32_t bytes=(uint32_t)length;audio_effects_source=0;
        CHECK(!vm_allocate(&audio_effects_source,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        memcpy(memory+audio_effects_source,audio_effects_input,length);
        uint32_t context=AUDIO_EFFECTS_CONTEXT;memset(memory+context,0,512);MEM32(context+64)=AUDIO_EFFECTS_CPU;
        MEM32(context+88)=audio_effects_source;
        MEM32(context+96)=0x653470;MEM32(context+100)=0x100;MEM32(context+124)=0xa5a55a5a;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7+life);
        double fp=12345.6789+life;memcpy(memory+context+48,&fp,8);
        for(unsigned i=0;i<audio_component_results;++i)MEM32(context+128+i*4)=0xdeadbeef;
        thread_cpu_create_join(AUDIO_EFFECTS_CPU,context,life&1);
        CHECK(MEM32(context+76)==1 && MEM32(context+124)==0xa5a55a5a);
        CHECK(MEM32(context+96)==0x653470 && MEM32(context+100)==0x100);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(MEM32(context+112)==0x11223344 && MEM32(context+116)==0x44556677 && MEM32(context+120)==0x22334455);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        for(unsigned i=0;i<audio_component_results;++i)CHECK(!MEM32(context+128+i*4));
        CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==threads && MEM32(stats+8)==handles);
        test_memory_snapshot(after);CHECK(!memcmp(baseline,after,sizeof(baseline)) && audio_component_pool_count()==pool);
        CHECK(MEM32(audio_original_gate+16)==UINT32_MAX && !MEM32(audio_original_gate+20) && !MEM32(audio_original_gate+24));
        fprintf(stderr,"[ORIGINAL AUDIO EFFECTS] life=%u installs=4 original_descriptor_count=3 nullable_location/output=1 source_preserved=1 exact_memory/thread/handle=1 muted=1\n",life);
    }
    CHECK(audio_effects_checks==18 && !audio_effects_previous);
    free(audio_effects_input);audio_effects_input=NULL;
    uint32_t gate=audio_original_gate;audio_original_gate=native_component_original_gate=0;pool_free(gate);
    fprintf(stderr,"PASS: twelve real effects installs/replacements on three actual original CPU lifetimes, copied input and descriptor ownership, nullable locations/output, original VM release after engine retirement, exact GPR/ESP/flags/x87/XMM and all nine memory/thread/handle baselines; no Play\n");
}
#undef EFFECTS_BYTES
#undef EFFECTS_FIELD
#undef EFFECTS_OUTPUT
