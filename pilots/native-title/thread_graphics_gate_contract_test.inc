/* Compose the production original graphics gate with two real original CPU
 * owners and native renderer/VM locks. The contender must block in the real
 * kernel before acquiring any host lock while the holder performs VM calls. */
#define GRAPHICS_GATE_OWNER 0x13d00u
#define GRAPHICS_GATE_WAITER 0x13e00u
#define GRAPHICS_GATE_HELPER 0x13f80u
static unsigned graphics_gate_calls[2];
static const char *test_graphics_gate_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==GRAPHICS_GATE_HELPER && regs[1]<=1);
    unsigned waiter=regs[1];uint32_t context=regs[2],shared=0x1b200;
    CHECK(context==(waiter?0x1b100u:0x1b000u));++graphics_gate_calls[waiter];
    if(waiter)CHECK(MEM32(shared)==1);
    const char *error=graphics_original_enter();if(error)return error;
    CHECK(MEM32(shared)==(waiter?2u:0u));
    AcquireSRWLockExclusive(&graphics_lock);
    uint32_t base=0,bytes=8192;
    CHECK(!vm_allocate(&base,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(base && bytes==8192);MEM32(base)=0x13572468u+waiter;
    if(!waiter) {
        MEM32(shared)=1;uint32_t result=0,args[]={MEM32(context+88),0};
        CHECK(!pilot_io_device_thread_component_call(225,args,&result) && !result);
        uint32_t wait[]={MEM32(context+92),0,0};
        CHECK(!pilot_io_device_thread_component_call(233,wait,&result) && !result);
        CHECK(MEM32(graphics_original_gate+16)==1 && MEM32(graphics_original_gate+20)==1);
        CHECK(MEM32(graphics_original_gate+24)==MEM32(context));
    }
    uint32_t span=4096,old=0,words[7];
    CHECK(!vm_protect(&base,&span,PAGE_READONLY,&old) && old==PAGE_READWRITE);
    CHECK(!vm_query(base,words) && words[4]==MEM_COMMIT && words[5]==PAGE_READONLY);
    CHECK(MEM32(base)==0x13572468u+waiter && test_write_faults(base));
    CHECK(!vm_protect(&base,&span,PAGE_READWRITE,&old) && old==PAGE_READONLY);
    span=0;CHECK(!vm_free(&base,&span,MEM_RELEASE) && span==8192);
    MEMORY_BASIC_INFORMATION host;
    CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_RESERVE);
    MEM32(shared)=waiter?3:2;
    /* Releasing original ownership first would wake the higher-priority
     * contender into a host-lock/controller deadlock. */
    ReleaseSRWLockExclusive(&graphics_lock);
    error=graphics_original_leave();if(error)return error;
    regs[0]=0x76543210;regs[4]+=4;return NULL;
}
static void test_graphics_gate_cpu_code(uint32_t entry,int waiter)
{
    struct ThreadChildCode code={0},*c=&code;
#define GATE_BYTES(...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
    GATE_BYTES(0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68);
    if(waiter) {
        /* Set the executing original thread's priority before waking the
         * holder, even if creation scheduled this worker before host setup. */
        child_push(c,30);GATE_BYTES(0x64,0xa1,0x28,0,0,0,0x50);child_call(c,148);
        GATE_BYTES(0x89,0x47,84);child_push(c,0);child_push_field(c,92);child_call(c,225);
    }
    GATE_BYTES(0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8,
        0xb9);child_word(c,(uint32_t)waiter);GATE_BYTES(0x89,0xfa,0xb8);child_word(c,GRAPHICS_GATE_HELPER);
    GATE_BYTES(0xff,0xd0,0x89,0x47,4,0x9c,0x5a,0x89,0x57,12,0x89,0x67,72,
        0x0f,0x11,0x47,32,0xdd,0x5f,56,0xc7,0x47,76,1,0,0,0,
        0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
#undef GATE_BYTES
    CHECK(c->size<0x100);memcpy(memory+entry,c->bytes,c->size);
}
static void test_thread_graphics_gate_lle(const char *path)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));vm_ensure();
    test_graphics_component_sources(path);
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};
    memcpy(memory+GRAPHICS_GATE_HELPER,helper,sizeof(helper));char sha[65];test_component_hash(helper,sizeof(helper),sha);
    CHECK(!pilot_io_device_thread_component_bind_abi(GRAPHICS_GATE_HELPER,sizeof(helper),sha,0,test_graphics_gate_body));
    test_graphics_gate_cpu_code(GRAPHICS_GATE_OWNER,0);test_graphics_gate_cpu_code(GRAPHICS_GATE_WAITER,1);
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    uint32_t settled_handles=0;
    for(unsigned life=0;life<3;++life) {
        uint32_t contexts[]={0x1b000,0x1b100},handles[2],objects[2],events[2],out=0x19800;
        memset(memory+contexts[0],0,0x300);
        for(unsigned i=0;i<2;++i) {
            uint32_t create[]={out,0,1,0};CHECK(!event_lle_call(189,create));events[i]=MEM32(out);
        }
        for(unsigned i=0;i<2;++i) {
            uint32_t context=contexts[i],entry=i?GRAPHICS_GATE_WAITER:GRAPHICS_GATE_OWNER;
            MEM32(context+64)=entry;MEM32(context+88)=events[0];MEM32(context+92)=events[1];
            for(unsigned b=0;b<16;++b)MEM8(context+16+b)=(uint8_t)(0x35+b*7+i);
            double fp=12345.6789+i;memcpy(memory+context+48,&fp,8);
            uint32_t create[]={out,0,entry,context,0};CHECK(!event_lle_call(254,create));handles[i]=MEM32(out);
            uint32_t ref[]={handles[i],0,out+4};CHECK(!event_lle_call(246,ref));objects[i]=MEM32(out+4);MEM32(context)=objects[i];
            if(!i) {
                uint32_t priority[]={objects[i],29};CHECK(event_lle_call(148,priority)==8);
                uint32_t wait[]={events[0],0,0};CHECK(!event_lle_call(233,wait));
                CHECK(MEM32(0x1b200)==1 && MEM32(graphics_original_gate+24)==objects[0]);
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
        CHECK(MEM32(0x1b100+84)==8 && MEM32(0x1b200)==3);
        CHECK(MEM32(graphics_original_gate+16)==UINT32_MAX && !MEM32(graphics_original_gate+20) && !MEM32(graphics_original_gate+24));
        CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads);
        /* The prior original-CPU trace identifies the captured file worker's
         * own NtClose of pre-existing handle0x10 during the first blocked
         * interval. Every authored handle is independently invalid above.
         * Allow that one known retirement, then require exact restoration. */
        if(!life) {
            CHECK(MEM32(stats+8)==baseline_handles || MEM32(stats+8)+1==baseline_handles);
            settled_handles=MEM32(stats+8);
        } else CHECK(MEM32(stats+8)==settled_handles);
    }
    /* Dedicated original scheduling uses the production raw-CPU worker, not
     * the mixed fixture's generated test_thread_cpu_body continuation. */
    CHECK(graphics_gate_calls[0]==3 && graphics_gate_calls[1]==3 && !thread_cpu_calls);
    uint32_t gate=graphics_original_gate;graphics_original_gate=0;pool_free(gate);
    fprintf(stderr,"PASS: three actual original graphics-gate contention lifetimes; six CPU owners, native graphics/VM locks across original services, real block/wake ownership, protection/release, flags/FP/ESP and handle cleanup\n");
}
