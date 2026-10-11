/* Original thread lifetime bookkeeping for the native callback transport.
 * Handles, objects, stacks, TLS, scheduling and exit are original services.
 * The retained original object reference owns the publication until both
 * handle and explicit caller references are gone and the thread has exited. */
static struct OriginalThread {
    uint32_t handle,object,entry,context[2],bytes,stack,stack_bytes,refs;
    unsigned arguments;
    int closed,started,published;
    HANDLE host_thread,request_event,resume_event;
    uint32_t native_regs[8],call_regs[8],call_args[10],return_pc,call_return,call_flags;
    unsigned call_ordinal,call_arguments,request;
    struct VmCallerAccess vm_access;
    int in_kernel,in_checkpoint,in_cpu,cpu_used,terminate,cpu_exit_request;
    uint32_t cpu_entry,cpu_return;
    uint32_t creation_child,birth_parent;
    uint32_t checkpoint_pc,checkpoint_flags,initial_flags;
    volatile LONG checkpoint_requested;
    uint32_t exit_status;
    const char *callback_error;
} original_threads[64];
static PilotIoThread thread_callback;
static struct OriginalThread *pending_thread,*pending_thread_return,*pending_cpu_service,*pending_cpu_birth;
static __declspec(thread) struct OriginalThread *native_thread_worker;
static int thread_pcr_published;
static unsigned original_cpu_threads;
static void execute(unsigned ordinal,const uint32_t *args,uint32_t *result);
static uint32_t original_export(unsigned ordinal);
static void thread_after_call(unsigned ordinal,const uint32_t *args,uint32_t result);
static void thread_cpu_birth_prepare(struct OriginalThread *parent,uint32_t *entry);
static int settings_owner_initialized;
static void settings_before_query(const uint32_t *args)
{
    if(args[0]>0xff) {failure="original factory/encrypted settings require owner integration";return;}
    if(args[0]>18 && args[0]!=0xff)return; /* Original unknown-key error needs no EEPROM. */
    if(settings_owner_initialized)return;
#ifdef PILOT_USB_LLE
    if(!pilot_usb_board_eeprom_bound()) {failure="original settings require shared EEPROM owner";return;}
    /* Retire captured boot-input validity once for this settings owner.
     * Original code loads all256 bytes through actual HAL word transactions
     * and owns its subsequent cache; no query result or bytes are supplied. */
    uint32_t pa=0,invalid=0;
    if(!physical(0x8004f920u,&pa)){failure="original settings cache flag outside RAM";return;}
    memcpy(physical_backing(pa),&invalid,4);settings_owner_initialized=1;
#else
    failure="original settings require connected board owner";
#endif
}

static struct OriginalThread *thread_object(uint32_t object)
{
    for(unsigned i=0;i<64;++i)
        if(original_threads[i].object==object && object)return original_threads+i;
    return NULL;
}
static int thread_cpu_import_fault(uc_engine *engine,uint64_t address)
{
    if(!original_cpu_threads || address<0xfe000004u || address>=0xfe000600u || (address&3))return 0;
    struct OriginalThread *thread=thread_object(kernel_word(0x8003da64u));
    if(!thread || !thread->in_cpu)return 0;
    if(thread->in_kernel) {
        failure="original CPU import reached during an unfinished service";
        uc_emu_stop(engine);return 1;
    }
    thread->call_ordinal=(unsigned)(address-0xfe000000u)/4;
    pending_cpu_service=thread;uc_emu_stop(engine);return 1;
}
static int thread_cpu_npx_owner(void)
{
    if(!original_cpu_threads)return 0;
    struct OriginalThread *thread=thread_object(kernel_word(0x8003da64u));
    return thread && thread->in_cpu;
}
/* Check returns before the broad kernel-code admission: an ordinary start
 * routine returns to its actual Ps thunk, while the loader returns to its
 * private adapter. Never execute a fake instruction at either boundary. */
