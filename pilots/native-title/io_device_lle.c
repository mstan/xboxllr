/* Execute original device/object/allocator code over a private initialized
 * kernel capture. Published pages share one backing with native callers and
 * asynchronous devices, including every interpreter alias of their PFN.
 * This is one selected implementation,
 * not duplicate HLE/LLE execution. The USB build also executes connected IRQ
 * gates, calling the registered original ISR through its existing AOT body. */
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unicorn/unicorn.h>
#ifdef PILOT_UNICORN_IRQ_BOUNDARY
#include <unicorn_irq_boundary.h>
#endif
#include "io_device_lle.h"
#include "io_device_config.h"
#include "guest_layout.h"
#ifdef PILOT_USB_LLE
#include "usb_lle.h"
#include "calendar_kernel.h"
#endif

#define RAM_BYTES (64u*1024*1024)
#define ROM_BYTES (256u*1024)
#define SCRATCH PILOT_IO_BRIDGE_BASE
#define STACK (SCRATCH+0x8000u)
#define RETURN_PC (SCRATCH+0x9000u)
#define MAX_PAGES 4096
#define MAX_PUBLISHED (RAM_BYTES/4096)
static SRWLOCK lock=SRWLOCK_INIT;
static uc_engine *cpu;
static unsigned char *ram,*guest_memory;
static unsigned char *rom;
static unsigned char *shared_pages[RAM_BYTES/4096];
static const char *failure;
static struct {uint32_t va,pa;unsigned char *backing;} mapped[MAX_PAGES];
static struct {uint32_t va,pa,pins;} published[MAX_PUBLISHED];
static unsigned mapped_count,published_count;
/* Exact indices into the owned publication ledger. Device allocations add
 * hundreds of pages; scanning all of them for each cached alias or USB DMA
 * packet turns original interrupt service into quadratic host bookkeeping.
 * Page-table/ownership/pin checks remain in place. Zero means unpublished. */
static uint16_t publication_by_va[1u<<20],publication_by_pa[RAM_BYTES/4096];
static unsigned publication_slot(uint32_t page)
{
    unsigned entry=publication_by_va[page>>12];
    return entry?entry-1:published_count;
}
static struct {uint32_t base,bytes,contiguous,dma_leases;} owners[MAX_PUBLISHED];
static unsigned owner_count;
static int gpu_bootstrap_published;
static struct {uint32_t base,bytes,count;} pin_ranges[MAX_PUBLISHED];
static uint32_t image_start,image_bytes;
/* Admission window supplied only by a successful original VM commit. It does
 * not fabricate translations: publish_range still walks the actual PTEs. */
static uint32_t vm_publication_base,vm_publication_bytes;
/* A host-access lease belongs to one outstanding original VM call, not a VAD
 * or another allocator. Original-thread calls may have independent leases. */
struct VmAccessPage {uint32_t va;DWORD protection;};
struct VmCallerAccess {struct VmAccessPage *pages;unsigned count;int active;};
static void vm_caller_prepare(unsigned ordinal,const uint32_t *args,struct VmCallerAccess *access);
static void vm_caller_complete(unsigned ordinal,const uint32_t *args,uint32_t status,struct VmCallerAccess *access);
static void storage_caller_prepare(unsigned ordinal,const uint32_t *args);
static void contiguous_caller_prepare(unsigned ordinal,const uint32_t *args);
static void contiguous_caller_complete(unsigned ordinal,const uint32_t *args,uint32_t result);
static struct {uint32_t base,bytes;} stacks[64];
static unsigned stack_count;
static uint32_t interrupt_objects[16],pending_callback;
static PilotIoIsr isr_callback,dpc_callback;
static int pending_dpc;
static uint32_t dpc_stack_top;
static struct {uint16_t first,last;PilotIoPort callback;} device_ports[8];
static PilotIoMmio device_mmio;
static PilotIoDeviceWork device_work;
/* The outer CPU owner keeps its nonrecursive lock throughout device work.
 * Only this same thread can borrow it for synchronous validated RAM transfers.
 * Other host threads still acquire the lock normally. */
static __declspec(thread) unsigned device_bus_borrow;
static uint32_t device_mmio_first,device_mmio_bytes;
static volatile LONG current_irql;
static volatile LONG caller_if=1;
static unsigned execution_depth;
static PilotIoClock clock_provider;
static void *clock_context;
#ifdef PILOT_USB_LLE
static int device_irq_pending;
static int clock_initializing;
static unsigned clock_initialization_writes;
static uint32_t clock_initialization_ports[3],clock_initialization_values[3];
#endif
static const int general_regs[]={UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,
    UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI};
static int thread_cpu_import_fault(uc_engine *engine,uint64_t address);
static int thread_cpu_npx_owner(void);

static unsigned char *physical_backing(uint32_t pa)
{
    unsigned char *shared=shared_pages[pa/4096];
    return shared?shared+(pa&4095):ram+pa;
}
static uint32_t ram_word(uint32_t pa)
{
    uint32_t value=0;
    if(pa>RAM_BYTES-4){failure="device LLE page table outside RAM";return 0;}
    memcpy(&value,physical_backing(pa),4);return value;
}
static int physical_translation(uint32_t va,uint32_t *pa)
{
    uint32_t pde=ram_word(0xf000u+(va>>22)*4);
    if(!(pde&1))return 0;
    if(pde&128)*pa=(pde&0xffc00000u)+(va&0x3fffff);
    else {
        uint32_t pte=ram_word((pde&0xfffff000u)+((va>>12)&1023)*4);
        if(!(pte&1))return 0;
        *pa=(pte&0xfffff000u)+(va&4095);
    }
    return 1;
}
/* Allocator/publication/DMA ownership remains strictly RAM. Bus-backed ROM
 * is a separate immutable owner, never an entry in the writable RAM ledger. */
