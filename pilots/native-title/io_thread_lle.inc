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
    int in_kernel,terminate;
    uint32_t exit_status;
    const char *callback_error;
} original_threads[64];
static PilotIoThread thread_callback;
static struct OriginalThread *pending_thread,*pending_thread_return;
static __declspec(thread) struct OriginalThread *native_thread_worker;
static int thread_pcr_published;
static void execute(unsigned ordinal,const uint32_t *args,uint32_t *result);
static void thread_after_call(unsigned ordinal,const uint32_t *args,uint32_t result);

static struct OriginalThread *thread_object(uint32_t object)
{
    for(unsigned i=0;i<64;++i)
        if(original_threads[i].object==object && object)return original_threads+i;
    return NULL;
}
static void thread_before_call(unsigned ordinal,const uint32_t *args)
{
    if(ordinal==254 || ordinal==255) {
        uint32_t current=kernel_word(0x8003da64u);
        if(!thread_callback || ((kernel_word(current+0x30)>>16)&255)!=31) {
            failure="thread LLE creation requires bound callback and qualified priority-31 caller";return;
        }
        unsigned i;for(i=0;i<64 && original_threads[i].object;++i){}
        if(i==64){failure="thread LLE ownership capacity";return;}
        if(ordinal==255 && (args[1]>0x1000 || args[2]>0x100000 || args[3]>0x10000))
            failure="thread LLE creation exceeds qualified stack/TLS/extension scope";
    }
    if(ordinal==250) {
        struct OriginalThread *thread=thread_object(args[0]);
        if(!thread || !thread->refs)failure="thread LLE unmatched caller object reference";
    }
}

/* Called only at an original kernel start call site, before executing any
 * generated instruction. The object must belong to a successful original
 * create request, and its arguments must match that request. */
static int thread_admit(uc_engine *engine,uint32_t address)
{
    if(!thread_callback || (kernel_word(0x8003da60u)&255))return 0;
    struct OriginalThread *thread=thread_object(kernel_word(0x8003da64u));
    if(!thread)return 0;
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

enum { THREAD_REQUEST_CALL=1, THREAD_REQUEST_FINISH=2 };
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
