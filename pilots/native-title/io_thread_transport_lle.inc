static uint32_t original_export(unsigned ordinal)
{
    uint32_t pe=kernel_word(0x8001003cu),exports=0x80010000u+kernel_word(0x80010000u+pe+120);
    uint32_t first=kernel_word(exports+16),table=0x80010000u+kernel_word(exports+28);
    return 0x80010000u+kernel_word(table+4*(ordinal-first));
}
static unsigned thread_service_arguments(unsigned ordinal)
{
    /* Original scheduling/dispatcher operations. Other families must connect
     * their publication/lifetime postconditions before using this boundary. */
    switch(ordinal) {
    case 121:case 125:return 0;
    case 97:case 128:case 137:case 186:case 187:case 256:return 1;
    case 113:case 148:case 224:case 225:case 231:return 2;
    case 107:case 108:case 119:case 233:return 3;
    case 149:case 189:case 234:return 4;
    case 150:case 159:return 5;
    default:
        fprintf(stderr,"[THREAD LLE SERVICE GUARD] ordinal=%u thread=%08x\n",ordinal,kernel_word(0x8003da64u));
        failure="thread LLE service family requires continuation integration";return 0;
    }
}
static void thread_note_close(uint32_t handle)
{
    for(unsigned i=0;i<64;++i)
        if(original_threads[i].object && original_threads[i].handle==handle)original_threads[i].closed=1;
}
static void thread_service_prepare(struct OriginalThread *thread,uint32_t *entry)
{
    unsigned count=thread_service_arguments(thread->call_ordinal);if(failure)return;
    uint32_t sp=thread->call_regs[4];
    if(thread->in_kernel || kernel_word(0x8003da64u)!=thread->object ||
       sp<thread->stack || (uint64_t)sp+(count+1)*4>(uint64_t)thread->stack+thread->stack_bytes) {
        failure="thread LLE service frame outside executing original thread";return;
    }
    uint32_t return_pc;memcpy(&return_pc,guest_memory+sp,4);
    /* Preserve the actual generated caller's return address. The original
     * RET reaches this address before the transport resumes the waiting C
     * frame. Never rewrite a blocked thread's stack onto a shared scratch VA. */
    if(return_pc<0x10000u || return_pc>=0x80000000u || !guest_page(return_pc&~4095u)) {
        failure="thread LLE service return outside resident native caller";return;
    }
    memcpy(thread->call_args,guest_memory+sp+4,count*4);
    thread->call_arguments=count;thread->call_return=return_pc;
    thread->call_flags=2u|(caller_if?0x200u:0);
    thread->in_kernel=1;
    check_publications(1);if(failure)return;
    for(unsigned i=0;i<8;++i)IO_UC(uc_reg_write(cpu,general_regs[i],thread->call_regs+i));
    IO_UC(uc_reg_write(cpu,UC_X86_REG_EFLAGS,&thread->call_flags));
    *entry=original_export(thread->call_ordinal);
}
static void thread_service_complete(struct OriginalThread *thread)
{
    uint32_t regs[8],flags;
    for(unsigned i=0;i<8;++i)IO_UC(uc_reg_read(cpu,general_regs[i],regs+i));
    IO_UC(uc_reg_read(cpu,UC_X86_REG_EFLAGS,&flags));
    if(!thread->in_kernel || regs[4]!=thread->call_regs[4]+(thread->call_arguments+1)*4 ||
       (flags&0x600)!=(thread->call_flags&0x600) || regs[3]!=thread->call_regs[3] ||
       regs[5]!=thread->call_regs[5] || regs[6]!=thread->call_regs[6] || regs[7]!=thread->call_regs[7]) {
        failure="thread LLE service returned with invalid ABI state";return;
    }
    check_publications(0);if(failure)return;
    if(thread->call_ordinal==187 && (int32_t)regs[0]>=0)thread_note_close(thread->call_args[0]);
    thread->in_kernel=0;memcpy(thread->call_regs,regs,sizeof(regs));
}
static void thread_checkpoint_prepare(struct OriginalThread *thread,uint32_t *entry)
{
    if(thread->in_kernel || thread->in_checkpoint || kernel_word(0x8003da64u)!=thread->object) {
        failure="thread LLE checkpoint does not own original execution";return;
    }
    check_publications(1);if(failure)return;
    for(unsigned i=0;i<8;++i)IO_UC(uc_reg_write(cpu,general_regs[i],thread->call_regs+i));
    IO_UC(uc_reg_write(cpu,UC_X86_REG_EFLAGS,&thread->checkpoint_flags));
    thread->in_checkpoint=1;*entry=thread->checkpoint_pc;
}
static void thread_checkpoint_complete(struct OriginalThread *thread)
{
    uint32_t regs[8],flags,pc;
    for(unsigned i=0;i<8;++i)IO_UC(uc_reg_read(cpu,general_regs[i],regs+i));
    IO_UC(uc_reg_read(cpu,UC_X86_REG_EFLAGS,&flags));
    IO_UC(uc_reg_read(cpu,UC_X86_REG_EIP,&pc));
    if(!thread->in_checkpoint || thread->in_kernel || pc!=thread->checkpoint_pc ||
       flags!=thread->checkpoint_flags || memcmp(regs,thread->call_regs,sizeof(regs))) {
        failure="thread LLE original checkpoint returned with changed integer context";return;
    }
    check_publications(0);if(failure)return;
    thread->in_checkpoint=0;
}
static DWORD thread_clock_wait(void)
{
    if(!clock_provider)return INFINITE;
#ifdef PILOT_USB_LLE
    uint64_t ticks=0,next=0,now=0;
    const char *error=pilot_usb_pit_clock(&ticks,&next);
    if(!error)error=clock_provider(clock_context,&now);
    if(error){failure=error;return 0;}
    if(now<ticks){failure="thread LLE backwards clock input";return 0;}
    if(!next)return INFINITE;
    if(now>=next)return 0;
    uint64_t delta=next-now;
    uint64_t ms=(delta/1125000)*1000+((delta%1125000)*1000+1124999)/1125000;
    return ms>=INFINITE?INFINITE-1:(DWORD)ms;
#else
    failure="thread LLE clock without PIT/PIC";return 0;
#endif
}
static void thread_handoff(struct OriginalThread *thread,int start,uint32_t *entry)
{
    if(start) {
        thread->request_event=CreateEventW(NULL,FALSE,FALSE,NULL);
        thread->resume_event=CreateEventW(NULL,FALSE,FALSE,NULL);
        if(thread->request_event && thread->resume_event)
            thread->host_thread=CreateThread(NULL,0,original_thread_worker,thread,0,NULL);
        if(!thread->host_thread) {
            if(thread->request_event)CloseHandle(thread->request_event);
            if(thread->resume_event)CloseHandle(thread->resume_event);
            thread->request_event=thread->resume_event=NULL;
            failure="thread LLE native continuation allocation failed";return;
        }
    }
    thread->request=0;
    InterlockedExchange(&thread->checkpoint_requested,0);
    InterlockedExchange(&current_irql,(LONG)(kernel_word(0x8003da60u)&255));
    if(failure)return;
    if(!SetEvent(thread->resume_event)){failure="thread LLE native resume failed";return;}
    DWORD wait=WAIT_TIMEOUT;
    while(wait==WAIT_TIMEOUT) {
        DWORD deadline=thread_clock_wait();if(failure)return;
        if(!deadline)InterlockedExchange(&thread->checkpoint_requested,1);
        ReleaseSRWLockExclusive(&lock);
        wait=WaitForSingleObject(thread->request_event,deadline?deadline:INFINITE);
        AcquireSRWLockExclusive(&lock);
        if(wait==WAIT_TIMEOUT)InterlockedExchange(&thread->checkpoint_requested,1);
    }
    if(wait!=WAIT_OBJECT_0){failure="thread LLE native request wait failed";return;}
    if(failure)return;
    if(thread->callback_error){failure=thread->callback_error;return;}
    if(thread->request==THREAD_REQUEST_CALL){thread_service_prepare(thread,entry);return;}
    if(thread->request==THREAD_REQUEST_CHECKPOINT){thread_checkpoint_prepare(thread,entry);return;}
    if(thread->request!=THREAD_REQUEST_FINISH){failure="thread LLE missing native continuation action";return;}
    /* Native work is finished; release its transport resources independently
     * of original thread/object lifetime. Original termination still follows. */
    if(WaitForSingleObject(thread->host_thread,INFINITE)!=WAIT_OBJECT_0 ||
       !CloseHandle(thread->host_thread) || !CloseHandle(thread->request_event) || !CloseHandle(thread->resume_event)) {
        failure="thread LLE native continuation retirement failed";return;
    }
    thread->host_thread=thread->request_event=thread->resume_event=NULL;
    check_publications(1);if(failure)return;
    for(unsigned i=0;i<8;++i)IO_UC(uc_reg_write(cpu,general_regs[i],thread->native_regs+i));
    *entry=thread->return_pc;
    if(thread->terminate) {
        uint32_t sp=thread->native_regs[4]-8,frame[]={0,thread->exit_status};
        if(sp<thread->stack || (uint64_t)sp+sizeof(frame)>(uint64_t)thread->stack+thread->stack_bytes) {
            failure="thread LLE termination frame outside original stack";return;
        }
        memcpy(guest_memory+sp,frame,sizeof(frame));
        IO_UC(uc_reg_write(cpu,UC_X86_REG_ESP,&sp));
        *entry=original_export(258);
    }
}
