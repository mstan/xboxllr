/* Original thread lifetime bookkeeping for the native callback transport.
 * Handles, objects, stacks, TLS, scheduling and exit are original services.
 * The retained original object reference owns the publication until both
 * handle and explicit caller references are gone and the thread has exited. */
static struct OriginalThread {
    uint32_t handle,object,entry,context[2],bytes,stack,stack_bytes,refs;
    unsigned arguments;
    int closed,started,published;
} original_threads[64];
static PilotIoThread thread_callback;
static struct OriginalThread *pending_thread;
static unsigned native_thread_callback;
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
    if(native_thread_callback) {
        /* Until non-LIFO native continuations are implemented, permit only
         * observations that cannot switch away from this native host frame.
         * In particular, setting an event can immediately preempt the caller. */
        if(ordinal!=121 && ordinal!=125 && ordinal!=128 && ordinal!=256)
            failure="thread LLE native callback service requires continuation handoff";
        return;
    }
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
    if(!thread || thread->started || address!=thread->entry)return 0;
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
