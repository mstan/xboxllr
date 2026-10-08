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
#include "io_device_lle.h"
#include "io_device_config.h"
#ifdef PILOT_USB_LLE
#include "usb_lle.h"
#endif

#define RAM_BYTES (64u*1024*1024)
#define SCRATCH PILOT_IO_BRIDGE_BASE
#define STACK (SCRATCH+0x8000u)
#define RETURN_PC (SCRATCH+0x9000u)
#define MAX_PAGES 4096
#define MAX_PUBLISHED (RAM_BYTES/4096)
static SRWLOCK lock=SRWLOCK_INIT;
static uc_engine *cpu;
static unsigned char *ram,*guest_memory;
static unsigned char *shared_pages[RAM_BYTES/4096];
static const char *failure;
static struct {uint32_t va,pa;unsigned char *backing;} mapped[MAX_PAGES];
static struct {uint32_t va,pa,pins;} published[MAX_PUBLISHED];
static unsigned mapped_count,published_count;
static struct {uint32_t base,bytes;} owners[MAX_PUBLISHED];
static unsigned owner_count;
static struct {uint32_t base,bytes,count;} pin_ranges[MAX_PUBLISHED];
static uint32_t image_start,image_bytes;
static struct {uint32_t base,bytes;} stacks[64];
static unsigned stack_count;
static uint32_t interrupt_objects[16],pending_callback;
static PilotIoIsr isr_callback;
static volatile LONG current_irql;
static volatile LONG caller_if=1;
static unsigned execution_depth;
static const int general_regs[]={UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,
    UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI};

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
static int physical(uint32_t va,uint32_t *pa)
{
    uint32_t pde=ram_word(0xf000u+(va>>22)*4);
    if(!(pde&1))return 0;
    if(pde&128)*pa=(pde&0xffc00000u)+(va&0x3fffff);
    else {
        uint32_t pte=ram_word((pde&0xfffff000u)+((va>>12)&1023)*4);
        if(!(pte&1))return 0;
        *pa=(pte&0xfffff000u)+(va&4095);
    }
    return *pa<RAM_BYTES;
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
    uint32_t page=(uint32_t)address&~4095u,pa;
    unsigned char *backing;
    unsigned publication;for(publication=0;publication<published_count && published[publication].va!=page;++publication){}
    if(publication==published_count && (page<0x80000000u || (page>=0xe0000000u && page<0xe4000000u))) {
        if(!guest_page(page)){failure="device LLE invalid caller memory";return false;}
        backing=guest_memory+page;pa=UINT32_MAX;
    } else {
        if(!physical(page,&pa)){failure="device LLE unmodelled memory or MMIO";return false;}
        backing=physical_backing(pa);
    }
    if(mapped_count==MAX_PAGES){failure="device LLE mapping capacity";return false;}
    mapped[mapped_count].va=page;mapped[mapped_count].pa=pa;
    mapped[mapped_count++].backing=backing;
    uc_err error=uc_mem_map_ptr(engine,page,4096,UC_PROT_ALL,backing);
    if(error){failure=uc_strerror(error);return false;}
    return true;
}
static void forbidden(uc_engine *engine,const char *reason)
{failure=reason;uc_emu_stop(engine);}
static uint32_t port_in(uc_engine *engine,uint32_t port,int size,void *data)
{
#ifdef PILOT_USB_LLE
    uint32_t value=0;(void)data;
    const char *error=pilot_usb_pic_port(port,size,0,&value);
    if(error)forbidden(engine,error);
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
    (void)data;const char *error=pilot_usb_pic_port(port,size,1,&value);
    if(error)forbidden(engine,error);
#else
    (void)port;(void)size;(void)value;(void)data;forbidden(engine,"device LLE unmodelled port write");
#endif
}
static void interrupt(uc_engine *engine,uint32_t number,void *data)
{
    (void)data;
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
            if(!e)e=uc_mem_write(engine,sp,frame,sizeof(frame));
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
static void code_block(uc_engine *engine,uint64_t address,uint32_t size,void *data)
{
    (void)size;(void)data;
    if(address>=0x80010000u && address<0x80080000u)return;
    for(unsigned i=0;i<16;++i)if(interrupt_objects[i]) {
        uint32_t object=interrupt_objects[i],routine=0;
        if(address>=object+0x18 && address<object+0x70)return;
        if(uc_mem_read(engine,object,&routine,4)==UC_ERR_OK && address==routine && isr_callback) {
            pending_callback=routine;uc_emu_stop(engine);return;
        }
    }
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
            unsigned j;for(j=0;j<published_count && published[j].va!=mapped[i].va;++j){}
            stale=j<published_count || !guest_page(mapped[i].va);
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
    memcpy(ram+pa,guest_memory+va,4096);
    shared_pages[pa/4096]=NULL;
    published[i]=published[--published_count];
    refresh_mappings();if(failure)return;
    if(!VirtualFree(guest_memory+va,4096,MEM_DECOMMIT))
        failure="device LLE publication release failed";
}
static void check_publications(int before_call)
{
    for(unsigned i=0;i<published_count;) {
        uint32_t pa,va=published[i].va;
        if(!physical(va,&pa)) {
            if(before_call){failure="device LLE published mapping changed between calls";return;}
            release_publication(i);if(failure)return;continue;
        }
        if(pa!=published[i].pa){failure="device LLE published alias changed between calls";return;}
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
    if(!(image || (object>=0xd0000000u && (uint64_t)object+bytes<=0xd4000000ull) ||
         (object>=0x80000000u && (uint64_t)object+bytes<=0x84000000ull))) {
        failure="device LLE unexpected allocation namespace";return;
    }
    uint32_t end=(object+bytes+4095)&~4095u;
    for(uint32_t va=object&~4095u;va<end;va+=4096) {
        unsigned i;for(i=0;i<published_count && published[i].va!=va;++i){}
        if(i<published_count)continue;
        uint32_t pa;
        if(published_count==MAX_PUBLISHED || !physical(va,&pa)) {
            failure="device LLE invalid publication";return;
        }
        if(shared_pages[pa/4096]){failure="device LLE duplicate physical publication";return;}
        if(image) {
            if(!guest_page(va)){failure="device LLE caller page is not resident";return;}
        } else if(guest_page(va)){failure="device LLE allocation collides with native memory";return;}
        else if(!VirtualAlloc(guest_memory+va,4096,MEM_COMMIT,PAGE_READWRITE)) {
            failure="device LLE publication allocation failed";return;
        }
        if(!image)memcpy(guest_memory+va,ram+pa,4096);
        shared_pages[pa/4096]=guest_memory+va;
        published[published_count].va=va;published[published_count].pins=0;published[published_count++].pa=pa;
    }
    if(!image) {
        if(owner_count==MAX_PUBLISHED){failure="device LLE publication ownership capacity";return;}
        owners[owner_count].base=object;owners[owner_count++].bytes=bytes;
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
static void run_original(uint32_t entry)
{
    /* Never re-enter uc_emu_start from a hook. At an actual ISR call, stop,
     * run its generated body over shared bytes, then restore this CPU. A
     * nested original service has its own stack and saved engine context. */
    for(unsigned calls=0;calls<1024;++calls) {
        pending_callback=0;
        uc_err error=uc_emu_start(cpu,entry,RETURN_PC,0,1000000);
        if(failure)return;
        if(error){failure=uc_strerror(error);return;}
        if(!pending_callback)return;
        uint32_t routine=pending_callback,regs[8],return_pc;
        for(unsigned i=0;i<8;++i)IO_UC(uc_reg_read(cpu,general_regs[i],regs+i));
        IO_UC(uc_mem_read(cpu,regs[4],&return_pc,4));
        uc_context *context=NULL;IO_UC(uc_context_alloc(cpu,&context));
        error=uc_context_save(cpu,context);
        if(error){uc_context_free(context);failure=uc_strerror(error);return;}
        check_publications(0);
        InterlockedExchange(&current_irql,(LONG)(kernel_word(0x8003da60u)&255));
        if(!failure) {
            ReleaseSRWLockExclusive(&lock);
            const char *callback_error=isr_callback(routine,regs);
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
    failure="device LLE ISR callback budget exhausted";
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
    if(type==MEM_DECOMMIT) {
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
static void execute(unsigned ordinal,const uint32_t *args,uint32_t *result)
{
    unsigned pin_slot=MAX_PUBLISHED;
    if(ordinal!=14 && ordinal!=15 && ordinal!=17 && ordinal!=23 &&
       ordinal!=44 && ordinal!=47 && ordinal!=65 && ordinal!=68 && ordinal!=109 && ordinal!=166 &&
       ordinal!=171 && ordinal!=173 && ordinal!=175 && ordinal!=178 && ordinal!=182 &&
       ordinal!=98 && ordinal!=100 && ordinal!=103 && ordinal!=129 && ordinal!=151 &&
       ordinal!=160 && ordinal!=161 && ordinal!=184 && ordinal!=199) {
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
            unsigned i;for(i=0;i<published_count && published[i].va!=page;++i){}
            if(i==published_count && caller_owned((uint32_t)page,4096) && guest_page((uint32_t)page) &&
               !(ordinal==175 && args[2])) {
                image_vm((uint32_t)page,4096,MEM_COMMIT);if(failure)return;
                for(i=0;i<published_count && published[i].va!=page;++i){}
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
    /* Bound the qualified small device allocation family. Larger allocations
     * require separate original paging/allocator qualification, not guessed NTSTATUS. */
    if(ordinal==65 && args[1]>0x170){failure="device LLE extension exceeds qualified scope";return;}
    check_publications(1);if(failure)return;
    int retire=ordinal==17 || ordinal==171 || (ordinal==68 && kernel_word(args[0]+4)==0);
    uint32_t pe=kernel_word(0x8001003cu),exports=0x80010000u+kernel_word(0x80010000u+pe+120);
    uint32_t base=kernel_word(exports+16),table=0x80010000u+kernel_word(exports+28);
    uint32_t entry=0x80010000u+kernel_word(table+4*(ordinal-base));
    if(failure)return;
    unsigned count=ordinal==65?6:ordinal==109?7:(ordinal==166 || ordinal==184)?5:
                   (ordinal==175 || ordinal==178 || ordinal==182 || ordinal==199)?3:
                   (ordinal==15 || ordinal==44 || ordinal==47)?2:
                   (ordinal==103 || ordinal==129 || ordinal==160 || ordinal==161)?0:1;
    if(ordinal==160 || ordinal==161) {
        uint32_t level=args[0]&255;
        if(level>31){failure="device LLE IRQL out of range";return;}
        IO_UC(uc_reg_write(cpu,UC_X86_REG_ECX,&level));
    }
    uint32_t words[8]={RETURN_PC},stack=STACK+(execution_depth-1)*0x4000u;
    uint32_t sp=stack,observed,flags=2u|(caller_if?0x200u:0);
    memcpy(words+1,args,count*4);
    IO_UC(uc_mem_write(cpu,stack,words,(count+1)*4));
    IO_UC(uc_reg_write(cpu,UC_X86_REG_ESP,&sp));
    IO_UC(uc_reg_write(cpu,UC_X86_REG_EFLAGS,&flags));
    int saved[]={UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP};
    for(unsigned i=0;i<4;++i){uint32_t value=0x12345678u+i;IO_UC(uc_reg_write(cpu,saved[i],&value));}
    run_original(entry);
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
    if(ordinal==65 && (int32_t)*result>=0) {
        /* The output slot may itself be in an existing device extension.
         * Read the completed kernel view before publishing changed pages. */
        uint32_t object;IO_UC(uc_mem_read(cpu,args[5],&object,4));
        publish_range(object,0x48+args[1]);if(failure)return;
    }
    if(ordinal==166 && *result){publish_range(*result,args[0]);if(failure)return;}
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

static void deliver_irq(int *delivered)
{
#ifdef PILOT_USB_LLE
    int vector=pilot_usb_pic_vector(0);*delivered=0;
    if(vector<0)return;
    if(vector<0x30 || vector>=0x40){failure="device LLE invalid PIC vector";return;}
    uint32_t gate=0x8003de40u+(vector-0x30)*8;
    uint32_t low=kernel_word(gate),high=kernel_word(gate+4);
    uint32_t entry=(low&65535)|(high&0xffff0000u),object=interrupt_objects[vector-0x30];
    if(!object || entry!=object+0x18 || (low>>16)!=8 || (high&0xffff)!=0x8e00) {
        failure="device LLE IRQ has no qualified connected handler";return;
    }
    check_publications(1);if(failure)return;
    uint32_t frame[]={RETURN_PC,8,0x202},sp=SCRATCH+0x4000,flags=2;
    IO_UC(uc_mem_write(cpu,sp,frame,sizeof(frame)));
    for(unsigned i=0;i<8;++i){uint32_t v=i==4?sp:0x12345000u+i;IO_UC(uc_reg_write(cpu,general_regs[i],&v));}
    IO_UC(uc_reg_write(cpu,UC_X86_REG_EFLAGS,&flags));
    if(pilot_usb_pic_vector(1)!=vector){failure="device LLE PIC changed before INTA";return;}
    run_original(entry);if(failure)return;
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
void pilot_io_device_set_if(int enabled){InterlockedExchange(&caller_if,enabled!=0);}
unsigned pilot_io_device_irql(void){return (unsigned)InterlockedCompareExchange(&current_irql,0,0);}
const char *pilot_io_device_irq(unsigned char *guest,int *delivered)
{
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
    AcquireSRWLockExclusive(&lock);
    if(!guest_memory)initialize(guest);
    if(!failure && guest_memory!=guest)failure="device LLE guest address space changed";
    if(!failure && execution_depth>=2)failure="device LLE nested service stack limit";
    if(!failure){++execution_depth;execute(ordinal,args,result);--execution_depth;}
    const char *error=failure; /* An incomplete service poisons this context. */
    ReleaseSRWLockExclusive(&lock);return error;
}

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
int pilot_io_device_pinned_dma(uint32_t pa,void *buffer,uint32_t bytes,int write)
{
    int valid=0;
    AcquireSRWLockExclusive(&lock);
    if(failure || !bytes || (uint64_t)pa+bytes>RAM_BYTES)goto done;
    /* Validate the complete physical transaction before changing any bytes. */
    for(unsigned pass=0;pass<2;++pass)for(uint32_t at=pa,left=bytes;left;) {
        uint32_t page=at&~4095u,offset=at&4095,span=4096-offset;
        if(span>left)span=left;
        unsigned i;for(i=0;i<published_count && published[i].pa!=page;++i){}
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
    ReleaseSRWLockExclusive(&lock);return valid;
}
