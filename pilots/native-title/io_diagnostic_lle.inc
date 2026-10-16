/* Passive raw-engine observation; never a CPU/device admission path. */
static struct {
    PilotIoDiagnostic receiver;
    uint32_t entry,sp,caller,thread,driver;
    unsigned active,finished;
} original_diagnostic;
static __declspec(thread) unsigned original_diagnostic_borrow;

static int diagnostic_ram_read(uint32_t pa,void *out,uint32_t bytes)
{
    if(!out || !bytes || (uint64_t)pa+bytes>RAM_BYTES)return 0;
    for(uint32_t left=bytes;left;) {
        uint32_t span=4096-(pa&4095);if(span>left)span=left;
        memcpy(out,physical_backing(pa),span);
        out=(unsigned char*)out+span;pa+=span;left-=span;
    }
    return 1;
}
static int diagnostic_translation(uint32_t va,uint32_t *pa)
{
    uint32_t pde=0,pte=0;
    if(!diagnostic_ram_read(0xf000u+(va>>22)*4,&pde,4) || !(pde&1))return 0;
    if(pde&128)*pa=(pde&0xffc00000u)+(va&0x3fffff);
    else {
        if(!diagnostic_ram_read((pde&0xfffff000u)+((va>>12)&1023)*4,&pte,4) || !(pte&1))return 0;
        *pa=(pte&0xfffff000u)+(va&4095);
    }
    return *pa<RAM_BYTES;
}
int pilot_io_device_diagnostic_read(uint32_t address,void *out,uint32_t bytes,int physical_address)
{
    if((!original_diagnostic_borrow && !device_bus_borrow) || !execution_depth ||
       !out || !bytes || (uint64_t)address+bytes>0x100000000ull)return 0;
    if(physical_address)return diagnostic_ram_read(address,out,bytes);
    /* Two passes guarantee no partial output on an invalid later page. */
    for(unsigned pass=0;pass<2;++pass)for(uint32_t at=address,left=bytes;left;) {
        uint32_t pa,span=4096-(at&4095);if(span>left)span=left;
        const unsigned char *backing=NULL;
        uint32_t page=at&~4095u;
        /* Only invalid_memory's plain RAM/native mappings enter mapped[].
         * uc_mmio_map regions and physical MMIO aliases cannot pass this
         * backing/type check. No uc_mem_read can invoke a device callback. */
        for(unsigned i=0;i<mapped_count;++i)if(mapped[i].va==page) {
            if(mapped[i].pa<RAM_BYTES)backing=mapped[i].backing;
            else if(mapped[i].pa==UINT32_MAX && mapped[i].backing==guest_memory+page) {
                MEMORY_BASIC_INFORMATION info;
                if(VirtualQuery(mapped[i].backing,&info,sizeof(info))==sizeof(info) &&
                   info.State==MEM_COMMIT && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))backing=mapped[i].backing;
            }
            if(!backing)return 0;break;
        }
        if(!backing) {
            for(unsigned i=0;i<8;++i)if((device_mmio[i].callback || device_mmio[i].memory) && at>=device_mmio[i].first &&
               (uint64_t)at<(uint64_t)device_mmio[i].first+device_mmio[i].bytes)return 0;
            if(!diagnostic_translation(at,&pa) || (uint64_t)pa+span>RAM_BYTES)return 0;
            backing=physical_backing(pa)-(at&4095);
        }
        if(pass)memcpy((unsigned char*)out+at-address,backing+(at&4095),span);
        at+=span;left-=span;
    }
    return 1;
}
static int diagnostic_context(uc_engine *engine,PilotIoDiagnosticEvent *event)
{
    memset(event,0,sizeof(*event));
    if(uc_reg_read(engine,UC_X86_REG_EIP,&event->pc)!=UC_ERR_OK ||
       uc_reg_read(engine,UC_X86_REG_EFLAGS,&event->flags)!=UC_ERR_OK)return 0;
    for(unsigned i=0;i<8;++i)if(uc_reg_read(engine,general_regs[i],event->regs+i)!=UC_ERR_OK)return 0;
    uint32_t pa;
    if(!diagnostic_translation(0x8003da64u,&pa) || !diagnostic_ram_read(pa,&event->thread,4))return 0;
    struct OriginalThread *thread=thread_object(event->thread);
    event->in_kernel=thread?thread->in_kernel:0xffffffffu;
    event->entry=original_diagnostic.entry;event->entry_sp=original_diagnostic.sp;
    event->caller=original_diagnostic.caller;event->driver=original_diagnostic.driver;
    return 1;
}
int pilot_io_device_diagnostic_context(PilotIoDiagnosticEvent *event)
{
    if(!event || !device_bus_borrow || original_diagnostic_borrow || !execution_depth)return 0;
    if(!diagnostic_context(cpu,event))return 0;
    event->kind=compiled_mmio_scope?PILOT_IO_DIAGNOSTIC_COMPILED_MMIO:PILOT_IO_DIAGNOSTIC_MMIO;
    return 1;
}
static void diagnostic_block(uc_engine *engine,uint32_t address)
{
    if(!original_diagnostic.receiver || original_diagnostic.finished)return;
    if(address!=original_diagnostic.entry &&
       (!original_diagnostic.active || address!=original_diagnostic.caller))return;
    PilotIoDiagnosticEvent event;
    if(!diagnostic_context(engine,&event))return;
    struct OriginalThread *thread=thread_object(event.thread);
    if(!thread || !thread->in_cpu || event.regs[4]<thread->stack ||
       (uint64_t)event.regs[4]+4>(uint64_t)thread->stack+thread->stack_bytes)return;
    if(address==original_diagnostic.entry && !original_diagnostic.active) {
        uint32_t caller=0;
        if(uc_mem_read(engine,event.regs[4],&caller,4)!=UC_ERR_OK)return;
        original_diagnostic.active=1;original_diagnostic.sp=event.regs[4];
        original_diagnostic.thread=event.thread;original_diagnostic.caller=caller;
        original_diagnostic.driver=event.regs[1];
        event.kind=PILOT_IO_DIAGNOSTIC_ENTRY;event.entry_sp=event.regs[4];
        event.caller=caller;event.driver=event.regs[1];
    } else if(original_diagnostic.active && address==original_diagnostic.caller &&
              event.thread==original_diagnostic.thread &&
              event.regs[4]==original_diagnostic.sp+4) {
        event.kind=PILOT_IO_DIAGNOSTIC_RETURN;original_diagnostic.finished=1;
    } else {
        if(address==original_diagnostic.caller && event.thread==original_diagnostic.thread)
            original_diagnostic.finished=1; // Unsupported frame: no later false return.
        return;
    }
    /* Reuse the existing same-thread no-CPU-reentry boundary. This is not a
     * MMIO transaction (diagnostic_context explicitly distinguishes it). */
    original_diagnostic_borrow=1;device_bus_borrow=1;device_bus_dma_lease=0;
    original_diagnostic.receiver(&event);
    device_bus_borrow=0;device_bus_dma_lease=0;original_diagnostic_borrow=0;
}
const char *pilot_io_device_diagnostic_bind(uint32_t entry,uint32_t bytes,
    const char *sha256,PilotIoDiagnostic receiver)
{
    if(device_bus_borrow || original_diagnostic_borrow)return "diagnostic bind during borrowed owner";
    AcquireSRWLockExclusive(&lock);const char *error=NULL;
    if(!cpu || execution_depth)error="diagnostic binding requires idle original CPU";
    else if(!receiver)memset(&original_diagnostic,0,sizeof(original_diagnostic));
    else if(!sha256 || strlen(sha256)!=64 || !bytes || bytes>16384 ||
            entry<0x10000u || (uint64_t)entry+bytes>0x80000000ull)
        error="diagnostic source boundary invalid";
    else {
        for(uint64_t page=entry&~4095u;page<(uint64_t)entry+bytes;page+=4096)
            if(!guest_page((uint32_t)page)){error="diagnostic source is not resident";break;}
        unsigned char digest[32];char actual[65];
        if(!error && BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,guest_memory+entry,bytes,digest,32)<0)
            error="diagnostic source hash failed";
        if(!error) {
            for(unsigned i=0;i<32;++i)sprintf(actual+i*2,"%02x",digest[i]);
            if(strcmp(actual,sha256))error="diagnostic source differs from reviewed bytes";
        }
        if(!error) {
            memset(&original_diagnostic,0,sizeof(original_diagnostic));
            original_diagnostic.receiver=receiver;original_diagnostic.entry=entry;
        }
    }
    ReleaseSRWLockExclusive(&lock);return error;
}
