/* Build-selected display floor. Native rendering produces actual pixels;
 * owned contiguous scanout pages and PFIFO packets transport them to the
 * maintained NV2A. Original ISR/DPC/vblank/wait bodies own guest completion.
 * The single guest CPU owns all device calls, DMA and original driver state. */
#include "pilot_display_bindings.h"
#include "display_lle_config.h"
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
    int (*scanout)(void*,uint8_t*,uint32_t,uint32_t*,uint32_t*);
    uint32_t allocations[4],bytes[4],put;
    unsigned width,height,pitch,hz;
    uint64_t origin,frame_number;
    uint64_t next_capture;
    int active;
    unsigned waiters;
    volatile LONG completed;
    struct {unsigned char *pixels;uint32_t swap,interval;} queue[2];
    unsigned count;
} display_lle;
static SRWLOCK display_lle_queue_lock=SRWLOCK_INIT;

static void display_lle_check(void)
{
    const char *error=display_lle.error(display_lle.device);
    if(error)stop(error,0xfd000000u);
}
static int display_lle_dma(uint32_t address,void *buffer,size_t bytes,int write)
{
    for(unsigned i=0;i<4;++i) {
        uint32_t base=display_lle.allocations[i]&0x0fffffffu;
        if(display_lle.allocations[i] && address>=base &&
           (uint64_t)address+bytes<=(uint64_t)base+display_lle.bytes[i]) {
            unsigned char *at=memory+display_lle.allocations[i]+(address-base);
            if(write)memcpy(at,buffer,bytes);else memcpy(buffer,at,bytes);
            return 1;
        }
    }
    return 0;
}
static int display_lle_dma_read(uint32_t a,void *b,size_t n){return display_lle_dma(a,b,n,0);}
static int display_lle_dma_write(uint32_t a,const void *b,size_t n){return display_lle_dma(a,(void*)b,n,1);}
static void display_lle_write(uint32_t address,unsigned size,uint32_t value)
{
    display_lle.write(display_lle.device,address,size,value);display_lle_check();
}
static uint32_t display_lle_read(uint32_t address,unsigned size)
{
    uint32_t value=display_lle.read(display_lle.device,address,size);display_lle_check();return value;
}
static const char *display_lle_access(uint32_t address,unsigned size,int write,uint32_t *value)
{
    if(!display_lle.active)return "NV2A access without the selected display device";
    if(write)display_lle.write(display_lle.device,address,size,*value);
    else *value=display_lle.read(display_lle.device,address,size);
    static unsigned accesses;
    if(accesses++<12)fprintf(stderr,"[DISPLAY LLE MMIO] %s%u %08x=%08x\n",write?"W":"R",size,address,*value);
    return display_lle.error(display_lle.device);
}
static uint8_t display_lle_field(void)
{
    if(!display_lle.active)stop("encoder field read without active display",0x80c0);
    /* Bounded field input tied to elapsed display edges, stable on repeated
     * reads. Authentic encoder phase/polarity is not qualified by this floor. */
    return (uint8_t)((display_lle.frame_number&1u)<<5);
}
static uint32_t display_lle_kernel(unsigned ordinal,const uint32_t *args)
{
    uint32_t result=0;const char *error=pilot_io_device_lle(ordinal,memory,args,&result);
    if(error)stop(error,ordinal);return result;
}
static void display_lle_library_identity(void)
{
    FILE *file=_wfopen(PILOT_DISPLAY_LIBRARY,L"rb");
    if(!file || _fseeki64(file,0,SEEK_END))stop("display DLL identity read failed",0);
    __int64 length=_ftelli64(file);
    if(length<=0 || length>512*1024*1024)stop("display DLL identity size",0);
    rewind(file);unsigned char *bytes=malloc((size_t)length),hash[32];char hex[65];
    if(!bytes || fread(bytes,1,(size_t)length,file)!=(size_t)length || fclose(file))
        stop("display DLL identity bytes",0);
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,bytes,(ULONG)length,hash,32)<0)
        stop("display DLL SHA256 failed",0);
    free(bytes);for(unsigned i=0;i<32;++i)sprintf(hex+i*2,"%02x",hash[i]);
    fprintf(stderr,"[DISPLAY LLE LIBRARY] sha256=%s\n",hex);
}
static void display_lle_open(unsigned width,unsigned height,unsigned hz,uint64_t origin)
{
    if(display_lle.device || !width || width>1920 || !height || height>1080 || hz!=60)
        stop("unqualified display creation",hz);
    if(!InitOnceExecuteOnce(&usb_once,usb_start,NULL,NULL))stop("shared machine bus initialization failed",0);
    for(uint64_t address=0xfd000000u;address<0xfe000000u;) {
        MEMORY_BASIC_INFORMATION info;
        if(!VirtualQuery(memory+address,&info,sizeof(info)) || info.State!=MEM_RESERVE)
            stop("native storage collides with NV2A MMIO aperture",(uint32_t)address);
        address=(unsigned char*)info.BaseAddress-memory+info.RegionSize;
    }
    if(!display_lle.library) {
        display_lle_library_identity();
        DLL_DIRECTORY_COOKIE dependency=AddDllDirectory(PILOT_DISPLAY_DEPENDENCIES);
        if(!dependency)stop("display DLL dependency directory unavailable",GetLastError());
        display_lle.library=LoadLibraryExW(PILOT_DISPLAY_LIBRARY,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS|LOAD_LIBRARY_SEARCH_USER_DIRS);
        RemoveDllDirectory(dependency);
        if(!display_lle.library)stop("maintained NV2A reference DLL unavailable",GetLastError());
#define DISPLAY_LOAD(member) do {*(FARPROC*)&display_lle.member=GetProcAddress(display_lle.library,"xlle_nv2a_reference_" #member);if(!display_lle.member)stop("display DLL ABI missing: " #member,0);} while(0)
        DISPLAY_LOAD(create);DISPLAY_LOAD(destroy);DISPLAY_LOAD(error);DISPLAY_LOAD(bus);
        DISPLAY_LOAD(read);DISPLAY_LOAD(write);DISPLAY_LOAD(frame);DISPLAY_LOAD(scanout);
#undef DISPLAY_LOAD
    }
    display_lle.device=display_lle.create();
    if(!display_lle.device)stop("NV2A reference allocation failed",0);
    display_lle.bus(display_lle.device,display_lle_dma_read,display_lle_dma_write,pilot_usb_pic_raise);
    display_lle_check();
    display_lle.width=width;display_lle.height=height;display_lle.pitch=(width*4+63)&~63u;
    display_lle.hz=hz;display_lle.origin=origin;display_lle.frame_number=0;
    display_lle.completed=0;display_lle.count=0;
    for(unsigned i=0;i<4;++i) {
        display_lle.bytes[i]=i==3?4096:display_lle.pitch*height;
        display_lle.allocations[i]=physical_allocate(display_lle.bytes[i],0,0x03ffffffu,4096,PAGE_READWRITE);
        if(!display_lle.allocations[i])stop("display contiguous allocation failed",i);
        ++physical_find(display_lle.allocations[i],display_lle.bytes[i])->borrowers;
    }
    uint32_t packet[]={0x40000,1,0xc0120,0,0,3};
    memcpy(memory+display_lle.allocations[3],packet,sizeof(packet));
    const uint32_t setup[][2]={{0x700008,1},{0x70000c,0x80010020},
        {0x700200,0x97},{0x700180,0x3d},{0x700184,4095},
        {0x700188,display_lle.allocations[3]&0x0fffffffu},
        {0x002504,1},{0x003204,0x100},{0x00322c,0x18},{0x003244,0},
        {0x003220,1},{0x003250,1},{0x003200,1},{0x800040,sizeof(packet)}};
    for(unsigned i=0;i<sizeof(setup)/sizeof(setup[0]);++i)display_lle_write(0xfd000000u+setup[i][0],4,setup[i][1]);
    display_lle.put=sizeof(packet);
    unsigned units=display_lle.pitch/8;
    const uint32_t crtc[][2]={{0x13,units&255},{0x19,(units>>3)&0xe0},{0x25,(units>>6)&0x20},{0x28,3}};
    for(unsigned i=0;i<4;++i) {
        display_lle_write(0xfd6013d4u,1,crtc[i][0]);display_lle_write(0xfd6013d5u,1,crtc[i][1]);
    }
    display_lle_write(0xfd680800u,4,height-1);display_lle_write(0xfd680820u,4,width-1);
    display_lle_write(0xfd600800u,4,display_lle.allocations[0]&0x0fffffffu);
    memset(memory+PILOT_DISPLAY_MINIPORT,0,PILOT_DISPLAY_MINIPORT_BYTES);
    MEM32(PILOT_DISPLAY_MINIPORT)=0xfd000000u;
    uint32_t event[]={PILOT_DISPLAY_MINIPORT+PILOT_DISPLAY_EVENT_OFFSET,0,0};
    display_lle_kernel(108,event);
    uint32_t dpc[]={PILOT_DISPLAY_MINIPORT+PILOT_DISPLAY_DPC_OBJECT_OFFSET,PILOT_DISPLAY_DPC,PILOT_DISPLAY_MINIPORT};
    display_lle_kernel(107,dpc);
    uint32_t irq[]={PILOT_DISPLAY_MINIPORT+PILOT_DISPLAY_INTERRUPT_OFFSET,PILOT_DISPLAY_ISR,PILOT_DISPLAY_MINIPORT,0x33,23,0,0};
    display_lle_kernel(109,irq);
    if(!(display_lle_kernel(98,irq)&255))stop("display IRQ3 connection failed",irq[0]);
    MEM32(PILOT_DISPLAY_MINIPORT+PILOT_DISPLAY_CONNECTED_OFFSET)=1;
    MEM32(PILOT_DISPLAY_MINIPORT+PILOT_DISPLAY_PMC_ENABLE_OFFSET)=1;
    display_lle_write(0xfd400140u,4,1u<<20);
    display_lle_write(0xfd600140u,4,1);
    display_lle_write(0xfd000140u,4,1);
    display_lle.active=1;
    fprintf(stderr,"[DISPLAY LLE] original IRQ3/ISR/DPC/wait/callback; maintained NV2A; owned %ux%u scanout\n",width,height);
}
static uint32_t display_lle_completed(void)
{
    return (uint32_t)InterlockedCompareExchange(&display_lle.completed,0,0);
}
/* Called with the renderer lock, with the guest CPU released. GPU readback
 * waits for real work. It never writes guest pages concurrently with the CPU. */
