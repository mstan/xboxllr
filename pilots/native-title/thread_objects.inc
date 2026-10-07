/* Native thread-object references and scheduling controls. A reference owns a
 * duplicated real thread handle, so NtClose cannot invalidate the object while
 * a caller still holds it. Guest handles, objects and host handles are distinct.
 * The reference map is guarded by thread_lock; no scheduler/IRQL success stubs. */
static struct PilotThread *thread_from_handle(uint32_t handle,HANDLE *host)
{
    if(handle==0xfffffffeu) {
        if(!current_thread_slot)stop("non-title current-thread object is unqualified",handle);
        *host=GetCurrentThread();return &threads[current_thread_slot];
    }
    unsigned slot=handle-0x40000000u;
    if(slot && slot<=thread_count && threads[slot].handle){*host=threads[slot].handle;return &threads[slot];}
    if((handle&0xf0000000u)==0x50000000u || (handle&0xf0000000u)==0x60000000u)
        stop("non-thread object reference is unqualified",handle);
    return NULL;
}
static struct PilotThread *thread_from_object(uint32_t object,HANDLE *host)
{
    for(unsigned i=1;i<=thread_count;++i)if(threads[i].object==object) {
        struct PilotThread *t=&threads[i];
        *host=t->reference_handle?t->reference_handle:t->handle;
        if(!*host && i==current_thread_slot)*host=GetCurrentThread();
        if(!*host)stop("thread object has no owned host reference",object);
        return t;
    }
    stop("unowned thread object",object);
}
static int thread_host_priority(int delta)
{
    if(delta<=-16)return THREAD_PRIORITY_IDLE;
    if(delta>=16)return THREAD_PRIORITY_TIME_CRITICAL;
    return delta< -2?THREAD_PRIORITY_LOWEST:delta>2?THREAD_PRIORITY_HIGHEST:delta;
}
static void thread_object_kernel(unsigned ordinal)
{
    if(ordinal==224 || ordinal==231) {
        uint32_t handle=MEM32(g_esp+4),out=MEM32(g_esp+8);
        HANDLE host=NULL;
        AcquireSRWLockExclusive(&thread_lock);
        struct PilotThread *t=thread_from_handle(handle,&host);
        g_eax=t?0:0xc0000008u;
        if(t && t->finished)g_eax=0xc000004bu;
        if(!g_eax) {
            uint32_t previous=t->suspend_count;
            if(ordinal==231 && previous==127)g_eax=0xc000004au;
            else {
                if(ordinal==231)++t->suspend_count;
                else if(previous && !--t->suspend_count)WakeAllConditionVariable(&thread_condition);
                if(out)MEM32(out)=previous;
            }
        }
        ReleaseSRWLockExclusive(&thread_lock);
        /* Self-suspend waits here. Remote suspension takes effect at the next
         * function entry/backedge; never freeze a host allocation or I/O lock. */
        PILOT_THREAD_CHECKPOINT();
        g_esp+=12;return;
    }
    AcquireSRWLockExclusive(&thread_lock);
    if(ordinal==250) {
        HANDLE host=NULL;struct PilotThread *t=thread_from_object(g_ecx,&host);
        if(!t->object_refs)stop("unbalanced thread object dereference",g_ecx);
        if(!--t->object_refs) {
            if(!CloseHandle(t->reference_handle))stop("thread object reference close failed",GetLastError());
            t->reference_handle=NULL;
        }
        g_esp+=4; /* Xbox fastcall: object in ECX, no stack argument. */
    } else if(ordinal==246) {
        uint32_t handle=MEM32(g_esp+4),type=MEM32(g_esp+8),out=MEM32(g_esp+12);
        HANDLE host=NULL;struct PilotThread *t=thread_from_handle(handle,&host);
        if(!out)g_eax=0xc000000du;
        else if(!t)g_eax=0xc0000008u;
        else if(type && type!=PILOT_THREAD_OBJECT_TYPE)g_eax=0xc0000024u;
        else {
            if(t->object_refs==UINT32_MAX)stop("thread object reference overflow",handle);
            if(!t->object_refs && !DuplicateHandle(GetCurrentProcess(),host,GetCurrentProcess(),
                                                  &t->reference_handle,0,FALSE,DUPLICATE_SAME_ACCESS))
                g_eax=0xc000009au;
            else {++t->object_refs;MEM32(out)=t->object;g_eax=0;}
        }
        g_esp+=16;
    } else {
        uint32_t object=MEM32(g_esp+4);HANDLE host=NULL;
        struct PilotThread *t=thread_from_object(object,&host);
        if(ordinal==124){g_eax=(uint32_t)t->priority;g_esp+=8;}
        else if(ordinal==143) {
            int32_t delta=(int32_t)MEM32(g_esp+8);
            if(delta>=16)delta=16;else if(delta<=-16)delta=-16;
            else if(delta>7)delta=7;else if(delta< -7)delta=-7;
            if(!SetThreadPriority(host,thread_host_priority(delta)))stop("native thread priority change failed",GetLastError());
            g_eax=(uint32_t)t->priority;t->priority=delta;g_esp+=12;
        } else if(ordinal==144) {
            BOOL previous;
            if(!GetThreadPriorityBoost(host,&previous) || !SetThreadPriorityBoost(host,MEM32(g_esp+8)!=0))
                stop("native thread priority boost change failed",GetLastError());
            g_eax=previous!=FALSE;g_esp+=12;
        } else stop("unknown thread object operation",ordinal);
    }
    ReleaseSRWLockExclusive(&thread_lock);
}
