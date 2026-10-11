/* DMA uses the selected original allocator's actual physical pages. Retain
 * contiguous schedules until controller reset; ordinary image/pool buffers
 * require the driver's live pin range. Arbitrary native VM is not mapped. */
#include "usb_lle.h"
#include "mmio_fault.h"
#include "input_host.h"
static INIT_ONCE usb_once=INIT_ONCE_STATIC_INIT;
static uint8_t usb_dma_leases[256];
static uint64_t usb_isr_count;
static RECOMP_TLS struct OriginalThreadExit {
    jmp_buf target;
    uint32_t status;
    int active;
} original_thread_exit;
static const char *usb_original_callback(uint32_t entry,uint32_t regs[8],int is_dpc,
    unsigned thread_arguments,int *terminate,uint32_t *exit_status)
{
    recomp_func_t routine=recomp_lookup(entry);
#ifdef PILOT_SCHEDULING_LLE
    if(thread_arguments==2 && entry==PILOT_IO_BOOTSTRAP_ENTRY)routine=scheduling_lle_bootstrap;
#if defined(PILOT_D3D_NATIVE) && !defined(PILOT_DISPLAY_LLE)
    if(is_dpc && !thread_arguments && entry==PILOT_IO_GRAPHICS_DPC_ENTRY)
        routine=graphics_original_callback_dpc;
#endif
    /* Original CPU execution does not require a generated discovery body. */
    if(thread_arguments && entry!=PILOT_IO_BOOTSTRAP_ENTRY)routine=NULL;
#endif
    if(!routine && !thread_arguments)return "original kernel callback has no AOT binding";
#ifndef PILOT_SCHEDULING_LLE
    if(!routine)return "original kernel callback has no AOT binding";
#endif
    if(++usb_isr_count<=4)
        fprintf(stderr,"[%s] call=%llu entry=%08x irql=%u\n",thread_arguments?"KERNEL LLE THREAD":is_dpc?"KERNEL LLE DPC":"USB LLE ISR",
                (unsigned long long)usb_isr_count,entry,pilot_io_device_irql());
    uint32_t saved[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi,
        next_kernel_ordinal,g_icall_saved_esp,g_icall_dispatch_form,g_seh_ebp};
    double fp[8];memcpy(fp,g_fp_stack,sizeof(fp));
    int fp_top=g_fp_top,fp_cmp=g_fp_cmp,df=g_df;
    uint16_t fp_control=g_fp_control_word,fp_cc=g_fp_cc;
    RecompXmm xmm[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    RecompMmx mm[]={g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7};
    uint32_t saved_fs=g_fs_base;
    uint8_t irql=g_fs_base?MEM8(g_fs_base+0x24):0;
    int saved_if=cpu_interrupt_flag,saved_shadow=cpu_interrupt_shadow;
    int saved_in_dpc=dispatch_in_dpc;
    volatile uint32_t *saved_checkpoint=g_pilot_suspend_count;
    if(thread_arguments)g_pilot_suspend_count=(volatile uint32_t*)pilot_io_device_thread_checkpoint_request();
    /* The qualified original ISR/DPC dispatchers enable interrupts before
     * calling generated code. Their actual IRQL remains authoritative. */
    cpu_interrupt_flag=1;cpu_interrupt_shadow=0;pilot_io_device_set_if(1);
    g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_ebx=regs[3];
    g_esp=regs[4];g_ebp=regs[5];g_esi=regs[6];g_edi=regs[7];
    if(thread_arguments)g_fs_base=0x8003da3cu;
    g_df=0; /* The original interrupt thunk executed CLD. */
    if(g_fs_base)MEM8(g_fs_base+0x24)=(uint8_t)pilot_io_device_irql();
    if(is_dpc)dispatch_in_dpc=1;
    ++usb_in_interrupt;
#ifdef PILOT_SCHEDULING_LLE
    if(!thread_arguments)++usb_device_callback_depth;
#endif
    if(thread_arguments) {
        if(original_thread_exit.active)stop("nested original native thread continuation",entry);
        original_thread_exit.active=1;
        if(!setjmp(original_thread_exit.target)) {
#ifdef PILOT_SCHEDULING_LLE
            if(entry!=PILOT_IO_BOOTSTRAP_ENTRY)scheduling_lle_execute(entry);
            else
#endif
            routine();
        }
        else {*terminate=1;*exit_status=original_thread_exit.status;}
        original_thread_exit.active=0;
    } else routine();
#ifdef PILOT_SCHEDULING_LLE
    if(!thread_arguments)--usb_device_callback_depth;
#endif
    --usb_in_interrupt;
    dispatch_in_dpc=saved_in_dpc;
    if(usb_isr_count<=4)fprintf(stderr,"[MACHINE CALLBACK RETURN] entry=%08x dpc=%d context=%08x\n",entry,is_dpc,MEM32(regs[4]+8));
    if(!(thread_arguments && *terminate) && g_esp!=regs[4]+(thread_arguments?(thread_arguments+1)*4:(is_dpc?20:12)))
        stop("original callback stdcall imbalance",entry);
    if(thread_arguments && !*terminate && (g_ebx!=regs[3] || g_ebp!=regs[5] || g_esi!=regs[6] || g_edi!=regs[7]))
        stop("original thread callback preserved register changed",entry);
    if(!thread_arguments && (g_ebx!=regs[3] || g_esi!=regs[6] || g_edi!=regs[7]))
        stop("original interrupt callback preserved register changed",entry);
    regs[0]=g_eax;regs[1]=g_ecx;regs[2]=g_edx;regs[3]=g_ebx;
    regs[4]=g_esp;regs[6]=g_esi;regs[7]=g_edi;
    /* Generated ISR/DPC functions keep EBP local. g_ebp publishes their
     * active frame for nested generated calls, and LEAVE restores only the
     * local value. Returning that publication would corrupt the original
     * dispatcher's preserved queue pointer. Its incoming EBP remains the
     * nonvolatile result. Actual CPU thread continuations transport EBP. */
    if(thread_arguments)regs[5]=g_ebp;
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
    g_fs_base=saved_fs;
    g_pilot_suspend_count=saved_checkpoint;
    if(g_fs_base)MEM8(g_fs_base+0x24)=irql;
    cpu_interrupt_flag=saved_if;cpu_interrupt_shadow=saved_shadow;pilot_io_device_set_if(saved_if);
    return NULL;
}
static const char *usb_original_isr(uint32_t entry,uint32_t regs[8])
{return usb_original_callback(entry,regs,0,0,NULL,NULL);}
static const char *usb_original_dpc(uint32_t entry,uint32_t regs[8])
{return usb_original_callback(entry,regs,1,0,NULL,NULL);}
static const char *usb_original_thread(uint32_t entry,uint32_t regs[8],unsigned arguments,
    int *terminate,uint32_t *status)
{return usb_original_callback(entry,regs,0,arguments,terminate,status);}
static void original_thread_kernel_dispatch(void)
{
    if(next_kernel_ordinal==258) {
        original_thread_exit.status=MEM32(g_esp+4);
        longjmp(original_thread_exit.target,1);
    }
    uint32_t regs[]={g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi};
    const char *error=pilot_io_device_thread_call(next_kernel_ordinal,regs);
    if(error)stop(error,next_kernel_ordinal);
    g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_ebx=regs[3];
    g_esp=regs[4];g_ebp=regs[5];g_esi=regs[6];g_edi=regs[7];
}
#ifdef PILOT_SCHEDULING_LLE
/* Compiled ISR/DPC bodies run while the original CPU callback is suspended.
 * Their kernel calls reuse the qualified C service floor, never the native
 * dispatcher or the managed-thread continuation transport. Only completed
 * original returns advance the compiled caller's real stdcall frame. */
static const char *original_callback_kernel_call(unsigned ordinal)
{
    if(!usb_device_callback_depth)return "original kernel service requires an ISR/DPC callback";
    unsigned count;
    switch(ordinal) {
    case 103:case 121:case 129:case 160:case 161:count=0;break;
    case 97:case 128:case 137:case 138:case 151:count=1;break;
    case 113:count=2;break;
    case 107:case 108:case 119:case 145:count=3;break;
    case 149:count=4;break;
    case 150:count=5;break;
    default:return "original ISR/DPC kernel service requires qualification";
    }
    if((uint64_t)g_esp+(count+1)*4>0xffffffffull)
        return "original ISR/DPC kernel call frame wraps";
    uint32_t args[5]={0},result=g_eax;
    for(unsigned i=0;i<count;++i)args[i]=MEM32(g_esp+4+i*4);
    if(ordinal==145 && args[2])
        return "original ISR/DPC Wait=TRUE requires scheduler handoff";
    if(ordinal==160 || ordinal==161)args[0]=g_ecx;
    const char *error=pilot_io_device_lle(ordinal,memory,args,&result);
    if(error)return error;
    g_eax=result;g_esp+=(count+1)*4;
    if(g_fs_base)MEM8(g_fs_base+0x24)=(uint8_t)pilot_io_device_irql();
    return NULL;
}
#endif
static void usb_poll(void)
{
    if(usb_in_interrupt)return;
    AcquireSRWLockExclusive(&scheduler_lock);
    InterlockedExchange(&scheduler_usb_pending,0);scheduler_notify_locked();
    ReleaseSRWLockExclusive(&scheduler_lock);
    const char *error=pilot_usb_lle_tick();
    if(error)stop(error,1);
#ifdef PILOT_DISPLAY_LLE
    display_lle_tick();
#endif
#ifdef PILOT_GPU_LLE
    gpu_lle_tick();
#endif
    if(!cpu_interrupt_flag || cpu_interrupt_shadow)return;
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
    const char *error=pilot_usb_lle_host_bind(pilot_input_host_state,pilot_input_host_rumble,pilot_input_host_caps);
    if(error)stop(error,0);
    HANDLE worker=CreateThread(NULL,0,usb_clock_worker,NULL,0,NULL);
    if(!worker)stop("USB LLE clock worker creation failed",GetLastError());
    CloseHandle(worker);
}
static int usb_dma_transfer(uint32_t pa,void *buffer,size_t bytes,int write)
{
    if(!bytes || (uint64_t)pa+bytes>64u*1024*1024)return 0;
#ifdef PILOT_MEMORY_LLE
    /* The original allocator/publication owner decides before native VM
     * bookkeeping. A native component may hold vm_lock across its actual
     * original service while a compiled OHCI callback consumes these pages. */
    if(!buffer)return 0;
    int result=pilot_io_device_contiguous_dma(pa,buffer,(uint32_t)bytes,write,PILOT_IO_DMA_USB);
    if(!result)result=pilot_io_device_pinned_dma(pa,buffer,(uint32_t)bytes,write);
    return result;
#else
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
    ReleaseSRWLockExclusive(&vm_lock);
    if(!allocation) {
        result=pilot_io_device_contiguous_dma(pa,buffer,(uint32_t)bytes,write,PILOT_IO_DMA_USB);
        if(!result)result=pilot_io_device_pinned_dma(pa,buffer,(uint32_t)bytes,write);
    }
    return result;
#endif
}
static int usb_dma_read(uint32_t pa,void *out,size_t size){return usb_dma_transfer(pa,out,size,0);}
static int usb_dma_write(uint32_t pa,const void *in,size_t size){return usb_dma_transfer(pa,(void*)in,size,1);}
static void usb_dma_release(void)
{
#ifdef PILOT_MEMORY_LLE
    /* The real controller reset releases this owner's retained schedules;
     * native vm_lock and its separate allocation records are not consulted. */
    pilot_io_device_dma_release(PILOT_IO_DMA_USB);
#else
    AcquireSRWLockExclusive(&vm_lock);
    for(unsigned i=0;i<256;++i)if(usb_dma_leases[i]) {
        if(!physical_allocations[i].bytes || !physical_allocations[i].borrowers)
            stop("USB DMA ownership lost",i);
        --physical_allocations[i].borrowers;usb_dma_leases[i]=0;
    }
    ReleaseSRWLockExclusive(&vm_lock);
    pilot_io_device_dma_release(PILOT_IO_DMA_USB);
#endif
}
static const char *usb_original_mmio(uint32_t address,unsigned size,int write,uint32_t *value)
{
    if(size!=4 || (address&3))return "USB LLE original CPU requires aligned DWORD MMIO";
    return pilot_usb_lle_access(address,write,value);
}
static BOOL CALLBACK usb_start(PINIT_ONCE once,void *parameter,void **context)
{
    (void)once;(void)parameter;(void)context;
    const char *error=pilot_usb_lle_bind(usb_dma_read,usb_dma_write,usb_dma_release);
    if(error)stop(error,0);
    pilot_io_device_isr_bind(usb_original_isr);
    pilot_io_device_dpc_bind(usb_original_dpc);
    pilot_io_device_thread_bind(usb_original_thread);
    error=pilot_io_device_initialize(memory);
    if(!error)error=pilot_io_device_mmio_bind_dma(0xfed00000u,4096,usb_original_mmio,PILOT_IO_DMA_USB);
    if(!error)error=pilot_io_device_work_bind_dma(pilot_usb_lle_tick,PILOT_IO_DMA_USB);
    if(error)stop(error,0xfed00000u);
    fprintf(stderr,"[USB LLE] shared OHCI/XID/PIC; original interrupt dispatch; owned DMA\n");
    return TRUE;
}
static const char *usb_access(uint32_t address,int write,uint32_t *value)
{
    if(!InitOnceExecuteOnce(&usb_once,usb_start,NULL,NULL))return "USB LLE initialization failed";
    return pilot_usb_lle_access(address,write,value);
}
