/* Execute original device/object/allocator code over a private initialized
 * kernel capture. Caller buffers are shared; published device pages are copied
 * at the synchronous service boundary. This is one selected implementation,
 * not duplicate HLE/LLE execution. USB and driver callbacks are not implied. */
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unicorn/unicorn.h>
#include "io_device_lle.h"
#include "io_device_config.h"

#define RAM_BYTES (64u*1024*1024)
#define SCRATCH 0x7ffe0000u
#define STACK (SCRATCH+0x8000u)
#define RETURN_PC (SCRATCH+0x9000u)
#define MAX_PAGES 4096
#define MAX_PUBLISHED 256
static SRWLOCK lock=SRWLOCK_INIT;
static uc_engine *cpu;
static unsigned char *ram,*guest_memory;
static const char *failure;
static struct {uint32_t va,pa;} mapped[MAX_PAGES],published[MAX_PUBLISHED];
static unsigned mapped_count,published_count;

static uint32_t ram_word(uint32_t pa)
{
    uint32_t value=0;
    if(pa>RAM_BYTES-4){failure="device LLE page table outside RAM";return 0;}
    memcpy(&value,ram+pa,4);return value;
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
    if(page<0x80000000u || (page>=0xe0000000u && page<0xe4000000u)) {
        if(!guest_page(page)){failure="device LLE invalid caller memory";return false;}
        backing=guest_memory+page;
    } else {
        if(!physical(page,&pa)){failure="device LLE unmodelled memory or MMIO";return false;}
        if(mapped_count==MAX_PAGES){failure="device LLE mapping capacity";return false;}
        backing=ram+pa;
        mapped[mapped_count].va=page;mapped[mapped_count++].pa=pa;
    }
    uc_err error=uc_mem_map_ptr(engine,page,4096,UC_PROT_ALL,backing);
    if(error){failure=uc_strerror(error);return false;}
    return true;
}
static void forbidden(uc_engine *engine,const char *reason)
{failure=reason;uc_emu_stop(engine);}
static uint32_t port_in(uc_engine *engine,uint32_t port,int size,void *data)
{
    (void)port;(void)size;(void)data;
    forbidden(engine,"device LLE unmodelled port read");
    return 0; /* Callback ABI only: failure prevents returning to the title. */
}
static void port_out(uc_engine *engine,uint32_t port,int size,uint32_t value,void *data)
{(void)port;(void)size;(void)value;(void)data;forbidden(engine,"device LLE unmodelled port write");}
static void interrupt(uc_engine *engine,uint32_t number,void *data)
{(void)number;(void)data;forbidden(engine,"device LLE unmodelled interrupt");}
static void code_block(uc_engine *engine,uint64_t address,uint32_t size,void *data)
{
    (void)size;(void)data;
    if(address<0x80010000u || address>=0x80080000u)
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
    IO_UC(uc_mem_map(cpu,SCRATCH,0x10000,UC_PROT_ALL));
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
        if(!physical(mapped[i].va,&pa) || pa!=mapped[i].pa) {
            IO_UC(uc_mem_unmap(cpu,mapped[i].va,4096));
            mapped[i]=mapped[--mapped_count];
        } else ++i;
    }
}
static void copy_published(int into_kernel)
{
    for(unsigned i=0;i<published_count;) {
        uint32_t pa,va=published[i].va;
        if(!physical(va,&pa)) {
            if(into_kernel){failure="device LLE published mapping changed between calls";return;}
            if(!VirtualFree(guest_memory+va,4096,MEM_DECOMMIT)) {
                failure="device LLE publication release failed";return;
            }
            published[i]=published[--published_count];continue;
        }
        if(into_kernel) {
            if(pa!=published[i].pa){failure="device LLE published alias changed between calls";return;}
            memcpy(ram+pa,guest_memory+va,4096);
        } else {memcpy(guest_memory+va,ram+pa,4096);published[i].pa=pa;}
        ++i;
    }
}
static void publish_object(uint32_t object,uint32_t extension)
{
    if(object<0xd0000000u || (uint64_t)object+0x48+extension>0xd4000000ull) {
        failure="device LLE unexpected allocation namespace";return;
    }
    uint32_t end=(object+0x48+extension+4095)&~4095u;
    for(uint32_t va=object&~4095u;va<end;va+=4096) {
        unsigned i;for(i=0;i<published_count && published[i].va!=va;++i){}
        if(i<published_count)continue;
        uint32_t pa;
        if(published_count==MAX_PUBLISHED || !physical(va,&pa)) {
            failure="device LLE invalid publication";return;
        }
        if(guest_page(va)){failure="device LLE allocation collides with native memory";return;}
        if(!VirtualAlloc(guest_memory+va,4096,MEM_COMMIT,PAGE_READWRITE)) {
            failure="device LLE publication allocation failed";return;
        }
        published[published_count].va=va;published[published_count++].pa=pa;
        memcpy(guest_memory+va,ram+pa,4096);
    }
}
static void execute(unsigned ordinal,const uint32_t *args,uint32_t *result)
{
    if(ordinal!=65 && ordinal!=68){failure="device LLE unsupported ordinal";return;}
    /* Bound the qualified small device allocation family. Larger allocations
     * require separate original paging/allocator qualification, not guessed NTSTATUS. */
    if(ordinal==65 && args[1]>0x170){failure="device LLE extension exceeds qualified scope";return;}
    copy_published(1);if(failure)return;
    uint32_t pe=kernel_word(0x8001003cu),exports=0x80010000u+kernel_word(0x80010000u+pe+120);
    uint32_t base=kernel_word(exports+16),table=0x80010000u+kernel_word(exports+28);
    uint32_t entry=0x80010000u+kernel_word(table+4*(ordinal-base));
    if(failure)return;
    unsigned count=ordinal==65?6:1;
    uint32_t words[7]={RETURN_PC},sp=STACK,observed,flags=0x202;
    memcpy(words+1,args,count*4);
    IO_UC(uc_mem_write(cpu,STACK,words,(count+1)*4));
    IO_UC(uc_reg_write(cpu,UC_X86_REG_ESP,&sp));
    IO_UC(uc_reg_write(cpu,UC_X86_REG_EFLAGS,&flags));
    int saved[]={UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP};
    for(unsigned i=0;i<4;++i){uint32_t value=0x12345678u+i;IO_UC(uc_reg_write(cpu,saved[i],&value));}
    uc_err error=uc_emu_start(cpu,entry,RETURN_PC,0,1000000);
    if(failure)return;
    if(error){failure=uc_strerror(error);return;}
    IO_UC(uc_reg_read(cpu,UC_X86_REG_EIP,&observed));
    if(observed!=RETURN_PC){failure="device LLE instruction limit without return";return;}
    IO_UC(uc_reg_read(cpu,UC_X86_REG_ESP,&observed));
    if(observed!=STACK+(count+1)*4){failure="device LLE stdcall stack imbalance";return;}
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
        publish_object(object,args[1]);if(failure)return;
    }
    copy_published(0);
}
#undef IO_UC

const char *pilot_io_device_lle(unsigned ordinal,unsigned char *guest,
                               const uint32_t *args,uint32_t *result)
{
    AcquireSRWLockExclusive(&lock);
    if(!guest_memory)initialize(guest);
    if(!failure && guest_memory!=guest)failure="device LLE guest address space changed";
    if(!failure)execute(ordinal,args,result);
    const char *error=failure; /* An incomplete service poisons this context. */
    ReleaseSRWLockExclusive(&lock);return error;
}