static int physical(uint32_t va,uint32_t *pa)
{
    return physical_translation(va,pa) && *pa<RAM_BYTES;
}
static uint32_t kernel_word(uint32_t va)
{
    uint32_t pa;
    if(!physical(va,&pa)){failure="device LLE unmapped kernel metadata";return 0;}
    return ram_word(pa);
}
static int guest_page(uint32_t va)
{
    MEMORY_BASIC_INFORMATION info;
    if(VirtualQuery(guest_memory+va,&info,sizeof(info))!=sizeof(info))return 0;
    return info.State==MEM_COMMIT && !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD));
}
static bool invalid_memory(uc_engine *engine,uc_mem_type type,uint64_t address,
                           int size,int64_t value,void *data)
{
    (void)type;(void)size;(void)value;(void)data;
    if(address>UINT32_MAX){failure="device LLE non-x86 address";return false;}
    if(type==UC_MEM_FETCH_UNMAPPED && thread_cpu_import_fault(engine,address))return false;
    uint32_t page=(uint32_t)address&~4095u,pa;
    unsigned char *backing;uint32_t protection=UC_PROT_ALL;
    unsigned publication=publication_slot(page);
    if(publication==published_count && (page<0x80000000u || (page>=0xe0000000u && page<0xe4000000u) ||
       (page>=PILOT_KERNEL_DATA_BASE && page<PILOT_KERNEL_DATA_BASE+PILOT_KERNEL_DATA_BYTES))) {
        if(!guest_page(page)) {
            uint32_t pc=0;uc_reg_read(engine,UC_X86_REG_EIP,&pc);
            fprintf(stderr,"[IO CALLER MEMORY GUARD] access=%u address=%08x bytes=%d pc=%08x\n",
                type,(uint32_t)address,size,pc);
            failure="device LLE invalid caller memory";return false;
        }
        backing=guest_memory+page;pa=UINT32_MAX;
    } else {
        if(!physical_translation(page,&pa) || (pa>=RAM_BYTES && (!rom || pa<0xff000000u))) {
            uint32_t pc=0;uc_reg_read(engine,UC_X86_REG_EIP,&pc);
            fprintf(stderr,"[IO DEVICE LLE MEMORY GUARD] access=%u address=%08x bytes=%d pc=%08x thread=%08x\n",
                type,(uint32_t)address,size,pc,kernel_word(0x8003da64u));
            failure="device LLE unmodelled memory or MMIO";return false;
        }
        if(pa>=0xff000000u) {
            if(type==UC_MEM_WRITE_UNMAPPED || type==UC_MEM_WRITE_PROT ||
               type==UC_MEM_FETCH_UNMAPPED || type==UC_MEM_FETCH_PROT) {
                failure="device LLE immutable ROM permits qualified reads only";return false;
            }
            backing=rom+((pa-0xff000000u)&(ROM_BYTES-1));protection=UC_PROT_READ;
        } else backing=physical_backing(pa);
    }
    if(mapped_count==MAX_PAGES){failure="device LLE mapping capacity";return false;}
    mapped[mapped_count].va=page;mapped[mapped_count].pa=pa;
    mapped[mapped_count++].backing=backing;
    uc_err error=uc_mem_map_ptr(engine,page,4096,protection,backing);
    if(error){failure=uc_strerror(error);return false;}
    return true;
}
static void forbidden(uc_engine *engine,const char *reason)
{failure=reason;uc_emu_stop(engine);}
static uint64_t device_mmio_read(uc_engine *engine,uint64_t offset,unsigned size,void *data)
{
    (void)data;uint32_t value=0;
    if(!device_mmio || (size!=1 && size!=2 && size!=4) || offset+size>device_mmio_bytes) {
        forbidden(engine,"device LLE unqualified MMIO read");return 0;
    }
    if(device_bus_borrow){forbidden(engine,"device LLE nested MMIO callback");return 0;}
    device_bus_borrow=1;
    const char *error=device_mmio(device_mmio_first+(uint32_t)offset,size,0,&value);
    device_bus_borrow=0;
    if(error)forbidden(engine,error);
    return value;
}
static void device_mmio_write(uc_engine *engine,uint64_t offset,unsigned size,uint64_t value,void *data)
{
    (void)data;uint32_t word=(uint32_t)value;
    if(!device_mmio || (size!=1 && size!=2 && size!=4) || offset+size>device_mmio_bytes) {
        forbidden(engine,"device LLE unqualified MMIO write");return;
    }
    if(device_bus_borrow){forbidden(engine,"device LLE nested MMIO callback");return;}
    device_bus_borrow=1;
    const char *error=device_mmio(device_mmio_first+(uint32_t)offset,size,1,&word);
    device_bus_borrow=0;
    if(error)forbidden(engine,error);
}
#ifdef PILOT_FILES_LLE
#include "io_storage_lle.inc"
#endif
static uint32_t port_in(uc_engine *engine,uint32_t port,int size,void *data)
{
#ifdef PILOT_USB_LLE
    uint32_t value=0;(void)data;
    for(unsigned i=0;i<8;++i)if(device_ports[i].callback && port>=device_ports[i].first && port<=device_ports[i].last) {
        const char *error=device_ports[i].callback((uint16_t)port,size,0,&value);
        if(error)forbidden(engine,error);
        return value;
    }
#ifdef PILOT_FILES_LLE
    if(storage.instance && storage_owns_port(port)) {
        if(!storage.port(storage.instance,port,size,0,&value))forbidden(engine,storage.error(storage.instance));
        return value;
    }
#endif
    const char *error=(port==0x80cf || (port>=0x70 && port<=0x73) || (port>=0xc000 && port<=0xc00f))?pilot_usb_board_port(port,size,0,&value):
        pilot_usb_pic_port(port,size,0,&value);
    if(error) {
        uint32_t pc=0;uc_reg_read(engine,UC_X86_REG_EIP,&pc);
        fprintf(stderr,"[IO DEVICE PORT] read port=%04x size=%d pc=%08x\n",port,size,pc);
        forbidden(engine,error);
    }
    return value;
#else
    (void)port;(void)size;(void)data;
    forbidden(engine,"device LLE unmodelled port read");
    return 0; /* Callback ABI only: failure prevents returning to the title. */
#endif
}
static void port_out(uc_engine *engine,uint32_t port,int size,uint32_t value,void *data)
{
#ifdef PILOT_USB_LLE
    if(clock_initializing) {
        if(size!=1 || (port!=0x40 && port!=0x43) || clock_initialization_writes>=3) {
            forbidden(engine,"original PIT initialization reached another device");return;
        }
        clock_initialization_ports[clock_initialization_writes]=port;
        clock_initialization_values[clock_initialization_writes++]=value;
    }
    for(unsigned i=0;i<8;++i)if(device_ports[i].callback && port>=device_ports[i].first && port<=device_ports[i].last) {
        const char *error=device_ports[i].callback((uint16_t)port,size,1,&value);
        if(error)forbidden(engine,error);
        return;
    }
#ifdef PILOT_FILES_LLE
    if(storage.instance && storage_owns_port(port)) {
        if(!storage.port(storage.instance,port,size,1,&value))forbidden(engine,storage.error(storage.instance));
        return;
    }
#endif
    (void)data;const char *error=(port==0x80cf || (port>=0x70 && port<=0x73) || (port>=0xc000 && port<=0xc00f))?pilot_usb_board_port(port,size,1,&value):
        pilot_usb_pic_port(port,size,1,&value);
    if(error) {
        uint32_t pc=0;uc_reg_read(engine,UC_X86_REG_EIP,&pc);
        fprintf(stderr,"[IO DEVICE PORT] write port=%04x size=%d value=%08x pc=%08x\n",port,size,value,pc);
        forbidden(engine,error);
    }
#else
    (void)port;(void)size;(void)value;(void)data;forbidden(engine,"device LLE unmodelled port write");
#endif
}
static uc_err write_interrupt_frame(uc_engine *engine,uint32_t sp,const uint32_t frame[3])
{
    /* Native execution can touch a newly published original stack before
     * Unicorn has recreated its invalidated alias. uc_mem_write does not run
     * invalid-memory hooks, so materialize those actual aliases explicitly. */
    if((uint64_t)sp+12>0x100000000ull){failure="device LLE interrupt frame wraps address space";return UC_ERR_ARG;}
    for(uint64_t page=sp&~4095u;page<(uint64_t)sp+12;page+=4096) {
        if(page>=SCRATCH && page<SCRATCH+PILOT_IO_BRIDGE_BYTES)continue;
        unsigned i;for(i=0;i<mapped_count && mapped[i].va!=page;++i){}
        if(i==mapped_count && !invalid_memory(engine,UC_MEM_WRITE_UNMAPPED,page,4,0,NULL))return UC_ERR_WRITE_UNMAPPED;
    }
    return uc_mem_write(engine,sp,frame,12);
}
static void interrupt(uc_engine *engine,uint32_t number,void *data)
{
    (void)data;
    if(number==0x2d) {
        /* The captured boot worker's real DbgPrint uses INT2D followed by
         * INT3. Its original IDT handler advances the saved PC past INT3 and
         * dispatches the debugger service. Do not fabricate its return code
         * or skip either instruction in the host. Other debugger commands
         * remain outside this bounded print route. */
        uint32_t pc=0,command=0;
        uc_err e=uc_reg_read(engine,UC_X86_REG_EIP,&pc);
        if(!e)e=uc_reg_read(engine,UC_X86_REG_EAX,&command);
        uint32_t gate=0x8003dcc0u+number*8,low=kernel_word(gate),high=kernel_word(gate+4);
        uint32_t entry=(low&65535)|(high&0xffff0000u);
        if(!e && pc==0x80027975u && command==1 && entry==0x8001e85cu &&
           (low>>16)==8 && (high&65535)==0xee00) {
            uint32_t sp,frame[3],flags;
            e=uc_reg_read(engine,UC_X86_REG_ESP,&sp);
            frame[0]=pc;
            if(!e)e=uc_reg_read(engine,UC_X86_REG_CS,frame+1);
            if(!e)e=uc_reg_read(engine,UC_X86_REG_EFLAGS,frame+2);
            sp-=12;flags=frame[2]&~(0x100u|0x200u|0x4000u|0x10000u|0x20000u);
            if(!e)e=write_interrupt_frame(engine,sp,frame);
            if(!e)e=uc_reg_write(engine,UC_X86_REG_ESP,&sp);
            if(!e)e=uc_reg_write(engine,UC_X86_REG_EFLAGS,&flags);
            if(!e)e=uc_reg_write(engine,UC_X86_REG_EIP,&entry);
            if(e)forbidden(engine,uc_strerror(e));
            return;
        }
    }
    if(number==7 && thread_cpu_npx_owner()) {
        /* The reviewed capture's actual #NM gate owns lazy NPX switching.
         * Retry the faulting PC through its real FXSAVE/FXRSTOR handler, not
         * a host FP initializer or a guessed per-thread register copy. */
        uint32_t gate=0x8003dcc0u+7*8,low=kernel_word(gate),high=kernel_word(gate+4);
        uint32_t entry=(low&65535)|(high&0xffff0000u),cr0=0;
        uc_err e=uc_reg_read(engine,UC_X86_REG_CR0,&cr0);
        if(!e && entry==0x8001e950u && (low>>16)==8 && (high&65535)==0x8e00 && (cr0&12)) {
            uint32_t sp,frame[3],flags;
            e=uc_reg_read(engine,UC_X86_REG_ESP,&sp);
            if(!e)e=uc_reg_read(engine,UC_X86_REG_EIP,frame);
            if(!e)e=uc_reg_read(engine,UC_X86_REG_CS,frame+1);
            if(!e)e=uc_reg_read(engine,UC_X86_REG_EFLAGS,frame+2);
            sp-=12;flags=frame[2]&~(0x100u|0x200u|0x4000u|0x10000u|0x20000u);
            if(!e)e=write_interrupt_frame(engine,sp,frame);
            if(!e)e=uc_reg_write(engine,UC_X86_REG_ESP,&sp);
            if(!e)e=uc_reg_write(engine,UC_X86_REG_EFLAGS,&flags);
            if(!e)e=uc_reg_write(engine,UC_X86_REG_EIP,&entry);
            if(e)forbidden(engine,uc_strerror(e));
            return;
        }
    }
#ifdef PILOT_USB_LLE
    if(number>=0x20 && number<0x40) {
        uint32_t gate=0x8003dcc0u+number*8,low=kernel_word(gate),high=kernel_word(gate+4);
        if((low>>16)==8 && (high&65535)==0x8e00) {
            uint32_t sp,frame[3],flags,entry=(low&65535)|(high&0xffff0000u);
            uc_err e=uc_reg_read(engine,UC_X86_REG_ESP,&sp);
            if(!e)e=uc_reg_read(engine,UC_X86_REG_EIP,frame);
            if(!e)e=uc_reg_read(engine,UC_X86_REG_CS,frame+1);
            if(!e)e=uc_reg_read(engine,UC_X86_REG_EFLAGS,frame+2);
            sp-=12;flags=frame[2]&~(0x100u|0x200u|0x4000u|0x10000u|0x20000u);
            if(!e)e=write_interrupt_frame(engine,sp,frame);
            if(!e)e=uc_reg_write(engine,UC_X86_REG_ESP,&sp);
            if(!e)e=uc_reg_write(engine,UC_X86_REG_EFLAGS,&flags);
            if(!e)e=uc_reg_write(engine,UC_X86_REG_EIP,&entry);
            if(e)forbidden(engine,uc_strerror(e));
            return;
        }
    }
#endif
    /* The original lower-IRQL routine replays a deferred hardware vector
     * through INT n. Deliver through its actual IDT; do not acknowledge the
     * PIC a second time. Unicorn delegates INT to this hook. */
    if(number>=0x30 && number<0x40) {
        uint32_t gate=0x8003de40u+(number-0x30)*8;
        uint32_t low=kernel_word(gate),high=kernel_word(gate+4);
        uint32_t entry=(low&65535)|(high&0xffff0000u),object=interrupt_objects[number-0x30];
        if(object && entry==object+0x18 && (low>>16)==8 && (high&65535)==0x8e00) {
            uint32_t sp,frame[3],flags;
            uc_err e=uc_reg_read(engine,UC_X86_REG_ESP,&sp);
            if(!e)e=uc_reg_read(engine,UC_X86_REG_EIP,frame);
            if(!e)e=uc_reg_read(engine,UC_X86_REG_CS,frame+1);
            if(!e)e=uc_reg_read(engine,UC_X86_REG_EFLAGS,frame+2);
            sp-=12;flags=frame[2]&~(0x100u|0x200u|0x4000u|0x10000u|0x20000u);
            if(!e)e=write_interrupt_frame(engine,sp,frame);
            if(!e)e=uc_reg_write(engine,UC_X86_REG_ESP,&sp);
            if(!e)e=uc_reg_write(engine,UC_X86_REG_EFLAGS,&flags);
            if(!e)e=uc_reg_write(engine,UC_X86_REG_EIP,&entry);
            if(e)forbidden(engine,uc_strerror(e));
            return;
        }
    }
    uint32_t pc=0;uc_reg_read(engine,UC_X86_REG_EIP,&pc);
    fprintf(stderr,"[IO DEVICE LLE INTERRUPT] vector=%u pc=%08x\n",number,pc);
    forbidden(engine,"device LLE unmodelled interrupt");
}
#include "io_thread_lle.inc"
static void code_block(uc_engine *engine,uint64_t address,uint32_t size,void *data)
{
    (void)size;(void)data;
#ifdef PILOT_USB_LLE
    if(pilot_usb_pic_vector(0)>=0) {
        uint32_t flags=0;uc_reg_read(engine,UC_X86_REG_EFLAGS,&flags);
        if(flags&0x200) {
#ifdef PILOT_UNICORN_IRQ_BOUNDARY
            if(!xlle_unicorn_irq_inhibited(engine)){device_irq_pending=1;uc_emu_stop(engine);return;}
#else
            forbidden(engine,"device LLE IRQ requires the engine's actual inhibition state");return;
#endif
        }
    }
#endif
    if(thread_cpu_admit(engine,(uint32_t)address))return;
    if(thread_cpu_import_fault(engine,address))return;
    if(address>=0x80010000u && address<0x80080000u)return;
    for(unsigned i=0;i<16;++i)if(interrupt_objects[i]) {
        uint32_t object=interrupt_objects[i],routine=0;
        if(address>=object+0x18 && address<object+0x70)return;
        if(uc_mem_read(engine,object,&routine,4)==UC_ERR_OK && address==routine && isr_callback) {
            pending_callback=routine;uc_emu_stop(engine);return;
        }
    }
    if(dpc_callback && (kernel_word(0x8003da60u)&255)==2 && kernel_word(0x8003da94u)) {
        uint32_t sp,frame[5],object[7],flags;
        if(uc_reg_read(engine,UC_X86_REG_ESP,&sp)==UC_ERR_OK &&
           uc_mem_read(engine,sp,frame,sizeof(frame))==UC_ERR_OK &&
           uc_mem_read(engine,frame[1],object,sizeof(object))==UC_ERR_OK &&
           uc_reg_read(engine,UC_X86_REG_EFLAGS,&flags)==UC_ERR_OK &&
           (frame[0]==0x8001f68fu || frame[0]==0x8001e461u) && (object[0]&0xffffff)==0x13 &&
           object[3]==address && object[4]==frame[2] && (flags&0x600)==0x200) {
            /* Hash-bound timer-expiry and ordinary DPC call sites. Only the
             * original dispatchers invoke callbacks; no poll fabricates one. */
            pending_callback=(uint32_t)address;pending_dpc=1;uc_emu_stop(engine);return;
        }
    }
    if(thread_admit(engine,(uint32_t)address))return;
    uint32_t sp=0,frame[5]={0};uc_reg_read(engine,UC_X86_REG_ESP,&sp);
    uc_mem_read(engine,sp,frame,sizeof(frame));
    fprintf(stderr,"[IO CALLBACK GUARD] pc=%08x sp=%08x return=%08x object=%08x irql=%u active=%08x\n",
            (uint32_t)address,sp,frame[0],frame[1],kernel_word(0x8003da60u)&255,kernel_word(0x8003da94u));
    forbidden(engine,"device LLE driver callback requires real native interop");
}

