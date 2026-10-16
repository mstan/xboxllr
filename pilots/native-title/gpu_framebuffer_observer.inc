/* Elective storage observation at actual original Swap returns. A Swap serial
 * is submission-side; neither that return nor PCRTC_START proves the newest
 * flip completed. Record the separate driver counters and current GPU buffer.
 * No guest services, DMA, clocks, queue drains or output-guard bypasses. */
#define GPU_FRAMEBUFFER_SWAP 0x001379a0u
#define GPU_FRAMEBUFFER_SWAP_BYTES 219u
#define GPU_FRAMEBUFFER_SWAP_SHA "83eda516e4ec44e46a83880796d99461ac3e16d5f32fb32e13401747e305ae4a"
#define GPU_FRAMEBUFFER_MAX (1860u+1920u*1080u*4u)
static struct {
    char directory[4096],library_sha[65];
    unsigned enabled,failed,records;
} gpu_framebuffer_observer;
static int gpu_framebuffer_read(uint32_t address,void *output,size_t bytes)
{
    return bytes<=0x04000000u && pilot_io_device_diagnostic_read(address,output,(uint32_t)bytes,1);
}
static int gpu_framebuffer_file(unsigned record,const char *suffix,const void *data,uint32_t bytes)
{
    char path[4352];
    if(sprintf_s(path,sizeof(path),"%s/framebuffer-%02u.%s",gpu_framebuffer_observer.directory,record,suffix)<0)return 0;
    HANDLE file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return 0;
    DWORD written=0;int okay=WriteFile(file,data,bytes,&written,NULL) && written==bytes;
    if(!CloseHandle(file))okay=0;return okay;
}
static const char *gpu_framebuffer_record(const PilotIoCallDiagnosticEvent *event)
{
    if(!gpu_framebuffer_observer.enabled || gpu_framebuffer_observer.failed ||
       gpu_framebuffer_observer.records>=4 || event->kind!=PILOT_IO_DIAGNOSTIC_RETURN)return NULL;
    const unsigned record=++gpu_framebuffer_observer.records;
    uint32_t device=0,serial=0;unsigned char miniport[0x1d0]={0};
    int driver_ok=pilot_io_device_diagnostic_read(0x00141b78u,&device,4,0) &&
        device && (uint64_t)device+0x2c14<=0x100000000ull &&
        pilot_io_device_diagnostic_read(device+0x2c10,&serial,4,0) &&
        pilot_io_device_diagnostic_read(device+0x23c0,miniport,sizeof(miniport),0);
    unsigned char *wire=malloc(GPU_FRAMEBUFFER_MAX);uint32_t required=0;
    int copied=wire && gpu_lle.framebuffer_snapshot(gpu_lle.device,wire,GPU_FRAMEBUFFER_MAX,
        &required,gpu_framebuffer_read) && required>=1860 && required<=GPU_FRAMEBUFFER_MAX;
    if(copied && (gpu_context_word(wire)!=0x31444246 || gpu_context_word(wire+4)!=1 ||
        gpu_context_word(wire+8)!=1860 || gpu_context_word(wire+120)!=1 ||
        (uint64_t)gpu_context_word(wire+16)+1860!=required))copied=0;
    int saved=gpu_framebuffer_file(record,"cpu.bin",event,sizeof(*event));
    if(driver_ok)saved=gpu_framebuffer_file(record,"miniport.bin",miniport,sizeof(miniport)) && saved;
    if(copied)saved=gpu_framebuffer_file(record,"fbd1.bin",wire,required) && saved;
    char json[2048];
    int length=sprintf_s(json,sizeof(json),
        "{\"stage\":\"framebuffer-before-display-processing\",\"qualified\":false,"
        "\"actual_swap_return\":true,\"call_id\":%llu,\"thread\":%u,\"caller\":%u,\"entry_sp\":%u,"
        "\"return_eax\":%u,\"device\":%u,\"device_swap_serial\":%u,\"driver_copied\":%s,"
        "\"completed\":%u,\"submitted\":%u,\"driver_frame\":%u,\"snapshot_copied\":%s,"
        "\"storage_copied\":%s,\"latest_submission_displayed_claim\":false,"
        "\"source_entry\":%u,\"source_bytes\":%u,\"source_sha256\":\"%s\",\"library_sha256\":\"%s\","
        "\"diagnostic_device_reads_writes_clocks_drains\":0}\n",
        (unsigned long long)event->id,event->thread,event->caller,event->entry_sp,event->regs[0],device,serial,
        driver_ok?"true":"false",gpu_context_word(miniport+0x1bc),gpu_context_word(miniport+0x1cc),
        gpu_context_word(miniport+0x1c0),copied?"true":"false",
        copied && gpu_context_word(wire+12)?"true":"false",GPU_FRAMEBUFFER_SWAP,GPU_FRAMEBUFFER_SWAP_BYTES,
        GPU_FRAMEBUFFER_SWAP_SHA,gpu_framebuffer_observer.library_sha);
    if(length<0 || !gpu_framebuffer_file(record,"json",json,(uint32_t)length) || !saved || !copied || !driver_ok)
        gpu_framebuffer_observer.failed=1;
    fprintf(stderr,"[GPU FRAMEBUFFER] record=%u swap_return=%u completed=%u submitted=%u storage=%u failed=%u; pre-display diagnostic only\n",
        record,event->regs[0],gpu_context_word(miniport+0x1bc),gpu_context_word(miniport+0x1cc),
        copied?gpu_context_word(wire+12):0,gpu_framebuffer_observer.failed);
    free(wire);return NULL;
}
static void gpu_framebuffer_observer_open(const char *library_sha)
{
    const char *directory=getenv("XLLE_PILOT_FRAMEBUFFER_CAPTURE_DIR");
    if(!directory || !*directory)return;
    DWORD attributes=GetFileAttributesA(directory);
    if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) ||
       strlen(directory)>=sizeof(gpu_framebuffer_observer.directory))stop("framebuffer capture requires existing private directory",0);
    *(FARPROC*)&gpu_lle.framebuffer_snapshot=GetProcAddress(gpu_lle.library,"xlle_nv2a_reference_framebuffer_snapshot");
    if(!gpu_lle.framebuffer_snapshot)stop("GPU passive framebuffer ABI missing",0);
    strcpy_s(gpu_framebuffer_observer.directory,sizeof(gpu_framebuffer_observer.directory),directory);
    strcpy_s(gpu_framebuffer_observer.library_sha,sizeof(gpu_framebuffer_observer.library_sha),library_sha);
    const PilotIoDiagnosticCall call={GPU_FRAMEBUFFER_SWAP,GPU_FRAMEBUFFER_SWAP_BYTES,4,GPU_FRAMEBUFFER_SWAP_SHA};
    // Fresh-table API rejects competing observers; never unbind audio capture.
    const char *error=pilot_io_device_diagnostic_calls_bind(&call,1,gpu_framebuffer_record);
    if(error)stop(error,GPU_FRAMEBUFFER_SWAP);
    gpu_framebuffer_observer.enabled=1;
    fprintf(stderr,"[GPU FRAMEBUFFER] at most4 actual Swap-return observations; full output guards retained\n");
}
