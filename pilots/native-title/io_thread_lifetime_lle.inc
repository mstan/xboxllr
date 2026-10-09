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
        unsigned i;for(i=0;i<64 && original_threads[i].object;++i){}
        struct OriginalThread *thread=original_threads+i;
        uint32_t handle=0,out=SCRATCH+0xe000,status=0;
        error=uc_mem_read(cpu,args[0],&handle,4);
        if(error)failure=uc_strerror(error);
        if(!failure) {
            uint32_t ref[]={handle,0,out};execute(246,ref,&status);
            if(!failure && status)failure="thread LLE original ownership reference failed";
        }
        if(!failure) {
            memset(thread,0,sizeof(*thread));thread->handle=handle;
            error=uc_mem_read(cpu,out,&thread->object,4);
            if(error)failure=uc_strerror(error);
            thread->arguments=ordinal==254?1:2;
            thread->entry=args[ordinal==254?2:9];
            thread->context[0]=args[ordinal==254?3:5];thread->context[1]=ordinal==254?0:args[6];
            thread->bytes=0x140u+(ordinal==254?0:args[1]);
            if(!failure)publish_range(thread->object,thread->bytes);
            /* The real PCR page lasts for the kernel session. */
            if(!failure && !thread_pcr_published) {
                publish_range(0x8003d000u,4096);if(!failure)thread_pcr_published=1;
            }
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
        if(thread->closed && !thread->refs) {
            uint32_t status=0;execute(250,&thread->object,&status);
            if(!failure)retire_owner(thread->object);
            if(!failure)memset(thread,0,sizeof(*thread));
        }
    }
    error=uc_context_restore(cpu,context);uc_context_free(context);
    if(error)failure=uc_strerror(error);
}
