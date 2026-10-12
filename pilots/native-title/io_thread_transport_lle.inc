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
    case 1:case 35:case 36:case 121:case 125:case 129:case 160:case 161:case 250:return 0;
    case 97:case 128:case 137:case 138:case 186:case 187:case 256:case 258:
    case 4:case 14:case 17:case 23:case 37:case 68:case 69:case 98:case 100:case 165:case 171:case 173:case 179:case 180:
    case 124:case 151:case 181:case 277:case 291:case 294:case 301:case 316:case 327:case 328:return 1;
    case 67:case 113:case 143:case 144:case 148:case 198:case 203:case 209:case 217:case 224:case 225:case 231:
    case 15:case 44:case 47:case 168:case 289:case 290:
    /* Original RtlTimeFieldsToTime/RtlTimeToTimeFields both take two
     * pointers and RET8. Execute their unchanged kernel bodies on this
     * caller's retained CPU; calendar conversion does not advance time. */
    case 304:case 305:return 2;
    case 99:case 107:case 108:case 119:case 145:case 178:case 182:case 199:case 215:case 233:case 246:
    case 175:case 279:return 3;
    case 2:case 45:case 50:case 149:case 189:case 204:case 234:return 4;
    case 24:case 150:case 159:case 166:case 184:case 211:case 218:case 226:case 254:return 5;
    case 3:case 46:case 65:case 202:return 6;
    case 109:return 7;
    case 219:case 236:return 8;
    case 190:return 9;
    case 196:case 200:case 207:case 255:return 10;
    default:
        fprintf(stderr,"[THREAD LLE SERVICE GUARD] ordinal=%u thread=%08x\n",ordinal,kernel_word(0x8003da64u));
        if(ordinal==49) {
            /* Observe the guarded nonreturning request; do not execute a
             * reset, replace it with RET, or mutate the caller's frame. */
            uint32_t sp=0,frame[2]={0},bp=0,parent[7]={0};
            if(uc_reg_read(cpu,UC_X86_REG_ESP,&sp)==UC_ERR_OK &&
               uc_mem_read(cpu,sp,frame,sizeof(frame))==UC_ERR_OK)
                fprintf(stderr,"[THREAD FIRMWARE REQUEST] caller=%08x routine=%u stack=%08x\n",frame[0],frame[1],sp);
            struct OriginalThread *thread=thread_object(kernel_word(0x8003da64u));
            if(thread && uc_reg_read(cpu,UC_X86_REG_EBP,&bp)==UC_ERR_OK &&
               bp>=thread->stack && (uint64_t)bp+sizeof(parent)<=(uint64_t)thread->stack+thread->stack_bytes &&
               uc_mem_read(cpu,bp,parent,sizeof(parent))==UC_ERR_OK)
                fprintf(stderr,"[THREAD FIRMWARE CALLER] caller=%08x args=%08x/%08x/%08x/%08x/%08x\n",
                    parent[1],parent[2],parent[3],parent[4],parent[5],parent[6]);
            if(thread)for(unsigned depth=0;depth<8;++depth) {
                uint32_t link[7]={0};
                if(bp<thread->stack || (uint64_t)bp+sizeof(link)>(uint64_t)thread->stack+thread->stack_bytes ||
                   uc_mem_read(cpu,bp,link,sizeof(link))!=UC_ERR_OK)break;
                fprintf(stderr,"[THREAD FIRMWARE ANCESTOR] depth=%u frame=%08x caller=%08x args=%08x/%08x/%08x/%08x/%08x\n",
                    depth,bp,link[1],link[2],link[3],link[4],link[5],link[6]);
                if(link[0]<=bp)break;bp=link[0];
            }
        }
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
    if(thread->call_ordinal==160 || thread->call_ordinal==161 || thread->call_ordinal==250)
        thread->call_args[0]=thread->call_regs[1];
    thread_before_call(thread->call_ordinal,thread->call_args);if(failure)return;
    storage_caller_prepare(thread->call_ordinal,thread->call_args);if(failure)return;
    device_caller_prepare(thread->call_ordinal,thread->call_args,&thread->device_access);if(failure)return;
    vm_caller_prepare(thread->call_ordinal,thread->call_args,&thread->vm_access);if(failure)return;
    section_caller_prepare(thread->call_ordinal,thread->call_args,&thread->section_access);if(failure)return;
    thread->call_arguments=count;thread->call_return=return_pc;
    thread->call_flags=thread->in_component?thread->component_flags:2u|(caller_if?0x200u:0);
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
    /* The actual original RET is the mutation boundary. Any nested original
     * query preserves this complete CPU context and uses a separate frame. */
    device_caller_complete(thread->call_ordinal,thread->call_args,regs[0],&thread->device_access);if(failure)return;
    vm_caller_complete(thread->call_ordinal,thread->call_args,regs[0],&thread->vm_access);
    if(failure)return;
    section_caller_complete(thread->call_ordinal,thread->call_args,regs[0],&thread->section_access);
    check_publications(0);if(failure)return;
    if(thread->call_ordinal==187 && (int32_t)regs[0]>=0)thread_note_close(thread->call_args[0]);
    thread->in_kernel=0;memcpy(thread->call_regs,regs,sizeof(regs));
}
static void thread_cpu_service_prepare(struct OriginalThread *thread,uint32_t *entry)
{
    unsigned count=thread_service_arguments(thread->call_ordinal);if(failure)return;
    uint32_t sp,return_pc,flags;
    IO_UC(uc_reg_read(cpu,UC_X86_REG_ESP,&sp));
    IO_UC(uc_reg_read(cpu,UC_X86_REG_EFLAGS,&flags));
    if(!thread->in_cpu || thread->in_kernel || kernel_word(0x8003da64u)!=thread->object ||
       sp<thread->stack || (uint64_t)sp+(count+1)*4>(uint64_t)thread->stack+thread->stack_bytes) {
        failure="original CPU service frame outside executing original thread";return;
    }
    IO_UC(owned_cpu_read(cpu,sp,&return_pc,4));
    if(return_pc<0x10000u || return_pc>=0x80000000u || !guest_page(return_pc&~4095u)) {
        failure="original CPU service return outside resident caller";return;
    }
    if(count)IO_UC(owned_cpu_read(cpu,sp+4,thread->call_args,count*4));
    /* ObfDereferenceObject is fastcall: its object is in the real ECX, not
     * an absent stack argument or a previous service's retained argument. */
    if(thread->call_ordinal==160 || thread->call_ordinal==161 || thread->call_ordinal==250)
        IO_UC(uc_reg_read(cpu,UC_X86_REG_ECX,thread->call_args));
    thread_before_call(thread->call_ordinal,thread->call_args);if(failure)return;
    storage_caller_prepare(thread->call_ordinal,thread->call_args);if(failure)return;
    device_caller_prepare(thread->call_ordinal,thread->call_args,&thread->device_access);if(failure)return;
    vm_caller_prepare(thread->call_ordinal,thread->call_args,&thread->vm_access);if(failure)return;
    section_caller_prepare(thread->call_ordinal,thread->call_args,&thread->section_access);if(failure)return;
    for(unsigned i=0;i<8;++i)IO_UC(uc_reg_read(cpu,general_regs[i],thread->call_regs+i));
    thread->call_arguments=count;thread->call_return=return_pc;thread->call_flags=flags;
    check_publications(1);if(failure)return;
    if(thread->call_ordinal==258) {
        /* Unwind only the retained host continuation. The actual CALL frame,
         * flags and FP state stay in this CPU until original Ps termination. */
        thread->cpu_exit_request=1;thread->terminate=1;
        thread->exit_status=thread->call_args[0];thread->in_cpu=0;--original_cpu_threads;
        return;
    }
    /* The private import gate transfers control only. Keep actual flags,
     * every register and FP state; the original CALL frame is already live. */
    thread->in_kernel=1;*entry=original_export(thread->call_ordinal);
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
static void thread_cpu_prepare(struct OriginalThread *thread,uint32_t *entry)
{
    uint32_t flags=0;IO_UC(uc_reg_read(cpu,UC_X86_REG_EFLAGS,&flags));
    if(thread->in_kernel || thread->in_checkpoint || thread->in_cpu ||
       kernel_word(0x8003da64u)!=thread->object || flags!=thread->initial_flags) {
        failure="original CPU entry no longer owns its admitted architectural context";return;
    }
    check_publications(1);if(failure)return;
    for(unsigned i=0;i<8;++i)IO_UC(uc_reg_write(cpu,general_regs[i],thread->call_regs+i));
    /* No reconstructed flags or FP initializer. The engine has not executed
     * since this original thread's admission; retain its complete CPU state. */
    original_components_sealed=1;
    thread->in_cpu=1;++original_cpu_threads;*entry=thread->cpu_entry;
}
static void thread_cpu_complete(struct OriginalThread *thread)
{
    uint32_t pc=0;IO_UC(uc_reg_read(cpu,UC_X86_REG_EIP,&pc));
    if(!thread->in_cpu || thread->in_kernel || thread->in_checkpoint ||
       pc!=thread->cpu_return || kernel_word(0x8003da64u)!=thread->object) {
        failure="original CPU returned outside its admitted thread";return;
    }
    for(unsigned i=0;i<8;++i)IO_UC(uc_reg_read(cpu,general_regs[i],thread->call_regs+i));
    check_publications(0);if(failure)return;
    thread->in_cpu=0;--original_cpu_threads;
}
static DWORD thread_clock_wait(void)
{
    if(!clock_provider)return INFINITE;
    /* External input is admitted only at stopped controller boundaries.
     * Host waiting must not schedule input or checkpoint opportunities. */
    if(clock_step)return INFINITE;
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
static void thread_handoff_impl(struct OriginalThread *thread,int start,uint32_t *entry)
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
    /* A controlled-input owner asks for the next real native checkpoint
     * before resuming. The retained PC/flags/FP boundary then returns to the
     * controller without depending on a host timer. QPC keeps its deadline. */
    InterlockedExchange(&thread->checkpoint_requested,clock_step?1:0);
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
    if(thread->request==THREAD_REQUEST_CPU){thread_cpu_prepare(thread,entry);return;}
    if(thread->request==THREAD_REQUEST_COMPONENT_DONE) {
        uint32_t return_pc=0;
        if(!thread->in_component || thread->in_kernel || !thread->in_cpu ||
           kernel_word(0x8003da64u)!=thread->object ||
           thread->component_index>=original_component_count ||
           thread->component_regs[4]!=thread->component_before[4]+4+
               original_components[thread->component_index].stack_bytes) {
            failure="selected helper completion outside its original CPU owner";return;
        }
        IO_UC(owned_cpu_read(cpu,thread->component_before[4],&return_pc,4));
        if(return_pc!=thread->component_return) {
            failure="selected helper changed original return frame";return;
        }
        for(unsigned i=1;i<8;++i)if(i!=4 && thread->component_regs[i]!=thread->component_before[i]) {
            failure="selected helper changed undeclared caller register";return;
        }
        check_publications(1);if(failure)return;
        for(unsigned i=0;i<8;++i)IO_UC(uc_reg_write(cpu,general_regs[i],thread->component_regs+i));
        IO_UC(uc_reg_write(cpu,UC_X86_REG_EFLAGS,&thread->component_flags));
        thread->in_component=0;*entry=thread->component_return;return;
    }
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
        if(thread->cpu_exit_request) {
            uint32_t sp=0,pc=0;
            IO_UC(uc_reg_read(cpu,UC_X86_REG_ESP,&sp));
            IO_UC(uc_reg_read(cpu,UC_X86_REG_EIP,&pc));
            if(kernel_word(0x8003da64u)!=thread->object || sp!=thread->call_regs[4] ||
               pc!=0xfe000408u || thread->call_ordinal!=258 || thread->call_arguments!=1 ||
               thread->native_regs[4]!=sp) {
                failure="original CPU termination lost its actual CALL frame";return;
            }
            *entry=original_export(258);return;
        }
        uint32_t sp=thread->native_regs[4]-8,frame[]={0,thread->exit_status};
        if(sp<thread->stack || (uint64_t)sp+sizeof(frame)>(uint64_t)thread->stack+thread->stack_bytes) {
            failure="thread LLE termination frame outside original stack";return;
        }
        memcpy(guest_memory+sp,frame,sizeof(frame));
        IO_UC(uc_reg_write(cpu,UC_X86_REG_ESP,&sp));
        *entry=original_export(258);
    }
}
static void thread_handoff(struct OriginalThread *thread,int start,uint32_t *entry)
{
    uint64_t cost=original_cost_begin();
    thread_handoff_impl(thread,start,entry);
    original_cost_end(ORIGINAL_COST_HANDOFF,cost);
}
static void thread_component_prepare(struct OriginalThread *thread,uint32_t *entry)
{
    uint32_t sp=0,flags=0;
    IO_UC(uc_reg_read(cpu,UC_X86_REG_ESP,&sp));IO_UC(uc_reg_read(cpu,UC_X86_REG_EFLAGS,&flags));
    if(thread->component_index>=original_component_count) {
        failure="selected helper has no bound complete entry";return;
    }
    const struct OriginalComponent *component=original_components+thread->component_index;
    if(thread->in_component || thread->in_kernel || !thread->in_cpu ||
       kernel_word(0x8003da64u)!=thread->object || (kernel_word(0x8003da60u)&255) ||
       (flags&0x600)!=0x200 || sp<thread->stack+44 ||
       (uint64_t)sp+4+component->stack_bytes>(uint64_t)thread->stack+thread->stack_bytes) {
        failure="selected helper lacks original thread/stack/passive context";return;
    }
    if(memcmp(guest_memory+component->entry,component->code,component->bytes)) {
        failure="selected helper original bytes changed after binding";return;
    }
    IO_UC(owned_cpu_read(cpu,sp,&thread->component_return,4));
    if(thread->component_return<0x10000u || thread->component_return>=0x80000000u ||
       !guest_page(thread->component_return&~4095u)) {
        failure="selected helper return outside resident original caller";return;
    }
    for(unsigned i=0;i<8;++i)IO_UC(uc_reg_read(cpu,general_regs[i],thread->component_regs+i));
    /* native_regs belonged to the outer retained callback; keep its entry
     * unchanged. A separate snapshot certifies this helper's complete ABI. */
    thread->component_flags=flags;
    memcpy(thread->component_before,thread->component_regs,sizeof(thread->component_before));
    thread->component_call_top=sp;thread->component_workspace=0;thread->component_vm_workspace=0;
    check_publications(0);if(failure)return;
    thread->in_component=1;thread->component_requested=1;
    thread_handoff(thread,0,entry);
}
