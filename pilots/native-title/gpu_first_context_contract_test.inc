/* Compiled observation controls over authored bytes on the real original
 * scheduler/CPU, plus the actual maintained Nv2a DLL. No original startup. */
#ifdef PILOT_GPU_FILES_WORKER
#define OBSERVER_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static unsigned observer_entries,observer_returns,observer_mmio;
static unsigned observer_expect_enabled;
static uint32_t observer_memory_address;
static const char *observer_memory_forbidden(uint32_t address,unsigned size,int write,uint64_t *value)
{(void)address;(void)size;(void)write;(void)value;CHECK(0);return "diagnostic invoked bus memory";}
static uint32_t observer_caller,observer_mmio_pc,observer_entry_sp,observer_thread;
static const unsigned char observer_helper[]={0x8b,0x41,4,0x83,0xc0,0x11,0x89,0x41,8,0xc3};
static void observer_receiver(const PilotIoDiagnosticEvent *event)
{
    CHECK(event->entry==GPU_THREAD_CPU+0x80 && event->driver==0x1b000 && event->regs[1]==0x1b000);
    CHECK(event->thread && !event->in_kernel);
    unsigned char source[sizeof(observer_helper)],canary[8];memset(canary,0xa5,sizeof(canary));
    CHECK(pilot_io_device_diagnostic_read(event->entry,source,sizeof(source),0));
    CHECK(!memcmp(source,observer_helper,sizeof(source)));
    CHECK(!pilot_io_device_diagnostic_read(0x60000000u,canary,8,0));
    CHECK(!pilot_io_device_diagnostic_read(64u*1024*1024-3,canary,4,1));
    CHECK(!pilot_io_device_diagnostic_read(0x13ffe,canary,8,0));
    CHECK(!pilot_io_device_diagnostic_read(0xfd400100u,canary,4,0));
    for(unsigned i=0;i<sizeof(canary);++i)CHECK(canary[i]==0xa5);
    PilotIoDiagnosticEvent context;memset(&context,0xa5,sizeof(context));
    CHECK(!pilot_io_device_diagnostic_context(&context)); // Entry callback is not raw MMIO.
    uint32_t result=0xdeadbeef,args[]={1};CHECK(pilot_io_device_lle(1,memory,args,&result));
    CHECK(result==0xdeadbeef && pilot_io_device_diagnostic_bind(0,0,NULL,NULL));
    uint32_t thread=0;CHECK(pilot_io_device_diagnostic_read(0x8003da64u,&thread,4,0) && thread==event->thread);
    if(event->kind==PILOT_IO_DIAGNOSTIC_ENTRY) {
        ++observer_entries;observer_entry_sp=event->regs[4];observer_thread=event->thread;
        CHECK(event->pc==GPU_THREAD_CPU+0x80 && event->caller==observer_caller);
    } else {
        CHECK(event->kind==PILOT_IO_DIAGNOSTIC_RETURN);++observer_returns;
        CHECK(event->pc==observer_caller && event->regs[4]==observer_entry_sp+4 &&
              event->thread==observer_thread && event->regs[0]==0x12345689);
    }
}
static const char *observer_checked_mmio(uint32_t address,unsigned size,int write,uint32_t *value)
{
    PilotIoDiagnosticEvent event;CHECK(pilot_io_device_diagnostic_context(&event));
    CHECK(event.kind==PILOT_IO_DIAGNOSTIC_MMIO && event.pc==observer_mmio_pc &&
          event.regs[1]==0x1b000 && event.thread && !event.in_kernel);
    CHECK(address==0xfd001804 && size==4 && !write);++observer_mmio;
    unsigned char copy[4]={0xa5,0xa5,0xa5,0xa5};
    if(observer_expect_enabled) {
        CHECK(!pilot_io_device_diagnostic_read(observer_memory_address,copy,sizeof(copy),0));
        CHECK(!memcmp(copy,"\xa5\xa5\xa5\xa5",4));
    } else CHECK(pilot_io_device_diagnostic_read(observer_memory_address,copy,sizeof(copy),0));
    if(observer_expect_enabled)CHECK(event.entry==GPU_THREAD_CPU+0x80 && event.driver==0x1b000);
    else CHECK(!event.entry);
    return gpu_lle_kernel_mmio(address,size,write,value);
}
static void observer_worker_code(void)
{
    struct ThreadChildCode code={0},*p=&code;
    OBSERVER_BYTES(p,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,0x8b,0xcf,0xe8);
    child_word(p,(GPU_THREAD_CPU+0x80)-(GPU_THREAD_CPU+p->size+4));
    observer_caller=GPU_THREAD_CPU+p->size;
    OBSERVER_BYTES(p,0x89,0x47,12,0xba);child_word(p,0xfd001804);
    observer_mmio_pc=GPU_THREAD_CPU+p->size;
    OBSERVER_BYTES(p,0x8b,0x02,0x89,0x47,16,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    memcpy(memory+GPU_THREAD_CPU,p->bytes,p->size);
    memcpy(memory+GPU_THREAD_CPU+0x80,observer_helper,sizeof(observer_helper));
}
static void test_gpu_first_context_observer(void)
{
    install_fault_handler();CHECK(!getenv("XLLE_PILOT_G03_CAPTURE_DIR"));
    CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));observer_worker_code();
    gpu_lle_open();CHECK(!gpu_context_observer.enabled && !gpu_lle.first_context_snapshot);
    gpu_test_write(0x1804,4); // Independently authored PCI command, not reset guess.
    CHECK(!pilot_io_device_mmio_bind(0xfd000000u,0x1000000u,NULL));
    CHECK(!pilot_io_device_mmio_bind(0xfd000000u,0x1000000u,observer_checked_mmio));
    unsigned char *pramin=calloc(1,0x100000);uint32_t pa=0;CHECK(pramin);
    /* The exact compiled address helper rejects partial DWORD limits and all
     * retail overflow, including prefetched (not just unread) words. */
    *(uint32_t*)(pramin+4)=2;CHECK(!gpu_context_packet_address(pramin,0,0,&pa));
    *(uint32_t*)(pramin+4)=3;CHECK(gpu_context_packet_address(pramin,0,0,&pa) && !pa);
    *(uint32_t*)(pramin+4)=0xffffffff;*(uint32_t*)(pramin+8)=0x03fff000;
    CHECK(gpu_context_packet_address(pramin,0,4092,&pa) && pa==0x03fffffc);
    CHECK(!gpu_context_packet_address(pramin,0,4093,&pa));
    CHECK(!gpu_context_packet_address(pramin,0xffff5,0,&pa));
    free(pramin);
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t threads=MEM32(stats+4),handles=MEM32(stats+8);
    uint32_t memory_bytes=4096;
    CHECK(!pilot_io_device_lle(165,memory,&memory_bytes,&observer_memory_address));
    CHECK(observer_memory_address && !(observer_memory_address&4095));
    for(unsigned life=0;life<2;++life) {
        uint32_t context=0x1b000;memset(memory+context,0,96);MEM32(context+4)=0x12345678;
        MEM32(context+64)=GPU_THREAD_CPU;observer_expect_enabled=life;
        if(life) {
            // The same real RAM translation read above is now a registered
            // memory bus owner; passive reads must not bypass that boundary.
            CHECK(!pilot_io_device_bus_memory_bind(observer_memory_address,4096,observer_memory_forbidden));
            CHECK(pilot_io_device_diagnostic_bind(GPU_THREAD_CPU+0x80,sizeof(observer_helper),
                "0000000000000000000000000000000000000000000000000000000000000000",observer_receiver));
            unsigned char digest[32];char hash[65];
            CHECK(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)observer_helper,sizeof(observer_helper),digest,32)>=0);
            for(unsigned i=0;i<32;++i)sprintf(hash+i*2,"%02x",digest[i]);
            CHECK(!pilot_io_device_diagnostic_bind(GPU_THREAD_CPU+0x80,sizeof(observer_helper),hash,observer_receiver));
        }
        thread_cpu_create_join(GPU_THREAD_CPU,context,life&1);
        CHECK(MEM32(context+8)==0x12345689 && MEM32(context+12)==0x12345689 && MEM32(context+16)==4);
        CHECK(observer_entries==life && observer_returns==life && observer_mmio==life+1);
        CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==threads && MEM32(stats+8)==handles);
    }
    CHECK(!pilot_io_device_diagnostic_bind(0,0,NULL,NULL));
    CHECK(!pilot_io_device_bus_memory_bind(observer_memory_address,4096,NULL));
    uint32_t unused=0;CHECK(!pilot_io_device_lle(171,memory,&observer_memory_address,&unused));
    PilotIoDiagnosticEvent event;memset(&event,0xa5,sizeof(event));
    CHECK(!pilot_io_device_diagnostic_context(&event));
    unsigned char canary[4]={0xa5,0xa5,0xa5,0xa5};CHECK(!pilot_io_device_diagnostic_read(0x10000,canary,4,0));
    CHECK(!memcmp(canary,"\xa5\xa5\xa5\xa5",4));
    CHECK(!pilot_io_device_work_unbind(gpu_lle_sample));
    CHECK(!gpu_lle.error(gpu_lle.device));gpu_lle.destroy(gpu_lle.device);gpu_lle.device=NULL;
    CHECK(!pilot_io_device_bus_memory_bind(0xf0000000u,0x04000000u,NULL));
    CHECK(!pilot_io_device_mmio_bind(0xfd000000u,0x1000000u,NULL));pilot_io_device_port_bind(0,0,NULL);
    CHECK(FreeLibrary(gpu_lle.library));gpu_lle.library=NULL;
    fprintf(stderr,"PASS: default-disabled and raw actual-entry/return/MMIO observer, exact source/frame, no reentry, ownership baselines\n");
}

