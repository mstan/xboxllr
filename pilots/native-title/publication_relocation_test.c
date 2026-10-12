/* Inspect actual page-table ownership without exporting a production test API.
 * All allocation/relocation work executes the unchanged original services. */
#include "io_device_lle.c"

static unsigned char *test_guest;
static unsigned test_steps,test_reads,test_work,test_fences;
static uint64_t test_ticks=17,test_observed_blocks;
static const char *frozen_clock(void *context,uint64_t *ticks)
{ (void)context;++test_reads;*ticks=test_ticks;return NULL; }
static const char *frozen_step(void *context,uint64_t blocks)
{ (void)context;++test_steps;test_observed_blocks=blocks;return NULL; }
static const char *count_work(void) {++test_work;return NULL;}
static void slice_fence(uc_engine *engine,uint64_t address,uint32_t size,void *unused)
{
    (void)address;(void)size;(void)unused;
    /* Test-only continuation fence after the actual successor retires. */
    uint32_t end=RETURN_PC;++test_fences;
    if(uc_reg_write(engine,UC_X86_REG_EIP,&end))forbidden(engine,"test fence failed");
    uc_emu_stop(engine);
}
static uint32_t original_call(unsigned ordinal,const uint32_t *args)
{
    uint32_t result=0;
    const char *error=pilot_io_device_lle(ordinal,test_guest,args,&result);
    if(error){fprintf(stderr,"FAIL ordinal=%u: %s\n",ordinal,error);exit(2);}
    return result;
}
int main(int argc,char **argv)
{
    const char *mode=argc>1?argv[1]:"relocate";
    test_guest=VirtualAlloc(NULL,0x100000000ull,MEM_RESERVE,PAGE_NOACCESS);
    if(!test_guest || !VirtualAlloc(test_guest+0x10000,0x10000,MEM_COMMIT,PAGE_READWRITE))return 3;
    const char *error=pilot_io_device_initialize(test_guest);
    if(!error)error=pilot_io_device_image(test_guest,0x10000,0x10000);
    if(error){fprintf(stderr,"%s\n",error);return 3;}
    uint32_t inputs[5]={0x7800000,0x800000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,0},out[7],status=0;
    error=pilot_io_device_vm(184,test_guest,inputs,out,&status);
    if(error || status)return 3;
    uint32_t va=0x7dfd000;
    *(uint32_t*)(test_guest+va)=0xabcdef12;
    uint32_t pa=original_call(173,&va);
    uint32_t table_pa=ram_word(0xf000u+(va>>22)*4)&0xfffff000u;
    if(!strcmp(mode,"reload-slice")) {
        const uint32_t sites[]={0x8001c16fu,0x80023587u};
        const uint32_t successors[]={0x8001c174u,0x8002358bu};
        const int regs[]={UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,
            UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EBP,UC_X86_REG_ESP,UC_X86_REG_EFLAGS,UC_X86_REG_CR3};
        uint32_t cr3=0,flags=0x202;
        if(pilot_io_device_clock_bind_step(frozen_clock,NULL,frozen_step) ||
           pilot_io_device_work_bind(count_work) || pilot_usb_pit_time(test_ticks))return 4;
        if(uc_reg_read(cpu,UC_X86_REG_CR3,&cr3) || uc_reg_write(cpu,UC_X86_REG_EAX,&cr3) ||
           uc_reg_write(cpu,UC_X86_REG_EFLAGS,&flags))return 4;
        for(unsigned site=0;site<2;++site) {
            uint32_t before[10],after[10],pc=0;
            for(unsigned r=0;r<10;++r)if(uc_reg_read(cpu,regs[r],before+r))return 4;
            test_steps=test_reads=test_work=test_fences=0;
            uint64_t blocks=clock_block_entries,ticks_before=0,deadline=0;
            if(pilot_usb_pit_clock(&ticks_before,&deadline))return 4;
            uc_hook fence;
            if(uc_hook_add(cpu,&fence,UC_HOOK_CODE,slice_fence,NULL,successors[site],successors[site]))return 4;
            run_original(sites[site],1,0);
            if(uc_hook_del(cpu,fence))return 4;
            uint64_t ticks_after=0;
            if(pilot_usb_pit_clock(&ticks_after,&deadline))return 4;
            if(failure || translation_retire_pc || translation_refresh_pending ||
               test_steps!=2 || test_reads!=2 || test_work!=2 || test_fences!=1 ||
               ticks_before!=test_ticks || ticks_after!=ticks_before ||
               test_observed_blocks<blocks || clock_block_entries<test_observed_blocks)return 4;
            /* One pump starts this call; the second follows the ordinary
             * successor slice. Reload refresh itself must add no third pump.
             * Observed block entries may increase at stopped boundaries. */
            if(uc_reg_read(cpu,UC_X86_REG_EIP,&pc) || pc!=RETURN_PC)return 4;
            for(unsigned r=0;r<10;++r)if(uc_reg_read(cpu,regs[r],after+r) || before[r]!=after[r])return 4;
        }
        puts("PASS both actual CR3 reloads at one-instruction slice boundary; registers preserved; exactly two normal pumps, no refresh pump, frozen PIT input");return 0;
    }
    if(!strcmp(mode,"reload-bytes")) {
        uint32_t address=0x80023584u,code_pa=0,cr3=0;
        if(!physical(address,&code_pa) || uc_reg_read(cpu,UC_X86_REG_CR3,&cr3) ||
           uc_reg_write(cpu,UC_X86_REG_EAX,&cr3))return 4;
        /* Explicit negative input: alter the hash-bound helper's MOV source. */
        unsigned char *byte=physical_backing(code_pa);*byte^=1;
        translation_reload(cpu,address+3,3,NULL);
        if(!failure || strcmp(failure,"device LLE unqualified translation reload") ||
           translation_retire_pc || translation_refresh_pending)return 4;
        puts("PASS altered reload instruction bytes rejected");return 0;
    }
    if(!strcmp(mode,"collision")) {
        /* Deliberately corrupt one actual allocated PTE into a live alias.
         * This negative control is not evidence for allocator semantics. */
        uint32_t alias=va-4096;
        uint32_t pte_pa=table_pa+((alias>>12)&1023)*4;
        uint32_t old_pte=ram_word(pte_pa),aliased_pte=pa|(old_pte&4095);
        memcpy(physical_backing(pte_pa),&aliased_pte,4);
        unsigned slot=publication_slot(alias);
        if(slot==published_count)return 4;
        /* Reconciliation must refuse two real, resident VAs with one PFN. */
        unsigned before=published_count;
        check_publications_impl(0);
        if(!failure || strcmp(failure,"device LLE relocation into pinned or aliased storage") ||
           before!=published_count || *(uint32_t*)(test_guest+va)!=0xabcdef12)return 4;
        puts("PASS genuine duplicate publication rejected without publishing or changing bytes");return 0;
    }
    if(!strcmp(mode,"reload-guard")) {
        uint32_t ax=1,cr3=0;
        if(uc_reg_read(cpu,UC_X86_REG_CR3,&cr3))return 4;
        ax=cr3^0x1000u;
        if(uc_reg_write(cpu,UC_X86_REG_EAX,&ax))return 4;
        translation_reload(cpu,0x80023587u,3,NULL);
        if(!failure || strcmp(failure,"device LLE unqualified translation reload") ||
           translation_retire_pc || translation_refresh_pending)return 4;
        puts("PASS unqualified CR3 switch rejected before cache mutation");return 0;
    }
    if(strcmp(mode,"relocate") || table_pa>=pa)return 3;
    uint32_t args[5]={pa-table_pa+4096,table_pa,pa+4095,4096,PAGE_READWRITE};
    uint32_t contiguous=original_call(166,args);
    uint32_t moved=original_call(173,&va);
    uint32_t moved_table=ram_word(0xf000u+(va>>22)*4)&0xfffff000u;
    if(contiguous!=0x80000000u+table_pa || moved==pa || moved_table==table_pa ||
       *(uint32_t*)(test_guest+va)!=0xabcdef12 || translation_retire_pc || translation_refresh_pending)return 4;
    fprintf(stderr,"original table %08x->%08x; user %08x->%08x; contiguous=%08x\n",
            table_pa,moved_table,pa,moved,contiguous);
    *(uint32_t*)(test_guest+contiguous)=0x99887766;
    if(*(uint32_t*)(test_guest+va)!=0xabcdef12)return 4;
    original_call(171,&contiguous);
    if(*(uint32_t*)(test_guest+va)!=0xabcdef12)return 4;
    puts("PASS original page-table and user relocation; independent bytes and original free");
    return 0;
}
