/* Actual original CPU callers consume native pool allocations on their own
 * original thread. Allocation/query/free share the maintained ledger and RAM
 * publication hooks; the observer does not edit original allocator lists. */
#define THREAD_COMPONENT_POOL_CPU 0x13f00u
#define THREAD_COMPONENT_POOL_HELPER 0x14f80u
#define COMPONENT_POOL_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static unsigned component_pool_calls,component_pool_pin_guards,component_pool_reuses;
static int component_pool_pinned_stop;
static void test_memory_snapshot(uint32_t out[9]);
static unsigned component_pool_owned_count(void)
{
    unsigned count=0;for(unsigned i=0;i<4096;++i)if(pool_blocks[i].span)++count;return count;
}
static uint32_t component_pool_service(unsigned ordinal,const uint32_t *args)
{
    uint32_t result=0xbad0cafe;
    CHECK(!pilot_io_device_thread_component_call(ordinal,args,&result));return result;
}
static void component_pool_cpu_bytes(uint32_t pointer,uint32_t bytes,uint32_t first,uint32_t last)
{
    CHECK(bytes>=8 && MEM32(pointer)==first && MEM32(pointer+bytes-4)==last);
}
static const char *test_component_pool_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==THREAD_COMPONENT_POOL_HELPER && pilot_io_device_thread_component_active());
    uint32_t phase=regs[1],context=regs[2],argument=MEM32(regs[4]+4);
    CHECK(context==0x1b000 && phase<=3);
    CHECK(regs[3]==0x11223344 && regs[5]==0x44556677 && regs[6]==0x22334455 && regs[7]==context);
    ++component_pool_calls;
    uint32_t saved_sp=g_esp,saved_ax=g_eax;g_esp=regs[4];
    uint32_t bytes=MEM32(context+100),tag=MEM32(context+104);
    if(!phase) {
        CHECK(argument==bytes && MEM32(regs[4]+8)==tag);
        CHECK(!pool_allocate(POOL_BYTES+1,tag));
        pool_kernel(15);uint32_t pointer=g_eax;CHECK(pointer);
        unsigned slot=pool_find(pointer);CHECK(pool_blocks[slot].usable>=bytes && pool_blocks[slot].tag==tag);
        regs[0]=pointer;
    } else if(phase==1) {
        CHECK(argument==MEM32(context));
        component_pool_cpu_bytes(argument,bytes,0x13572468,0x24681357);
        pool_kernel(23);regs[0]=g_eax;
        unsigned slot=pool_find(argument);CHECK(regs[0]==pool_blocks[slot].usable && regs[0]>=bytes);
        uint8_t ledger[sizeof(pool_blocks[slot])];memcpy(ledger,&pool_blocks[slot],sizeof(ledger));
        uint32_t pin[]={argument,16,0};(void)component_pool_service(175,pin);
        uint32_t pa=component_pool_service(173,&argument),value=0;CHECK(pa);
        CHECK(pilot_io_device_pinned_dma(pa,&value,4,0) && value==0x13572468);
        const char *error=pilot_io_device_release_check(argument,pool_blocks[slot].usable);
        CHECK(error && !strcmp(error,"device LLE allocation release while DMA pinned"));
        CHECK(!memcmp(ledger,&pool_blocks[slot],sizeof(ledger)));
        component_pool_cpu_bytes(argument,bytes,0x13572468,0x24681357);
        ++component_pool_pin_guards;
        if(component_pool_pinned_stop) {
            fprintf(stderr,"[COMPONENT POOL PIN GUARD] pointer=%08x requested=%u usable=%u ledger=owned bytes=13572468/24681357\n",
                argument,bytes,pool_blocks[slot].usable);
            /* Exercise the production release guard, before original17 or
             * the ledger retirement. This terminal case retains the pin. */
            pool_free(argument);CHECK(0);
        }
        pin[2]=1;(void)component_pool_service(175,pin);
        CHECK(!pilot_io_device_release_check(argument,pool_blocks[slot].usable));
        value=0xabcdef12;CHECK(!pilot_io_device_pinned_dma(pa,&value,4,0) && value==0xabcdef12);
    } else if(phase==2) {
        CHECK(argument==MEM32(context));
        component_pool_cpu_bytes(argument,bytes,0x13572468,0x24681357);
        unsigned slot=pool_find(argument);pool_kernel(17);
        CHECK(!test_pool_owned(argument) && !pool_blocks[slot].span);
        uint32_t again=pool_allocate(bytes,tag);CHECK(again && pool_find(again)==slot);
        if(again==argument)++component_pool_reuses;
        regs[0]=again;
    } else {
        CHECK(argument==MEM32(context+80));
        component_pool_cpu_bytes(argument,bytes,0x89abcdef,0xfedcba98);
        pool_kernel(23);CHECK(g_eax==pool_blocks[pool_find(argument)].usable && g_eax>=bytes);
        g_esp=regs[4];pool_kernel(17);CHECK(!test_pool_owned(argument));
        regs[0]=0x76543210;
    }
    g_esp=saved_sp;g_eax=saved_ax;regs[4]+=12;return NULL;
}
static void component_pool_call(struct ThreadChildCode *code,unsigned phase)
{
    COMPONENT_POOL_BYTES(code,0xb9);child_word(code,phase);
    COMPONENT_POOL_BYTES(code,0x89,0xfa,0xb8);child_word(code,THREAD_COMPONENT_POOL_HELPER);
    COMPONENT_POOL_BYTES(code,0xff,0xd0);
}
static void test_thread_component_pool_lle(int pinned_stop)
{
    component_pool_pinned_stop=pinned_stop;
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    CHECK(!pilot_io_device_thread_component_active());
    /* Ordinary noncomponent callers retain the existing direct route. */
    unsigned baseline_owned=component_pool_owned_count();
    uint32_t direct=pool_allocate(72,0x74736554);CHECK(direct && test_pool_owned(direct));
    MEM32(direct)=0x10203040;pool_free(direct);
    CHECK(!test_pool_owned(direct) && component_pool_owned_count()==baseline_owned);
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc2,8,0};
    memcpy(memory+THREAD_COMPONENT_POOL_HELPER,helper,sizeof(helper));
    char identity[65];test_component_hash(helper,sizeof(helper),identity);
    CHECK(!pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_POOL_HELPER,sizeof(helper),identity,8,test_component_pool_body));
    struct ThreadChildCode code={0},*c=&code;
    COMPONENT_POOL_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8);
    child_push_field(c,104);child_push_field(c,100);component_pool_call(c,0);
    COMPONENT_POOL_BYTES(c,0x89,0x07,0xc7,0x00,0x68,0x24,0x57,0x13,
        0x8b,0x57,100,0xc7,0x44,0x10,0xfc,0x57,0x13,0x68,0x24);
    child_push(c,0);child_push_field(c,0);component_pool_call(c,1);COMPONENT_POOL_BYTES(c,0x89,0x47,4);
    child_push(c,0);child_push_field(c,0);component_pool_call(c,2);
    COMPONENT_POOL_BYTES(c,0x89,0x47,80,0xc7,0x00,0xef,0xcd,0xab,0x89,
        0x8b,0x57,100,0xc7,0x44,0x10,0xfc,0x98,0xba,0xdc,0xfe);
    child_push(c,0);child_push_field(c,80);component_pool_call(c,3);
    COMPONENT_POOL_BYTES(c,0x89,0x47,84,0x9c,0x5a,0x89,0x57,12,0x89,0x67,72,
        0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x5f,108,0x89,0x6f,112,0x89,0x77,116,
        0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<THREAD_COMPONENT_POOL_HELPER-THREAD_COMPONENT_POOL_CPU);
    memcpy(memory+THREAD_COMPONENT_POOL_CPU,c->bytes,c->size);
