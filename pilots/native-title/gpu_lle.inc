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
    const char *(*error)(void*);
    void (*bus)(void*,int(*)(uint32_t,void*,size_t),int(*)(uint32_t,const void*,size_t),void(*)(int,int));
    uint32_t (*read)(void*,uint32_t,int);
    void (*write)(void*,uint32_t,int,uint32_t);
    void (*frame)(void*,uint64_t);
    void (*clock)(void*,uint64_t,uint32_t);
    uint64_t origin,field;
} gpu_lle;

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
    if(!allocation)result=pilot_io_device_pinned_dma(pa,buffer,(uint32_t)bytes,write);
    return result;
}
static int gpu_lle_dma_read(uint32_t a,void *b,size_t n){return gpu_lle_dma(a,b,n,0);}
static int gpu_lle_dma_write(uint32_t a,const void *b,size_t n){return gpu_lle_dma(a,(void*)b,n,1);}
static const char *gpu_lle_access(uint32_t address,unsigned size,int write,uint32_t *value)
{
    if(!gpu_lle.device)return "NV2A access before whole GPU initialization";
    gpu_lle_tick();
    if(write)gpu_lle.write(gpu_lle.device,address,size,*value);
    else *value=gpu_lle.read(gpu_lle.device,address,size);
    static unsigned accesses;
    const char *error=gpu_lle.error(gpu_lle.device);
    if(accesses++<32 || error)
        fprintf(stderr,"[GPU LLE MMIO] %s%u %08x=%08x%s\n",write?"W":"R",size,address,*value,error?" failed":"");
    return error;
}
static uint8_t gpu_lle_field(void){return (uint8_t)((gpu_lle.field&1u)<<5);}
static void gpu_lle_tick(void)
{
    if(!gpu_lle.device)return;
    uint64_t elapsed=dispatch_monotonic()-gpu_lle.origin;
    /* Same bounded nominal 60-Hz field clock as the qualified display floor.
     * Reading a register never advances time. Slow rendering is permitted. */
    uint64_t field=(elapsed/10000000)*60+(elapsed%10000000)*60/10000000;
    /* Pinned xemu reset oscillator; PLL reprogramming remains an explicit
     * unsupported MMIO access. Counter epoch/subcycle phase are bounded. */
    const uint64_t hz=233333324;
    gpu_lle.clock(gpu_lle.device,(elapsed/10000000)*hz+(elapsed%10000000)*hz/10000000,
                  (uint32_t)(((elapsed%10000000)*60%10000000)*1000/10000000));
    if(field>gpu_lle.field) {
        gpu_lle.field=field;gpu_lle.frame(gpu_lle.device,field);
    }
    const char *error=gpu_lle.error(gpu_lle.device);
    if(error)stop(error,0xfd000000u);
}
static void gpu_lle_open(void)
{
    if(gpu_lle.device)stop("GPU device already owned",0);
    if(!InitOnceExecuteOnce(&usb_once,usb_start,NULL,NULL))stop("shared GPU machine bus initialization failed",0);
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
    GPU_LOAD(create);GPU_LOAD(destroy);GPU_LOAD(error);GPU_LOAD(bus);GPU_LOAD(read);GPU_LOAD(write);GPU_LOAD(frame);GPU_LOAD(clock);
#undef GPU_LOAD
    gpu_lle.device=gpu_lle.create();
    if(!gpu_lle.device)stop("NV2A reference allocation failed",0);
    gpu_lle.bus(gpu_lle.device,gpu_lle_dma_read,gpu_lle_dma_write,pilot_usb_pic_raise);
    const char *error=gpu_lle.error(gpu_lle.device);
    if(error)stop(error,0xfd000000u);
    gpu_lle.origin=dispatch_monotonic();
    gpu_lle.field=0;
    fprintf(stderr,"[GPU LLE] original XDK renderer; maintained NV2A; shared original RAM/PIC/ISR\n");
}