static int thread_cpu_admit(uc_engine *engine,uint32_t address)
{
    if(!original_cpu_threads)return 0;
    struct OriginalThread *thread=thread_object(kernel_word(0x8003da64u));
    if(!thread || !thread->in_cpu)return 0;
    /* In this hash-bound kernel the handle has been installed and ESI holds
     * its status before the next original ready-thread call. Stop outside the
     * engine to retain ownership before that call can dispatch the child. */
    if(address==0x80025332u && thread->in_kernel &&
       (thread->call_ordinal==254 || thread->call_ordinal==255) && !thread->creation_child) {
        pending_cpu_birth=thread;uc_emu_stop(engine);return 1;
    }
    if(thread->in_kernel && address==thread->call_return) {
        pending_thread_return=thread;uc_emu_stop(engine);return 1;
    }
    if(address==thread->cpu_return) {
        pending_thread_return=thread;uc_emu_stop(engine);return 1;
    }
    return 0;
}
static void thread_before_call(unsigned ordinal,const uint32_t *args)
{
    if(ordinal==24) {settings_before_query(args);if(failure)return;}
    if(ordinal==254 || ordinal==255) {
        uint32_t current=kernel_word(0x8003da64u);
        struct OriginalThread *parent=thread_object(current);
        int original_parent=parent && parent->in_cpu;
        if(!thread_callback || (!original_parent && ((kernel_word(current+0x30)>>16)&255)!=31)) {
            fprintf(stderr,"[THREAD LLE CREATE GUARD] ordinal=%u current=%08x priority=%u suspended=%u entry=%08x\n",
                ordinal,current,(kernel_word(current+0x30)>>16)&255,ordinal==255?args[7]:0,args[ordinal==254?2:9]);
            failure="thread LLE creation requires bound callback and qualified priority-31 caller";return;
        }
        unsigned i;for(i=0;i<64 && original_threads[i].object;++i){}
        if(i==64){failure="thread LLE ownership capacity";return;}
        if(original_parent && parent->creation_child) {
            failure="original CPU creation has an unfinished child admission";return;
        }
        if(ordinal==255 && (args[1]>0x1000 || args[2]>0x100000 || args[3]>0x10000))
            failure="thread LLE creation exceeds qualified stack/TLS/extension scope";
    }
    if(ordinal==250) {
        struct OriginalThread *thread=thread_object(args[0]);
        if(!thread || !thread->refs)failure="thread LLE unmatched caller object reference";
    }
}

/* Admit an original start or a pending service's actual return. Starts must
 * belong to a successful creation and retain that request's arguments. */
static int thread_admit(uc_engine *engine,uint32_t address)
{
    if(!thread_callback)return 0;
    struct OriginalThread *thread=thread_object(kernel_word(0x8003da64u));
    if(!thread)return 0;
    if(thread->in_checkpoint && address==thread->checkpoint_pc) {
        pending_thread_return=thread;uc_emu_stop(engine);return 1;
    }
    /* ISR/DPC admission has already run. Ordinary original CPU instructions
     * keep the actual thread/IRQL/FP context, including raised-IRQL callers. */
    if(thread->in_cpu && address>=0x10000u && address<0x80000000u)return 1;
    if(kernel_word(0x8003da60u)&255)return 0;
    if(thread->in_kernel && address==thread->call_return) {
        pending_thread_return=thread;uc_emu_stop(engine);return 1;
    }
    if(thread->started || address!=thread->entry)return 0;
    uint32_t sp,frame[3],flags;
    if(uc_reg_read(engine,UC_X86_REG_ESP,&sp)!=UC_ERR_OK ||
       uc_mem_read(engine,sp,frame,sizeof(frame))!=UC_ERR_OK ||
       uc_reg_read(engine,UC_X86_REG_EFLAGS,&flags)!=UC_ERR_OK)return 0;
    if(frame[0]!=(thread->arguments==1?0x80025196u:0x80020134u) ||
       frame[1]!=thread->context[0] || (thread->arguments==2 && frame[2]!=thread->context[1]) ||
       (flags&0x600)!=0x200)return 0;
    thread->started=1;pending_thread=thread;
    pending_callback=address;uc_emu_stop(engine);return 1;
}

enum { THREAD_REQUEST_CALL=1, THREAD_REQUEST_FINISH=2, THREAD_REQUEST_CHECKPOINT=3,
       THREAD_REQUEST_CPU=4 };
/* Only the original engine's controller runs guest work. Host workers retain
 * C call stacks and TLS while blocked; no host priority/timeout chooses which
 * one runs. The event handshakes transfer sole execution ownership. */
