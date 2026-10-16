/* Whole original graphics route. The preserved XDK owns device setup, packets,
 * resource lifetime and ISR callbacks; the maintained NV2A consumes actual RAM.
 * No native renderer, substituted query counts or synthetic completions run.
 * All calls occur while holding the single guest CPU. */
#include "gpu_lle_config.h"
static struct {
    HMODULE library;
    void *device;
    void *(*create)(void);
    void (*destroy)(void*);
    int (*av_idle)(void*,uint32_t);
    const char *(*error)(void*);
    void (*bus)(void*,int(*)(uint32_t,void*,size_t),int(*)(uint32_t,const void*,size_t),void(*)(int,int));
    uint32_t (*read)(void*,uint32_t,int);
    void (*write)(void*,uint32_t,int,uint32_t);
    int (*aperture)(void*,uint32_t,unsigned,int,uint64_t*);
    void (*frame)(void*,uint64_t);
    void (*clock)(void*,uint64_t,uint32_t);
    int (*board_start)(void*,uint64_t,uint64_t,uint64_t);
    int (*board_advance)(void*,uint64_t,uint64_t,uint64_t);
    uint32_t (*port)(void*,uint16_t,int,int,uint32_t);
    void (*field)(void*,int);
    int (*av_outputs)(void*,uint32_t*,uint32_t*);
    int (*encoder_clocks)(void*,uint64_t*,uint32_t*);
    int (*encoder_advance)(void*,uint64_t,unsigned);
    int (*encoder_timing)(void*,uint64_t*,int*);
    int (*scanout)(void*,uint8_t*,uint32_t,uint32_t*,uint32_t*);
    int (*first_context_snapshot)(void*,uint8_t*,uint32_t,uint32_t*);
    int (*framebuffer_snapshot)(void*,uint8_t*,uint32_t,uint32_t*,int(*)(uint32_t,void*,size_t));
    int (*digital_snapshot)(void*,uint8_t*,uint32_t,uint32_t*,int(*)(uint32_t,void*,size_t));
    uint64_t origin;
    int external_clock_fixture;
    uint32_t launch_framebuffer;
    int launch_ready;
} gpu_lle;
static const char *gpu_lle_sample(void);
#include "gpu_first_context_observer.inc"
#include "gpu_framebuffer_observer.inc"

