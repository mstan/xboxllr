/* Guest DMA bytes remain owned by the selected original contiguous allocator. Retain an
 * allocation once hardware references it, until a real controller reset drops
 * its schedules. This bounded bridge does not alias arbitrary guest VM pages. */
#include "usb_lle.h"
#include "mmio_fault.h"
static INIT_ONCE usb_once=INIT_ONCE_STATIC_INIT;
static uint8_t usb_dma_leases[256];
static uint64_t usb_isr_count;
static const char *usb_original_isr(uint32_t entry,uint32_t regs[8])
{
    recomp_func_t routine=recomp_lookup(entry);
    if(!routine)return "original USB ISR has no AOT binding";
    if(++usb_isr_count<=4)
        fprintf(stderr,"[USB LLE ISR] call=%llu entry=%08x irql=%u\n",
                (unsigned long long)usb_isr_count,entry,pilot_io_device_irql());
    uint32_t saved[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi,
        next_kernel_ordinal,g_icall_saved_esp,g_icall_dispatch_form,g_seh_ebp};
    double fp[8];memcpy(fp,g_fp_stack,sizeof(fp));
    int fp_top=g_fp_top,fp_cmp=g_fp_cmp,df=g_df;
    uint16_t fp_control=g_fp_control_word,fp_cc=g_fp_cc;
    RecompXmm xmm[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    RecompMmx mm[]={g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7};
    uint8_t irql=g_fs_base?MEM8(g_fs_base+0x24):0;
    g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_ebx=regs[3];
    g_esp=regs[4];g_ebp=regs[5];g_esi=regs[6];g_edi=regs[7];
    g_df=0; /* The original interrupt thunk executed CLD. */
    if(g_fs_base)MEM8(g_fs_base+0x24)=(uint8_t)pilot_io_device_irql();
    ++usb_in_interrupt;routine();--usb_in_interrupt;
    if(g_esp!=regs[4]+12)stop("original USB ISR stdcall imbalance",entry);
    regs[0]=g_eax;regs[1]=g_ecx;regs[2]=g_edx;regs[3]=g_ebx;
    regs[4]=g_esp;regs[5]=g_ebp;regs[6]=g_esi;regs[7]=g_edi;
    g_eax=saved[0];g_ecx=saved[1];g_edx=saved[2];g_ebx=saved[3];
    g_esp=saved[4];g_ebp=saved[5];g_esi=saved[6];g_edi=saved[7];
    next_kernel_ordinal=saved[8];g_icall_saved_esp=saved[9];
    g_icall_dispatch_form=saved[10];g_seh_ebp=saved[11];
    memcpy(g_fp_stack,fp,sizeof(fp));g_fp_top=fp_top;g_fp_cmp=fp_cmp;g_df=df;
    g_fp_control_word=fp_control;g_fp_cc=fp_cc;
    g_xmm0=xmm[0];g_xmm1=xmm[1];g_xmm2=xmm[2];g_xmm3=xmm[3];
    g_xmm4=xmm[4];g_xmm5=xmm[5];g_xmm6=xmm[6];g_xmm7=xmm[7];
    g_mm0=mm[0];g_mm1=mm[1];g_mm2=mm[2];g_mm3=mm[3];
    g_mm4=mm[4];g_mm5=mm[5];g_mm6=mm[6];g_mm7=mm[7];
    if(g_fs_base)MEM8(g_fs_base+0x24)=irql;
    return NULL;
}
static void usb_poll(void)
{
    if(usb_in_interrupt)return;
    AcquireSRWLockExclusive(&scheduler_lock);
    InterlockedExchange(&scheduler_usb_pending,0);scheduler_notify_locked();
    ReleaseSRWLockExclusive(&scheduler_lock);
    const char *error=pilot_usb_lle_tick();
    if(error)stop(error,1);
    int delivered=0;
    /* The device may assert while a caller is at high IRQL. Let the actual
     * kernel gate defer that interrupt and the actual lowering code resume it. */
    if(pilot_usb_pic_vector(0)<0)return;
    error=pilot_io_device_irq(memory,&delivered);
    if(error)stop(error,1);
}
static DWORD WINAPI usb_clock_worker(void *unused)
{
    (void)unused;
    /* This worker never races guest DMA. It requests a guest checkpoint or
     * takes the otherwise idle single CPU while all guest threads wait. */
    const uint32_t base=PILOT_IO_CLOCK_BASE,top=base+PILOT_IO_CLOCK_BYTES;
    commit(base,PILOT_IO_CLOCK_BYTES);g_fs_base=base;
    MEM32(base)=0xffffffffu;MEM32(base+4)=top;MEM32(base+8)=base+0x2000;
    MEM32(base+0x18)=base;MEM32(base+0x1c)=base;MEM32(base+0x20)=base+0x28;
    MEM32(base+0x28)=base+0x1000;g_esp=top;
    MEM32(base+0x1000+0x12c)=GetCurrentThreadId();
    for(;;) {
        Sleep(1);
        if(scheduler_usb_tick_request()){usb_poll();scheduler_retire();}
    }
}
static void usb_clock_start(void)
{
    if(!scheduler_enabled)stop("USB LLE requires the single guest CPU scheduler",0);
    HANDLE worker=CreateThread(NULL,0,usb_clock_worker,NULL,0,NULL);
    if(!worker)stop("USB LLE clock worker creation failed",GetLastError());
    CloseHandle(worker);
}
static int usb_dma_transfer(uint32_t pa,void *buffer,size_t bytes,int write)
{
    if(!bytes || (uint64_t)pa+bytes>64u*1024*1024)return 0;
    AcquireSRWLockExclusive(&vm_lock);
    struct PhysicalAllocation *allocation=physical_find(pa|0x80000000u,(uint32_t)bytes);
    int result=0;
    if(allocation) {
        uint32_t first=pa/4096,last=(uint32_t)((pa+bytes+4095)/4096);
        for(uint32_t page=first;page<last;++page)
            if(write && (physical_protection[page]&6)!=PAGE_READWRITE)goto done;
        unsigned slot=(unsigned)(allocation-physical_allocations);
        if(!usb_dma_leases[slot]){usb_dma_leases[slot]=1;++allocation->borrowers;}
        if(write)memcpy(memory+0x80000000ull+pa,buffer,bytes);
        else memcpy(buffer,memory+0x80000000ull+pa,bytes);
        result=1;
    }
done:
    ReleaseSRWLockExclusive(&vm_lock);return result;
}
static int usb_dma_read(uint32_t pa,void *out,size_t size){return usb_dma_transfer(pa,out,size,0);}
static int usb_dma_write(uint32_t pa,const void *in,size_t size){return usb_dma_transfer(pa,(void*)in,size,1);}
static void usb_dma_release(void)
{
    AcquireSRWLockExclusive(&vm_lock);
    for(unsigned i=0;i<256;++i)if(usb_dma_leases[i]) {
        if(!physical_allocations[i].bytes || !physical_allocations[i].borrowers)
            stop("USB DMA ownership lost",i);
        --physical_allocations[i].borrowers;usb_dma_leases[i]=0;
    }
    ReleaseSRWLockExclusive(&vm_lock);
}
static BOOL CALLBACK usb_start(PINIT_ONCE once,void *parameter,void **context)
{
    (void)once;(void)parameter;(void)context;
    const char *error=pilot_usb_lle_bind(usb_dma_read,usb_dma_write,usb_dma_release);
    if(error)stop(error,0);
    pilot_io_device_isr_bind(usb_original_isr);
    fprintf(stderr,"[USB LLE] shared OHCI/XID/PIC; original interrupt dispatch; owned DMA\n");
    return TRUE;
}
static const char *usb_access(uint32_t address,int write,uint32_t *value)
{
    if(!InitOnceExecuteOnce(&usb_once,usb_start,NULL,NULL))return "USB LLE initialization failed";
    return pilot_usb_lle_access(address,write,value);
}