#ifdef PILOT_MEMORY_LLE
    /* Publish the actual authored image before counters so its one-time
     * original PFNs do not get mistaken for a leaked pool/thread owner. */
    CHECK(!pilot_io_device_resident(0x10000,0x10000));
#endif
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    uint32_t baseline_memory[9],after_memory[9];test_memory_snapshot(baseline_memory);
    for(unsigned life=0;life<(pinned_stop?1u:3u);++life) {
        uint32_t context=0x1b000;memset(memory+context,0,128);MEM32(context+64)=THREAD_COMPONENT_POOL_CPU;
        MEM32(context+96)=0xa5a55a5a;MEM32(context+100)=life==1?4097:72;MEM32(context+104)=0x6c6f6f50u+life;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7);
        double expected=12345.6789;memcpy(memory+context+48,&expected,8);
        thread_cpu_create_join(THREAD_COMPONENT_POOL_CPU,context,life&1);
        CHECK(!pinned_stop && MEM32(context+84)==0x76543210 && MEM32(context+76)==1 && MEM32(context+96)==0xa5a55a5a);
        CHECK(MEM32(context) && MEM32(context+80) && MEM32(context+4)>=MEM32(context+100));
        CHECK(!test_pool_owned(MEM32(context)) && !test_pool_owned(MEM32(context+80)) && component_pool_owned_count()==baseline_owned);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(MEM32(context+108)==0x11223344 && MEM32(context+112)==0x44556677 && MEM32(context+116)==0x22334455);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(!memcmp(memory+THREAD_COMPONENT_POOL_HELPER,helper,sizeof(helper)));
        CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
        test_memory_snapshot(after_memory);CHECK(!memcmp(baseline_memory,after_memory,sizeof(baseline_memory)));
    }
    CHECK(component_pool_calls==12 && component_pool_pin_guards==3 && thread_cpu_calls==3);
    fprintf(stderr,"PASS: three selected-component original CPU pool lifetimes, small/page-crossing allocate/query/consume/free/reuse, three pin-release guards, ledger+original memory/thread/handle cleanup, ESP/GPR/flags/x87/XMM; observed address reuse=%u\n",component_pool_reuses);
}
#undef COMPONENT_POOL_BYTES
