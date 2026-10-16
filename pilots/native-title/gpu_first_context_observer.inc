/* Elective, passive G03 capture. Only the raw engine and current Nv2a owner
 * supply observations. No driver invocation or context-transfer admission. */
#define GPU_CONTEXT_PRODUCER 0x0013f26du
#define GPU_CONTEXT_PRODUCER_BYTES 2527u
#define GPU_CONTEXT_PRODUCER_SHA "d0725f94d600582ce3e217a9bad7419baed4b33db5d7549ad5b711c27db655d4"
#define GPU_CONTEXT_WIRE_MAX (21u*4+32u*13*4+8192u*8+256u*9*4+0x100000u)
#define GPU_CONTEXT_PACKET_MAX (0x100000u/4+256u)
static struct {
    char directory[4096],library_sha[65],observed_source_sha[65];
    unsigned enabled,failed,records,returned,selector_observed,selector_accepted,load_observed;
    unsigned source_copied,source_bound,capture_ram;
    PilotIoDiagnosticEvent entry;
} gpu_context_observer;
static uint32_t gpu_context_word(const unsigned char *p)
{uint32_t value;memcpy(&value,p,4);return value;}
static int gpu_context_file(unsigned record,const char *suffix,const void *data,uint32_t bytes)
{
    char path[4352];
    if(sprintf_s(path,sizeof(path),"%s/record-%02u.%s",gpu_context_observer.directory,record,suffix)<0)return 0;
    HANDLE file=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE)return 0;
    DWORD written=0;int okay=WriteFile(file,data,bytes,&written,NULL) && written==bytes;
    if(!CloseHandle(file))okay=0;return okay;
}
/* Diagnostic mapping of an actual captured descriptor. This is a source-byte
 * observation, not DMA admission. Check the entire DWORD and retail extent. */
