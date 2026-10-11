/* Real original CPU owners contend across native audio/graphics before
 * either host lock or their shared pool/VM lock. Original critical-section
 * wait/wake owns ordering; this fixture never manufactures a ready result. */
#define AUDIO_GATE_OWNER 0x16400u
#define AUDIO_GATE_WAITER 0x16500u
#define AUDIO_GATE_HELPER 0x16680u
#define AUDIO_GATE_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static unsigned audio_gate_calls[2],audio_gate_pool_calls;
static unsigned audio_gate_baseline_owned;
static unsigned audio_gate_owned_count(void)
{
    unsigned count=0;for(unsigned i=0;i<4096;++i)if(pool_blocks[i].span)++count;return count;
}
static const char *test_audio_gate_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==AUDIO_GATE_HELPER && regs[1]<=1 && pilot_io_device_thread_component_active());
    unsigned waiter=regs[1];uint32_t context=regs[2],shared=0x1b200;
    CHECK(context==(waiter?0x1b100u:0x1b000u));++audio_gate_calls[waiter];
    if(waiter)CHECK(MEM32(shared)==1);
    const char *error;
    SRWLOCK *host_lock=&audio_lock;
#ifdef PILOT_D3D_NATIVE
    if(waiter) {error=graphics_original_enter();host_lock=&graphics_lock;}
    else error=audio_original_enter();
#else
    error=audio_original_enter();
#endif
    if(error)return error;
    CHECK(MEM32(shared)==(waiter?2u:0u));
    CHECK(MEM32(audio_original_gate+20)==1 && MEM32(audio_original_gate+24)==MEM32(context));
    if(waiter) {
        /* The actual original wake must happen after the holder drops its
         * audio host lock, even when this contender uses graphics_lock. */
        CHECK(TryAcquireSRWLockExclusive(&audio_lock));ReleaseSRWLockExclusive(&audio_lock);
    }
    AcquireSRWLockExclusive(host_lock);
    uint32_t bytes=waiter?36:44,tag=0x4c6c6547u+waiter;
    uint32_t pointer=pool_allocate(bytes,tag);CHECK(pointer);
    unsigned slot=pool_find(pointer);CHECK(pool_blocks[slot].tag==tag && pool_blocks[slot].usable>=bytes);
    CHECK(audio_gate_owned_count()==audio_gate_baseline_owned+1);
    MEM32(pointer)=0x13572468u+waiter;MEM32(pointer+bytes-4)=0x24681357u+waiter;
    if(!waiter) {
        MEM32(shared)=1;uint32_t result=0,args[]={MEM32(context+88),0};
        CHECK(!pilot_io_device_thread_component_call(225,args,&result) && !result);
        uint32_t wait[]={MEM32(context+92),0,0};
        CHECK(!pilot_io_device_thread_component_call(233,wait,&result) && !result);
        CHECK(MEM32(audio_original_gate+16)==1 && MEM32(audio_original_gate+20)==1);
        CHECK(MEM32(audio_original_gate+24)==MEM32(context));
    }
    uint32_t usable=0;CHECK(!pool_original_call(23,&pointer,&usable));
    CHECK(usable==pool_blocks[slot].usable && MEM32(pointer)==0x13572468u+waiter && MEM32(pointer+bytes-4)==0x24681357u+waiter);
    CHECK(!pilot_io_device_release_check(pointer,usable));pool_free(pointer);++audio_gate_pool_calls;
    CHECK(!test_pool_owned(pointer) && audio_gate_owned_count()==audio_gate_baseline_owned);
    MEM32(shared)=waiter?3:2;
    ReleaseSRWLockExclusive(host_lock);
#ifdef PILOT_D3D_NATIVE
    error=waiter?graphics_original_leave():audio_original_leave();
#else
    error=audio_original_leave();
#endif
    if(error)return error;
    regs[0]=0x76543210;regs[4]+=4;return NULL;
}
static void test_audio_gate_cpu_code(uint32_t entry,int waiter)
{
    struct ThreadChildCode code={0},*c=&code;
    AUDIO_GATE_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68);
    if(waiter) {
        /* Priority is set by the executing worker before signalling the
         * holder; creation may schedule it before host reference setup. */
        child_push(c,30);AUDIO_GATE_BYTES(c,0x64,0xa1,0x28,0,0,0,0x50);child_call(c,148);
        AUDIO_GATE_BYTES(c,0x89,0x47,84);child_push(c,0);child_push_field(c,92);child_call(c,225);
    }
    AUDIO_GATE_BYTES(c,0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8,
        0xb9);child_word(c,(uint32_t)waiter);AUDIO_GATE_BYTES(c,0x89,0xfa,0xb8);child_word(c,AUDIO_GATE_HELPER);
    AUDIO_GATE_BYTES(c,0xff,0xd0,0x89,0x47,4,0x9c,0x5a,0x89,0x57,12,0x89,0x67,72,
        0x0f,0x11,0x47,32,0xdd,0x5f,56,0xc7,0x47,76,1,0,0,0,
        0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<0x100);memcpy(memory+entry,c->bytes,c->size);
}
static void test_thread_audio_gate_lle(const char *path)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));vm_ensure();
    test_audio_component_sources(path);