static void display_lle_present(uint32_t swap,uint32_t interval)
{
    unsigned bytes=display_lle.width*display_lle.height*4;
    unsigned char *pixels=malloc(bytes);
    if(!pixels || pilot_gfx_readback(pixels,bytes)<0)stop("LLE display GPU readback failed",swap);
    AcquireSRWLockExclusive(&display_lle_queue_lock);
    if(!display_lle.active || display_lle.count==2)stop("LLE display queue ownership",swap);
    unsigned at=display_lle.count++;
    display_lle.queue[at].pixels=pixels;display_lle.queue[at].swap=swap;
    display_lle.queue[at].interval=interval;
    ReleaseSRWLockExclusive(&display_lle_queue_lock);
}
static void display_lle_tick(void)
{
    if(!display_lle.active)return;
    uint32_t completed=MEM32(PILOT_DISPLAY_MINIPORT+PILOT_DISPLAY_COMPLETED_OFFSET);
    if(completed!=display_lle_completed()) {
        InterlockedExchange(&display_lle.completed,(LONG)completed);
        if(completed<5 || completed%600==0)fprintf(stderr,"[DISPLAY LLE RETIRE] swap=%u frame=%u scanout=%08x\n",completed,
            MEM32(PILOT_DISPLAY_MINIPORT+PILOT_DISPLAY_FRAME_OFFSET),display_lle_read(0xfd600800u,4));
        const char *capture=getenv("XLLE_PILOT_PRESENT_CAPTURE");
        uint64_t now=dispatch_monotonic();
        if(capture && *capture && now>=display_lle.next_capture) {
            unsigned bytes=display_lle.width*display_lle.height*4;
            uint8_t *rgba=malloc(bytes);uint32_t width=0,height=0;
            if(!rgba || !display_lle.scanout(display_lle.device,rgba,bytes,&width,&height)) {
                display_lle_check();stop("LLE scanout capture failed",completed);
            }
            if(width!=display_lle.width || height!=display_lle.height)stop("LLE scanout dimensions differ",completed);
            char path[1024];
            if(snprintf(path,sizeof(path),"%s/scanout-%u.ppm",capture,completed)>=(int)sizeof(path))stop("LLE scanout capture path",0);
            FILE *file=fopen(path,"wb");
            if(!file || fprintf(file,"P6\n%u %u\n255\n",width,height)<0)stop("LLE scanout capture open",0);
            for(unsigned i=0;i<width*height;++i)if(fwrite(rgba+i*4,3,1,file)!=1)stop("LLE scanout capture write",0);
            free(rgba);if(fclose(file))stop("LLE scanout capture close",0);
            display_lle.next_capture=now+100000000;
            fprintf(stderr,"[LLE SCANOUT CAPTURE] swap=%u file=%s\n",completed,path);
        }
    }
    AcquireSRWLockExclusive(&display_lle_queue_lock);
    if(display_lle.count && !(display_lle_read(0xfd400100u,4)&(1u<<20)) &&
       display_lle_read(0xfd800044u,4)==display_lle.put) {
        uint32_t swap=display_lle.queue[0].swap,slot=swap%3;
        unsigned char *pixels=display_lle.queue[0].pixels,*target=memory+display_lle.allocations[slot];
        for(unsigned y=0;y<display_lle.height;++y)for(unsigned x=0;x<display_lle.width;++x) {
            unsigned char *s=pixels+(y*display_lle.width+x)*4,*d=target+y*display_lle.pitch+x*4;
            d[0]=s[2];d[1]=s[1];d[2]=s[0];d[3]=s[3];
        }
        uint32_t address=display_lle.allocations[slot]&0x0fffffffu;
        uint32_t interval=display_lle.queue[0].interval==0x80000000u?8:1;
        uint32_t packet[]={(1u<<18)|0x100,((address|interval)<<5)|1};
        /* Reuse only fully consumed command storage, with no pending trap. */
        display_lle_write(0xfd800044u,4,0);
        memcpy(memory+display_lle.allocations[3],packet,sizeof(packet));
        display_lle.put=sizeof(packet);display_lle_write(0xfd800040u,4,display_lle.put);
        free(pixels);--display_lle.count;
        memmove(display_lle.queue,display_lle.queue+1,display_lle.count*sizeof(display_lle.queue[0]));
    }
    ReleaseSRWLockExclusive(&display_lle_queue_lock);
    uint64_t elapsed=dispatch_monotonic()-display_lle.origin;
    uint64_t frame=elapsed/10000000*display_lle.hz+(elapsed%10000000)*display_lle.hz/10000000;
    if(frame!=display_lle.frame_number) {
        display_lle.frame_number=frame;display_lle.frame(display_lle.device,frame);display_lle_check();
    }
}
static void display_lle_close(void)
{
    if(!display_lle.active)return;
    if(display_lle.waiters)stop("display close with active original waits",display_lle.waiters);
    if(display_lle.count || MEM32(PILOT_DISPLAY_MINIPORT+PILOT_DISPLAY_SUBMITTED_OFFSET)!=
       MEM32(PILOT_DISPLAY_MINIPORT+PILOT_DISPLAY_COMPLETED_OFFSET))stop("display close with pending flips",0);
    display_lle_write(0xfd000140u,4,0);display_lle_write(0xfd400140u,4,0);display_lle_write(0xfd600140u,4,0);
    uint32_t interrupt=PILOT_DISPLAY_MINIPORT+PILOT_DISPLAY_INTERRUPT_OFFSET;
    if(!(display_lle_kernel(100,&interrupt)&255))stop("display disconnect failed",interrupt);
    dispatch_queue(PILOT_DISPLAY_MINIPORT+PILOT_DISPLAY_DPC_OBJECT_OFFSET,0,0,1);
    display_lle.active=0;display_lle.destroy(display_lle.device);display_lle.device=NULL;
    for(unsigned i=0;i<4;++i) {
        --physical_find(display_lle.allocations[i],display_lle.bytes[i])->borrowers;
        physical_free(display_lle.allocations[i]);display_lle.allocations[i]=0;
    }
}
static void display_lle_reset(void)
{
    /* This title changes presentation interval without changing scanout mode.
     * The original buffer-setup helper's packet resets only hardware flip
     * indices; its monotonic submitted/completed counters and callback survive.
     * native_display_reference.py independently executes that original helper. */
    if(display_lle.count || display_lle_completed()!=graphics_swap_submitted ||
       display_lle_read(0xfd800044u,4)!=display_lle.put ||
       display_lle_read(0xfd400100u,4))stop("display reset before queue drain",0);
    uint32_t packet[]={0xc0120,0,1,2};
    display_lle_write(0xfd800044u,4,0);
    memcpy(memory+display_lle.allocations[3],packet,sizeof(packet));
    display_lle.put=sizeof(packet);display_lle_write(0xfd800040u,4,display_lle.put);
    fprintf(stderr,"[DISPLAY LLE RESET] interval=%08x completed=%u callback=%08x\n",graphics_parameters[12],
        display_lle_completed(),MEM32(PILOT_DISPLAY_MINIPORT+PILOT_DISPLAY_CALLBACK_OFFSET));
}
