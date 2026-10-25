/* Reclaim only fully retired native lifetimes. Guest close/reference release
 * are distinct from the real host thread's final return. Reaping occurs at
 * creation or an explicit teardown boundary, never each guest checkpoint. */
static void thread_reap_slot(unsigned slot)
{
    AcquireSRWLockExclusive(&thread_lock);
    if(slot && slot<=thread_count) {
        struct PilotThread *thread=&threads[slot];
        if(!thread->object || thread->retiring || thread->handle ||
           thread->object_refs || !thread->finished)goto done;
        if(!thread->execution_handle || thread->reference_handle)
            stop("thread retirement lost its execution ownership",slot);
        DWORD status=WaitForSingleObject(thread->execution_handle,0);
        DWORD observed=(DWORD)pilot_session_value64(PILOT_SESSION_WAIT,UINT64_C(0x100000000)|thread->identity,status);
        if(pilot_session_mode()==PILOT_SESSION_REPLAY && observed==WAIT_OBJECT_0 && status==WAIT_TIMEOUT) {
            status=WaitForSingleObject(thread->execution_handle,30000);
            if(status!=WAIT_OBJECT_0)pilot_session_diverge("recorded native thread did not exit",thread->identity);
        }
        status=observed;
        if(status==WAIT_TIMEOUT)goto done;
        if(status!=WAIT_OBJECT_0)stop("thread exit observation failed",GetLastError());
        thread->retiring=1;
        /* Reserve this slot while dropping the object lock. The real original
         * VM free and host decommit must not run with thread_lock held. */
        ReleaseSRWLockExclusive(&thread_lock);
        uint32_t base=0x60000000u+slot*0x100000u;
#ifdef PILOT_USB_LLE
        if(thread->stack_registered) {
            int released=0;
            const char *error=pilot_io_device_stack_release(memory,
                base+0x100000u-thread->stack_size,thread->stack_size,&released);
            if(error)stop(error,base);
            if(!released) {
                /* DMA ownership can outlast execution. Keep the retired guest
                 * identity and its memory until a later creation can reap it. */
                AcquireSRWLockExclusive(&thread_lock);thread->retiring=0;goto done;
            }
        }
#endif
        AcquireSRWLockExclusive(&vm_lock);
        if(!VirtualFree(memory+base,0x100000u,MEM_DECOMMIT))
            stop("thread context decommit failed",base);
        ReleaseSRWLockExclusive(&vm_lock);
        AcquireSRWLockExclusive(&thread_lock);
        if(!CloseHandle(thread->execution_handle))stop("thread execution reference close failed",GetLastError());
        memset(thread,0,sizeof(*thread));
    }
done:
    ReleaseSRWLockExclusive(&thread_lock);
}
static void thread_reap(void)
{
    AcquireSRWLockShared(&thread_lock);unsigned count=thread_count;ReleaseSRWLockShared(&thread_lock);
    for(unsigned slot=1;slot<=count;++slot)thread_reap_slot(slot);
}