#define IO_UC(call) do {uc_err e=(call);if(e){failure=uc_strerror(e);return;}} while(0)
static void initialize(unsigned char *guest)
{
    guest_memory=guest;
    FILE *file=fopen(PILOT_IO_DEVICE_SNAPSHOT,"rb");
    if(!file){failure="device LLE owned RAM capture missing";return;}
    ram=VirtualAlloc(NULL,RAM_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!ram){fclose(file);failure="device LLE RAM allocation failed";return;}
    size_t read=fread(ram,1,RAM_BYTES,file);int extra=fgetc(file);fclose(file);
    if(read!=RAM_BYTES || extra!=EOF){failure="device LLE capture size mismatch";return;}
    unsigned char hash[32];char hex[65];
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,ram,RAM_BYTES,hash,32)<0) {
        failure="device LLE capture hash failed";return;
    }
    for(unsigned i=0;i<32;++i)sprintf(hex+i*2,"%02x",hash[i]);
    if(strcmp(hex,PILOT_IO_DEVICE_SNAPSHOT_SHA)){failure="device LLE unreviewed RAM capture";return;}
#ifdef PILOT_IO_DEVICE_ROM
    file=fopen(PILOT_IO_DEVICE_ROM,"rb");
    if(!file){failure="device LLE owned ROM image missing";return;}
    rom=VirtualAlloc(NULL,ROM_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!rom){fclose(file);failure="device LLE ROM allocation failed";return;}
    read=fread(rom,1,ROM_BYTES,file);extra=fgetc(file);fclose(file);
    if(read!=ROM_BYTES || extra!=EOF){failure="device LLE ROM profile size mismatch";return;}
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,rom,ROM_BYTES,hash,32)<0) {
        failure="device LLE ROM hash failed";return;
    }
    char rom_hex[65];
    for(unsigned i=0;i<32;++i)sprintf(rom_hex+i*2,"%02x",hash[i]);
    if(strcmp(rom_hex,PILOT_IO_DEVICE_ROM_SHA)){failure="device LLE ROM changed since configuration";return;}
    DWORD previous;
    if(!VirtualProtect(rom,ROM_BYTES,PAGE_READONLY,&previous)) {
        failure="device LLE immutable ROM protection failed";return;
    }
#endif
    if(guest_page(SCRATCH)){failure="device LLE private stack collides with caller";return;}
    IO_UC(uc_open(UC_ARCH_X86,UC_MODE_32,&cpu));
    if(!VirtualAlloc(guest+SCRATCH,0x10000,MEM_COMMIT,PAGE_READWRITE)) {
        failure="device LLE shared interrupt stack allocation failed";return;
    }
    IO_UC(uc_mem_map_ptr(cpu,SCRATCH,0x10000,UC_PROT_ALL,guest+SCRATCH));
    /* Flat ring-0 segments, matching the captured IDT's selector 8. This
     * architectural frame glue follows our existing UnicornFloor injector. */
    /* FS uses the captured kernel's real PCR for SEH and allocator locks.
     * Its current thread/exception chain are preserved by synchronous calls. */
    const uint32_t pcr=0x8003da3cu;
    const uint64_t fs=0x00c093000000ffffull|((uint64_t)(pcr&0xffffff)<<16)|((uint64_t)(pcr>>24)<<56);
    const uint64_t gdt[]={0,0x00cf9b000000ffffull,0x00cf93000000ffffull,fs};
    IO_UC(uc_mem_write(cpu,SCRATCH+0xf000,gdt,sizeof(gdt)));
    uc_x86_mmr gdtr={0};gdtr.base=SCRATCH+0xf000;gdtr.limit=sizeof(gdt)-1;
    IO_UC(uc_reg_write(cpu,UC_X86_REG_GDTR,&gdtr));
    uint32_t selector=8;IO_UC(uc_reg_write(cpu,UC_X86_REG_CS,&selector));
    selector=16;IO_UC(uc_reg_write(cpu,UC_X86_REG_SS,&selector));
    IO_UC(uc_reg_write(cpu,UC_X86_REG_DS,&selector));
    IO_UC(uc_reg_write(cpu,UC_X86_REG_ES,&selector));
    selector=24;IO_UC(uc_reg_write(cpu,UC_X86_REG_FS,&selector));
#ifdef PILOT_USB_LLE
    failure=pilot_usb_pic_bootstrap((uint16_t)kernel_word(0x8003d88cu),
                                    (uint16_t)kernel_word(0x8003d890u));
    if(failure)return;
#endif
    uc_hook hook;
    IO_UC(uc_hook_add(cpu,&hook,UC_HOOK_MEM_INVALID,invalid_memory,NULL,1,0));
    IO_UC(uc_hook_add(cpu,&hook,UC_HOOK_INSN,port_in,NULL,1,0,UC_X86_INS_IN));
    IO_UC(uc_hook_add(cpu,&hook,UC_HOOK_INSN,port_out,NULL,1,0,UC_X86_INS_OUT));
    IO_UC(uc_hook_add(cpu,&hook,UC_HOOK_INTR,interrupt,NULL,1,0));
    IO_UC(uc_hook_add(cpu,&hook,UC_HOOK_BLOCK,code_block,NULL,1,0));
    fprintf(stderr,"[IO DEVICE LLE] original kernel; initialized capture %s; no replaced callees\n",hex);
}
/* Publish only actual object allocations, leaving the captured namespace and
 * allocator private. The native HLE pool uses a disjoint VA range in this build.
 * Refresh cached interpreter mappings after original page-table changes. */