static int gpu_lle_dma(uint32_t pa,void *buffer,size_t bytes,int write)
{
    if(!bytes || (uint64_t)pa+bytes>64u*1024*1024)return 0;
    AcquireSRWLockShared(&vm_lock);
    struct PhysicalAllocation *allocation=physical_find(pa|0x80000000u,(uint32_t)bytes);
    int result=0;
    if(allocation) {
        /* DMA is synchronous with this bus access. Lifetime across queued work
         * belongs to the original driver/fences; stale/unowned accesses fail.
         * Do not borrow USB's controller-reset leases for a different device. */
        uint32_t first=pa/4096,last=(uint32_t)((pa+bytes+4095)/4096);
        for(uint32_t page=first;page<last;++page)
            if(write && (physical_protection[page]&6)!=PAGE_READWRITE)goto done;
        if(write)memcpy(memory+0x80000000ull+pa,buffer,bytes);
        else memcpy(buffer,memory+0x80000000ull+pa,bytes);
        result=1;
    }
done:
    ReleaseSRWLockShared(&vm_lock);
    if(!allocation) {
        result=pilot_io_device_bootstrap_dma(pa,buffer,(uint32_t)bytes,write);
        if(!result)result=pilot_io_device_contiguous_dma(pa,buffer,(uint32_t)bytes,write,PILOT_IO_DMA_SYNC);
        if(!result)result=pilot_io_device_pinned_dma(pa,buffer,(uint32_t)bytes,write);
    }
    return result;
}
static int gpu_lle_dma_read(uint32_t a,void *b,size_t n){return gpu_lle_dma(a,b,n,0);}
static int gpu_lle_dma_write(uint32_t a,const void *b,size_t n){return gpu_lle_dma(a,(void*)b,n,1);}
static const char *gpu_lle_access(uint32_t address,unsigned size,int write,uint32_t *value)
{
    if(!gpu_lle.device)return "NV2A access before whole GPU initialization";
    // Compiled ISR/DPC continuations must retain the stopped CPU's ownership
    // and synchronous DMA borrow, just like their original instruction path.
    // Do not fall back to idle access after any scoped device rejection.
    if(pilot_io_device_compiled_mmio_active())
        return pilot_io_device_compiled_mmio(address,size,write,value);
    gpu_lle_tick();
    if(write)gpu_lle.write(gpu_lle.device,address,size,*value);
    else *value=gpu_lle.read(gpu_lle.device,address,size);
    static unsigned accesses;
    const char *error=gpu_lle.error(gpu_lle.device);
    if(accesses++<32 || error)
        fprintf(stderr,"[GPU LLE MMIO] %s%u %08x=%08x%s\n",write?"W":"R",size,address,*value,error?" failed":"");
    return error;
}
static const char *gpu_lle_port(uint16_t port,int size,int write,uint32_t *value)
{
    if(!gpu_lle.device)return "GPU platform port before initialization";
    /* Observe the already published shared PIT time before changing clocks.
     * This never samples QPC or pumps CPU/PIT work inside a port callback. */
    const char *error=gpu_lle_sample();if(error)return error;
    uint32_t result=gpu_lle.port(gpu_lle.device,port,size,write,*value);
    if(!write)*value=result;
    return gpu_lle.error(gpu_lle.device);
}
static const char *gpu_lle_kernel_mmio(uint32_t address,unsigned size,int write,uint32_t *value)
{
    if(!gpu_lle.device)return "kernel NV2A access before GPU initialization";
    /* The outer original CPU retains its lock. The bus permits synchronous,
     * fully validated RAM borrowing; it never executes another original CPU.
     * Register admission/errors remain the maintained NV2A owner's decision. */
    const char *error=gpu_lle_sample();if(error)return error;
    if(write && gpu_context_observer.enabled)gpu_context_prewrite(address,size,*value);
    if(write)gpu_lle.write(gpu_lle.device,address,size,*value);
    else *value=gpu_lle.read(gpu_lle.device,address,size);
    error=gpu_lle.error(gpu_lle.device);
    if(write && gpu_context_observer.enabled)gpu_context_postwrite(address,error);
    return error;
}
static const char *gpu_lle_cpu_aperture(uint32_t address,unsigned size,int write,uint64_t *value)
{
    if(!gpu_lle.device)return "GPU memory aperture before initialization";
    const char *error=gpu_lle_sample();if(error)return error;
    if(address<0xf0000000u || (uint64_t)address+size>0xf4000000ull)
        return "GPU CPU aperture outside retail UMA window";
    if(!gpu_lle.aperture(gpu_lle.device,address-0xf0000000u,size,write,value)) {
        error=gpu_lle.error(gpu_lle.device);
        return error?error:"GPU CPU aperture transfer rejected";
    }
    return gpu_lle.error(gpu_lle.device);
}
static const char *gpu_lle_sample(void)
{
    if(!gpu_lle.device)return "GPU device work without owned instance";
    uint64_t ticks=0,next=0;
    const char *error=pilot_usb_pit_clock(&ticks,&next);if(error)return error;
    if(ticks<gpu_lle.origin)return "GPU shared device time moved backwards";
    uint64_t elapsed=ticks-gpu_lle.origin;
    /* Pinned xemu reset oscillator; PLL reprogramming remains an explicit
     * unsupported MMIO access. Counter epoch/subcycle phase are bounded. */
    const uint64_t hz=233333324;
    gpu_lle.clock(gpu_lle.device,(elapsed/1125000)*hz+(elapsed%1125000)*hz/1125000,0);
    // Same published PIT-time snapshot as the original CPU/device owner.
    // Reads and time spent in host code cannot independently advance it.
    // CLKO/FP counters supply retrace, vblank IRQs and encoder sync/FIELD.
    if(!gpu_lle.external_clock_fixture)
        gpu_lle.board_advance(gpu_lle.device,1,elapsed,1125000);
    return gpu_lle.error(gpu_lle.device);
}
static void gpu_lle_tick(void)
{
    if(!gpu_lle.device)return;
    const char *error=gpu_lle_sample();if(error)stop(error,0xfd000000u);
}
static void gpu_lle_open_profile_clock(uint32_t idle_phase,int external_clock_fixture)
{
    if(gpu_lle.device)stop("GPU device already owned",0);
    if(!InitOnceExecuteOnce(&usb_once,usb_start,NULL,NULL))stop("shared GPU machine bus initialization failed",0);
    const char *bootstrap_error=pilot_io_device_gpu_bootstrap(memory);
    if(bootstrap_error)stop(bootstrap_error,0x80000000u);
    for(uint64_t address=0xfd000000u;address<0xfe000000u;) {
        MEMORY_BASIC_INFORMATION info;
        if(!VirtualQuery(memory+address,&info,sizeof(info)) || info.State!=MEM_RESERVE)
            stop("native storage collides with NV2A MMIO aperture",(uint32_t)address);
        address=(unsigned char*)info.BaseAddress-memory+info.RegionSize;
    }
    FILE *file=_wfopen(PILOT_GPU_LIBRARY,L"rb");
    if(!file || _fseeki64(file,0,SEEK_END))stop("GPU DLL identity read failed",0);
    __int64 length=_ftelli64(file);
    if(length<=0 || length>512*1024*1024)stop("GPU DLL identity size",0);
    rewind(file);unsigned char *bytes=malloc((size_t)length),hash[32];char hex[65];
    if(!bytes || fread(bytes,1,(size_t)length,file)!=(size_t)length || fclose(file))
        stop("GPU DLL identity bytes",0);
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,bytes,(ULONG)length,hash,32)<0)
        stop("GPU DLL SHA256 failed",0);
    free(bytes);for(unsigned i=0;i<32;++i)sprintf(hex+i*2,"%02x",hash[i]);
    fprintf(stderr,"[GPU LLE LIBRARY] sha256=%s\n",hex);
    DLL_DIRECTORY_COOKIE dependency=AddDllDirectory(PILOT_GPU_DEPENDENCIES);
    if(!dependency)stop("GPU DLL dependency directory unavailable",GetLastError());
    gpu_lle.library=LoadLibraryExW(PILOT_GPU_LIBRARY,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS|LOAD_LIBRARY_SEARCH_USER_DIRS);
    RemoveDllDirectory(dependency);
    if(!gpu_lle.library)stop("maintained NV2A reference DLL unavailable",GetLastError());