static int gpu_context_packet_address(const unsigned char *pramin,uint32_t instance,
    uint32_t offset,uint32_t *pa)
{
    if((uint64_t)instance+12>0x100000u)return 0;
    uint32_t flags=gpu_context_word(pramin+instance),limit=gpu_context_word(pramin+instance+4);
    if((uint64_t)offset+3>limit)return 0;
    *pa=(((gpu_context_word(pramin+instance+8)&0xfffff000u)|((flags&0xfff00000u)>>20))+offset)&0x07ffffffu;
    return (uint64_t)*pa+4<=64u*1024*1024;
}
static int gpu_context_packet(unsigned char *rows,unsigned *count,const unsigned char *pramin,
    uint32_t kind,uint32_t channel,uint32_t descriptor,uint32_t offset,uint32_t expected,int compare)
{
    uint32_t pa=0,word=0;
    if(*count>=GPU_CONTEXT_PACKET_MAX || !gpu_context_packet_address(pramin,descriptor,offset,&pa) ||
       !pilot_io_device_diagnostic_read(pa,&word,4,1))return 0;
    uint32_t values[]={kind,channel,offset,pa,word};memcpy(rows+(*count)++*20,values,20);
    return !compare || word==expected;
}
static void gpu_context_capture(const PilotIoDiagnosticEvent *event)
{
    if(!gpu_context_observer.enabled || gpu_context_observer.failed || gpu_context_observer.records>=4)return;
    unsigned record=++gpu_context_observer.records;
    uint32_t required=0;unsigned packet_count=0;
    unsigned char *wire=malloc(GPU_CONTEXT_WIRE_MAX),*packets=malloc(GPU_CONTEXT_PACKET_MAX*20u);
    unsigned char driver[0x164];memset(driver,0,sizeof(driver));
    uint32_t driver_address=event->driver;
    int driver_ok=driver_address && pilot_io_device_diagnostic_read(driver_address,driver,sizeof(driver),0);
    int snapshot_ok=wire && gpu_lle.first_context_snapshot &&
        gpu_lle.first_context_snapshot(gpu_lle.device,wire,GPU_CONTEXT_WIRE_MAX,&required);
    int packets_ok=snapshot_ok && packets;
    int ranges_ok=0;
    if(snapshot_ok) {
        uint32_t nr=gpu_context_word(wire+16),nq=gpu_context_word(wire+20);
        uint32_t pramin_at=21*4+32*13*4+nr*8+nq*9*4;
        if(gpu_context_word(wire)!=0x31434647 || gpu_context_word(wire+4)!=1 ||
           gpu_context_word(wire+8)!=0x100000 || gpu_context_word(wire+12)!=32 ||
           nr>8192 || nq>256 || (uint64_t)pramin_at+0x100000!=required)packets_ok=snapshot_ok=0;
        else {
            const unsigned char *pramin=wire+pramin_at,*queue=wire+21*4+32*13*4+nr*8;
            for(uint32_t i=0;i<nq && packets_ok;++i) {
                const unsigned char *p=queue+i*36;
                uint32_t channel=gpu_context_word(p+8),get=gpu_context_word(p+28);
                if(channel>=32 || get<4 || (get&3))packets_ok=0;
                else packets_ok=gpu_context_packet(packets,&packet_count,pramin,1,channel,
                    gpu_context_word(wire+21*4+channel*52+8),get-4,gpu_context_word(p+24),1);
            }
            uint64_t unread=0;
            for(uint32_t channel=0;channel<32 && packets_ok;++channel) {
                const unsigned char *c=wire+21*4+channel*52;
                uint32_t get=gpu_context_word(c),put=gpu_context_word(c+4);
                if(!gpu_context_word(c+48) || get==put)continue;
                if(gpu_context_word(c+44) || ((get|put)&3) || put<get ||
                   (unread+=(uint64_t)put-get)>0x100000u){packets_ok=0;break;}
                for(uint32_t at=get;at<put && packets_ok;at+=4)
                    packets_ok=gpu_context_packet(packets,&packet_count,pramin,2,channel,
                        gpu_context_word(c+8),at,0,0);
            }
            if(driver_ok && gpu_context_word(driver)==0xfd000000u) {
                uint32_t channel=gpu_context_word(driver+(event->kind==PILOT_IO_DIAGNOSTIC_RETURN?0x100:0x134));
                if(channel<2) {
                    uint32_t instance=gpu_context_word(driver+0x138+channel*4),table=gpu_context_word(driver+0x140);
                    uint64_t ramfc=(uint64_t)gpu_context_word(driver+0x128)+channel*64;
                    ranges_ok=instance<=0xffff && table<=0xffff &&
                        (uint64_t)instance*16+0x37f0<=0x100000 && (uint64_t)table*16+8<=0x100000 &&
                        ramfc>=0x700000 && ramfc+64<=0x800000 &&
                        gpu_context_word(pramin+table*16+channel*4)==instance;
                }
            }
        }
    }
    int saved=gpu_context_file(record,"cpu.bin",event,sizeof(*event));
    if(driver_ok)saved=gpu_context_file(record,"driver.bin",driver,sizeof(driver)) && saved;
    if(snapshot_ok)saved=gpu_context_file(record,"gfc1.bin",wire,required) && saved;
    if(packets && packet_count)saved=gpu_context_file(record,"packets.bin",packets,packet_count*20u) && saved;
    // One elective physical RAM snapshot at the actual pre-LOAD boundary.
    // Retained packet/context bytes alone cannot replay vertex/texture fetches.
    // The diagnostic copy uses canonical RAM backing, without device accesses,
    // CPU reentry or clocks, while the same execution owner is borrowed.
    const int ram_requested=gpu_context_observer.capture_ram && event->address==0xfd400788u;
    int ram_copied=0;
    if(ram_requested) {
        const uint32_t ram_bytes=64u*1024*1024;
        unsigned char *ram=malloc(ram_bytes);
        ram_copied=ram && pilot_io_device_diagnostic_read(0,ram,ram_bytes,1) &&
            gpu_context_file(record,"ram.bin",ram,ram_bytes);
        free(ram);saved=ram_copied && saved;
    }
    char json[2048];
    int length=sprintf_s(json,sizeof(json),
        "{\"kind\":%u,\"raw_pc\":%u,\"raw_thread\":%u,\"raw_in_kernel\":%u,"
        "\"compiled_callback_request\":%s,\"raw_pc_is_request_pc\":%s,"
        "\"driver_address\":%u,\"same_original_driver\":%s,\"source_entry\":%u,\"source_bytes\":%u,\"source_sha256\":\"%s\","
        "\"observed_source_sha256\":\"%s\",\"raw_source_copied\":%s,\"original_source_bound\":%s,"
        "\"library_sha256\":\"%s\",\"driver_copied\":%s,\"const_snapshot_copied\":%s,"
        "\"parser_bytes_complete\":%s,\"derived_image_table_ramfc_valid\":%s,\"packet_rows\":%u,"
        "\"actual_producer_return_observed\":%s,\"selector_request_observed\":%s,"
        "\"selector_accepted_before_this_record\":%s,\"load_request_observed\":%s,"
        "\"preload_ram_requested\":%s,\"preload_ram_copied\":%s,"
        "\"g03_qualified\":false,\"diagnostic_device_reads_writes_clocks_drains\":0}\n",
        event->kind,event->pc,event->thread,event->in_kernel,
        event->kind==PILOT_IO_DIAGNOSTIC_COMPILED_MMIO?"true":"false",
        event->kind==PILOT_IO_DIAGNOSTIC_COMPILED_MMIO?"false":"true",driver_address,
        driver_address && driver_address==gpu_context_observer.entry.driver?"true":"false",GPU_CONTEXT_PRODUCER,
        GPU_CONTEXT_PRODUCER_BYTES,GPU_CONTEXT_PRODUCER_SHA,gpu_context_observer.observed_source_sha,
        gpu_context_observer.source_copied?"true":"false",gpu_context_observer.source_bound?"true":"false",
        gpu_context_observer.library_sha,
        driver_ok?"true":"false",snapshot_ok?"true":"false",packets_ok?"true":"false",
        ranges_ok?"true":"false",packet_count,gpu_context_observer.returned?"true":"false",
        gpu_context_observer.selector_observed?"true":"false",gpu_context_observer.selector_accepted?"true":"false",
        gpu_context_observer.load_observed?"true":"false",
        ram_requested?"true":"false",ram_copied?"true":"false");
    if(length<0 || !gpu_context_file(record,"json",json,(uint32_t)length) || !saved) {
        gpu_context_observer.failed=1;fprintf(stderr,"[G03 OBSERVER] incomplete file capture; original execution unchanged\n");
    }
    free(wire);free(packets);
}
static void gpu_context_return_receiver(const PilotIoDiagnosticEvent *event)
{
    if(event->kind==PILOT_IO_DIAGNOSTIC_ENTRY) {
        gpu_context_observer.entry=*event;
        unsigned char source[GPU_CONTEXT_PRODUCER_BYTES],digest[32];
        if(pilot_io_device_diagnostic_read(event->pc,source,sizeof(source),0)) {
            gpu_context_observer.source_copied=1;
            if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,source,sizeof(source),digest,32)>=0) {
                for(unsigned i=0;i<32;++i)sprintf(gpu_context_observer.observed_source_sha+i*2,"%02x",digest[i]);
                gpu_context_observer.source_bound=!strcmp(gpu_context_observer.observed_source_sha,GPU_CONTEXT_PRODUCER_SHA);
            }
            if(!gpu_context_file(0,"raw-producer-source.bin",source,sizeof(source)))gpu_context_observer.failed=1;
        }
    }
    else if(event->kind==PILOT_IO_DIAGNOSTIC_RETURN) {
        /* Actual caller and frame, not the producer's RET instruction. */
        if(event->pc!=0x0013c08eu || event->thread!=gpu_context_observer.entry.thread ||
           event->regs[4]!=gpu_context_observer.entry.regs[4]+4)return;
        gpu_context_observer.returned=1;
    }
    gpu_context_capture(event);
}
static void gpu_context_prewrite(uint32_t address,unsigned size,uint32_t value)
{
    if(!gpu_context_observer.enabled || gpu_context_observer.failed ||
       (address!=0xfd400784u && address!=0xfd400788u))return;
    if((address==0xfd400784u && gpu_context_observer.selector_observed) ||
       (address==0xfd400788u && gpu_context_observer.load_observed))return;
    PilotIoDiagnosticEvent event;
    if(!pilot_io_device_diagnostic_context(&event))return;
    event.address=address;event.size=size;event.value=value;
    const int compiled=event.kind==PILOT_IO_DIAGNOSTIC_COMPILED_MMIO;
    /* This actual function carries driver in EBX and aperture in ESI. Copy
     * those current registers, never substitute earlier trace metadata. */
    static const unsigned char selector[]={0x89,0x86,0x84,0x07,0x40,0x00};
    static const unsigned char load[]={0xc7,0x86,0x88,0x07,0x40,0x00,0x01,0,0,0};
    unsigned char instruction[10];unsigned bytes=address==0xfd400784u?sizeof(selector):sizeof(load);
    const unsigned char *expected=address==0xfd400784u?selector:load;
    uint32_t pc=address==0xfd400784u?0x0013fd25u:0x0013fd2bu;
    int instruction_copied=!compiled && pilot_io_device_diagnostic_read(event.pc,instruction,bytes,0);
    event.driver=!compiled && size==4 && event.pc==pc && event.regs[6]==0xfd000000u &&
        instruction_copied && !memcmp(instruction,expected,bytes)?event.regs[3]:0;
    if(compiled) {
        // Current generated registers are separate evidence from the saved
        // raw dispatcher entry. There is no invented guest instruction PC.
        const uint32_t regs[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi};
        if(!gpu_context_file(gpu_context_observer.records+1,"compiled-regs.bin",regs,sizeof(regs)))
            gpu_context_observer.failed=1;
        event.driver=size==4 && g_esi==0xfd000000u?g_ebx:0;
    }
    if(instruction_copied && !gpu_context_file(gpu_context_observer.records+1,"request-source.bin",instruction,bytes))
        gpu_context_observer.failed=1;
    /* Preserve even mismatched raw requests as evidence, never as admission. */
    if(address==0xfd400784u) {
        if(gpu_context_observer.selector_observed)return;
        gpu_context_observer.selector_observed=1;
    } else {
        if(gpu_context_observer.load_observed)return;
        gpu_context_observer.load_observed=1;
    }
    gpu_context_capture(&event);
}
static void gpu_context_postwrite(uint32_t address,const char *error)
{
    if(!gpu_context_observer.enabled || address!=0xfd400784u || !gpu_context_observer.selector_observed)return;
    gpu_context_observer.selector_accepted=!error;
    if(error && !gpu_context_file(0,"selector-guard.txt",error,(uint32_t)strlen(error)))
        gpu_context_observer.failed=1;
    fprintf(stderr,"[G03 SELECTOR RESULT] accepted=%u guard=%s; load_observed=%u\n",
        gpu_context_observer.selector_accepted,error?error:"none",gpu_context_observer.load_observed);
}
static void gpu_context_observer_open(const char *library_sha)
{
    const char *directory=getenv("XLLE_PILOT_G03_CAPTURE_DIR");
    if(!directory || !*directory)return;
    DWORD attributes=GetFileAttributesA(directory);
    if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) ||
       strlen(directory)>=sizeof(gpu_context_observer.directory))stop("G03 requires existing private capture directory",0);
    *(FARPROC*)&gpu_lle.first_context_snapshot=GetProcAddress(gpu_lle.library,"xlle_nv2a_reference_first_context_snapshot");
    if(!gpu_lle.first_context_snapshot)stop("G03 passive snapshot ABI missing",0);
    memset(&gpu_context_observer,0,sizeof(gpu_context_observer));
    const char *capture_ram=getenv("XLLE_PILOT_G03_CAPTURE_RAM");
    gpu_context_observer.capture_ram=capture_ram && !strcmp(capture_ram,"1");
    strcpy_s(gpu_context_observer.directory,sizeof(gpu_context_observer.directory),directory);
    strcpy_s(gpu_context_observer.library_sha,sizeof(gpu_context_observer.library_sha),library_sha);
    const char *error=pilot_io_device_diagnostic_bind(GPU_CONTEXT_PRODUCER,GPU_CONTEXT_PRODUCER_BYTES,
        GPU_CONTEXT_PRODUCER_SHA,gpu_context_return_receiver);
    if(error)stop(error,GPU_CONTEXT_PRODUCER);
    gpu_context_observer.enabled=1;
    fprintf(stderr,"[G03 OBSERVER] raw original entry/actual-return and pre-write capture enabled; transfer guards retained\n");
}
