/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "windows.h"
#include "winternl.h"
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/syscall.h>
#include <sched.h>
#include <assert.h>

static _Thread_local DWORD last_error;
DWORD GetLastError(void){return last_error;}
void SetLastError(DWORD error){last_error=error;}
static BOOL error(DWORD value){SetLastError(value);return FALSE;}
LONG InterlockedIncrement(volatile LONG *p){return __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST);}
LONG InterlockedDecrement(volatile LONG *p){return __atomic_sub_fetch(p,1,__ATOMIC_SEQ_CST);}
LONG InterlockedExchange(volatile LONG *p,LONG value){return __atomic_exchange_n(p,value,__ATOMIC_SEQ_CST);}
LONG InterlockedCompareExchange(volatile LONG *p,LONG value,LONG expected)
{__atomic_compare_exchange_n(p,&expected,value,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return expected;}
LONG64 InterlockedIncrement64(volatile LONG64 *p){return __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST);}
LONG64 InterlockedExchange64(volatile LONG64 *p,LONG64 value){return __atomic_exchange_n(p,value,__ATOMIC_SEQ_CST);}
LONG64 InterlockedAdd64(volatile LONG64 *p,LONG64 value){return __atomic_add_fetch(p,value,__ATOMIC_SEQ_CST);}
LONG64 InterlockedCompareExchange64(volatile LONG64 *p,LONG64 value,LONG64 expected)
{__atomic_compare_exchange_n(p,&expected,value,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return expected;}
void InitializeSRWLock(PSRWLOCK p){pthread_mutex_init(p,NULL);}
void AcquireSRWLockExclusive(PSRWLOCK p){if(pthread_mutex_lock(p))abort();}
void AcquireSRWLockShared(PSRWLOCK p){AcquireSRWLockExclusive(p);}
void ReleaseSRWLockExclusive(PSRWLOCK p){if(pthread_mutex_unlock(p))abort();}
void ReleaseSRWLockShared(PSRWLOCK p){ReleaseSRWLockExclusive(p);}
BOOLEAN TryAcquireSRWLockExclusive(PSRWLOCK p){return pthread_mutex_trylock(p)==0;}
void InitializeCriticalSection(LPCRITICAL_SECTION p)
{pthread_mutexattr_t a;pthread_mutexattr_init(&a);pthread_mutexattr_settype(&a,PTHREAD_MUTEX_RECURSIVE);pthread_mutex_init(&p->mutex,&a);pthread_mutexattr_destroy(&a);p->RecursionCount=0;}
void EnterCriticalSection(LPCRITICAL_SECTION p){AcquireSRWLockExclusive(&p->mutex);InterlockedIncrement(&p->RecursionCount);}
void LeaveCriticalSection(LPCRITICAL_SECTION p){InterlockedDecrement(&p->RecursionCount);ReleaseSRWLockExclusive(&p->mutex);}
static uint64_t clock_ns(clockid_t clock)
{struct timespec time;if(clock_gettime(clock,&time))abort();return (uint64_t)time.tv_sec*1000000000u+time.tv_nsec;}
static struct timespec realtime_after(uint64_t ns)
{uint64_t time=clock_ns(CLOCK_REALTIME)+ns;return (struct timespec){(time_t)(time/1000000000u),(long)(time%1000000000u)};}
BOOL SleepConditionVariableSRW(PCONDITION_VARIABLE c,PSRWLOCK p,DWORD ms,ULONG flags)
{
    if(flags&~CONDITION_VARIABLE_LOCKMODE_SHARED)return error(ERROR_INVALID_PARAMETER);
    struct timespec until=realtime_after((uint64_t)ms*1000000);
    int result=ms==INFINITE?pthread_cond_wait(c,p):pthread_cond_timedwait(c,p,&until);
    return result?error(result==ETIMEDOUT?ERROR_TIMEOUT:ERROR_INVALID_PARAMETER):TRUE;
}
void WakeAllConditionVariable(PCONDITION_VARIABLE c){pthread_cond_broadcast(c);}
BOOL InitOnceExecuteOnce(PINIT_ONCE once,PINIT_ONCE_FN routine,void *parameter,void **context)
{
    pthread_mutex_lock(&once->mutex);
    BOOL result=once->done || routine(once,parameter,context);
    if(result)once->done=1;
    pthread_mutex_unlock(&once->mutex);return result;
}
BOOL QueryPerformanceCounter(LARGE_INTEGER *out){if(!out)return FALSE;out->QuadPart=(int64_t)clock_ns(CLOCK_MONOTONIC);return TRUE;}
BOOL QueryPerformanceFrequency(LARGE_INTEGER *out){if(!out)return FALSE;out->QuadPart=1000000000;return TRUE;}
uint64_t GetTickCount64(void){return clock_ns(CLOCK_MONOTONIC)/1000000;}
void Sleep(DWORD ms)
{struct timespec delay={ms/1000,(long)(ms%1000)*1000000};if(!ms){sched_yield();return;}while(nanosleep(&delay,&delay) && errno==EINTR){}}
static const uint64_t epoch=116444736000000000ull;
static void filetime(FILETIME *out,uint64_t value){out->dwLowDateTime=(DWORD)value;out->dwHighDateTime=(DWORD)(value>>32);}
void GetSystemTimePreciseAsFileTime(FILETIME *out){filetime(out,epoch+clock_ns(CLOCK_REALTIME)/100);}
BOOL FileTimeToSystemTime(const FILETIME *in,SYSTEMTIME *out)
{
    uint64_t ticks=((uint64_t)in->dwHighDateTime<<32)|in->dwLowDateTime;
    if(ticks<epoch)return error(ERROR_INVALID_PARAMETER);
    time_t seconds=(time_t)((ticks-epoch)/10000000);struct tm time;
    if(!gmtime_r(&seconds,&time))return FALSE;
    *out=(SYSTEMTIME){time.tm_year+1900,time.tm_mon+1,time.tm_wday,time.tm_mday,time.tm_hour,time.tm_min,time.tm_sec,(ticks%10000000)/10000};return TRUE;
}
BOOL SystemTimeToFileTime(const SYSTEMTIME *in,FILETIME *out)
{
    struct tm time={.tm_year=in->wYear-1900,.tm_mon=in->wMonth-1,.tm_mday=in->wDay,.tm_hour=in->wHour,.tm_min=in->wMinute,.tm_sec=in->wSecond};
    if(in->wYear<1601 || in->wMilliseconds>999)return error(ERROR_INVALID_PARAMETER);
    time_t seconds=timegm(&time);
    if(time.tm_year!=in->wYear-1900 || time.tm_mon!=in->wMonth-1 || time.tm_mday!=in->wDay || time.tm_hour!=in->wHour || time.tm_min!=in->wMinute || time.tm_sec!=in->wSecond)return error(ERROR_INVALID_PARAMETER);
    filetime(out,(uint64_t)((int64_t)epoch+(int64_t)seconds*10000000)+in->wMilliseconds*10000);return TRUE;
}

/* One mutex protects wait predicates and notifications, including multiwait.
 * Checking readiness and consuming auto-reset events is one atomic operation.
 */
enum HandleKind {EVENT=1,SEMAPHORE,TIMER,THREAD,MUTANT};
struct Apc {struct Apc *next;PAPCFUNC routine;ULONG_PTR argument;};
struct WaitHandle {
    struct WaitHandle *next;enum HandleKind kind;unsigned references;
    int manual,signaled,count,limit,started,finished,priority,boost_disabled;
    uint64_t deadline,period;int absolute;
    pthread_t thread;DWORD id,exit_code;LPTHREAD_START_ROUTINE routine;void *argument;
    DWORD owner;struct Apc *apcs;
};
static pthread_mutex_t wait_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wait_changed=PTHREAD_COND_INITIALIZER;
static struct WaitHandle *handles;
static _Thread_local struct WaitHandle *current;
static struct WaitHandle *lookup(HANDLE pointer)
{for(struct WaitHandle *h=handles;h;h=h->next)if(h==pointer)return h;return NULL;}
static struct WaitHandle *new_handle(enum HandleKind kind)
{
    struct WaitHandle *h=calloc(1,sizeof(*h));if(!h){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return NULL;}
    h->kind=kind;h->references=1;pthread_mutex_lock(&wait_mutex);h->next=handles;handles=h;pthread_mutex_unlock(&wait_mutex);return h;
}
static void drop_locked(struct WaitHandle *h)
{
    assert(h->references);if(--h->references)return;
    struct WaitHandle **at=&handles;while(*at!=h)at=&(*at)->next;*at=h->next;free(h);
}
HANDLE CreateEventA(void *security,BOOL manual,BOOL initial,LPCSTR name)
{
    if(security || (name && *name)){SetLastError(ERROR_NOT_SUPPORTED);return NULL;}
    struct WaitHandle *h=new_handle(EVENT);if(h){h->manual=!!manual;h->signaled=!!initial;}return h;
}
HANDLE CreateEventW(void *security,BOOL manual,BOOL initial,LPCWSTR name)
{if(name && *name){SetLastError(ERROR_NOT_SUPPORTED);return NULL;}return CreateEventA(security,manual,initial,NULL);}
HANDLE OpenEventA(DWORD access,BOOL inherit,LPCSTR name)
{(void)access;(void)inherit;(void)name;SetLastError(ERROR_NOT_SUPPORTED);return NULL;}
BOOL SetEvent(HANDLE pointer)
{
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);BOOL ok=h && h->kind==EVENT;
    if(ok){h->signaled=1;pthread_cond_broadcast(&wait_changed);}pthread_mutex_unlock(&wait_mutex);return ok?TRUE:error(ERROR_INVALID_HANDLE);
}
BOOL ResetEvent(HANDLE pointer)
{pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);BOOL ok=h && h->kind==EVENT;if(ok)h->signaled=0;pthread_mutex_unlock(&wait_mutex);return ok?TRUE:error(ERROR_INVALID_HANDLE);}
HANDLE CreateSemaphoreW(void *security,LONG initial,LONG maximum,LPCWSTR name)
{
    if(security || (name && *name) || initial<0 || maximum<=0 || initial>maximum){SetLastError(ERROR_INVALID_PARAMETER);return NULL;}
    struct WaitHandle *h=new_handle(SEMAPHORE);if(h){h->count=initial;h->limit=maximum;}return h;
}
BOOL ReleaseSemaphore(HANDLE pointer,LONG count,LONG *previous)
{
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);BOOL ok=h && h->kind==SEMAPHORE && count>0 && count<=h->limit-h->count;
    if(ok){if(previous)*previous=h->count;h->count+=count;pthread_cond_broadcast(&wait_changed);}pthread_mutex_unlock(&wait_mutex);return ok?TRUE:error(ERROR_INVALID_PARAMETER);
}
static void thread_finished(void *pointer)
{
    struct WaitHandle *h=pointer;pthread_mutex_lock(&wait_mutex);h->finished=1;pthread_cond_broadcast(&wait_changed);drop_locked(h);pthread_mutex_unlock(&wait_mutex);
}
static void *thread_main(void *pointer)
{
    struct WaitHandle *h=pointer;current=h;
    pthread_cleanup_push(thread_finished,h);
    pthread_mutex_lock(&wait_mutex);h->id=(DWORD)syscall(SYS_gettid);pthread_cond_broadcast(&wait_changed);
    while(!h->started)pthread_cond_wait(&wait_changed,&wait_mutex);
    pthread_mutex_unlock(&wait_mutex);h->exit_code=h->routine(h->argument);
    pthread_cleanup_pop(1);current=NULL;return NULL;
}
HANDLE CreateThread(void *security,SIZE_T stack,LPTHREAD_START_ROUTINE routine,void *argument,DWORD flags,DWORD *id)
{
    if(security || !routine || flags&~CREATE_SUSPENDED){SetLastError(ERROR_INVALID_PARAMETER);return NULL;}
    struct WaitHandle *h=new_handle(THREAD);if(!h)return NULL;
    h->routine=routine;h->argument=argument;h->started=!(flags&CREATE_SUSPENDED);h->references++;
    pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    if(stack && pthread_attr_setstacksize(&attr,stack)){pthread_attr_destroy(&attr);pthread_mutex_lock(&wait_mutex);drop_locked(h);drop_locked(h);pthread_mutex_unlock(&wait_mutex);SetLastError(ERROR_INVALID_PARAMETER);return NULL;}
    int result=pthread_create(&h->thread,&attr,thread_main,h);pthread_attr_destroy(&attr);
    if(result){pthread_mutex_lock(&wait_mutex);drop_locked(h);drop_locked(h);pthread_mutex_unlock(&wait_mutex);SetLastError(ERROR_NOT_ENOUGH_MEMORY);return NULL;}
    pthread_mutex_lock(&wait_mutex);while(!h->id)pthread_cond_wait(&wait_changed,&wait_mutex);if(id)*id=h->id;pthread_mutex_unlock(&wait_mutex);return h;
}
DWORD ResumeThread(HANDLE pointer)
{
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);DWORD previous=UINT32_MAX;
    if(h && h->kind==THREAD){previous=!h->started;h->started=1;pthread_cond_broadcast(&wait_changed);}pthread_mutex_unlock(&wait_mutex);if(previous==UINT32_MAX)SetLastError(ERROR_INVALID_HANDLE);return previous;
}
void ExitThread(DWORD result){if(current)current->exit_code=result;pthread_exit(NULL);}
void ExitProcess(UINT result){exit((int)result);}
HANDLE GetCurrentProcess(void){return (HANDLE)(intptr_t)-1;}
HANDLE GetCurrentThread(void)
{
    if(!current){current=new_handle(THREAD);if(!current)abort();current->thread=pthread_self();current->id=(DWORD)syscall(SYS_gettid);current->started=1;}
    return current;
}
DWORD GetCurrentProcessId(void){return (DWORD)getpid();}
DWORD GetCurrentThreadId(void){return (DWORD)syscall(SYS_gettid);}
DWORD GetThreadId(HANDLE pointer){if(pointer==(HANDLE)(intptr_t)-2)return GetCurrentThreadId();pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);DWORD id=h && h->kind==THREAD?h->id:0;pthread_mutex_unlock(&wait_mutex);return id;}
static int ready_locked(struct WaitHandle *h,uint64_t *next)
{
    if(h->kind==THREAD)return h->finished;
    if(h->kind==MUTANT)return !h->owner || h->owner==GetCurrentThreadId();
    if(h->kind==SEMAPHORE)return h->count>0;
    if(h->kind==TIMER && h->deadline) {
        uint64_t now=clock_ns(h->absolute?CLOCK_REALTIME:CLOCK_MONOTONIC);
        if(now>=h->deadline)h->signaled=1;
        else if(h->deadline-now<*next)*next=h->deadline-now;
    }
    return h->signaled;
}
static void consume_locked(struct WaitHandle *h)
{
    if(h->kind==SEMAPHORE){--h->count;return;}
    if(h->kind==MUTANT){h->owner=GetCurrentThreadId();++h->count;return;}
    if(h->kind==THREAD || h->manual)return;
    h->signaled=0;
    if(h->kind==TIMER && h->deadline) {
        if(h->period){uint64_t now=clock_ns(h->absolute?CLOCK_REALTIME:CLOCK_MONOTONIC);h->deadline+=((now-h->deadline)/h->period+1)*h->period;}
        else h->deadline=0;
    }
}
DWORD WaitForMultipleObjects(DWORD count,const HANDLE *pointers,BOOL all,DWORD ms)
{
    if(!count || count>64 || !pointers){SetLastError(ERROR_INVALID_PARAMETER);return WAIT_FAILED;}
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *list[64];
    for(unsigned i=0;i<count;++i) {
        list[i]=lookup(pointers[i]);if(!list[i]){pthread_mutex_unlock(&wait_mutex);SetLastError(ERROR_INVALID_HANDLE);return WAIT_FAILED;}
        for(unsigned j=0;j<i;++j)if(list[j]==list[i]){pthread_mutex_unlock(&wait_mutex);SetLastError(ERROR_INVALID_PARAMETER);return WAIT_FAILED;}
    }
    uint64_t deadline=ms==INFINITE?UINT64_MAX:clock_ns(CLOCK_MONOTONIC)+(uint64_t)ms*1000000;
    DWORD result=WAIT_TIMEOUT;
    for(;;) {
        unsigned ready=0;uint64_t delay=UINT64_MAX;
        for(unsigned i=0;i<count;++i)if(ready_locked(list[i],&delay)) {
            if(!all){consume_locked(list[i]);result=WAIT_OBJECT_0+i;goto done;}++ready;
        }
        if(all && ready==count){for(unsigned i=0;i<count;++i)consume_locked(list[i]);result=WAIT_OBJECT_0;break;}
        uint64_t now=clock_ns(CLOCK_MONOTONIC);if(now>=deadline)break;
        if(deadline-now<delay)delay=deadline-now;
        int status;
        if(delay==UINT64_MAX)status=pthread_cond_wait(&wait_changed,&wait_mutex);
        else {struct timespec until=realtime_after(delay);status=pthread_cond_timedwait(&wait_changed,&wait_mutex,&until);}
        if(status && status!=ETIMEDOUT){SetLastError(ERROR_INVALID_PARAMETER);result=WAIT_FAILED;break;}
    }
done:
    pthread_mutex_unlock(&wait_mutex);return result;
}
DWORD WaitForSingleObject(HANDLE h,DWORD ms){return WaitForMultipleObjects(1,&h,FALSE,ms);}
/* File handles are owned by host_files.c, with a distinct checked registry. */
extern int pilot_posix_file_close(HANDLE);
extern int pilot_posix_file_duplicate(HANDLE,HANDLE*);
BOOL CloseHandle(HANDLE pointer)
{
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);
    if(h){drop_locked(h);pthread_mutex_unlock(&wait_mutex);return TRUE;}
    pthread_mutex_unlock(&wait_mutex);return pilot_posix_file_close(pointer)?TRUE:error(ERROR_INVALID_HANDLE);
}
BOOL DuplicateHandle(HANDLE source,HANDLE pointer,HANDLE target,HANDLE *out,DWORD access,BOOL inherit,DWORD flags)
{
    (void)access;if(source!=GetCurrentProcess() || target!=GetCurrentProcess() || !out || inherit || flags&~DUPLICATE_SAME_ACCESS)return error(ERROR_NOT_SUPPORTED);
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);
    if(h){++h->references;*out=h;pthread_mutex_unlock(&wait_mutex);return TRUE;}
    pthread_mutex_unlock(&wait_mutex);return pilot_posix_file_duplicate(pointer,out)?TRUE:error(ERROR_INVALID_HANDLE);
}
HANDLE CreateWaitableTimerW(void *security,BOOL manual,LPCWSTR name)
{if(security || (name && *name)){SetLastError(ERROR_NOT_SUPPORTED);return NULL;}struct WaitHandle *h=new_handle(TIMER);if(h)h->manual=!!manual;return h;}
HANDLE CreateWaitableTimerExW(void *security,LPCWSTR name,DWORD flags,DWORD access)
{(void)access;if(flags&~(1u|CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)){SetLastError(ERROR_NOT_SUPPORTED);return NULL;}return CreateWaitableTimerW(security,flags&1,name);}
BOOL SetWaitableTimer(HANDLE pointer,const LARGE_INTEGER *due,LONG period,void *callback,void *argument,BOOL resume)
{
    if(!due || period<0 || callback || argument || resume)return error(ERROR_NOT_SUPPORTED);
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);BOOL ok=h && h->kind==TIMER;
    if(ok) {
        h->signaled=0;h->period=(uint64_t)period*1000000;h->absolute=due->QuadPart>=0;
        if(h->absolute)h->deadline=(uint64_t)due->QuadPart>epoch?((uint64_t)due->QuadPart-epoch)*100:1;
        else {uint64_t duration=0-(uint64_t)due->QuadPart;uint64_t now=clock_ns(CLOCK_MONOTONIC);h->deadline=duration>(UINT64_MAX-now)/100?UINT64_MAX:now+duration*100;}
        pthread_cond_broadcast(&wait_changed);
    }
    pthread_mutex_unlock(&wait_mutex);return ok?TRUE:error(ERROR_INVALID_HANDLE);
}
BOOL CancelWaitableTimer(HANDLE pointer)
{pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);BOOL ok=h && h->kind==TIMER;if(ok)h->deadline=0;pthread_cond_broadcast(&wait_changed);pthread_mutex_unlock(&wait_mutex);return ok?TRUE:error(ERROR_INVALID_HANDLE);}
int GetThreadPriority(HANDLE pointer)
{if(pointer==(HANDLE)(intptr_t)-2)return 0;pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);int value=h?h->priority:THREAD_PRIORITY_ERROR_RETURN;pthread_mutex_unlock(&wait_mutex);return value;}
BOOL SetThreadPriority(HANDLE pointer,int value)
{pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);BOOL ok=h && h->kind==THREAD;if(ok)h->priority=value;pthread_mutex_unlock(&wait_mutex);return ok?TRUE:error(ERROR_INVALID_HANDLE);}
BOOL GetThreadPriorityBoost(HANDLE pointer,BOOL *out)
{pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);BOOL ok=h && out;if(ok)*out=h->boost_disabled;pthread_mutex_unlock(&wait_mutex);return ok?TRUE:error(ERROR_INVALID_HANDLE);}
BOOL SetThreadPriorityBoost(HANDLE pointer,BOOL disabled)
{pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);BOOL ok=h!=NULL;if(ok)h->boost_disabled=!!disabled;pthread_mutex_unlock(&wait_mutex);return ok?TRUE:error(ERROR_INVALID_HANDLE);}
BOOL GetThreadTimes(HANDLE pointer,FILETIME *created,FILETIME *exited,FILETIME *kernel,FILETIME *user)
{
    clockid_t clock=CLOCK_THREAD_CPUTIME_ID;
    if(pointer!=GetCurrentThread()) {
        pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);int result=h && h->kind==THREAD?pthread_getcpuclockid(h->thread,&clock):EINVAL;pthread_mutex_unlock(&wait_mutex);if(result)return error(ERROR_INVALID_HANDLE);
    }
    uint64_t ns=clock_ns(clock);filetime(created,0);filetime(exited,0);filetime(kernel,0);filetime(user,ns/100);return TRUE;
}
BOOL GetProcessAffinityMask(HANDLE process,DWORD_PTR *allowed,DWORD_PTR *system)
{
    if(process!=GetCurrentProcess() || !allowed || !system)return error(ERROR_INVALID_PARAMETER);
    cpu_set_t set;if(sched_getaffinity(0,sizeof(set),&set))return error(ERROR_NOT_SUPPORTED);
    *allowed=0;*system=0;long total=sysconf(_SC_NPROCESSORS_CONF);
    for(unsigned i=0;i<sizeof(DWORD_PTR)*8;++i){if(CPU_ISSET(i,&set))*allowed|=(DWORD_PTR)1<<i;if(i<(unsigned)total)*system|=(DWORD_PTR)1<<i;}return TRUE;
}
BOOL GetThreadGroupAffinity(HANDLE thread,GROUP_AFFINITY *out)
{
    if(thread!=GetCurrentThread() || !out)return error(ERROR_NOT_SUPPORTED);
    cpu_set_t set;if(pthread_getaffinity_np(pthread_self(),sizeof(set),&set))return error(ERROR_NOT_SUPPORTED);
    memset(out,0,sizeof(*out));for(unsigned i=0;i<sizeof(DWORD_PTR)*8;++i)if(CPU_ISSET(i,&set))out->Mask|=(DWORD_PTR)1<<i;return TRUE;
}
BOOL SetThreadGroupAffinity(HANDLE thread,const GROUP_AFFINITY *affinity,GROUP_AFFINITY *previous)
{
    if(thread!=GetCurrentThread() || !affinity || affinity->Group || !affinity->Mask)return error(ERROR_NOT_SUPPORTED);
    if(previous && !GetThreadGroupAffinity(thread,previous))return FALSE;
    cpu_set_t set;CPU_ZERO(&set);for(unsigned i=0;i<sizeof(DWORD_PTR)*8;++i)if(affinity->Mask&((DWORD_PTR)1<<i))CPU_SET(i,&set);
    return pthread_setaffinity_np(pthread_self(),sizeof(set),&set)?error(ERROR_NOT_SUPPORTED):TRUE;
}
DWORD QueueUserAPC(PAPCFUNC routine,HANDLE thread,ULONG_PTR value)
{
    if(!routine){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
    struct Apc *apc=malloc(sizeof(*apc));if(!apc){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}
    *apc=(struct Apc){NULL,routine,value};pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(thread);
    if(!h || h->kind!=THREAD || h->finished){pthread_mutex_unlock(&wait_mutex);free(apc);SetLastError(ERROR_INVALID_HANDLE);return 0;}
    struct Apc **tail=&h->apcs;while(*tail)tail=&(*tail)->next;*tail=apc;pthread_cond_broadcast(&wait_changed);pthread_mutex_unlock(&wait_mutex);return 1;
}
static int dispatch_apcs(void)
{
    struct WaitHandle *self=GetCurrentThread();int delivered=0;
    for(;;) {
        pthread_mutex_lock(&wait_mutex);struct Apc *apc=self->apcs;if(apc)self->apcs=apc->next;pthread_mutex_unlock(&wait_mutex);
        if(!apc)break;apc->routine(apc->argument);free(apc);delivered=1;
    }
    return delivered;
}
static DWORD nt_timeout(const LARGE_INTEGER *timeout)
{
    if(!timeout)return INFINITE;
    uint64_t ticks;
    if(timeout->QuadPart<=0)ticks=0-(uint64_t)timeout->QuadPart;
    else {uint64_t now=epoch+clock_ns(CLOCK_REALTIME)/100;ticks=(uint64_t)timeout->QuadPart>now?(uint64_t)timeout->QuadPart-now:0;}
    uint64_t ms=ticks/10000+(ticks%10000!=0);return ms>=INFINITE?INFINITE-1:(DWORD)ms;
}
LONG pilot_posix_nt_wait_many(ULONG count,const HANDLE *list,ULONG kind,BOOLEAN alertable,LARGE_INTEGER *timeout)
{
    if(kind>1)return (LONG)0xc000000du;
    if(alertable && dispatch_apcs())return WAIT_IO_COMPLETION;
    DWORD milliseconds=nt_timeout(timeout);uint64_t end=milliseconds==INFINITE?UINT64_MAX:GetTickCount64()+milliseconds;
    for(;;) {
        DWORD slice=milliseconds;if(alertable && slice>10)slice=10;
        DWORD result=WaitForMultipleObjects(count,list,kind==0,slice);
        if(result!=WAIT_TIMEOUT)return result==WAIT_FAILED?(LONG)0xc0000008u:(LONG)result;
        if(alertable && dispatch_apcs())return WAIT_IO_COMPLETION;
        uint64_t now=GetTickCount64();if(now>=end)return WAIT_TIMEOUT;
        milliseconds=end==UINT64_MAX?INFINITE:(DWORD)(end-now);
    }
}
LONG pilot_posix_nt_wait(HANDLE h,BOOLEAN alertable,LARGE_INTEGER *timeout)
{return pilot_posix_nt_wait_many(1,&h,1,alertable,timeout);}
LONG pilot_posix_nt_create_event(PHANDLE out,ACCESS_MASK access,POBJECT_ATTRIBUTES attr,ULONG type,BOOLEAN initial)
{(void)access;if(!out || attr || type>1)return (LONG)0xc000000du;*out=CreateEventW(NULL,type==0,initial,NULL);return *out?0:(LONG)0xc000009au;}
LONG pilot_posix_nt_set_event(HANDLE pointer,PLONG previous)
{
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);BOOL ok=h && h->kind==EVENT;
    if(ok){if(previous)*previous=h->signaled;h->signaled=1;pthread_cond_broadcast(&wait_changed);}pthread_mutex_unlock(&wait_mutex);return ok?0:(LONG)0xc0000008u;
}
LONG pilot_posix_nt_reset_event(HANDLE pointer,PLONG previous)
{
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);BOOL ok=h && h->kind==EVENT;
    if(ok){if(previous)*previous=h->signaled;h->signaled=0;}pthread_mutex_unlock(&wait_mutex);return ok?0:(LONG)0xc0000008u;
}
LONG pilot_posix_nt_query_event(HANDLE pointer,ULONG kind,void *out,ULONG bytes,PULONG written)
{
    if(kind || !out || bytes<8)return (LONG)0xc0000004u;
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);BOOL ok=h && h->kind==EVENT;
    if(ok){uint32_t data[]={h->manual?0:1,(uint32_t)h->signaled};memcpy(out,data,sizeof(data));if(written)*written=8;}pthread_mutex_unlock(&wait_mutex);return ok?0:(LONG)0xc0000008u;
}
LONG pilot_posix_nt_create_mutant(PHANDLE out,ACCESS_MASK access,POBJECT_ATTRIBUTES attr,BOOLEAN initial)
{
    (void)access;if(!out || attr)return (LONG)0xc000000du;
    struct WaitHandle *h=new_handle(MUTANT);if(!h)return (LONG)0xc000009au;
    if(initial){h->owner=GetCurrentThreadId();h->count=1;}*out=h;return 0;
}
LONG pilot_posix_nt_release_mutant(HANDLE pointer,PLONG previous)
{
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);uint32_t status=0xc0000008u;
    if(h && h->kind==MUTANT) {
        status=0xc0000046u;if(h->owner==GetCurrentThreadId()) {
            if(previous)*previous=1-h->count;if(--h->count==0)h->owner=0;pthread_cond_broadcast(&wait_changed);status=0;
        }
    }
    pthread_mutex_unlock(&wait_mutex);return (LONG)status;
}
LONG pilot_posix_nt_query_mutant(HANDLE pointer,ULONG kind,void *out,ULONG bytes,PULONG written)
{
    if(kind || !out || bytes<8)return (LONG)0xc0000004u;
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(pointer);BOOL ok=h && h->kind==MUTANT;
    if(ok){unsigned char data[8]={0};int32_t count=1-h->count;memcpy(data,&count,4);data[4]=h->owner==GetCurrentThreadId();memcpy(out,data,8);if(written)*written=8;}
    pthread_mutex_unlock(&wait_mutex);return ok?0:(LONG)0xc0000008u;
}
LONG pilot_posix_nt_signal_wait(HANDLE signal,HANDLE wait,BOOLEAN alertable,LARGE_INTEGER *timeout)
{
    pthread_mutex_lock(&wait_mutex);struct WaitHandle *h=lookup(signal);uint32_t status=0;
    if(!h)status=0xc0000008u;
    else if(h->kind==EVENT){h->signaled=1;pthread_cond_broadcast(&wait_changed);}
    else if(h->kind==MUTANT && h->owner==GetCurrentThreadId()){if(--h->count==0)h->owner=0;pthread_cond_broadcast(&wait_changed);}
    else status=0xc0000024u;
    pthread_mutex_unlock(&wait_mutex);return status?(LONG)status:pilot_posix_nt_wait(wait,alertable,timeout);
}
LONG pilot_posix_nt_delay(BOOLEAN alertable,LARGE_INTEGER *timeout)
{
    if(!timeout)return (LONG)0xc000000du;
    HANDLE timer=CreateWaitableTimerW(NULL,FALSE,NULL);if(!timer)return (LONG)0xc000009au;
    LONG result=(LONG)0xc000000du;if(SetWaitableTimer(timer,timeout,0,NULL,NULL,FALSE))result=pilot_posix_nt_wait(timer,alertable,NULL);CloseHandle(timer);return result;
}