#define GPU_LOAD(member) do {*(FARPROC*)&gpu_lle.member=GetProcAddress(gpu_lle.library,"xlle_nv2a_reference_" #member);if(!gpu_lle.member)stop("GPU DLL ABI missing: " #member,0);} while(0)
    GPU_LOAD(create);GPU_LOAD(destroy);GPU_LOAD(av_idle);GPU_LOAD(error);GPU_LOAD(bus);GPU_LOAD(read);GPU_LOAD(write);GPU_LOAD(aperture);GPU_LOAD(frame);GPU_LOAD(clock);GPU_LOAD(board_start);GPU_LOAD(board_advance);GPU_LOAD(port);GPU_LOAD(field);GPU_LOAD(av_outputs);GPU_LOAD(encoder_clocks);GPU_LOAD(encoder_advance);GPU_LOAD(encoder_timing);GPU_LOAD(scanout);
#undef GPU_LOAD
    gpu_lle.device=gpu_lle.create();
    if(!gpu_lle.device)stop("NV2A reference allocation failed",0);
    /* The retained initialized-kernel profile starts with no outstanding AV
     * request. Original AV3 observes only phase equality, then its complement;
     * canonical phase0 represents both quiescent phases. This does not seed
     * the encoder, active timing banks, retrace or an already-completed mode. */
    if(!gpu_lle.av_idle(gpu_lle.device,idle_phase))stop("NV2A quiescent AV profile failed",0);
    gpu_lle.bus(gpu_lle.device,gpu_lle_dma_read,gpu_lle_dma_write,pilot_usb_pic_raise);
    const char *error=gpu_lle.error(gpu_lle.device);
    if(error)stop(error,0xfd000000u);
    gpu_context_observer_open(hex);
    gpu_framebuffer_observer_open(hex);
    uint64_t next=0;
    error=pilot_usb_pit_clock(&gpu_lle.origin,&next);if(error)stop(error,0xfd000000u);
    gpu_lle.external_clock_fixture=external_clock_fixture;
    if(!external_clock_fixture && !gpu_lle.board_start(gpu_lle.device,1,0,1125000))
        stop(gpu_lle.error(gpu_lle.device),0xfd000000u);
    pilot_io_device_port_bind(0xcf8,0xcff,gpu_lle_port);
    pilot_io_device_port_bind(0x80c0,0x80c0,gpu_lle_port);
    pilot_io_device_port_bind(0x80d3,0x80d3,gpu_lle_port);
    pilot_io_device_port_bind(0x80d6,0x80d6,gpu_lle_port);
    pilot_io_device_port_bind(0x80d8,0x80d8,gpu_lle_port);
    pilot_io_device_port_bind(0xc000,0xc00f,gpu_lle_port);
    error=pilot_io_device_mmio_bind(0xfd000000u,0x1000000u,gpu_lle_kernel_mmio);
    if(error)stop(error,0xfd000000u);
    // Original large-page mappings expose the retail64MiB UMA PCI aperture.
    // Each access rechecks those mappings and borrows the actual RAM owner.
    error=pilot_io_device_bus_memory_bind(0xf0000000u,0x04000000u,gpu_lle_cpu_aperture);
    if(error)stop(error,0xf0000000u);
    error=pilot_io_device_work_bind(gpu_lle_sample);if(error)stop(error,0xfd000000u);
    fprintf(stderr,"[GPU LLE] original XDK renderer; maintained NV2A; shared original RAM/PIC/ISR\n");
}
static void gpu_lle_open_profile(uint32_t phase){gpu_lle_open_profile_clock(phase,0);}
static void gpu_lle_open(void){gpu_lle_open_profile(0);}

