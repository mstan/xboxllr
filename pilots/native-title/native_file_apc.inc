/* Native I/O completion APCs retain the requesting thread's identity. Host
 * completion workers publish data and queue a wake only; generated guest code
 * executes after an alertable wait has reacquired the guest CPU permit.
 * NtUserIoApcDispatcher's argument/severity rules come from the owned kernel,
 * not desktop OVERLAPPED or a donor callback implementation. */
#include <fenv.h>
struct NativeFileApc {
    struct NativeFileApc *next;
    HANDLE thread;
    uint32_t slot,identity,routine,context,iosb;
};
static struct NativeFileApc *native_apc_head[64],*native_apc_tail[64];
static RECOMP_TLS unsigned native_apc_ready,native_apc_depth;
static void native_apc_discard(struct NativeFileApc *apc)
{
    if(!apc)return;
    if(apc->thread && !CloseHandle(apc->thread))stop("file APC thread reference close failed",apc->identity);
    free(apc);
}
static void native_apc_thread_retire_locked(unsigned slot)
{
    while(native_apc_head[slot]) {
        struct NativeFileApc *apc=native_apc_head[slot];native_apc_head[slot]=apc->next;
        native_apc_discard(apc);
    }
    native_apc_tail[slot]=NULL;
}
static VOID CALLBACK native_apc_wake(ULONG_PTR unused)
{
    (void)unused;
    if(native_apc_ready==UINT_MAX)stop("native file APC wake overflow",0);
    ++native_apc_ready; /* Never enter guest code on this host APC frame. */
}
static int native_apc_pending(void)
{return native_apc_ready!=0;}
static VOID CALLBACK native_apc_nudge(ULONG_PTR unused)
{(void)unused;}
static void native_apc_rearm_if_ready(void)
{
    if(native_apc_ready && !QueueUserAPC(native_apc_nudge,GetCurrentThread(),0))
        stop("native file APC alertable rearm failed",GetLastError());
}
static struct NativeFileApc *native_apc_prepare(uint32_t routine,uint32_t context,uint32_t iosb,uint32_t *status)
{
#ifdef PILOT_SCHEDULING_LLE
    (void)context;(void)iosb;(void)status;
    stop("native file APC requires native thread scheduling",routine);
#else
    uint32_t body=routine==0xfe0003a0u?context:routine;
    if(dispatch_in_dpc || !current_thread_slot || current_thread_slot>=64 || !recomp_lookup(body))
        stop("file APC requires an owned native thread and generated callback",body);
    struct NativeFileApc *apc=calloc(1,sizeof(*apc));
    *status=0xc000009au;if(!apc)return NULL;
    AcquireSRWLockShared(&thread_lock);
    struct PilotThread *thread=&threads[current_thread_slot];
    if(!thread->identity || thread->finished || thread->retiring ||
       (thread->host_tid?thread->host_tid:thread->tid)!=GetCurrentThreadId())
        stop("file APC lost requesting thread identity",current_thread_slot);
    int copied=DuplicateHandle(GetCurrentProcess(),thread->execution_handle,GetCurrentProcess(),&apc->thread,
                               THREAD_SET_CONTEXT|SYNCHRONIZE,FALSE,0);
    apc->slot=current_thread_slot;apc->identity=thread->identity;
    ReleaseSRWLockShared(&thread_lock);
    if(!copied){free(apc);return NULL;}
    apc->routine=routine;apc->context=context;apc->iosb=iosb;*status=0;return apc;
#endif
}
static void native_apc_complete(struct NativeFileApc *apc)
{
    if(!apc)return;
    AcquireSRWLockExclusive(&thread_lock);
    struct PilotThread *thread=&threads[apc->slot];
    if(thread->identity!=apc->identity || thread->finished || thread->retiring) {
        ReleaseSRWLockExclusive(&thread_lock);native_apc_discard(apc);return;
    }
    if(native_apc_tail[apc->slot])native_apc_tail[apc->slot]->next=apc;
    else native_apc_head[apc->slot]=apc;
    native_apc_tail[apc->slot]=apc;
    if(!QueueUserAPC(native_apc_wake,apc->thread,0))stop("native file APC wake queue failed",apc->identity);
    ReleaseSRWLockExclusive(&thread_lock);
}
static void native_apc_call(const struct NativeFileApc *apc)
{
    uint32_t routine=apc->routine,arguments[]={apc->context,apc->iosb,0};
    if(routine==0xfe0003a0u) {
        /* Original 232 tests NT_ERROR (severity3), not signed NT_SUCCESS.
         * Warning/informational statuses retain Information and error zero. */
        uint32_t status=MEM32(apc->iosb),failed=(status&0xc0000000u)==0xc0000000u;routine=apc->context;
        arguments[0]=failed?pilot_nt_status_to_dos_error(status):0;
        arguments[1]=failed?0:MEM32(apc->iosb+4);arguments[2]=apc->iosb;
    }
    recomp_func_t body=recomp_lookup(routine);uint32_t top=g_esp;
    if(!body || dispatch_in_dpc || top<16 || !wait_range(top-16,16,1) || native_apc_depth>=16)
        stop("unqualified native file APC callback frame",routine);
    struct NativeCpuContext saved;native_cpu_save(&saved);const uint32_t *regs=saved.regs;
    ++native_apc_depth;g_esp=top-16;MEM32(g_esp)=0;memcpy(memory+g_esp+4,arguments,sizeof(arguments));
    body();--native_apc_depth;
    if(g_esp!=top || g_ebx!=regs[4] || g_esi!=regs[5] || g_edi!=regs[6] || g_fs_base!=regs[8])
        stop("native file APC stdcall cleanup differs",routine);
    native_cpu_restore(&saved);
}
static void native_apc_deliver(void)
{
    while(native_apc_ready) {
        AcquireSRWLockExclusive(&thread_lock);
        struct NativeFileApc *apc=native_apc_head[current_thread_slot];
        if(!apc || apc->identity!=threads[current_thread_slot].identity)
            stop("native file APC wake lost its queue owner",current_thread_slot);
        native_apc_head[current_thread_slot]=apc->next;
        if(!apc->next)native_apc_tail[current_thread_slot]=NULL;
        --native_apc_ready;ReleaseSRWLockExclusive(&thread_lock);
        /* A callback may terminate its own thread. Retire the heap/handle
         * owner first so that a nonreturning callback cannot leak it. */
        struct NativeFileApc execution=*apc;native_apc_discard(apc);
        static LONG reported;
        if(InterlockedIncrement(&reported)<=8 || pilot_verbose)
            fprintf(stderr,"[FILE APC] thread=%lu routine=%08x context=%08x iosb=%08x status=%08x bytes=%u\n",
                    GetCurrentThreadId(),execution.routine,execution.context,execution.iosb,MEM32(execution.iosb),MEM32(execution.iosb+4));
        native_apc_call(&execution);
    }
}
