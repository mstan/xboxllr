/* Private pilot loader policy over real Ps-created thread/stack/TLS state.
 * This adapter executes the original XBE entry and real termination;
 * it is not a retail XBE-loader or completed scheduling-family claim. */
static void scheduling_lle_execute(uint32_t entry)
{
    uint32_t regs[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi};
    const char *error=pilot_io_device_thread_execute(entry,regs);
    if(error)stop(error,entry);
    g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_ebx=regs[3];
    g_esp=regs[4];g_ebp=regs[5];g_esi=regs[6];g_edi=regs[7];
    int terminated=0;uint32_t status=0;
    error=pilot_io_device_thread_completion(&terminated,&status);if(error)stop(error,entry);
    if(terminated){original_thread_exit.status=status;longjmp(original_thread_exit.target,1);}
}
static void scheduling_lle_bootstrap(void)
{
    uint32_t entry=MEM32(g_esp+4),directory=MEM32(g_esp+8);
    uint32_t thread=MEM32(g_fs_base+0x28),tls=MEM32(thread+0x28);
    if(directory) {
        uint32_t first=MEM32(directory),end=MEM32(directory+4),zero=MEM32(directory+16);
        uint32_t bytes=(((end-first)+zero+15)&~15u)+4;
        if(!tls || tls<MEM32(thread+0x20) || (uint64_t)tls+bytes>MEM32(thread+0x1c))
            stop("original bootstrap TLS outside original stack",tls);
        memset(memory+tls,0,bytes);memcpy(memory+tls,memory+first,end-first);
    }
    fprintf(stderr,"[SCHEDULING LLE ENTRY] actual thread=%08x stack=%08x..%08x tls=%08x tib-top=%08x entry=%08x\n",
        thread,MEM32(thread+0x20),MEM32(thread+0x1c),tls,MEM32(g_fs_base+4),entry);
    uint32_t sp=g_esp;g_esp-=4;MEM32(g_esp)=PILOT_IO_BOOTSTRAP_ENTRY;
    scheduling_lle_execute(entry);if(g_esp!=sp)stop("original bootstrap entry stack imbalance",entry);
    /* SystemRoutine cannot return. The existing callback boundary resumes
     * actual PsTerminateSystemThread after unwinding this native C frame. */
    original_thread_exit.status=0;longjmp(original_thread_exit.target,1);
}
static void scheduling_lle_finalize_imports(void)
{
    uint32_t type=0;const char *error=pilot_io_device_thread_type(&type);
    if(error)stop(error,259);
    for(size_t i=0;i<sizeof(pilot_imports)/sizeof(pilot_imports[0]);++i)
        if(pilot_imports[i].ordinal==259)MEM32(pilot_imports[i].address)=type;
}
static void scheduling_lle_run(void)
{
    const char *clock_error=pilot_io_device_clock_start();
    if(clock_error)stop(clock_error,0);
#ifdef PILOT_AUDIO_REFERENCE
    audio_reference_clock_start();
#elif defined(PILOT_AUDIO_DEVICE_LLE)
    clock_error=pilot_audio_device_timeline_start(PILOT_REFERENCE_BLOCKS_PER_MS,pilot_input_host_reference_time);
    if(clock_error)stop(clock_error,0);
    fprintf(stderr,"[ORIGINAL AUDIO TIME] stopped reference input: %u observed blocks per 1ms PIT opportunity; shared original/compiled TSC, USB/input and GP/EP/ACI; no hardware CPU-cycle claim\n",PILOT_REFERENCE_BLOCKS_PER_MS);
#endif
#ifdef PILOT_GPU_LLE
    // The selected timeline owns the initializer and title from the same epoch.
    clock_error=gpu_lle_prepare_launch();if(clock_error)stop(clock_error,3);
#endif
    clock_error=pilot_io_device_sections_start();
    if(clock_error)stop(clock_error,327);
#if defined(PILOT_D3D_NATIVE) && !defined(PILOT_DISPLAY_LLE)
    clock_error=graphics_original_clock_bind();
    if(clock_error)stop(clock_error,0);
#endif
    uint32_t directory=file32(0x12c),tls_size=0;
    if(directory) {
        uint32_t first=MEM32(directory),end=MEM32(directory+4),zero=MEM32(directory+16);
        if(end<first || (uint64_t)end-first+zero>0xd000)stop("original bootstrap TLS size",directory);
        tls_size=(((end-first)+zero+15)&~15u)+4;
    }
    const char *error=NULL;
    uint32_t out=PILOT_IO_BRIDGE_BASE+0x1800u;
    uint32_t create[]={out,0,file32(0x130),tls_size,out+4,PILOT_ENTRY,directory,0,0,PILOT_IO_BOOTSTRAP_ENTRY};
    uint32_t status=files_call(255,create);
    if(status)stop("original bootstrap creation failed",status);
    uint32_t handle=MEM32(out),join[]={handle,0,0};
    fprintf(stderr,"[SCHEDULING] LLE original thread/event/timer namespace; bootstrap id=%u handle=%08x; original default priority retained\n",MEM32(out+4),handle);
    status=files_call(233,join);if(status)stop("original bootstrap join failed",status);
    if(files_call(187,&handle))stop("original bootstrap handle close failed",handle);
    uint32_t object=0;error=pilot_io_device_next_thread(&object);if(error)stop(error,0);
    if(!object)stop("original bootstrap returned without a live title thread",PILOT_ENTRY);
    do {
        uint32_t wait[]={object,0,0,0,0};status=files_call(159,wait);
        if(status)stop("original title thread join failed",status);
        error=pilot_io_device_next_thread(&object);if(error)stop(error,0);
    } while(object);
    fprintf(stderr,"[SCHEDULING LLE] all managed title threads terminated in the original kernel\n");
}