/* Explicit initialized NTSC launch input, not reconstructed warm hardware.
 * The retained kernel has encoder8A but no mode/saved image. Original title
 * consumers read encoder controls before their first mode setup, so execute
 * the actual AV producer once before creating any title thread. Mode0 is the
 * original API's default0F010101; format12/pitch2560 select640x480x32 storage.
 * Publish the real persistent black framebuffer through the original saved-
 * display handoff. Original137060 waits for GPU idle, completes its new mode,
 * then unpersists/frees the saved allocation and clears the saved pointer.
 * Its original contiguous owner is the sole lifetime authority: do not retain
 * a native PhysicalAllocation shadow that could admit DMA after original free.
 * No callback or PCRTC_START observation retires memory. */
static const char *gpu_lle_prepare_launch(void)
{
    if(!gpu_lle.device || gpu_lle.external_clock_fixture || !pilot_io_device_clock_bound())
        return "GPU launch profile requires the shared original clock/device owner";
    if(gpu_lle.launch_framebuffer || gpu_lle.launch_ready)
        return "GPU launch profile already initialized or retained after failure";
    uint32_t before[4],after[4];const char *error=pilot_io_device_av_state(before);
    if(error)return error;
    if(before[0]!=0x8a || before[1] || before[2])
        return "GPU launch profile requires original8A with no current mode/saved image";
    uint32_t allocate[]={640u*480u*4u,0,0x03ffffffu,4096,PAGE_READWRITE};
    error=pilot_io_device_lle(166,memory,allocate,&gpu_lle.launch_framebuffer);if(error)return error;
    if(!gpu_lle.launch_framebuffer)return "GPU launch framebuffer allocation failed";
    memset(memory+gpu_lle.launch_framebuffer,0,640u*480u*4u);
    uint32_t args[]={0xfd000000u,0,0,0x12,2560,gpu_lle.launch_framebuffer&0x03ffffffu},result=0xccccccccu;
    error=pilot_io_device_lle(3,memory,args,&result);if(error)return error;
    if(result)return "Original AV launch setup returned an unfinished step";
    error=pilot_io_device_av_state(after);if(error)return error;
    if(after[0]!=0x8a || after[1]!=0x0f010101u || after[2] || after[3]!=before[3])
        return "Original AV launch setup produced an unexpected kernel state";
    uint32_t persist[]={gpu_lle.launch_framebuffer,640u*480u*4u,1};
    error=pilot_io_device_lle(178,memory,persist,&result);if(error)return error;
    error=pilot_io_device_lle(4,memory,&gpu_lle.launch_framebuffer,&result);if(error)return error;
    error=pilot_io_device_av_state(after);if(error)return error;
    if(after[0]!=0x8a || after[1]!=0x0f010101u || after[2]!=gpu_lle.launch_framebuffer || after[3]!=before[3])
        return "Original AV launch saved-display handoff failed";
    gpu_lle.launch_ready=1;
    fprintf(stderr,"[GPU LLE LAUNCH] original AV3 default NTSC mode=%08x framebuffer=%08x bytes=1228800; original persistent saved-display owner\n",
        after[1],gpu_lle.launch_framebuffer);
    return NULL;
}