struct PilotPosixWait {PTP_WAIT_CALLBACK callback;void *context;};
PTP_WAIT CreateThreadpoolWait(PTP_WAIT_CALLBACK callback,void *context,void *environment)
{
    if(!callback || environment){SetLastError(ERROR_NOT_SUPPORTED);return NULL;}
    PTP_WAIT wait=malloc(sizeof(*wait));if(wait)*wait=(struct PilotPosixWait){callback,context};return wait;
}
void CloseThreadpoolWait(PTP_WAIT wait){free(wait);}
void SetThreadpoolWait(PTP_WAIT wait,HANDLE event,const FILETIME *timeout)
{
    /* Native file transfers complete synchronously. Preserve that completion
     * boundary rather than fabricate STATUS_PENDING or a duplicate callback.
     * Unexpected use is an explicit host error until an async owner exists.
     */
    (void)wait;(void)event;(void)timeout;fprintf(stderr,"[PILOT STOP] unqualified Linux threadpool wait\n");abort();
}

/* Reservations retain exact logical page permissions. Recommit preserves
 * existing bytes; decommit discards them, so later commit observes zeroes.
 */
struct Reservation {struct Reservation *next;unsigned char *base;uint16_t *pages;size_t bytes;DWORD allocation_protect;};
static struct Reservation *reservations;
static pthread_mutex_t memory_mutex=PTHREAD_MUTEX_INITIALIZER;
enum {PAGE_BYTES=4096};
static int protection(DWORD p)
{
    switch(p&~(PAGE_NOCACHE|PAGE_WRITECOMBINE)){case PAGE_NOACCESS:return PROT_NONE;case PAGE_READONLY:return PROT_READ;case PAGE_READWRITE:case PAGE_WRITECOPY:return PROT_READ|PROT_WRITE;case PAGE_EXECUTE:return PROT_EXEC;case PAGE_EXECUTE_READ:return PROT_EXEC|PROT_READ;case PAGE_EXECUTE_READWRITE:case PAGE_EXECUTE_WRITECOPY:return PROT_EXEC|PROT_READ|PROT_WRITE;default:return -1;}
}
static struct Reservation *reservation(const void *pointer)
{uintptr_t p=(uintptr_t)pointer;for(struct Reservation *r=reservations;r;r=r->next)if(p>=(uintptr_t)r->base && p-(uintptr_t)r->base<r->bytes)return r;return NULL;}
void *VirtualAlloc(void *address,SIZE_T bytes,DWORD type,DWORD protect)
{
    if(!bytes || bytes>SIZE_MAX-PAGE_BYTES || protection(protect)<0 || type&~(MEM_RESERVE|MEM_COMMIT|MEM_TOP_DOWN) || !(type&(MEM_RESERVE|MEM_COMMIT)) || sysconf(_SC_PAGESIZE)!=PAGE_BYTES){SetLastError(ERROR_INVALID_PARAMETER);return NULL;}
    uintptr_t start=(uintptr_t)address&~(uintptr_t)(PAGE_BYTES-1);size_t size=(bytes+(uintptr_t)address-start+PAGE_BYTES-1)&~(size_t)(PAGE_BYTES-1);
    pthread_mutex_lock(&memory_mutex);struct Reservation *r=reservation(address);
    if(!r || type&MEM_RESERVE) {
        if(r){pthread_mutex_unlock(&memory_mutex);SetLastError(ERROR_INVALID_PARAMETER);return NULL;}
        int flags=MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE;
        if(address)flags|=MAP_FIXED_NOREPLACE;
        void *base=mmap((void*)start,size,PROT_NONE,flags,-1,0);
        if(base==MAP_FAILED){pthread_mutex_unlock(&memory_mutex);SetLastError(ERROR_NOT_ENOUGH_MEMORY);return NULL;}
        r=calloc(1,sizeof(*r));uint16_t *pages=calloc(size/PAGE_BYTES,sizeof(*pages));
        if(!r || !pages){free(r);free(pages);munmap(base,size);pthread_mutex_unlock(&memory_mutex);SetLastError(ERROR_NOT_ENOUGH_MEMORY);return NULL;}
        r->base=base;r->bytes=size;r->pages=pages;r->allocation_protect=protect;r->next=reservations;reservations=r;start=(uintptr_t)base;
    }
    if(start<(uintptr_t)r->base || size>r->bytes-(start-(uintptr_t)r->base)) {pthread_mutex_unlock(&memory_mutex);SetLastError(ERROR_INVALID_PARAMETER);return NULL;}
    if(type&MEM_COMMIT) {
        if(mprotect((void*)start,size,protection(protect))){pthread_mutex_unlock(&memory_mutex);SetLastError(ERROR_ACCESS_DENIED);return NULL;}
        size_t first=(start-(uintptr_t)r->base)/PAGE_BYTES;
        for(size_t i=0;i<size/PAGE_BYTES;++i)r->pages[first+i]=(uint16_t)protect;
    }
    pthread_mutex_unlock(&memory_mutex);return (void*)start;
}
BOOL VirtualFree(void *address,SIZE_T bytes,DWORD type)
{
    pthread_mutex_lock(&memory_mutex);struct Reservation *r=reservation(address);BOOL ok=FALSE;
    if(!r)goto done;
    if(type==MEM_RELEASE && !bytes && address==r->base) {
        if(munmap(r->base,r->bytes))goto done;
        struct Reservation **at=&reservations;while(*at!=r)at=&(*at)->next;*at=r->next;free(r->pages);free(r);ok=TRUE;
    } else if(type==MEM_DECOMMIT) {
        uintptr_t start=(uintptr_t)address&~(uintptr_t)(PAGE_BYTES-1);size_t size=bytes?(bytes+(uintptr_t)address-start+PAGE_BYTES-1)&~(size_t)(PAGE_BYTES-1):r->bytes;
        if((!bytes && address!=r->base) || size>r->bytes-(start-(uintptr_t)r->base))goto done;
        if(mprotect((void*)start,size,PROT_NONE) || madvise((void*)start,size,MADV_DONTNEED))goto done;
        memset(r->pages+(start-(uintptr_t)r->base)/PAGE_BYTES,0,size/PAGE_BYTES*sizeof(*r->pages));ok=TRUE;
    }
done:
    pthread_mutex_unlock(&memory_mutex);return ok?TRUE:error(ERROR_INVALID_PARAMETER);
}
BOOL VirtualProtect(void *address,SIZE_T bytes,DWORD protect,DWORD *previous)
{
    if(!bytes || bytes>SIZE_MAX-PAGE_BYTES || !previous || protection(protect)<0)return error(ERROR_INVALID_PARAMETER);
    uintptr_t start=(uintptr_t)address&~(uintptr_t)(PAGE_BYTES-1);size_t size=(bytes+(uintptr_t)address-start+PAGE_BYTES-1)&~(size_t)(PAGE_BYTES-1);
    pthread_mutex_lock(&memory_mutex);struct Reservation *r=reservation(address);BOOL ok=FALSE;
    if(!r || size>r->bytes-(start-(uintptr_t)r->base))goto done;
    size_t page=(start-(uintptr_t)r->base)/PAGE_BYTES;
    for(size_t i=0;i<size/PAGE_BYTES;++i)if(!r->pages[page+i])goto done;
    if(mprotect((void*)start,size,protection(protect)))goto done;
    *previous=r->pages[page];for(size_t i=0;i<size/PAGE_BYTES;++i)r->pages[page+i]=(uint16_t)protect;ok=TRUE;
done:
    pthread_mutex_unlock(&memory_mutex);return ok?TRUE:error(ERROR_INVALID_PARAMETER);
}
SIZE_T VirtualQuery(const void *address,MEMORY_BASIC_INFORMATION *out,SIZE_T bytes)
{
    if(!out || bytes<sizeof(*out)){SetLastError(ERROR_INVALID_PARAMETER);return 0;}
    pthread_mutex_lock(&memory_mutex);struct Reservation *r=reservation(address);
    if(!r){pthread_mutex_unlock(&memory_mutex);SetLastError(ERROR_INVALID_PARAMETER);return 0;}
    size_t page=((uintptr_t)address-(uintptr_t)r->base)/PAGE_BYTES,first=page,last=page+1;uint16_t p=r->pages[page];
    while(first && r->pages[first-1]==p)--first;
    while(last<r->bytes/PAGE_BYTES && r->pages[last]==p)++last;
    *out=(MEMORY_BASIC_INFORMATION){r->base+first*PAGE_BYTES,r->base,r->allocation_protect,(last-first)*PAGE_BYTES,p?MEM_COMMIT:MEM_RESERVE,p,MEM_PRIVATE};
    pthread_mutex_unlock(&memory_mutex);return sizeof(*out);
}
void GetSystemInfo(SYSTEM_INFO *out){*out=(SYSTEM_INFO){(DWORD)sysconf(_SC_PAGESIZE),65536};}
BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX *out)
{
    if(!out || out->dwLength!=sizeof(*out))return error(ERROR_INVALID_PARAMETER);
    struct sysinfo info;if(sysinfo(&info))return FALSE;
    uint64_t total=(uint64_t)info.totalram*info.mem_unit,available=(uint64_t)(info.freeram+info.bufferram)*info.mem_unit;
    *out=(MEMORYSTATUSEX){sizeof(*out),(DWORD)(100-(available*100/total)),total,available,total+(uint64_t)info.totalswap*info.mem_unit,available+(uint64_t)info.freeswap*info.mem_unit,UINT64_C(0x7fffffffffff),UINT64_C(0x7fffffffffff),0};return TRUE;
}
HANDLE GetProcessHeap(void){return (HANDLE)(intptr_t)1;}
void *HeapAlloc(HANDLE heap,DWORD flags,SIZE_T bytes){if(heap!=GetProcessHeap() || flags&~HEAP_ZERO_MEMORY){SetLastError(ERROR_INVALID_PARAMETER);return NULL;}return flags&HEAP_ZERO_MEMORY?calloc(1,bytes):malloc(bytes);}
BOOL HeapFree(HANDLE heap,DWORD flags,void *pointer){if(heap!=GetProcessHeap() || flags)return error(ERROR_INVALID_PARAMETER);free(pointer);return TRUE;}
DWORD SetErrorMode(DWORD mode){(void)mode;return 0;}
