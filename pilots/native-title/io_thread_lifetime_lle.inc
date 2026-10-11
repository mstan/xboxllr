/* A bridge ownership call must not overwrite the outer controller's live
 * scratch frame. The caller saves/restores the complete CPU context; this
 * helper reserves the independently bounded nested frame for original code. */
static void thread_owned_execute(unsigned ordinal,const uint32_t *args,uint32_t *result)
{
    if(!execution_depth || execution_depth>=2) {
        failure="thread LLE nested ownership frame capacity";return;
    }
    ++execution_depth;execute(ordinal,args,result);--execution_depth;
}
static struct OriginalThread *thread_retain_created(unsigned ordinal,const uint32_t *args,
                                                    uint32_t expected_object,uint32_t parent)
{
    unsigned i;for(i=0;i<64 && original_threads[i].object;++i){}
    if(i==64){failure="thread LLE ownership capacity at registration";return NULL;}
    uint32_t handle=0,out=SCRATCH+0xe000,status=0,object=0;
    uc_err error=uc_mem_read(cpu,args[0],&handle,4);
    if(error){failure=uc_strerror(error);return NULL;}
    uint32_t ref[]={handle,0,out};thread_owned_execute(246,ref,&status);
    if(failure)return NULL;
    if(status){failure="thread LLE original ownership reference failed";return NULL;}
    error=uc_mem_read(cpu,out,&object,4);
    if(error){failure=uc_strerror(error);return NULL;}
    if(!object || thread_object(object) || (expected_object && object!=expected_object)) {
        failure="thread LLE creation handle does not own its actual new object";return NULL;
    }
    struct OriginalThread *thread=original_threads+i;
    memset(thread,0,sizeof(*thread));thread->handle=handle;thread->object=object;
    thread->arguments=ordinal==254?1:2;thread->birth_parent=parent;
    thread->entry=args[ordinal==254?2:9];
    thread->context[0]=args[ordinal==254?3:5];thread->context[1]=ordinal==254?0:args[6];
    thread->bytes=0x140u+(ordinal==254?0:args[1]);
    publish_range(thread->object,thread->bytes);
    /* The real PCR page lasts for the kernel session. */
    if(!failure && !thread_pcr_published) {
        publish_range(0x8003d000u,4096);if(!failure)thread_pcr_published=1;
    }
    return failure?NULL:thread;
}
static void thread_cpu_birth_prepare(struct OriginalThread *parent,uint32_t *entry)
{
    uint32_t bp=0,status=0,object=0,pc=0,output=0;
    IO_UC(uc_reg_read(cpu,UC_X86_REG_EIP,&pc));
    IO_UC(uc_reg_read(cpu,UC_X86_REG_EBP,&bp));
    IO_UC(uc_reg_read(cpu,UC_X86_REG_ESI,&status));
    if(!parent->in_cpu || !parent->in_kernel || parent->creation_child ||
       kernel_word(0x8003da64u)!=parent->object || pc!=0x80025332u ||
       bp<parent->stack+4 || (uint64_t)bp+48>(uint64_t)parent->stack+parent->stack_bytes) {
        failure="original CPU birth outside actual creation frame";return;
    }
    /* Failed handle creation has a distinct original termination/cleanup
     * route. Keep it guarded until that lifetime is connected and qualified. */
    if((int32_t)status<0){failure="original CPU failed child creation cleanup requires qualification";return;}
    IO_UC(uc_mem_read(cpu,bp-4,&object,4));
    IO_UC(uc_mem_read(cpu,bp+8,&output,4));
    if(output!=parent->call_args[0]){failure="original CPU birth changed its handle output frame";return;}
    uc_context *context=NULL;IO_UC(uc_context_alloc(cpu,&context));
    uc_err error=uc_context_save(cpu,context);
    if(!error) {
        struct OriginalThread *child=thread_retain_created(parent->call_ordinal,parent->call_args,
                                                          object,parent->object);
        if(child)parent->creation_child=child->object;
        error=uc_context_restore(cpu,context);
    }
    uc_context_free(context);
    if(error)failure=uc_strerror(error);
    *entry=pc;
}
static void thread_after_call(unsigned ordinal,const uint32_t *args,uint32_t result)
{
    /* Preserve complete service results, notably EDX:EAX, across real
     * original reference/dereference operations used for bridge ownership. */
    int any=ordinal==254 || ordinal==255;
    for(unsigned i=0;i<64 && !any;++i)any=original_threads[i].object!=0;
    if(!any)return;
    uc_context *context=NULL;IO_UC(uc_context_alloc(cpu,&context));
    uc_err error=uc_context_save(cpu,context);
    if(error){uc_context_free(context);failure=uc_strerror(error);return;}
    if((ordinal==254 || ordinal==255) && (int32_t)result>=0) {
        struct OriginalThread *parent=thread_object(kernel_word(0x8003da64u));
        if(parent && parent->in_cpu) {
            struct OriginalThread *child=thread_object(parent->creation_child);
            if(!child || child->birth_parent!=parent->object) {
                failure="original CPU creation returned without early child ownership";
            } else {
                child->birth_parent=0;parent->creation_child=0;
            }
        } else {
            thread_retain_created(ordinal,args,0,0);
        }
    }
    if(ordinal==246 && (int32_t)result>=0) {
        uint32_t object=0;error=uc_mem_read(cpu,args[2],&object,4);
        if(error)failure=uc_strerror(error);
        struct OriginalThread *thread=thread_object(object);
        if(thread)++thread->refs;
        else if(!failure)failure="thread LLE reference returned an unqualified object";
    }
    if(ordinal==250 && !failure)--thread_object(args[0])->refs;
    if(ordinal==187 && (int32_t)result>=0)thread_note_close(args[0]);
    for(unsigned i=0;i<64 && !failure;++i) {
        struct OriginalThread *thread=original_threads+i;
        if(!thread->object || !kernel_word(thread->object+4))continue;
        if(thread->published) {
            uint32_t pa;
            for(uint32_t va=thread->stack;va<thread->stack+thread->stack_bytes;va+=4096)
                if(physical(va,&pa)){failure="thread LLE exited stack remains mapped";break;}
            if(failure)break;
            retire_owner(thread->stack);thread->published=0;
        }
        if(thread->closed && !thread->refs && !thread->birth_parent) {
            uint32_t status=0;thread_owned_execute(250,&thread->object,&status);
            if(!failure)retire_owner(thread->object);
            if(!failure)memset(thread,0,sizeof(*thread));
        }
    }
    error=uc_context_restore(cpu,context);uc_context_free(context);
    if(error)failure=uc_strerror(error);
}