static void refresh_mappings(void)
{
    for(unsigned i=0;i<mapped_count;) {
        uint32_t pa;
        int stale;
        if(mapped[i].pa==UINT32_MAX) {
            unsigned j=publication_slot(mapped[i].va);
            stale=j<published_count || !guest_page(mapped[i].va);
        } else if(mapped[i].pa>=0xff000000u) {
            stale=!rom || !physical_translation(mapped[i].va,&pa) || pa!=mapped[i].pa ||
                  rom+((pa-0xff000000u)&(ROM_BYTES-1))!=mapped[i].backing;
        } else stale=!physical(mapped[i].va,&pa) || pa!=mapped[i].pa ||
                     physical_backing(pa)!=mapped[i].backing;
        if(stale) {
            IO_UC(uc_mem_unmap(cpu,mapped[i].va,4096));
            mapped[i]=mapped[--mapped_count];
        } else ++i;
    }
}
static void release_publication(unsigned i)
{
    uint32_t va=published[i].va,pa=published[i].pa;
    /* A freed pool page may remain in the original allocator's free lists.
     * Retain those real bytes when its last native owner goes away. */
    DWORD previous;
    if(!VirtualProtect(guest_memory+va,4096,PAGE_READWRITE,&previous)) {
        failure="device LLE publication retirement protection failed";return;
    }
    memcpy(ram+pa,guest_memory+va,4096);
    shared_pages[pa/4096]=NULL;
    publication_by_va[va/4096]=0;publication_by_pa[pa/4096]=0;
    published[i]=published[--published_count];
    if(i<published_count) {
        publication_by_va[published[i].va/4096]=(uint16_t)(i+1);
        publication_by_pa[published[i].pa/4096]=(uint16_t)(i+1);
    }
    refresh_mappings();if(failure)return;
    if(!VirtualFree(guest_memory+va,4096,MEM_DECOMMIT))
        failure="device LLE publication release failed";
}
static void check_publications(int before_call)
{
    for(unsigned i=0;i<published_count;) {
        uint32_t pa,va=published[i].va;
        if(publication_by_va[va/4096]!=i+1 || publication_by_pa[published[i].pa/4096]!=i+1) {
            failure="device LLE publication index mismatch";return;
        }
        if(!physical(va,&pa)) {
            if(before_call){failure="device LLE published mapping changed between calls";return;}
            release_publication(i);if(failure)return;continue;
        }
        if(pa!=published[i].pa) {
            if(before_call){failure="device LLE published alias changed between calls";return;}
            /* Original MmAllocateContiguousMemory can relocate an unpinned
             * virtual page into a free PFN. The original copy/PTE update has
             * already happened. Preserve both resulting physical contents,
             * then publish the new PFN behind the same caller VA. Do not pin
             * every page just to prevent the original allocator doing this. */
            uint32_t old_pa=published[i].pa;
            if(published[i].pins || shared_pages[pa/4096]) {
                failure="device LLE relocation into pinned or aliased storage";return;
            }
            DWORD protection,previous;
            if(!VirtualProtect(guest_memory+va,4096,PAGE_READWRITE,&protection)) {
                failure="device LLE relocation access failed";return;
            }
            memcpy(ram+old_pa,guest_memory+va,4096);
            memcpy(guest_memory+va,ram+pa,4096);
            shared_pages[old_pa/4096]=NULL;publication_by_pa[old_pa/4096]=0;
            shared_pages[pa/4096]=guest_memory+va;publication_by_pa[pa/4096]=(uint16_t)(i+1);
            published[i].pa=pa;
            if(!VirtualProtect(guest_memory+va,4096,protection,&previous)) {
                failure="device LLE relocation protection restore failed";return;
            }
            refresh_mappings();if(failure)return;
        }
        ++i;
    }
}
static int caller_owned(uint32_t object,uint32_t bytes)
{
    uint64_t end=(uint64_t)object+bytes;
    if(image_bytes && object>=image_start && end<=(uint64_t)image_start+image_bytes)return 1;
    for(unsigned i=0;i<stack_count;++i)
        if(object>=stacks[i].base && end<=(uint64_t)stacks[i].base+stacks[i].bytes)return 1;
    return 0;
}
static void publish_range(uint32_t object,uint32_t bytes)
{
    int image=caller_owned(object,bytes);
    int vm=vm_publication_bytes && object>=vm_publication_base &&
           (uint64_t)object+bytes<=(uint64_t)vm_publication_base+vm_publication_bytes;
    if(!(image || vm || (object>=0xd0000000u && (uint64_t)object+bytes<=0xd4000000ull) ||
         (object>=0x80000000u && (uint64_t)object+bytes<=0x84000000ull))) {
        failure="device LLE unexpected allocation namespace";return;
    }
    uint32_t end=(object+bytes+4095)&~4095u;
    for(uint32_t va=object&~4095u;va<end;va+=4096) {
        unsigned i=publication_slot(va);
        if(i<published_count)continue;
        uint32_t pa;
        if(published_count==MAX_PUBLISHED || !physical(va,&pa)) {
            failure="device LLE invalid publication";return;
        }
        if(shared_pages[pa/4096]) {
            unsigned other=publication_by_pa[pa/4096];uint32_t actual=UINT32_MAX;
            if(other)physical(published[other-1].va,&actual);
            fprintf(stderr,"[IO PAGE COLLISION] new=%08x pa=%08x old=%08x old-current-pa=%08x\n",
                    va,pa,other?published[other-1].va:0,actual);
            failure="device LLE duplicate physical publication";return;
        }
        if(image) {
            if(!guest_page(va)){failure="device LLE caller page is not resident";return;}
        } else if(guest_page(va)){failure="device LLE allocation collides with native memory";return;}
        else if(!VirtualAlloc(guest_memory+va,4096,MEM_COMMIT,PAGE_READWRITE)) {
            failure="device LLE publication allocation failed";return;
        }
        if(!image)memcpy(guest_memory+va,ram+pa,4096);
        shared_pages[pa/4096]=guest_memory+va;
        published[published_count].va=va;published[published_count].pins=0;published[published_count].pa=pa;
        publication_by_va[va/4096]=(uint16_t)(published_count+1);
        publication_by_pa[pa/4096]=(uint16_t)(published_count+1);++published_count;
    }
    if(!image && !vm) {
        if(owner_count==MAX_PUBLISHED){failure="device LLE publication ownership capacity";return;}
        owners[owner_count].base=object;owners[owner_count].bytes=bytes;
        owners[owner_count].contiguous=owners[owner_count].dma_leases=0;++owner_count;
    }
    refresh_mappings();
}
static void retire_owner(uint32_t object)
{
    unsigned owner;for(owner=0;owner<owner_count && owners[owner].base!=object;++owner){}
    if(owner==owner_count){failure="device LLE release of unowned publication";return;}
    owners[owner]=owners[--owner_count];
    /* The original allocator may keep a pool page for its private objects.
     * Its native publication has a separate lifetime: retain it only while a
     * caller-owned allocation shares the page. Do not charge stale views as
     * still-live guest allocations, or expose freed native objects forever. */
    for(unsigned i=0;i<published_count;) {
        uint32_t va=published[i].va;
        if(va<0x80000000u){++i;continue;}
        unsigned j;for(j=0;j<owner_count;++j)if(owners[j].base<va+4096ull &&
            (uint64_t)owners[j].base+owners[j].bytes>va)break;
        if(j<owner_count){++i;continue;}
        if(published[i].pins){failure="device LLE retiring pinned publication";return;}
        release_publication(i);if(failure)return;
    }
}
#include "io_thread_transport_lle.inc"
static void pump_original_clock(void)
{
    if(!clock_provider)return;
#ifdef PILOT_USB_LLE
    uint64_t ticks=0;
    const char *error=clock_provider(clock_context,&ticks);
    if(!error)error=pilot_usb_pit_time(ticks);
    if(!error && device_work) {
        if(device_bus_borrow){failure="device LLE nested device work";return;}
        device_bus_borrow=1;error=device_work();device_bus_borrow=0;
    }
    if(error)failure=error;
#else
    failure="original clock requires the maintained PIT/PIC build";
#endif
}
static void run_original(uint32_t entry,size_t instruction_limit)
{
    /* Never re-enter uc_emu_start from a hook. At an actual ISR call, stop,
     * run its generated body over shared bytes, then restore this CPU. A
     * nested original service has its own stack and saved engine context. */
    for(unsigned calls=0;calls<8192;++calls) {
        pending_callback=0;pending_dpc=0;pending_thread=NULL;pending_thread_return=NULL;pending_component=NULL;
        pending_cpu_service=NULL;pending_cpu_birth=NULL;
#ifdef PILOT_USB_LLE
        device_irq_pending=0;
#endif
        pump_original_clock();if(failure)return;
        size_t slice=instruction_limit;
        if(clock_provider) {
            slice=4096;
            while(instruction_limit/slice>4096)slice*=2;
            if(slice>instruction_limit)slice=instruction_limit;
        }
        uc_err error=uc_emu_start(cpu,entry,RETURN_PC,0,slice);
        if(failure)return;
        if(pending_cpu_service && (error==UC_ERR_OK || error==UC_ERR_FETCH_UNMAPPED)) {
            struct OriginalThread *thread=pending_cpu_service;
            thread_cpu_service_prepare(thread,&entry);if(failure)return;
            if(thread->cpu_exit_request)thread_handoff(thread,0,&entry);
            if(failure)return;
            continue;
        }
        if(error){failure=uc_strerror(error);return;}
        if(pending_cpu_birth) {
            thread_cpu_birth_prepare(pending_cpu_birth,&entry);if(failure)return;
            continue;
        }
        if(pending_component) {
            thread_component_prepare(pending_component,&entry);if(failure)return;
            continue;
        }
#ifdef PILOT_USB_LLE
        if(device_irq_pending) {
            int vector=pilot_usb_pic_vector(1);
            if(vector<0){failure="device LLE PIC changed before INTA";return;}
            interrupt(cpu,(uint32_t)vector,NULL);if(failure)return;
            IO_UC(uc_reg_read(cpu,UC_X86_REG_EIP,&entry));continue;
        }
#endif
        if(pending_thread_return) {
            struct OriginalThread *thread=pending_thread_return;
            if(thread->in_cpu && thread->in_kernel) {
                thread_service_complete(thread);if(failure)return;
                thread_after_call(thread->call_ordinal,thread->call_args,thread->call_regs[0]);
                if(failure)return;
                if(thread->in_component){thread_handoff(thread,0,&entry);if(failure)return;continue;}
                entry=thread->call_return;continue;
            }
            if(thread->in_cpu)thread_cpu_complete(thread);
            else if(thread->in_checkpoint)thread_checkpoint_complete(thread);
            else thread_service_complete(thread);
            if(failure)return;
            thread_handoff(thread,0,&entry);if(failure)return;
            continue;
        }
        if(!pending_callback) {
            IO_UC(uc_reg_read(cpu,UC_X86_REG_EIP,&entry));
            if(!clock_provider || entry==RETURN_PC)return;
            /* Exhaustion of a clock slice is not service completion. Resume
             * the actual original context, including its selected idle or
             * worker, after sampling the modeled device clock. */
            continue;
        }
        uint32_t routine=pending_callback,regs[8],return_pc;
        int is_dpc=pending_dpc;
        struct OriginalThread *thread=pending_thread;
        for(unsigned i=0;i<8;++i)IO_UC(uc_reg_read(cpu,general_regs[i],regs+i));
        IO_UC(uc_mem_read(cpu,regs[4],&return_pc,4));
        if(is_dpc) {
            uint32_t top=kernel_word(0x8003da98u),unused;
            /* The captured original DPC stack is three resident pages with
             * guard pages on both sides. Keep the original VA/PTE identity,
             * including escaped argument pointers; never relocate the frame. */
            if((top&4095) || top<0xd0003000u || top>0xd4000000u ||
               regs[4]<top-0x3000u || regs[4]>top-20 ||
               physical(top,&unused) || physical(top-0x4000u,&unused) ||
               (dpc_stack_top && dpc_stack_top!=top)) {
                failure="device LLE unqualified original DPC stack";return;
            }
            if(!dpc_stack_top){publish_range(top-0x3000u,0x3000u);if(failure)return;dpc_stack_top=top;}
        }
        if(thread) {
            uint32_t top=kernel_word(thread->object+0x1c),base=kernel_word(thread->object+0x20);
            uint32_t tls=kernel_word(thread->object+0x28),unused;
            if((top&4095) || (base&4095) || base<0xd0001000u || top>0xd4000000u ||
               top<=base || top-base>0x100000u || regs[4]<base || regs[4]+12ull>top ||
               physical(base-4096,&unused) || (tls && (tls<base || tls>=top))) {
                failure="thread LLE unqualified original stack/TLS";return;
            }
            thread->stack=base;thread->stack_bytes=top-base;
            publish_range(base,top-base);if(failure)return;
            thread->published=1;thread->return_pc=return_pc;
            memcpy(thread->native_regs,regs,sizeof(regs));
            IO_UC(uc_reg_read(cpu,UC_X86_REG_EFLAGS,&thread->initial_flags));
            thread_handoff(thread,1,&entry);if(failure)return;
            continue;
        }
        uc_context *context=NULL;IO_UC(uc_context_alloc(cpu,&context));
        error=uc_context_save(cpu,context);
        if(error){uc_context_free(context);failure=uc_strerror(error);return;}
        check_publications(0);
        InterlockedExchange(&current_irql,(LONG)(kernel_word(0x8003da60u)&255));
        if(!failure) {
            ReleaseSRWLockExclusive(&lock);
            const char *callback_error=(is_dpc?dpc_callback:isr_callback)(routine,regs);
            AcquireSRWLockExclusive(&lock);
            if(callback_error)failure=callback_error;
        }
        error=uc_context_restore(cpu,context);uc_context_free(context);
        if(error){failure=uc_strerror(error);return;}
        if(failure)return;
        check_publications(1);if(failure)return;
        for(unsigned i=0;i<8;++i)IO_UC(uc_reg_write(cpu,general_regs[i],regs+i));
        entry=return_pc;

    }
    failure="device LLE callback budget exhausted";
}
static void execute(unsigned ordinal,const uint32_t *args,uint32_t *result);
static void image_vm(uint32_t base,uint32_t bytes,uint32_t type)
{
    /* Reserve the loader's VA range once; commit real original-kernel pages
     * when that resident range first crosses the device boundary. Host bytes
     * remain authoritative, with original PTE/PFN allocation and pinning. */
    const uint32_t parameters=SCRATCH+0x2000;
    uint32_t input[]={base,bytes},result=0;
    IO_UC(uc_mem_write(cpu,parameters,input,sizeof(input)));
    if(type==MEM_DECOMMIT || type==MEM_RELEASE) {
        uint32_t args[]={parameters,parameters+4,type};execute(199,args,&result);
    } else {
        uint32_t args[]={parameters,0,parameters+4,type,PAGE_READWRITE};execute(184,args,&result);
    }
    if(failure)return;
    if(result){fprintf(stderr,"[IO IMAGE VM] base=%08x bytes=%u type=%x status=%08x\n",base,bytes,type,result);
        failure="device LLE original image mapping failed";return;}
    IO_UC(uc_mem_read(cpu,parameters,input,sizeof(input)));
    if(input[0]!=base || input[1]!=bytes){failure="device LLE image mapping range changed";return;}
    if(type==MEM_COMMIT)publish_range(base,bytes);
}
#include "io_storage_thread_lle.inc"
#include "io_contiguous_thread_lle.inc"
static void execute(unsigned ordinal,const uint32_t *args,uint32_t *result)
{
    unsigned pin_slot=MAX_PUBLISHED;
    if(gpu_bootstrap_published && (ordinal==17 || ordinal==171) && args[0]==0x80000000u) {
        failure="device LLE console bootstrap page is not a caller allocation";return;
    }
    storage_caller_prepare(ordinal,args);if(failure)return;
    if(ordinal!=1 && ordinal!=2 && ordinal!=3 && ordinal!=4 && ordinal!=14 && ordinal!=15 && ordinal!=17 && ordinal!=23 &&
       ordinal!=44 && ordinal!=45 && ordinal!=46 && ordinal!=47 && ordinal!=50 && ordinal!=65 && ordinal!=68 && ordinal!=107 && ordinal!=108 && ordinal!=109 && ordinal!=165 && ordinal!=166 &&
       ordinal!=168 && ordinal!=171 && ordinal!=173 && ordinal!=175 && ordinal!=178 && ordinal!=179 && ordinal!=180 && ordinal!=181 && ordinal!=182 &&
       ordinal!=98 && ordinal!=100 && ordinal!=103 && ordinal!=119 && ordinal!=121 && ordinal!=128 && ordinal!=129 && ordinal!=137 && ordinal!=151 &&
       ordinal!=160 && ordinal!=161 && ordinal!=184 && ordinal!=199 && ordinal!=204 && ordinal!=217
#ifdef PILOT_FILES_LLE
       && ordinal!=24 && ordinal!=35 && ordinal!=36 && ordinal!=37 && ordinal!=67 && ordinal!=69 &&
       ordinal!=97 && ordinal!=113 && ordinal!=125 && ordinal!=148 && ordinal!=149 && ordinal!=150 && ordinal!=159 &&
       ordinal!=186 && ordinal!=187 && ordinal!=189 && ordinal!=190 && ordinal!=196 && ordinal!=198 &&
       ordinal!=200 && ordinal!=202 && ordinal!=203 && ordinal!=207 && ordinal!=209 && ordinal!=211 &&
       ordinal!=215 && ordinal!=218 && ordinal!=219 && ordinal!=224 && ordinal!=225 && ordinal!=226 && ordinal!=231 && ordinal!=233 && ordinal!=234 && ordinal!=236 &&
       ordinal!=246 && ordinal!=250 && ordinal!=254 && ordinal!=255 && ordinal!=256 &&
       ordinal!=277 && ordinal!=279 && ordinal!=289 && ordinal!=290 && ordinal!=291 && ordinal!=294 && ordinal!=301 && ordinal!=316
#endif
       ) {
        failure="device LLE unsupported ordinal";return;
    }
    if(ordinal==173 || ordinal==175) {
        uint32_t start=args[0],bytes=ordinal==175?args[1]:1;
        if(!bytes || (uint64_t)start+bytes>0x100000000ull) {
            failure="device LLE invalid page service range";return;
        }
        if(ordinal==175 && (start<0x80000000u || start>=0xd0000000u)) {
            unsigned empty=MAX_PUBLISHED;
            for(unsigned i=0;i<MAX_PUBLISHED;++i) {
                if(!pin_ranges[i].count){if(empty==MAX_PUBLISHED)empty=i;continue;}
                if(pin_ranges[i].base==start && pin_ranges[i].bytes==bytes){pin_slot=i;break;}
            }
            if(pin_slot==MAX_PUBLISHED && !args[2])pin_slot=empty;
            if(pin_slot==MAX_PUBLISHED){failure="device LLE unmatched pin range or capacity";return;}
        }
        for(uint64_t page=start&~4095u;page<(uint64_t)start+bytes;page+=4096) {
            /* Every caller mapping must be an actual published original-kernel
             * allocation, not unrelated PTEs left in the bootstrap capture. */
            unsigned i=publication_slot((uint32_t)page);
            if(i==published_count && caller_owned((uint32_t)page,4096) && guest_page((uint32_t)page) &&
               !(ordinal==175 && args[2])) {
                image_vm((uint32_t)page,4096,MEM_COMMIT);if(failure)return;
                i=publication_slot((uint32_t)page);
            }
            if(i==published_count){
                fprintf(stderr,"[IO DEVICE LLE PAGE] ordinal=%u start=%08x bytes=%u unlock=%u page=%08x\n",
                        ordinal,start,bytes,ordinal==175?args[2]:0,(uint32_t)page);
                failure="device LLE page service requires owned mapping";return;
            }
            if(ordinal==175 && (page<0x80000000u || page>=0xd0000000u) &&
               (args[2]?published[i].pins==0:published[i].pins==UINT32_MAX)) {
                failure="device LLE page pin imbalance";return;
            }
        }
    }
    contiguous_caller_prepare(ordinal,args);if(failure)return;
    if(ordinal==168 && args[0]!=UINT32_MAX) {
        // This owned kernel capture has an existing GPU reservation. Original
        // MmClaimGpuInstanceMemory releases surplus pages; it does not grow
        // an already reduced reservation. Reject that unqualified lifecycle.
        uint32_t current=kernel_word(PILOT_IO_GPU_RESERVATION_BYTES);
        if(!args[0] || args[0]>current) {
            failure="device LLE GPU instance reservation growth/empty claim";return;
        }
    }
    /* Bound the qualified small device allocation family. Larger allocations
     * require separate original paging/allocator qualification, not guessed NTSTATUS. */
    if(ordinal==65 && args[1]>0x170){failure="device LLE extension exceeds qualified scope";return;}
    check_publications(1);if(failure)return;
    int retire=ordinal==17 || (ordinal==68 && kernel_word(args[0]+4)==0);
    uint32_t pe=kernel_word(0x8001003cu),exports=0x80010000u+kernel_word(0x80010000u+pe+120);
    uint32_t base=kernel_word(exports+16),table=0x80010000u+kernel_word(exports+28);
    uint32_t entry=0x80010000u+kernel_word(table+4*(ordinal-base));
    if(failure)return;
    unsigned count=(ordinal==3 || ordinal==46 || ordinal==65)?6:ordinal==109?7:(ordinal==166 || ordinal==184)?5:(ordinal==2 || ordinal==45 || ordinal==50 || ordinal==204)?4:
                   (ordinal==107 || ordinal==108 || ordinal==119 || ordinal==175 || ordinal==178 || ordinal==182 || ordinal==199)?3:
                   (ordinal==15 || ordinal==44 || ordinal==47 || ordinal==168 || ordinal==217)?2:
                   (ordinal==1 || ordinal==103 || ordinal==121 || ordinal==129 || ordinal==160 || ordinal==161)?0:1;
#ifdef PILOT_FILES_LLE
    switch(ordinal) {
    case 35:case 36:case 125:case 250:count=0;break;
    case 67:case 113:case 148:case 198:case 203:case 209:case 224:case 225:case 231:
    case 289:case 290:count=2;break;
    case 215:case 233:case 246:case 279:count=3;break;
    case 149:case 189:case 234:count=4;break;
    case 24:case 150:case 159:case 211:case 218:case 226:case 254:count=5;break;
    case 202:count=6;break;
    case 219:case 236:count=8;break;
    case 190:count=9;break;
    case 196:case 200:case 207:case 255:count=10;break;
    }
#endif
    if(ordinal==160 || ordinal==161) {
        uint32_t level=args[0]&255;
        if(level>31){failure="device LLE IRQL out of range";return;}
        IO_UC(uc_reg_write(cpu,UC_X86_REG_ECX,&level));
    }
    if(ordinal==250)IO_UC(uc_reg_write(cpu,UC_X86_REG_ECX,args));
    uint32_t words[11]={RETURN_PC},stack=STACK+(execution_depth-1)*0x4000u;
    uint32_t sp=stack,observed,flags=2u|(caller_if?0x200u:0);
    memcpy(words+1,args,count*4);
    IO_UC(uc_mem_write(cpu,stack,words,(count+1)*4));
    IO_UC(uc_reg_write(cpu,UC_X86_REG_ESP,&sp));
    IO_UC(uc_reg_write(cpu,UC_X86_REG_EFLAGS,&flags));
    int saved[]={UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP};
    for(unsigned i=0;i<4;++i){uint32_t value=0x12345678u+i;IO_UC(uc_reg_write(cpu,saved[i],&value));}
    /* Bulk original allocation really zeroes the requested RAM. Unicorn
     * counts REP iterations; a megabyte-scale loader commit can legitimately
     * exceed the small device-call budget. Permit bounded console-sized work
     * without replacing its zeroing or declaring an unfinished call complete. */
    run_original(entry,ordinal==184?64u*1024*1024:ordinal==279?4000000:1000000);
    if(failure)return;
    IO_UC(uc_reg_read(cpu,UC_X86_REG_EIP,&observed));
    if(observed!=RETURN_PC){failure="device LLE instruction limit without return";return;}
    IO_UC(uc_reg_read(cpu,UC_X86_REG_ESP,&observed));
    if(observed!=stack+(count+1)*4){failure="device LLE stdcall stack imbalance";return;}
    IO_UC(uc_reg_read(cpu,UC_X86_REG_EFLAGS,&observed));
    if((observed&0x200)!=(flags&0x200)){failure="device LLE changed caller interrupt flag";return;}
    for(unsigned i=0;i<4;++i) {
        IO_UC(uc_reg_read(cpu,saved[i],&observed));
        if(observed!=0x12345678u+i){failure="device LLE preserved register changed";return;}
    }
    IO_UC(uc_reg_read(cpu,UC_X86_REG_EAX,result));
    refresh_mappings();if(failure)return;
    /* Retire/remap existing publications before exposing a new contiguous
     * allocation that may occupy their former physical pages. */
    check_publications(0);if(failure)return;
    if(ordinal==65 && (int32_t)*result>=0) {
        /* The output slot may itself be in an existing device extension.
         * Read the completed kernel view before publishing changed pages. */
        uint32_t object;IO_UC(uc_mem_read(cpu,args[5],&object,4));
        publish_range(object,0x48+args[1]);if(failure)return;
    }
    contiguous_caller_complete(ordinal,args,*result);if(failure)return;
    if((ordinal==14 || ordinal==15) && *result){publish_range(*result,args[0]);if(failure)return;}
    check_publications(0);
    if(failure)return;
    if(retire){retire_owner(args[0]);if(failure)return;}
    if(ordinal==175) {
        if(pin_slot<MAX_PUBLISHED) {
            if(args[2])--pin_ranges[pin_slot].count;
            else {pin_ranges[pin_slot].base=args[0];pin_ranges[pin_slot].bytes=args[1];++pin_ranges[pin_slot].count;}
        }
        for(unsigned i=0;i<published_count;++i)if((published[i].va<0x80000000u || published[i].va>=0xd0000000u) &&
            published[i].va>=(args[0]&~4095u) && published[i].va<(uint64_t)args[0]+args[1]) {
            if(args[2])--published[i].pins;else ++published[i].pins;
        }
    }
    if(ordinal==109) {
        uint32_t irq=args[3]-0x30;
        if(irq>=16){failure="device LLE interrupt outside PIC scope";return;}
        /* A second initialized object must not replace an active vector. */
        if(!interrupt_objects[irq])interrupt_objects[irq]=args[0];
    }
    if(ordinal==98 && (*result&255)) {
        uint32_t irq;IO_UC(uc_mem_read(cpu,args[0]+8,&irq,4));
        if(irq>=16){failure="device LLE connected IRQ out of scope";return;}
        interrupt_objects[irq]=args[0];
    }
    InterlockedExchange(&current_irql,(LONG)(kernel_word(0x8003da60u)&255));
}