static DWORD WINAPI original_thread_worker(void *argument)
{
    struct OriginalThread *thread=argument;native_thread_worker=thread;
    if(WaitForSingleObject(thread->resume_event,INFINITE)!=WAIT_OBJECT_0)
        thread->callback_error="thread LLE initial host handoff failed";
    else thread->callback_error=thread_callback(thread->entry,thread->native_regs,
        thread->arguments,&thread->terminate,&thread->exit_status);
    thread->request=THREAD_REQUEST_FINISH;
    if(!SetEvent(thread->request_event)) {
        /* An OS synchronization failure cannot leave the controller waiting
         * forever or be reported as completed original work. */
        fprintf(stderr,"[PILOT STOP] thread LLE host handoff failed: %lu\n",GetLastError());
        ExitProcess(2);
    }
    native_thread_worker=NULL;return 0;
}
const char *pilot_io_device_thread_call(unsigned ordinal,uint32_t regs[8])
{
    struct OriginalThread *thread=native_thread_worker;
    if(!thread)return "thread LLE guest-frame call outside original native worker";
    memcpy(thread->call_regs,regs,sizeof(thread->call_regs));
    thread->call_ordinal=ordinal;thread->request=THREAD_REQUEST_CALL;
    if(!SetEvent(thread->request_event) || WaitForSingleObject(thread->resume_event,INFINITE)!=WAIT_OBJECT_0)
        return "thread LLE service host handoff failed";
    if(thread->callback_error)return thread->callback_error;
    memcpy(regs,thread->call_regs,sizeof(thread->call_regs));return NULL;
}
volatile int32_t *pilot_io_device_thread_checkpoint_request(void)
{
    return native_thread_worker?&native_thread_worker->checkpoint_requested:NULL;
}
const char *pilot_io_device_thread_initial_flags(uint32_t *flags)
{
    if(!native_thread_worker || !flags)return "thread LLE initial flags outside native worker";
    *flags=native_thread_worker->initial_flags;return NULL;
}
const char *pilot_io_device_thread_execute(uint32_t entry,uint32_t regs[8])
{
    struct OriginalThread *thread=native_thread_worker;
    if(!thread || !regs)return "original CPU execution outside admitted thread";
    if(thread->cpu_used || entry<0x10000u || entry>=0x80000000u ||
       !guest_page(entry&~4095u) || regs[4]<thread->stack ||
       (uint64_t)regs[4]+12>(uint64_t)thread->stack+thread->stack_bytes ||
       (entry!=thread->entry && (thread->entry!=PILOT_IO_BOOTSTRAP_ENTRY ||
        entry<image_start || (uint64_t)entry>=image_start+image_bytes)))
        return "original CPU execution requires an unchanged entry or owned loader frame";
    memcpy(thread->call_regs,regs,sizeof(thread->call_regs));thread->cpu_entry=entry;
    memcpy(&thread->cpu_return,guest_memory+regs[4],4);
    uint32_t expected=thread->entry==PILOT_IO_BOOTSTRAP_ENTRY?PILOT_IO_BOOTSTRAP_ENTRY:thread->return_pc;
    if(thread->cpu_return!=expected)return "original CPU execution has an invalid entry return";
    thread->cpu_used=1;thread->request=THREAD_REQUEST_CPU;
    if(!SetEvent(thread->request_event) || WaitForSingleObject(thread->resume_event,INFINITE)!=WAIT_OBJECT_0)
        return "original CPU execution host handoff failed";
    if(thread->callback_error)return thread->callback_error;
    memcpy(regs,thread->call_regs,sizeof(thread->call_regs));return NULL;
}
const char *pilot_io_device_thread_completion(int *terminated,uint32_t *status)
{
    struct OriginalThread *thread=native_thread_worker;
    if(!thread || !thread->cpu_used || !terminated || !status)
        return "original CPU completion outside admitted worker";
    *terminated=thread->cpu_exit_request;*status=thread->exit_status;return NULL;
}
const char *pilot_io_device_thread_checkpoint(uint32_t pc,uint32_t regs[8],uint32_t *flags)
{
    struct OriginalThread *thread=native_thread_worker;
    if(!thread || !regs || !flags)return "thread LLE checkpoint outside native worker";
    if(!InterlockedCompareExchange(&thread->checkpoint_requested,0,0))return NULL;
    /* Caller certifies a fully materialized integer instruction boundary.
     * Privileged/debug/ambiguous flags and nonresident continuations stay
     * guarded. FP/SIMD compute needs its own connected context transport. */
    if((*flags&~0xed7u) || !(*flags&2) || pc<0x10000u || pc>=0x80000000u ||
       !guest_page(pc&~4095u) || regs[4]<thread->stack ||
       (uint64_t)regs[4]+4>(uint64_t)thread->stack+thread->stack_bytes)
        return "thread LLE uncertified integer checkpoint state";
    memcpy(thread->call_regs,regs,sizeof(thread->call_regs));
    thread->checkpoint_pc=pc;thread->checkpoint_flags=*flags;
    thread->request=THREAD_REQUEST_CHECKPOINT;
    if(!SetEvent(thread->request_event) || WaitForSingleObject(thread->resume_event,INFINITE)!=WAIT_OBJECT_0)
        return "thread LLE checkpoint host handoff failed";
    if(thread->callback_error)return thread->callback_error;
    memcpy(regs,thread->call_regs,sizeof(thread->call_regs));*flags=thread->checkpoint_flags;
    return NULL;
}
