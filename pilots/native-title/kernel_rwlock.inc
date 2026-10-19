/* Host transport for the actual ERWLOCK counter/cohort algorithm. Only these
 * four exports own the object; external dispatcher waits/object mutation are
 * not silently substituted with successful calls. */
#include "kernel_rwlock.h"
static SRWLOCK kernel_rw_table_lock=SRWLOCK_INIT;
static PilotRwLock kernel_rw_locks[PILOT_ERWLOCK_MAX];
static unsigned kernel_rw_count;
/* Windows thread IDs may be recycled after exit. A fresh native thread gets
 * a distinct process-lifetime identity even if the OS assigns the same TID. */
static volatile LONG64 kernel_rw_token_sequence;
#ifdef _MSC_VER
static __declspec(thread) uint64_t kernel_rw_tls_token;
#else
static _Thread_local uint64_t kernel_rw_tls_token;
#endif
static uint64_t kernel_rw_thread_token(void)
{
    if(!kernel_rw_tls_token) {
        for(;;) {
            LONG64 observed=InterlockedCompareExchange64(&kernel_rw_token_sequence,0,0);
            /* Check before addition: signed overflow is undefined in C and
             * an increment intrinsic is not an exhaustion predicate. */
            if(observed<0 || observed==INT64_MAX)
                stop("read/write lock thread identity exhausted",0);
            LONG64 next=observed+1;
            if(InterlockedCompareExchange64(&kernel_rw_token_sequence,next,observed)==observed) {
                kernel_rw_tls_token=(uint64_t)next;
                break;
            }
        }
    }
    return kernel_rw_tls_token;
}

/* Integrator: reject independent wait/signal/reset operations on these embedded
 * dispatcher objects. Their native queue transport belongs to this module. */
static int kernel_rwlock_dispatcher_object(uint32_t va)
{
    int found=0;
    AcquireSRWLockShared(&kernel_rw_table_lock);
    for(unsigned i=0;i<kernel_rw_count;++i)
        if(va==kernel_rw_locks[i].va+16 || va==kernel_rw_locks[i].va+32){found=1;break;}
    ReleaseSRWLockShared(&kernel_rw_table_lock);
    return found;
}