#include "io_thread_lifetime_lle.inc"
static void deliver_irq(int *delivered)
{
#ifdef PILOT_USB_LLE
    int vector=pilot_usb_pic_vector(0);*delivered=0;
    if(vector<0)return;
    if(vector<0x30 || vector>=0x40){failure="device LLE invalid PIC vector";return;}
    uint32_t gate=0x8003de40u+(vector-0x30)*8;
    uint32_t low=kernel_word(gate),high=kernel_word(gate+4);
    uint32_t entry=(low&65535)|(high&0xffff0000u),object=interrupt_objects[vector-0x30];
    /* IRQ0 is a kernel-owned gate, not an IoConnectInterrupt driver object.
     * This binding belongs to the already hash-checked initialized capture. */
    int clock_gate=vector==0x30 && entry==0x800355f0u;
    if((!clock_gate && (!object || entry!=object+0x18)) || (low>>16)!=8 || (high&0xffff)!=0x8e00) {
        failure="device LLE IRQ has no qualified connected handler";return;
    }
    check_publications(1);if(failure)return;
    uint32_t frame[]={RETURN_PC,8,0x202},sp=SCRATCH+0x4000,flags=2;
    IO_UC(uc_mem_write(cpu,sp,frame,sizeof(frame)));
    for(unsigned i=0;i<8;++i){uint32_t v=i==4?sp:0x12345000u+i;IO_UC(uc_reg_write(cpu,general_regs[i],&v));}
    IO_UC(uc_reg_write(cpu,UC_X86_REG_EFLAGS,&flags));
    if(pilot_usb_pic_vector(1)!=vector){failure="device LLE PIC changed before INTA";return;}
    run_original(entry,1000000);if(failure)return;
    uint32_t observed;IO_UC(uc_reg_read(cpu,UC_X86_REG_EIP,&observed));
    if(observed!=RETURN_PC){failure="device LLE IRQ failed to return";return;}
    for(unsigned i=0;i<8;++i) {
        IO_UC(uc_reg_read(cpu,general_regs[i],&observed));
        if(observed!=(i==4?sp+12:0x12345000u+i)) {
            failure="device LLE IRQ corrupted interrupted registers";return;
        }
    }
    IO_UC(uc_reg_read(cpu,UC_X86_REG_EFLAGS,&observed));
    if(observed!=0x202){failure="device LLE IRQ failed to restore flags";return;}
    check_publications(0);
    InterlockedExchange(&current_irql,(LONG)(kernel_word(0x8003da60u)&255));
    *delivered=1;
#else
    (void)delivered;failure="device LLE IRQ requires USB/PIC build";
#endif
}
#undef IO_UC
void pilot_io_device_isr_bind(PilotIoIsr callback){isr_callback=callback;}
void pilot_io_device_dpc_bind(PilotIoIsr callback){dpc_callback=callback;}
void pilot_io_device_thread_bind(PilotIoThread callback){thread_callback=callback;}
const char *pilot_io_device_thread_trace(uint32_t entry)
{
    AcquireSRWLockExclusive(&lock);
    const char *error=NULL;
    if(execution_depth || (entry && (entry<0x10000u || entry>=0x80000000u)))
        error="original thread trace requires an idle user-code address";
    else {original_trace_entry=entry;original_trace_hits=0;}
    ReleaseSRWLockExclusive(&lock);return error;
}
const char *pilot_io_device_thread_component_bind(uint32_t entry,uint32_t bytes,
    const char *sha256,PilotIoComponent callback)
{
    AcquireSRWLockExclusive(&lock);
    const char *error=NULL;
    if(!cpu || execution_depth || !callback || !sha256 || strlen(sha256)!=64 ||
       !bytes || bytes>4096 || entry<image_start ||
       (uint64_t)entry+bytes>(uint64_t)image_start+image_bytes || component_callback)
        error="selected helper binding requires an idle, owned complete boundary";
    else {
        unsigned char digest[32];char actual[65];
        for(uint64_t page=entry&~4095u;page<(uint64_t)entry+bytes;page+=4096)
            if(!guest_page((uint32_t)page)){error="selected helper original bytes are not resident";break;}
        if(!error && BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,guest_memory+entry,bytes,digest,32)<0)
            error="selected helper original hash failed";
        if(!error) {
            for(unsigned i=0;i<32;++i)sprintf(actual+i*2,"%02x",digest[i]);
            if(strcmp(actual,sha256))error="selected helper differs from reviewed original bytes";
        }
        if(!error) {
            memcpy(original_component_code,guest_memory+entry,bytes);
            original_component_entry=entry;original_component_bytes=bytes;component_callback=callback;
        }
    }
    ReleaseSRWLockExclusive(&lock);return error;
}
const char *pilot_io_device_clock_bind(PilotIoClock provider,void *context)
{
    AcquireSRWLockExclusive(&lock);
    const char *error=NULL;
    if(execution_depth)error="original clock binding during execution";
    else {clock_provider=provider;clock_context=provider?context:NULL;}
    ReleaseSRWLockExclusive(&lock);return error;
}
#ifdef PILOT_USB_LLE
static struct OriginalClock {
    LARGE_INTEGER origin,frequency;
    uint64_t base;
} original_clock;
static const char *original_clock_input(void *context,uint64_t *ticks)
{
    struct OriginalClock *clock=context;LARGE_INTEGER now;
    if(!QueryPerformanceCounter(&now) || now.QuadPart<clock->origin.QuadPart)
        return "original runtime clock input failed/backwards";
    uint64_t elapsed=(uint64_t)(now.QuadPart-clock->origin.QuadPart);
    uint64_t frequency=(uint64_t)clock->frequency.QuadPart;
    *ticks=clock->base+(elapsed/frequency)*1125000+(elapsed%frequency)*1125000/frequency;
    return NULL;
}
#endif
const char *pilot_io_device_clock_start(void)
{
    AcquireSRWLockExclusive(&lock);
#ifdef PILOT_USB_LLE
    if(failure)goto done;
    if(!cpu || execution_depth || clock_provider || pilot_usb_pic_vector(0)>=0) {
        failure="original runtime clock start outside idle initialized transport";goto done;
    }
    uint64_t ticks=0,next=0;
    failure=pilot_usb_pit_clock(&ticks,&next);if(failure)goto done;
    if(next){failure="original runtime clock already programmed";goto done;}
    // INIT was retired before the owned RAM capture. Execute its reviewed
    // supplied page at the original VA on this same engine, then retire the
    // temporary code map and restore the caller's complete engine context.
    uc_context *context=NULL;uc_err error=uc_context_alloc(cpu,&context);
    if(error){failure=uc_strerror(error);goto done;}
    error=uc_context_save(cpu,context);
    if(error){uc_context_free(context);failure=uc_strerror(error);goto done;}
    int mapped_init=0;
    if(!error) {
        error=uc_mem_map(cpu,0x80078000u,4096,UC_PROT_READ|UC_PROT_EXEC);
        if(!error)mapped_init=1;
    }
    uint32_t sp=SCRATCH+0x7800u,pc=RETURN_PC,flags=0x202u;
    if(!error)error=uc_mem_write(cpu,0x80078000u,pilot_calendar_kernel+0x68000u,4096);
    if(!error)error=uc_mem_write(cpu,sp,&pc,4);
    if(!error)error=uc_reg_write(cpu,UC_X86_REG_ESP,&sp);
    if(!error)error=uc_reg_write(cpu,UC_X86_REG_EFLAGS,&flags);
    if(!error) {
        clock_initializing=1;clock_initialization_writes=0;++execution_depth;
        error=uc_emu_start(cpu,0x800786d4u,RETURN_PC,0,128);
        --execution_depth;clock_initializing=0;
    }
    uint32_t observed_sp=0;
    if(!error)error=uc_reg_read(cpu,UC_X86_REG_EIP,&pc);
    if(!error)error=uc_reg_read(cpu,UC_X86_REG_ESP,&observed_sp);
    if(!error)error=uc_reg_read(cpu,UC_X86_REG_EFLAGS,&flags);
    if(!failure && !error && (pc!=RETURN_PC || observed_sp!=sp+4 || flags!=0x202u ||
       clock_initialization_writes!=3 || clock_initialization_ports[0]!=0x43 ||
       clock_initialization_ports[1]!=0x40 || clock_initialization_ports[2]!=0x40 ||
       clock_initialization_values[0]!=0x34 || clock_initialization_values[1]!=0x65 ||
       clock_initialization_values[2]!=4))failure="original PIT initialization contract changed";
    if(error && !failure)failure=uc_strerror(error);
    if(mapped_init) {
        uc_err unmap=uc_mem_unmap(cpu,0x80078000u,4096);
        if(unmap && !failure)failure=uc_strerror(unmap);
    }
    uc_err restore=uc_context_restore(cpu,context);uc_context_free(context);
    if(restore && !failure)failure=uc_strerror(restore);
    if(failure)goto done;
    failure=pilot_usb_pit_clock(&original_clock.base,&next);if(failure)goto done;
    if(next!=ticks+1125 || !QueryPerformanceFrequency(&original_clock.frequency) ||
       original_clock.frequency.QuadPart<=0 || !QueryPerformanceCounter(&original_clock.origin)) {
        failure="original runtime PIT clock initialization failed";goto done;
    }
    clock_provider=original_clock_input;clock_context=&original_clock;
    fprintf(stderr,"[ORIGINAL CLOCK] original INIT OUTs; PIT/PIC IRQ0; one engine owner and native checkpoint requests\n");
#else
    failure="original runtime clock requires PIT/PIC";
#endif
done:
    const char *result=failure;ReleaseSRWLockExclusive(&lock);return result;
}
const char *pilot_io_device_thread_type(uint32_t *address)
{
    AcquireSRWLockExclusive(&lock);
    if(!cpu || execution_depth || !address)failure="thread LLE type observation outside initialized idle transport";
    if(!failure) {
        *address=original_export(259);
        if(!failure)publish_range(*address,0x1cu);
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
const char *pilot_io_device_next_thread(uint32_t *object)
{
    AcquireSRWLockExclusive(&lock);
    if(!cpu || execution_depth || !object)failure="thread LLE lifetime observation outside initialized idle transport";
    if(!failure) {
        *object=0;
        for(unsigned i=0;i<64;++i)if(original_threads[i].object && !kernel_word(original_threads[i].object+4)) {
            *object=original_threads[i].object;break;
        }
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
void pilot_io_device_port_bind(uint16_t first,uint16_t last,PilotIoPort callback)
{
    AcquireSRWLockExclusive(&lock);
    if(execution_depth){failure="device LLE port binding during execution";goto done;}
    if(!callback){memset(device_ports,0,sizeof(device_ports));goto done;}
    if(first>last){failure="device LLE invalid port binding";goto done;}
    unsigned empty=8;
    for(unsigned i=0;i<8;++i) {
        if(!device_ports[i].callback){if(empty==8)empty=i;continue;}
        if(first<=device_ports[i].last && last>=device_ports[i].first) {
            failure="device LLE overlapping port binding";goto done;
        }
    }
    if(empty==8){failure="device LLE port binding capacity";goto done;}
    device_ports[empty].first=first;device_ports[empty].last=last;device_ports[empty].callback=callback;
done:
    ReleaseSRWLockExclusive(&lock);
}
const char *pilot_io_device_mmio_bind(uint32_t first,uint32_t bytes,PilotIoMmio callback)
{
    if(device_bus_borrow)return "device LLE MMIO binding during device work";
    AcquireSRWLockExclusive(&lock);
    if(!cpu || execution_depth)failure="device LLE MMIO binding outside idle initialized transport";
    if(!failure && callback && (!bytes || (first&4095) || (bytes&4095) ||
        (uint64_t)first+bytes>0x100000000ull || device_mmio))
        failure="device LLE invalid/duplicate MMIO binding";
    if(!failure) {
        uc_err error=UC_ERR_OK;
        if(callback)error=uc_mmio_map(cpu,first,bytes,device_mmio_read,NULL,device_mmio_write,NULL);
        else if(device_mmio)error=uc_mem_unmap(cpu,device_mmio_first,device_mmio_bytes);
        if(error)failure=uc_strerror(error);
        else {device_mmio=callback;device_mmio_first=callback?first:0;device_mmio_bytes=callback?bytes:0;}
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
const char *pilot_io_device_work_bind(PilotIoDeviceWork callback)
{
    if(device_bus_borrow)return "device LLE work binding during device work";
    AcquireSRWLockExclusive(&lock);
    const char *error=failure;
    if(!error && (!cpu || execution_depth || (callback && device_work)))
        error="device LLE work binding requires idle sole owner";
    if(!error)device_work=callback;
    ReleaseSRWLockExclusive(&lock);return error;
}
void pilot_io_device_set_if(int enabled){InterlockedExchange(&caller_if,enabled!=0);}
unsigned pilot_io_device_irql(void){return (unsigned)InterlockedCompareExchange(&current_irql,0,0);}
const char *pilot_io_device_irq(unsigned char *guest,int *delivered)
{
    if(device_bus_borrow)return "device LLE CPU IRQ reentry from device work";
    AcquireSRWLockExclusive(&lock);
    if(!guest_memory)initialize(guest);
    if(!failure && guest_memory!=guest)failure="device LLE guest address space changed";
    if(!failure && execution_depth)failure="device LLE recursive hardware IRQ delivery";
    if(!failure){++execution_depth;deliver_irq(delivered);--execution_depth;}
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}

const char *pilot_io_device_lle(unsigned ordinal,unsigned char *guest,
                               const uint32_t *args,uint32_t *result)
{
    if(device_bus_borrow)return "device LLE CPU service reentry from device work";
    AcquireSRWLockExclusive(&lock);
    if(!guest_memory)initialize(guest);
    if(!failure && guest_memory!=guest)failure="device LLE guest address space changed";
    if(native_thread_worker)failure="thread LLE continuation handoff requires its real guest-call frame";
    if(!failure && execution_depth>=2)failure="device LLE nested service stack limit";
    if(!failure)thread_before_call(ordinal,args);
    if(!failure) {
        ++execution_depth;execute(ordinal,args,result);
        if(!failure)thread_after_call(ordinal,args,*result);
        --execution_depth;
    }
    const char *error=failure; /* An incomplete service poisons this context. */
    ReleaseSRWLockExclusive(&lock);return error;
}

#ifdef PILOT_FILES_LLE
const char *pilot_io_device_interrupt_time(unsigned char *guest,uint64_t *value)
{
    AcquireSRWLockExclusive(&lock);
    if(!guest_memory)initialize(guest);
    if(!failure && (guest_memory!=guest || !value))failure="device LLE invalid clock caller";
    if(!failure && execution_depth)failure="device LLE recursive clock query";
    if(!failure) {
        uint32_t low=0,high=0;++execution_depth;
        execute(125,NULL,&low);
        if(!failure) {
            uc_err error=uc_reg_read(cpu,UC_X86_REG_EDX,&high);
            if(error)failure=uc_strerror(error);
            else *value=((uint64_t)high<<32)|low;
        }
        --execution_depth;
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
const char *pilot_io_device_file_data(unsigned ordinal,uint32_t *value)
{
    AcquireSRWLockExclusive(&lock);
    if(!guest_memory || ordinal!=40 || !value)failure="storage LLE unsupported data export";
    if(!failure) {
        uint32_t pe=kernel_word(0x8001003cu),exports=0x80010000u+kernel_word(0x80010000u+pe+120);
        uint32_t base=kernel_word(exports+16),table=0x80010000u+kernel_word(exports+28);
        uint32_t address=0x80010000u+kernel_word(table+4*(ordinal-base));
        if(!failure)*value=kernel_word(address);
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
const char *pilot_io_device_files(unsigned char *guest)
{
    AcquireSRWLockExclusive(&lock);
    if(!guest_memory)initialize(guest);
    if(!failure && guest_memory!=guest)failure="storage LLE guest address space changed";
    if(!failure && !storage.ready) {
        storage_initialize();
        if(!failure) {
            uint32_t args[]={kernel_word(0x8003da64u),31},result=0;
            ++execution_depth;
            if(!failure)execute(148,args,&result);
            if(!failure)execute(36,args,&result);
            --execution_depth;
            if(!failure)storage.ready=1;
        }
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
#endif

const char *pilot_io_device_image(unsigned char *guest,uint32_t base,uint32_t bytes)
{
    AcquireSRWLockExclusive(&lock);
    if(!guest_memory)initialize(guest);
    if(!failure && (image_bytes || guest_memory!=guest || !bytes || (base&0xffff) ||
                    (bytes&4095) || base<0x10000 || (uint64_t)base+bytes>0x60000000))
        failure="device LLE invalid image reservation";
    if(!failure) {
        ++execution_depth;image_vm(base,bytes,MEM_RESERVE);--execution_depth;
        if(!failure){image_start=base;image_bytes=bytes;}
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
const char *pilot_io_device_stack(unsigned char *guest,uint32_t base,uint32_t bytes)
{
    AcquireSRWLockExclusive(&lock);
    if(!guest_memory)initialize(guest);
    uint32_t reserve=base&~0xffffu;
    uint64_t end=(uint64_t)base+bytes;
    if(!failure && (guest_memory!=guest || !bytes || (base&4095) || (bytes&4095) ||
                   base<0x10000 || end>0x80000000ull || stack_count==64))
        failure="device LLE invalid owned stack reservation";
    if(!failure && image_bytes && reserve<(uint64_t)image_start+image_bytes && end>image_start)
        failure="device LLE stack overlaps image reservation";
    for(unsigned i=0;!failure && i<stack_count;++i)
        if(reserve<(uint64_t)stacks[i].base+stacks[i].bytes && end>(stacks[i].base&~0xffffu))
            failure="device LLE overlapping stack reservations";
    for(uint64_t page=base;!failure && page<end;page+=4096)
        if(!guest_page((uint32_t)page))failure="device LLE owned stack is not resident";
    if(!failure) {
        ++execution_depth;image_vm(reserve,(uint32_t)(end-reserve),MEM_RESERVE);--execution_depth;
        if(!failure){stacks[stack_count].base=base;stacks[stack_count++].bytes=bytes;}
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
const char *pilot_io_device_stack_release(unsigned char *guest,uint32_t base,uint32_t bytes,int *released)
{
    AcquireSRWLockExclusive(&lock);
    if(released)*released=0;
    unsigned index;for(index=0;index<stack_count;++index)
        if(stacks[index].base==base && stacks[index].bytes==bytes)break;
    if(!failure && (!released || guest_memory!=guest || execution_depth || index==stack_count))
        failure="device LLE release requires an idle exact owned stack";
    uint32_t reserve=base&~0xffffu;
    uint64_t end=(uint64_t)base+bytes;
    int pinned=0;
    for(unsigned i=0;!failure && i<MAX_PUBLISHED;++i)
        if(pin_ranges[i].count && pin_ranges[i].base<end &&
           (uint64_t)pin_ranges[i].base+pin_ranges[i].bytes>reserve)pinned=1;
    for(unsigned i=0;!failure && i<published_count;++i)
        if(published[i].va>=reserve && published[i].va<end && published[i].pins)pinned=1;
    if(!failure && !pinned) {
        /* Original NtFreeVirtualMemory accepts a sized MEM_RELEASE. Use the
         * exact original reservation, including its 64-KiB alignment prefix;
         * execute() retains original status, PTE changes and PFN retirement. */
        ++execution_depth;image_vm(reserve,(uint32_t)(end-reserve),MEM_RELEASE);--execution_depth;
        for(unsigned i=0;!failure && i<published_count;++i)
            if(published[i].va>=reserve && published[i].va<end)
                failure="device LLE released stack retains published storage";
        /* An unmaterialized stack page can have a native-pointer Unicorn
         * alias. Host bytes are still committed here, so refresh_mappings
         * alone cannot retire that alias before the caller decommits them. */
        for(unsigned i=0;!failure && i<mapped_count;) {
            if(mapped[i].va>=reserve && mapped[i].va<end) {
                uc_err error=uc_mem_unmap(cpu,mapped[i].va,4096);
                if(error){failure=uc_strerror(error);break;}
                mapped[i]=mapped[--mapped_count];
            } else ++i;
        }
        if(!failure){stacks[index]=stacks[--stack_count];*released=1;}
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
const char *pilot_io_device_image_release(uint32_t base,uint32_t bytes)
{
    AcquireSRWLockExclusive(&lock);
    int found=0;
    for(unsigned i=0;i<published_count;++i)if(published[i].va>=base &&
        published[i].va<(uint64_t)base+bytes && published[i].va<0x80000000u) {
        found=1;if(published[i].pins)failure="device LLE image release while DMA pinned";
    }
    if(!failure && found){++execution_depth;image_vm(base,bytes,MEM_DECOMMIT);--execution_depth;}
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
const char *pilot_io_device_resident(uint32_t base,uint32_t bytes)
{
    AcquireSRWLockExclusive(&lock);
    if(!failure && (!guest_memory || execution_depth || !bytes || (base&4095) ||
       (bytes&4095) || !caller_owned(base,bytes)))failure="memory LLE residency requires an owned page range";
    if(!failure) {
        ++execution_depth;
        uint64_t end=(uint64_t)base+bytes;
        for(uint64_t at=base;!failure && at<end;) {
            MEMORY_BASIC_INFORMATION info;
            if(!VirtualQuery(guest_memory+at,&info,sizeof(info))) {failure="memory LLE caller residency query failed";break;}
            uint64_t next=(unsigned char*)info.BaseAddress-guest_memory+info.RegionSize;
            if(next>end)next=end;
            if(next<=at || (next&4095)){failure="memory LLE invalid host residency extent";break;}
            if(info.State==MEM_COMMIT) {
                if(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)){failure="memory LLE protected loader range unqualified";break;}
                image_vm((uint32_t)at,(uint32_t)(next-at),MEM_COMMIT);
            }
            at=next;
        }
        --execution_depth;
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}
const char *pilot_io_device_release_check(uint32_t base,uint32_t bytes)
{
    AcquireSRWLockShared(&lock);
    const char *error=NULL;
    for(unsigned i=0;i<MAX_PUBLISHED;++i)if(pin_ranges[i].count &&
       (uint64_t)pin_ranges[i].base+pin_ranges[i].bytes>base && pin_ranges[i].base<(uint64_t)base+bytes) {
        error="device LLE allocation release while DMA pinned";break;
    }
    ReleaseSRWLockShared(&lock);return error;
}
int pilot_io_device_contiguous_dma(uint32_t pa,void *buffer,uint32_t bytes,int write,unsigned lease)
{
    int valid=0;
    int acquired=!device_bus_borrow;if(acquired)AcquireSRWLockExclusive(&lock);
    if(device_bus_borrow && (!execution_depth || lease!=PILOT_IO_DMA_SYNC))goto done;
    if(failure || !buffer || !bytes || (lease&~PILOT_IO_DMA_USB) || (uint64_t)pa+bytes>RAM_BYTES)goto done;
    unsigned entry=publication_by_pa[pa/4096];
    if(!entry)goto done;
    uint32_t base=published[entry-1].va+(pa&4095);
    unsigned owner=contiguous_owner(base,bytes);
    if(owner==owner_count)goto done;
    /* Check the entire transaction before the first store or retained lease.
     * The allocation's actual PTE/PFN and published bytes remain authoritative. */
    for(unsigned pass=0;pass<2;++pass)for(uint32_t at=pa,left=bytes;left;) {
        unsigned index=publication_by_pa[at/4096];
        uint32_t offset=at&4095,span=4096-offset,actual;
        if(span>left)span=left;
        if(!index)goto done;
        uint32_t va=published[index-1].va+offset;
        if(va!=(uint64_t)base+at-pa || !physical(va,&actual) || actual!=at)goto done;
        MEMORY_BASIC_INFORMATION info;
        if(!VirtualQuery(guest_memory+va,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || (write && info.Protect!=PAGE_READWRITE))goto done;
        if(pass) {
            if(write)memcpy(guest_memory+va,(unsigned char*)buffer+at-pa,span);
            else memcpy((unsigned char*)buffer+at-pa,guest_memory+va,span);
        }
        at+=span;left-=span;
    }
    owners[owner].dma_leases|=lease;valid=1;
done:
    if(acquired)ReleaseSRWLockExclusive(&lock);return valid;
}
void pilot_io_device_dma_release(unsigned lease)
{
    AcquireSRWLockExclusive(&lock);
    if(lease==PILOT_IO_DMA_USB)
        for(unsigned i=0;i<owner_count;++i)owners[i].dma_leases&=~lease;
    ReleaseSRWLockExclusive(&lock);
}
int pilot_io_device_pinned_dma(uint32_t pa,void *buffer,uint32_t bytes,int write)
{
    int valid=0;
    int acquired=!device_bus_borrow;if(acquired)AcquireSRWLockExclusive(&lock);
    if(device_bus_borrow && !execution_depth)goto done;
    if(failure || !bytes || (uint64_t)pa+bytes>RAM_BYTES)goto done;
    /* Validate the complete physical transaction before changing any bytes. */
    for(unsigned pass=0;pass<2;++pass)for(uint32_t at=pa,left=bytes;left;) {
        uint32_t page=at&~4095u,offset=at&4095,span=4096-offset;
        if(span>left)span=left;
        unsigned index=publication_by_pa[page/4096],i=index?index-1:published_count;
        if(i==published_count || !published[i].pins)goto done;
        uint64_t va=published[i].va+offset,end=va+span;
        while(va<end) {
            uint64_t covered=va;
            for(unsigned j=0;j<MAX_PUBLISHED;++j)if(pin_ranges[j].count && pin_ranges[j].base<=va &&
               (uint64_t)pin_ranges[j].base+pin_ranges[j].bytes>covered)
                covered=(uint64_t)pin_ranges[j].base+pin_ranges[j].bytes;
            if(covered==va)goto done;
            va=covered;
        }
        MEMORY_BASIC_INFORMATION info;
        if(!VirtualQuery(guest_memory+published[i].va,&info,sizeof(info)) ||
           info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
           (write && info.Protect!=PAGE_READWRITE))goto done;
        if(pass) {
            unsigned char *host=guest_memory+published[i].va+offset;
            if(write)memcpy(host,(unsigned char*)buffer+at-pa,span);
            else memcpy((unsigned char*)buffer+at-pa,host,span);
        }
        at+=span;left-=span;
    }
    valid=1;
done:
    if(acquired)ReleaseSRWLockExclusive(&lock);return valid;
}

static int gpu_bootstrap_mapping(void)
{
    // Original Xbox direct mapping of physical page zero. Its PTE is checked
    // at admission and access, never manufactured from a requested address.
    uint32_t pde=ram_word(0xf000u+(0x80000000u>>22)*4);
    if((pde&3)!=3 || (pde&128))return 0;
    uint32_t pte=ram_word(pde&0xfffff000u),pa;
    return (pte&3)==3 && physical(0x80000000u,&pa) && pa==0;
}

const char *pilot_io_device_gpu_bootstrap(unsigned char *guest)
{
    AcquireSRWLockExclusive(&lock);
    if(!guest_memory)initialize(guest);
    if(!failure && (guest_memory!=guest || execution_depth || !gpu_bootstrap_mapping()))
        failure="device LLE original GPU bootstrap mapping is not writable physical zero";
    if(!failure && !gpu_bootstrap_published) {
        publish_range(0x80000000u,4096);
        if(!failure){gpu_bootstrap_published=1;refresh_mappings();}
    }
    const char *error=failure;ReleaseSRWLockExclusive(&lock);return error;
}

int pilot_io_device_bootstrap_dma(uint32_t pa,void *buffer,uint32_t bytes,int write)
{
    int valid=0;
    int acquired=!device_bus_borrow;if(acquired)AcquireSRWLockExclusive(&lock);
    if(device_bus_borrow && !execution_depth)goto done;
    if(failure || !gpu_bootstrap_published || !bytes || (uint64_t)pa+bytes>4096 ||
       !gpu_bootstrap_mapping())goto done;
    unsigned i=publication_slot(0x80000000u);
    if(i==published_count || published[i].pa || shared_pages[0]!=guest_memory+0x80000000ull)goto done;
    MEMORY_BASIC_INFORMATION info;
    if(!VirtualQuery(guest_memory+0x80000000ull,&info,sizeof(info)) ||
       info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
       (write && info.Protect!=PAGE_READWRITE))goto done;
    if(write)memcpy(physical_backing(pa),buffer,bytes);
    else memcpy(buffer,physical_backing(pa),bytes);
    valid=1;
done:
    if(acquired)ReleaseSRWLockExclusive(&lock);return valid;
}
#include "io_memory_lle.inc"