static unsigned char *observer_guard_before;
static uint32_t observer_guard_before_bytes;
static void observer_guard_verify(const char *error)
{
    // This authored request deliberately omits the original mode/reset setup.
    // LOAD is implemented; the incomplete caller must still fail atomically.
    CHECK(error && strstr(error,"NV2A context load configuration is unsupported"));
    CHECK(gpu_lle.error(gpu_lle.device) && gpu_context_observer.selector_accepted &&
          gpu_context_observer.selector_observed && gpu_context_observer.load_observed &&
          !gpu_context_observer.returned && !gpu_context_observer.source_bound &&
          gpu_context_observer.source_copied && !gpu_context_observer.failed && gpu_context_observer.records==3);
    // GFC1/v1 contains queue/channel/PRAMIN state, not the newly programmed
    // pointer. None of that execution/storage state may change on rejection.
    unsigned char *after=malloc(observer_guard_before_bytes);uint32_t needed=0;
    CHECK(after && gpu_lle.first_context_snapshot(gpu_lle.device,after,observer_guard_before_bytes,&needed));
    CHECK(needed==observer_guard_before_bytes && !memcmp(after,observer_guard_before,needed));free(after);
    fprintf(stderr,"PASS: actual raw-MMIO guard preserved at load, selector accepted, paused queue/canonical bytes unchanged, changed source rejected, return absent\n");
    ExitProcess(0); // Intentional terminal original guard; no fabricated join/cleanup.
}
static void test_gpu_first_context_capture_guard(const char *directory)
{
    install_fault_handler();CHECK(!getenv("XLLE_PILOT_G03_CAPTURE_DIR"));CHECK(directory && strlen(directory)<4096);
    CHECK(GetFileAttributesA(directory)!=INVALID_FILE_ATTRIBUTES);
    CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));gpu_thread_cpu_code();gpu_lle_open();
    *(FARPROC*)&gpu_lle.first_context_snapshot=GetProcAddress(gpu_lle.library,"xlle_nv2a_reference_first_context_snapshot");
    CHECK(gpu_lle.first_context_snapshot);
    uint32_t bytes=8192,base=0;CHECK(!pilot_io_device_lle(165,memory,&bytes,&base));
    uint32_t pa=base&0x03ffffff;const uint32_t packet[]={0x40000,1,0x401a4,2,0x81d6c,4096,0x12345678};
    memcpy(memory+base,packet,sizeof(packet));MEM32(base+4096)=0xabcdef01;
    const uint32_t setup[][4]={
        {1,0x80000000,pa|1,0}, // Actual bootstrap page jumps to this owned packet.
        {1,0xfd700008,1,0},{1,0xfd70000c,0x80010020,0},{1,0xfd700200,0x97,0},
        {1,0xfd700010,2,0},{1,0xfd700014,0x80010030,0},{1,0xfd700300,0x3d,0},
        {1,0xfd700304,8191,0},{1,0xfd700308,pa,0},{1,0xfd700180,0x3d,0},
        {1,0xfd700184,0x03ffffff,0},{1,0xfd700188,0,0},{1,0xfd002504,1,0},
        {1,0xfd003204,0x100,0},{1,0xfd00322c,0x18,0},{1,0xfd003244,0,0},
        {1,0xfd003220,1,0},{1,0xfd003250,1,0},{1,0xfd003200,1,0},
        {1,0xfd400720,0,0},{1,0xfd800040,pa+sizeof(packet),0}};
    gpu_thread_cpu_ops(setup,sizeof(setup)/sizeof(setup[0]),0);
    CHECK(MEM32(base+4096)==0xabcdef01);
    /* Keep one additional actual unread word beside the four prefetched
     * parameters. Disabling PUSH0 does not complete/retire the paused queue. */
    MEM32(base+sizeof(packet))=0xfeedface;
    gpu_test_write(0x3200,0);gpu_test_write(0x800040,pa+sizeof(packet)+4);
    CHECK(!gpu_lle.first_context_snapshot(gpu_lle.device,NULL,0,&observer_guard_before_bytes));
    observer_guard_before=malloc(observer_guard_before_bytes);uint32_t needed=0;
    CHECK(observer_guard_before && gpu_lle.first_context_snapshot(gpu_lle.device,observer_guard_before,observer_guard_before_bytes,&needed));
    const unsigned char helper[]={0x8b,0x41,4,0x83,0xc0,0x11,0x89,0x41,8,0xc3};
    memcpy(memory+0x1e400,helper,sizeof(helper));
    struct ThreadChildCode code={0},*p=&code;
    OBSERVER_BYTES(p,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,0x8b,0xcf,0xe8);
    child_word(p,0x1e400-(0x1e800+p->size+4));
    OBSERVER_BYTES(p,0xb8);child_word(p,0x300);OBSERVER_BYTES(p,0xbb);child_word(p,0x1b000);
    OBSERVER_BYTES(p,0xbe);child_word(p,0xfd000000);
    OBSERVER_BYTES(p,0x89,0x86,0x84,0x07,0x40,0x00);
    OBSERVER_BYTES(p,0xc7,0x86,0x88,0x07,0x40,0x00,0x01,0,0,0);
    OBSERVER_BYTES(p,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    memcpy(memory+0x1e800,p->bytes,p->size);
    memset(&gpu_context_observer,0,sizeof(gpu_context_observer));
    strcpy_s(gpu_context_observer.directory,sizeof(gpu_context_observer.directory),directory);
    gpu_context_observer.enabled=1;
    unsigned char digest[32];char hash[65];CHECK(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)helper,sizeof(helper),digest,32)>=0);
    for(unsigned i=0;i<32;++i)sprintf(hash+i*2,"%02x",digest[i]);
    CHECK(!pilot_io_device_diagnostic_bind(0x1e400,sizeof(helper),hash,gpu_context_return_receiver));
    uint32_t context=0x1b000;memset(memory+context,0,0x164);MEM32(context+4)=0x12345678;
    MEM32(context+64)=0x1e800;
    /* Actual original wait reports the retained device failure. This is an
     * intentionally incomplete lifetime, never a successful join/cleanup. */
    uint32_t out=0x19800,create[]={out,0,0x1e800,context,0};CHECK(!event_lle_call(254,create));
    uint32_t handle=MEM32(out),ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));
    uint32_t object=MEM32(out+4),priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
    uint32_t join[]={object,0,0,0,0},result=0xa5a5a5a5;
    observer_guard_verify(pilot_io_device_lle(159,memory,join,&result));CHECK(0);
}
static unsigned gpu_compiled_observations;
static const char *gpu_compiled_checked_mmio(uint32_t address,unsigned size,int write,uint32_t *value)
{
    PilotIoDiagnosticEvent event;
    CHECK(pilot_io_device_compiled_mmio_active() && pilot_io_device_diagnostic_context(&event));
    CHECK(event.kind==PILOT_IO_DIAGNOSTIC_COMPILED_MMIO && event.pc && event.thread);
    ++gpu_compiled_observations;
    return gpu_lle_kernel_mmio(address,size,write,value);
}
static void gpu_compiled_setup(void)
{
    CHECK(!pilot_io_device_compiled_mmio_active());gpu_lle_open();
    CHECK(!pilot_io_device_mmio_bind(0xfd000000u,0x1000000u,NULL));
    CHECK(!pilot_io_device_mmio_bind(0xfd000000u,0x1000000u,gpu_compiled_checked_mmio));
}
static void gpu_compiled_check(void)
{
    CHECK(pilot_io_device_compiled_mmio_active());
    uint64_t before=0,after=0,next=0;CHECK(!pilot_usb_pit_clock(&before,&next));
    unsigned observations=gpu_compiled_observations;
    gpu_test_write(0x400750,0x3d0000);
    for(unsigned i=0;i<15;++i)gpu_test_write(0x400754,0);
    CHECK(gpu_test_read(0x400750)==0x3d003c);
    gpu_test_write(0x400750,0x3d0000);
    for(unsigned i=0;i<15;++i)CHECK(gpu_test_read(0x400754)==0);
    gpu_test_write(0x400784,0x111d);CHECK(gpu_test_read(0x400784)==0x111d);
    CHECK(gpu_compiled_observations==observations+35);
    CHECK(!pilot_usb_pit_clock(&after,&next) && after==before);
}
static void test_gpu_compiled_context_prepare(void)
{
    timer_lle_device_setup=gpu_compiled_setup;timer_lle_device_check=gpu_compiled_check;
    test_timer_dpc_lle();
    timer_lle_device_setup=timer_lle_device_check=NULL;
    CHECK(gpu_compiled_observations && !pilot_io_device_compiled_mmio_active());
    CHECK(!gpu_lle.error(gpu_lle.device));
    CHECK(!pilot_io_device_work_unbind(gpu_lle_sample));
    gpu_lle.destroy(gpu_lle.device);gpu_lle.device=NULL;
    CHECK(!pilot_io_device_bus_memory_bind(0xf0000000u,0x04000000u,NULL));
    CHECK(!pilot_io_device_mmio_bind(0xfd000000u,0x1000000u,NULL));pilot_io_device_port_bind(0,0,NULL);
    CHECK(FreeLibrary(gpu_lle.library));gpu_lle.library=NULL;
    fprintf(stderr,"PASS: original DPC -> compiled GPU MOV -> scoped bus, distinct diagnostic context, FD clear/read and unchanged PIT\n");
}
#undef OBSERVER_BYTES
#endif