#ifdef PILOT_D3D_NATIVE
    test_graphics_component_sources(path);CHECK(graphics_original_gate==audio_original_gate);
#endif
    CHECK(audio_original_gate && native_component_original_gate==audio_original_gate);
    audio_gate_baseline_owned=audio_gate_owned_count();
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};
    memcpy(memory+AUDIO_GATE_HELPER,helper,sizeof(helper));char sha[65];test_component_hash(helper,sizeof(helper),sha);
    CHECK(!pilot_io_device_thread_component_bind_abi(AUDIO_GATE_HELPER,sizeof(helper),sha,0,test_audio_gate_body));
    test_audio_gate_cpu_code(AUDIO_GATE_OWNER,0);test_audio_gate_cpu_code(AUDIO_GATE_WAITER,1);
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8),settled_handles=0;
    for(unsigned life=0;life<3;++life) {
        uint32_t contexts[]={0x1b000,0x1b100},handles[2],objects[2],events[2],out=0x19800;
        memset(memory+contexts[0],0,0x300);
        for(unsigned i=0;i<2;++i) {
            uint32_t create[]={out,0,1,0};CHECK(!event_lle_call(189,create));events[i]=MEM32(out);
        }
        for(unsigned i=0;i<2;++i) {
            uint32_t context=contexts[i],entry=i?AUDIO_GATE_WAITER:AUDIO_GATE_OWNER;
            MEM32(context+64)=entry;MEM32(context+88)=events[0];MEM32(context+92)=events[1];
            for(unsigned b=0;b<16;++b)MEM8(context+16+b)=(uint8_t)(0x35+b*7+i);
            double fp=12345.6789+i;memcpy(memory+context+48,&fp,8);
            uint32_t create[]={out,0,entry,context,0};CHECK(!event_lle_call(254,create));handles[i]=MEM32(out);
            uint32_t ref[]={handles[i],0,out+4};CHECK(!event_lle_call(246,ref));objects[i]=MEM32(out+4);MEM32(context)=objects[i];
            if(!i) {
                uint32_t priority[]={objects[i],29};CHECK(event_lle_call(148,priority)==8);
                uint32_t wait[]={events[0],0,0};CHECK(!event_lle_call(233,wait));
                CHECK(MEM32(0x1b200)==1 && MEM32(audio_original_gate+24)==objects[0]);
            }
        }
        for(unsigned i=0;i<2;++i) {
            uint32_t join[]={objects[i],0,0,0,0};CHECK(!event_lle_call(159,join));
            CHECK(!event_lle_call(187,handles+i));event_lle_call(250,objects+i);CHECK(!event_lle_call(187,events+i));
            CHECK(event_lle_call(187,handles+i)==0xc0000008u && event_lle_call(187,events+i)==0xc0000008u);
            uint32_t context=contexts[i];CHECK(MEM32(context+4)==0x76543210 && MEM32(context+76)==1);
            CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
            CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        }
        CHECK(MEM32(0x1b100+84)==8 && MEM32(0x1b200)==3 && audio_gate_owned_count()==audio_gate_baseline_owned);
        CHECK(MEM32(audio_original_gate+16)==UINT32_MAX && !MEM32(audio_original_gate+20) && !MEM32(audio_original_gate+24));
        CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==baseline_threads);
        /* Reuse the qualified captured-worker handle0x10 retirement rule;
         * every authored event/thread handle is independently invalid above. */
        if(!life) {
            CHECK(MEM32(stats+8)==baseline_handles || MEM32(stats+8)+1==baseline_handles);settled_handles=MEM32(stats+8);
        } else CHECK(MEM32(stats+8)==settled_handles);
    }
    CHECK(audio_gate_calls[0]==3 && audio_gate_calls[1]==3 && audio_gate_pool_calls==6 && !thread_cpu_calls);
    uint32_t gate=native_component_original_gate;
    native_component_original_gate=audio_original_gate=0;
#ifdef PILOT_D3D_NATIVE
    graphics_original_gate=0;
#endif
    pool_free(gate);CHECK(audio_gate_owned_count()+1==audio_gate_baseline_owned);
    fprintf(stderr,"PASS: three actual original shared audio/graphics-gate contention lifetimes; six CPU owners, host-lock release before wake, actual pool allocate/query/free under native locks, real original blocking/ownership transfer, flags/FP/ESP and ledger/thread/handle cleanup\n");
}
#undef AUDIO_GATE_BYTES
