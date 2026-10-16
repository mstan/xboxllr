/* Elective storage observation at actual original Swap returns. A Swap serial
 * is submission-side; neither that return nor PCRTC_START proves the newest
 * flip completed. Record the separate driver counters and current GPU buffer.
 * No guest services, DMA, clocks, queue drains or output-guard bypasses. */
#define GPU_FRAMEBUFFER_SWAP 0x001379a0u
#define GPU_FRAMEBUFFER_SWAP_BYTES 219u
#define GPU_FRAMEBUFFER_SWAP_SHA "83eda516e4ec44e46a83880796d99461ac3e16d5f32fb32e13401747e305ae4a"
#define GPU_FRAMEBUFFER_MAX (1860u+1920u*1080u*4u)
#define GPU_DIGITAL_MAX (320u+640u*480u*8u)
static struct {
    char directory[4096],library_sha[65];
    unsigned enabled,failed,records,completed_only,digital,requests;
    uint32_t last_device,last_completed;
    uint32_t progress_device,progress_completed,progress_caller;
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
// The supervisor publishes an immutable marker after observing the phase it
// wants. A request is selection intent, never proof of title/demo/reentry state.
// Missing next marker waits without touching GPU state or consuming a record.
static int gpu_framebuffer_request(const char *directory,unsigned record)
{
    if(!record || record>16)return -1;
    char path[4352],bytes[12];const char marker[]="capture-v1\n";
    if(sprintf_s(path,sizeof(path),"%s/framebuffer-%02u.request",directory,record)<0)return -1;
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND?0:-1;
    DWORD read=0;int okay=ReadFile(file,bytes,sizeof(bytes),&read,NULL) && read==sizeof(marker)-1 &&
        !memcmp(bytes,marker,sizeof(marker)-1);
    if(!CloseHandle(file))okay=0;return okay?1:-1;
}
static const char *gpu_framebuffer_record(const PilotIoCallDiagnosticEvent *event)
{
    if(!gpu_framebuffer_observer.enabled || gpu_framebuffer_observer.failed ||
       gpu_framebuffer_observer.records>=(gpu_framebuffer_observer.requests?16u:4u) ||
       event->kind!=PILOT_IO_DIAGNOSTIC_RETURN)return NULL;
    uint32_t device=0,serial=0;unsigned char miniport[0x1d0]={0};
    int driver_ok=pilot_io_device_diagnostic_read(0x00141b78u,&device,4,0) &&
        device && (uint64_t)device+0x2c14<=0x100000000ull &&
        pilot_io_device_diagnostic_read(device+0x2c10,&serial,4,0) &&
        pilot_io_device_diagnostic_read(device+0x23c0,miniport,sizeof(miniport),0);
    const uint32_t completed=gpu_context_word(miniport+0x1bc);
    if(gpu_framebuffer_observer.requests && driver_ok &&
       (device!=gpu_framebuffer_observer.progress_device || event->caller!=gpu_framebuffer_observer.progress_caller ||
        (uint32_t)(completed-gpu_framebuffer_observer.progress_completed)>=120)) {
        fprintf(stderr,"[GPU FRAMEBUFFER PROGRESS] caller=%08x device=%08x completed=%u submitted=%u frame=%u; phase unclassified\n",
            event->caller,device,completed,gpu_context_word(miniport+0x1cc),gpu_context_word(miniport+0x1c0));
        gpu_framebuffer_observer.progress_device=device;gpu_framebuffer_observer.progress_completed=completed;
        gpu_framebuffer_observer.progress_caller=event->caller;
    }
    // Elective collection of distinct completed-count observations. Do not
    // equate the Swap return serial with completion. A different device address
    // is also retained; this does not identify allocation generations/reuse.
    if(driver_ok && gpu_framebuffer_observer.completed_only && gpu_framebuffer_observer.records &&
       device==gpu_framebuffer_observer.last_device && completed==gpu_framebuffer_observer.last_completed)return NULL;
    if(gpu_framebuffer_observer.requests) {
        if(driver_ok && !completed)return NULL; // no first completed flip yet
        const int request=gpu_framebuffer_request(gpu_framebuffer_observer.directory,gpu_framebuffer_observer.records+1);
        if(request<=0) {
            if(request<0) {
                gpu_framebuffer_observer.failed=1;
                fprintf(stderr,"[GPU FRAMEBUFFER] request_failed=1 record=%u\n",gpu_framebuffer_observer.records+1);
            }
            return NULL;
        }
    }
    const unsigned record=++gpu_framebuffer_observer.records;
    gpu_framebuffer_observer.last_device=device;gpu_framebuffer_observer.last_completed=completed;
    unsigned char *wire=malloc(GPU_FRAMEBUFFER_MAX);uint32_t required=0;
    int copied=wire && gpu_lle.framebuffer_snapshot(gpu_lle.device,wire,GPU_FRAMEBUFFER_MAX,
        &required,gpu_framebuffer_read) && required>=1860 && required<=GPU_FRAMEBUFFER_MAX;
    if(copied && (gpu_context_word(wire)!=0x31444246 || gpu_context_word(wire+4)!=1 ||
        gpu_context_word(wire+8)!=1860 || gpu_context_word(wire+120)!=1 ||
        (uint64_t)gpu_context_word(wire+16)+1860!=required))copied=0;
    int saved=gpu_framebuffer_file(record,"cpu.bin",event,sizeof(*event));
    if(driver_ok)saved=gpu_framebuffer_file(record,"miniport.bin",miniport,sizeof(miniport)) && saved;
    if(copied)saved=gpu_framebuffer_file(record,"fbd1.bin",wire,required) && saved;
    int digital_copied=0,digital_pixels=0;
    if(gpu_framebuffer_observer.digital) {
        unsigned char *digital=malloc(GPU_DIGITAL_MAX);uint32_t digital_bytes=0;
        digital_copied=digital && gpu_lle.digital_snapshot(gpu_lle.device,digital,GPU_DIGITAL_MAX,
            &digital_bytes,gpu_framebuffer_read) && digital_bytes>=320 && digital_bytes<=GPU_DIGITAL_MAX;
        if(digital_copied && (gpu_context_word(digital)!=0x31445047 || gpu_context_word(digital+4)!=1 ||
            gpu_context_word(digital+8)!=320 || gpu_context_word(digital+60)!=2 ||
            320ull+gpu_context_word(digital+24)+gpu_context_word(digital+28)!=digital_bytes))digital_copied=0;
        if(digital_copied) {
            digital_pixels=gpu_context_word(digital+12)==1;
            saved=gpu_framebuffer_file(record,"gpd1.bin",digital,digital_bytes) && saved;
        }
        free(digital);
    }
    char json[2048];
    int length=sprintf_s(json,sizeof(json),
        "{\"stage\":\"framebuffer-before-display-processing\",\"qualified\":false,"
        "\"actual_swap_return\":true,\"call_id\":%llu,\"thread\":%u,\"caller\":%u,\"entry_sp\":%u,"
        "\"return_eax\":%u,\"device\":%u,\"device_swap_serial\":%u,\"driver_copied\":%s,"
        "\"completed\":%u,\"submitted\":%u,\"driver_frame\":%u,\"snapshot_copied\":%s,"
        "\"storage_copied\":%s,\"distinct_completed_counts\":%s,\"latest_submission_displayed_claim\":false,"
        "\"digital_requested\":%s,\"digital_snapshot_copied\":%s,\"digital_input_pixels\":%s,"
        "\"request_controlled\":%s,"
        "\"source_entry\":%u,\"source_bytes\":%u,\"source_sha256\":\"%s\",\"library_sha256\":\"%s\","
        "\"diagnostic_device_reads_writes_clocks_drains\":0}\n",
        (unsigned long long)event->id,event->thread,event->caller,event->entry_sp,event->regs[0],device,serial,
        driver_ok?"true":"false",gpu_context_word(miniport+0x1bc),gpu_context_word(miniport+0x1cc),
        gpu_context_word(miniport+0x1c0),copied?"true":"false",
        copied && gpu_context_word(wire+12)?"true":"false",gpu_framebuffer_observer.completed_only?"true":"false",
        gpu_framebuffer_observer.digital?"true":"false",digital_copied?"true":"false",digital_pixels?"true":"false",
        gpu_framebuffer_observer.requests?"true":"false",
        GPU_FRAMEBUFFER_SWAP,GPU_FRAMEBUFFER_SWAP_BYTES,
        GPU_FRAMEBUFFER_SWAP_SHA,gpu_framebuffer_observer.library_sha);
    if(length<0 || !gpu_framebuffer_file(record,"json",json,(uint32_t)length) || !saved || !copied || !driver_ok ||
       (gpu_framebuffer_observer.digital && (!digital_copied || !digital_pixels)))
        gpu_framebuffer_observer.failed=1;
    fprintf(stderr,"[GPU FRAMEBUFFER] record=%u swap_return=%u completed=%u submitted=%u storage=%u digital=%u failed=%u; diagnostic only\n",
        record,event->regs[0],gpu_context_word(miniport+0x1bc),gpu_context_word(miniport+0x1cc),
        copied?gpu_context_word(wire+12):0,digital_pixels,gpu_framebuffer_observer.failed);
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
    const char *completed_only=getenv("XLLE_PILOT_FRAMEBUFFER_COMPLETED_ONLY");
    gpu_framebuffer_observer.completed_only=completed_only && !strcmp(completed_only,"1");
    const char *requests=getenv("XLLE_PILOT_FRAMEBUFFER_REQUESTS");
    gpu_framebuffer_observer.requests=requests && !strcmp(requests,"1");
    if(gpu_framebuffer_observer.requests)gpu_framebuffer_observer.completed_only=1;
    const char *digital=getenv("XLLE_PILOT_DIGITAL_INPUT_CAPTURE");
    gpu_framebuffer_observer.digital=digital && !strcmp(digital,"1");
    if(gpu_framebuffer_observer.digital) {
        *(FARPROC*)&gpu_lle.digital_snapshot=GetProcAddress(gpu_lle.library,"xlle_nv2a_reference_digital_snapshot");
        if(!gpu_lle.digital_snapshot)stop("GPU digital input ABI missing",0);
    }
    const PilotIoDiagnosticCall call={GPU_FRAMEBUFFER_SWAP,GPU_FRAMEBUFFER_SWAP_BYTES,4,GPU_FRAMEBUFFER_SWAP_SHA};
    // Fresh-table API rejects competing observers; never unbind audio capture.
    const char *error=pilot_io_device_diagnostic_calls_bind(&call,1,gpu_framebuffer_record);
    if(error)stop(error,GPU_FRAMEBUFFER_SWAP);
    gpu_framebuffer_observer.enabled=1;
    fprintf(stderr,"[GPU FRAMEBUFFER] at most%u actual Swap-return observations; distinct_completed_counts=%u request_controlled=%u; full output guards retained\n",
        gpu_framebuffer_observer.requests?16u:4u,gpu_framebuffer_observer.completed_only,gpu_framebuffer_observer.requests);
}