static void kernel_rw_publish(PilotRwLock *lock)
{
    MEM32(lock->va)=(uint32_t)lock->count;
    MEM32(lock->va+4)=lock->writers_waiting;
    MEM32(lock->va+8)=lock->readers_waiting;
    MEM32(lock->va+12)=lock->readers_entered;
}
static void kernel_rw_validate(PilotRwLock *lock)
{
    if(MEM32(lock->va)!=(uint32_t)lock->count || MEM32(lock->va+4)!=lock->writers_waiting ||
       MEM32(lock->va+8)!=lock->readers_waiting || MEM32(lock->va+12)!=lock->readers_entered ||
       MEM32(lock->va+24)!=lock->va+24 || MEM32(lock->va+28)!=lock->va+24 ||
       MEM32(lock->va+40)!=lock->va+40 || MEM32(lock->va+44)!=lock->va+40 ||
       MEM32(lock->va+20)!=0 || MEM32(lock->va+36)!=0 || MEM32(lock->va+48)!=0x7fffffffu ||
       memory[lock->va+16]!=1 || memory[lock->va+18]!=4 ||
       memory[lock->va+32]!=5 || memory[lock->va+34]!=5)
        stop("read/write lock modified outside supported exports",lock->va);
}
static int kernel_rwlock_dispatch(unsigned ordinal)
{
    if(ordinal!=12 && ordinal!=13 && ordinal!=18 && ordinal!=28)return 0;
    if((uint64_t)g_esp+8>0x100000000ull)stop("invalid read/write lock argument frame",g_esp);
    uint32_t va=MEM32(g_esp+4);
    if(!va || (va&3) || (uint64_t)va+PILOT_ERWLOCK_BYTES>0x100000000ull)
        stop("invalid read/write lock address",va);
    AcquireSRWLockExclusive(&kernel_rw_table_lock);
    unsigned index;
    for(index=0;index<kernel_rw_count;++index)if(kernel_rw_locks[index].va==va)break;
    if(ordinal==18) {
        if(index==kernel_rw_count) {
            if(index==PILOT_ERWLOCK_MAX)stop("read/write lock table exhausted",va);
            PilotRwLock *fresh=&kernel_rw_locks[index];
            fresh->writer_event=CreateEventW(NULL,FALSE,FALSE,NULL);
            fresh->reader_semaphore=CreateSemaphoreW(NULL,0,0x7fffffff,NULL);
            if(!fresh->writer_event || !fresh->reader_semaphore)stop("creating read/write lock transport failed",va);
            fresh->va=va;++kernel_rw_count;
        }
        PilotRwLock *lock=&kernel_rw_locks[index];
        if(lock->writer || lock->readers_entered || lock->writers_waiting || lock->readers_waiting)
            stop("reinitializing owned read/write lock",va);
        for(unsigned i=0;i<PILOT_ERWLOCK_OWNERS;++i)
            if(lock->owners[i].waiting || lock->owners[i].readers)stop("reinitializing pending read/write lock",va);
        lock->count=-1;
        memset(memory+va,0,PILOT_ERWLOCK_BYTES);
        memory[va+16]=1;memory[va+18]=4;
        MEM32(va+24)=va+24;MEM32(va+28)=va+24;
        memory[va+32]=5;memory[va+34]=5;
        MEM32(va+40)=va+40;MEM32(va+44)=va+40;MEM32(va+48)=0x7fffffffu;
        kernel_rw_publish(lock);
        ReleaseSRWLockExclusive(&kernel_rw_table_lock);g_esp+=8;return 1;
    }
    if(index==kernel_rw_count)stop("uninitialized read/write lock",va);
    PilotRwLock *lock=&kernel_rw_locks[index];
    kernel_rw_validate(lock);
    uint64_t thread=kernel_rw_thread_token();
    unsigned owner_index;
    for(owner_index=0;owner_index<PILOT_ERWLOCK_OWNERS;++owner_index)
        if(lock->owners[owner_index].thread==thread)break;
    if(owner_index==PILOT_ERWLOCK_OWNERS) {
        for(owner_index=0;owner_index<PILOT_ERWLOCK_OWNERS;++owner_index)
            if(!lock->owners[owner_index].readers && !lock->owners[owner_index].waiting)break;
        if(owner_index==PILOT_ERWLOCK_OWNERS)stop("read/write lock owner table exhausted",va);
        lock->owners[owner_index].thread=thread;
    }
    PilotRwOwner *owner=&lock->owners[owner_index];
    HANDLE wait=NULL;
    if(ordinal==12 || ordinal==13) {
        if(lock->writer==thread || owner->waiting || (owner->readers && (ordinal==12 || lock->writers_waiting)))
            stop("unsupported recursive read/write lock acquisition or upgrade",va);
        if(lock->count==INT32_MAX)stop("read/write lock count overflow",va);
        ++lock->count;
        if(ordinal==12) {
            if(lock->count==0)lock->writer=thread;
            else {++lock->writers_waiting;owner->waiting=12;wait=lock->writer_event;}
        } else if(lock->count==0 || (lock->readers_entered && !lock->writers_waiting)) {
            ++lock->readers_entered;++owner->readers;
        } else {++lock->readers_waiting;owner->waiting=13;wait=lock->reader_semaphore;}
    } else {
        if(lock->writer!=thread && !owner->readers)stop("read/write lock release without ownership",va);
        if(lock->writer==thread)lock->writer=0;else --owner->readers;
        --lock->count;
        if(lock->count<0)lock->readers_entered=0;
        else if(!lock->readers_entered && lock->readers_waiting) {
            LONG readers=(LONG)lock->readers_waiting;
            lock->readers_entered=lock->readers_waiting;lock->readers_waiting=0;
            if(!ReleaseSemaphore(lock->reader_semaphore,readers,NULL))stop("read/write lock reader grant failed",va);
        } else if(!lock->readers_entered || !--lock->readers_entered) {
            if(!lock->writers_waiting)stop("read/write lock missing queued writer",va);
            --lock->writers_waiting;
            if(!SetEvent(lock->writer_event))stop("read/write lock writer grant failed",va);
        }
    }
    kernel_rw_publish(lock);
    ReleaseSRWLockExclusive(&kernel_rw_table_lock);
    if(wait) {
        /* Never hold the native state lock while releasing/reacquiring the CPU
         * token; a releaser must be able to execute on another guest thread. */
        int blocked=scheduler_block_begin();
        DWORD result=WaitForSingleObject(wait,INFINITE);
        scheduler_block_end(blocked);
        if(result!=WAIT_OBJECT_0)stop("read/write lock host wait failed",va);
        AcquireSRWLockExclusive(&kernel_rw_table_lock);
        kernel_rw_validate(lock);
        if(owner->waiting==12)lock->writer=thread;else ++owner->readers;
        owner->waiting=0;
        ReleaseSRWLockExclusive(&kernel_rw_table_lock);
    }
    g_esp+=8;return 1;
}
