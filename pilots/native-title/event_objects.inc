/* Native waitable handles. Phobos's host-event approach, with actual NT
 * previous-state/query/timeout behavior rather than synthesized state. Kernel
 * Ke* layout/state transitions are independently executed in the LLE oracle.
 * No live switching: handles are native-mode objects, not guest KEVENT pointers. */
enum PilotWaitKind { PILOT_WAIT_EVENT=1, PILOT_WAIT_MUTANT=2 };
struct PilotWaitHandle {uint32_t id,kind;HANDLE host;struct PilotWaitHandle *next;};
static struct PilotWaitHandle *wait_handles;
static uint32_t event_next_id=0x30000001;
static SRWLOCK event_lock=SRWLOCK_INIT;
static INIT_ONCE event_once=INIT_ONCE_STATIC_INIT;
typedef LONG (NTAPI *EventCreate)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,ULONG,BOOLEAN);
typedef LONG (NTAPI *EventSet)(HANDLE,PLONG);
typedef LONG (NTAPI *EventQuery)(HANDLE,ULONG,PVOID,ULONG,PULONG);
typedef LONG (NTAPI *EventWait)(HANDLE,BOOLEAN,PLARGE_INTEGER);
typedef LONG (NTAPI *EventWaitMultiple)(ULONG,const HANDLE *,ULONG,BOOLEAN,PLARGE_INTEGER);
typedef LONG (NTAPI *EventSignalWait)(HANDLE,HANDLE,BOOLEAN,PLARGE_INTEGER);
typedef LONG (NTAPI *ThreadDelay)(BOOLEAN,PLARGE_INTEGER);
typedef LONG (NTAPI *MutantCreate)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,BOOLEAN);
static EventCreate event_nt_create;
static EventSet event_nt_set,event_nt_reset;
static EventQuery event_nt_query;
static EventWait event_nt_wait;
static EventWaitMultiple event_nt_wait_multiple;
static EventSignalWait event_nt_signal_wait;
static ThreadDelay thread_nt_delay;
static MutantCreate mutant_nt_create;
static EventSet mutant_nt_release;
static EventQuery mutant_nt_query;
static HANDLE media_wait_duplicate(uint32_t id,uint32_t *status);
static HANDLE thread_wait_duplicate(uint32_t id,uint32_t *status);
static BOOL CALLBACK event_start(PINIT_ONCE once,void *parameter,void **context)
{
    (void)once;(void)parameter;(void)context;HMODULE nt=GetModuleHandleW(L"ntdll.dll");
    event_nt_create=(EventCreate)(void*)GetProcAddress(nt,"NtCreateEvent");
    event_nt_set=(EventSet)(void*)GetProcAddress(nt,"NtSetEvent");
    event_nt_reset=(EventSet)(void*)GetProcAddress(nt,"NtResetEvent");
    event_nt_query=(EventQuery)(void*)GetProcAddress(nt,"NtQueryEvent");
    event_nt_wait=(EventWait)(void*)GetProcAddress(nt,"NtWaitForSingleObject");
    event_nt_wait_multiple=(EventWaitMultiple)(void*)GetProcAddress(nt,"NtWaitForMultipleObjects");
    event_nt_signal_wait=(EventSignalWait)(void*)GetProcAddress(nt,"NtSignalAndWaitForSingleObject");
    thread_nt_delay=(ThreadDelay)(void*)GetProcAddress(nt,"NtDelayExecution");
    mutant_nt_create=(MutantCreate)(void*)GetProcAddress(nt,"NtCreateMutant");
    mutant_nt_release=(EventSet)(void*)GetProcAddress(nt,"NtReleaseMutant");
    mutant_nt_query=(EventQuery)(void*)GetProcAddress(nt,"NtQueryMutant");
    if(!event_nt_create || !event_nt_set || !event_nt_reset || !event_nt_query || !event_nt_wait || !event_nt_wait_multiple ||
       !event_nt_signal_wait || !thread_nt_delay ||
       !mutant_nt_create || !mutant_nt_release || !mutant_nt_query)
        stop("native waitable services unavailable",0);
    return TRUE;
}
static int wait_native_thread_supported(void)
{
#ifdef PILOT_SCHEDULING_LLE
    return 0;
#else
    return !dispatch_in_dpc;
#endif
}
static HANDLE wait_duplicate_owned(uint32_t id,unsigned kind,uint32_t *status,int waiting)
{
    HANDLE host=NULL;*status=0xc0000008u;AcquireSRWLockShared(&event_lock);
    for(struct PilotWaitHandle *e=wait_handles;e;e=e->next)if(e->id==id) {
        if(kind && e->kind!=kind)*status=0xc0000024u;
        else {
            if(waiting && e->kind==PILOT_WAIT_MUTANT && !wait_native_thread_supported()) {
                ReleaseSRWLockShared(&event_lock);stop("native mutant wait context unqualified",id);
            }
            *status=DuplicateHandle(GetCurrentProcess(),e->host,GetCurrentProcess(),&host,0,FALSE,DUPLICATE_SAME_ACCESS)?0:0xc000009au;
        }
        break;
    }
    ReleaseSRWLockShared(&event_lock);return host;
}
static HANDLE wait_duplicate(uint32_t id,unsigned kind,uint32_t *status)
{return wait_duplicate_owned(id,kind,status,0);}
/* File completion APIs require an event, not merely any waitable handle. */
static HANDLE event_duplicate(uint32_t id,uint32_t *status)
{return wait_duplicate(id,PILOT_WAIT_EVENT,status);}
static uint32_t wait_close(uint32_t id)
{
    uint32_t status=0xc0000008u;AcquireSRWLockExclusive(&event_lock);
    struct PilotWaitHandle **at=&wait_handles;
    while(*at && (*at)->id!=id)at=&(*at)->next;
    if(*at){struct PilotWaitHandle *e=*at;*at=e->next;
        if(!CloseHandle(e->host))stop("owned waitable close failed",id);free(e);status=0;}
    ReleaseSRWLockExclusive(&event_lock);return status;
}
static uint32_t wait_publish(struct PilotWaitHandle *e,uint32_t output)
{
    AcquireSRWLockExclusive(&event_lock);
    if(event_next_id==0x40000000u)stop("waitable handle identity exhausted",0);
    e->id=event_next_id++;e->next=wait_handles;wait_handles=e;MEM32(output)=e->id;
    ReleaseSRWLockExclusive(&event_lock);return 0;
}
static int wait_range(uint32_t address,unsigned bytes,int writable)
{
    if(!address || (uint64_t)address+bytes>UINT64_C(0x100000000))return 0;
    uintptr_t at=(uintptr_t)(memory+address),end=at+bytes;
    if(end<at)return 0;
    while(at<end) {
        MEMORY_BASIC_INFORMATION info;
        DWORD allowed=PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY;
        if(!writable)allowed|=PAGE_READONLY|PAGE_EXECUTE_READ;
        if(!VirtualQuery((void*)at,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           (info.Protect&(PAGE_NOACCESS|PAGE_GUARD)) || !(info.Protect&allowed))return 0;
        uintptr_t next=(uintptr_t)info.BaseAddress+info.RegionSize;
        if(next<=at)return 0;at=next;
    }
    return 1;
}
static void wait_native_thread_context(unsigned ordinal)
{
    if(!wait_native_thread_supported())stop("native blocking wait context unqualified",ordinal);
}
static HANDLE wait_reference(uint32_t id,uint32_t *status)
{
    if((id&0xf0000000u)==0x40000000u)return thread_wait_duplicate(id,status);
    if((id&0xf0000000u)==0x50000000u)return media_wait_duplicate(id,status);
    return wait_duplicate_owned(id,0,status,1);
}
static void delay_thread_kernel(void)
{
    wait_native_thread_context(99);
    if(!InitOnceExecuteOnce(&event_once,event_start,NULL,NULL))stop("delay initialization failed",0);
    uint32_t mode=MEM8(g_esp+4),alertable=MEM8(g_esp+8),interval=MEM32(g_esp+12);
    if(mode>1 || alertable)stop("native delay APC/mode unqualified",mode);
    if(!wait_range(interval,8,0))stop("native delay interval range unqualified",interval);
    LARGE_INTEGER due;memcpy(&due,memory+interval,8);
    /* Relative 100ns intervals and zero share the original contract. Absolute
     * dates need a proved guest/host clock-domain translation, not a guess. */
    if(due.QuadPart>0)stop("native absolute delay unqualified",interval);
    g_eax=(uint32_t)thread_nt_delay(FALSE,&due);
    /* Desktop's optimized yield may report STATUS_NO_YIELD_PERFORMED.
     * Original KeDelay returns success on the expired/zero-time path, even
     * when no peer runs. Retain the real wait and all other native statuses. */
    if(g_eax==0x40000024u)g_eax=0;
    g_esp+=16;
}
static void wait_single_kernel(unsigned ordinal)
{
    uint32_t id=MEM32(g_esp+4),mode=ordinal==234?MEM8(g_esp+8):0;
    uint32_t alertable=MEM8(g_esp+(ordinal==234?12:8));
    uint32_t timeout=MEM32(g_esp+(ordinal==234?16:12));
    if(mode>1 || alertable)stop("native single wait APC/mode unqualified",id);
    if(timeout && !wait_range(timeout,8,0))stop("native single wait timeout range unqualified",timeout);
    LARGE_INTEGER due;if(timeout)memcpy(&due,memory+timeout,8);
    uint32_t status;HANDLE host=wait_reference(id,&status);g_eax=status;
    if(host) {
        g_eax=(uint32_t)event_nt_wait(host,FALSE,timeout?&due:NULL);
        if(!CloseHandle(host))stop("single wait operation reference close failed",id);
    }
    g_esp+=ordinal==234?20:16;
}
static void signal_wait_kernel(void)
{
    wait_native_thread_context(230);
    if(!InitOnceExecuteOnce(&event_once,event_start,NULL,NULL))stop("signal/wait initialization failed",0);
    uint32_t signal=MEM32(g_esp+4),wait=MEM32(g_esp+8),mode=MEM8(g_esp+12);
    uint32_t alertable=MEM8(g_esp+16),timeout=MEM32(g_esp+20);
    if(mode>1 || alertable)stop("native signal/wait APC/mode unqualified",signal);
    if(timeout && !wait_range(timeout,8,0))stop("native signal/wait timeout range unqualified",timeout);
    if((signal&0xf0000000u)!=0x30000000u)stop("native signal/wait signal type unqualified",signal);
    LARGE_INTEGER due;if(timeout)memcpy(&due,memory+timeout,8);
    /* Desktop paired waits do not promise the original cross-CPU handoff.
     * Actual thread termination is persistent; other targets are admitted only
     * for the source-compared zero-poll component, never a transient handshake. */
    if((wait&0xf0000000u)!=0x40000000u && (!timeout || due.QuadPart!=0))
        stop("native signal/wait blocking target unqualified",wait);
    uint32_t status;HANDLE first=event_duplicate(signal,&status);g_eax=status;
    if(!first && status==0xc0000024u)stop("native signal/wait signal type unqualified",signal);
    if(first) {
        HANDLE second=wait_reference(wait,&status);g_eax=status;
        if(second) {
            /* Both references precede the first mutation. Use the real paired
             * NT service; a separate SetEvent+Wait sequence is not used. */
            g_eax=(uint32_t)event_nt_signal_wait(first,second,FALSE,timeout?&due:NULL);
            if(!CloseHandle(second))stop("signal/wait wait reference close failed",wait);
        }
        if(!CloseHandle(first))stop("signal/wait signal reference close failed",signal);
    }
    g_esp+=24;
}
static void wait_multiple_kernel(void)
{
    wait_native_thread_context(235);
    if(!InitOnceExecuteOnce(&event_once,event_start,NULL,NULL))stop("multiwait initialization failed",0);
    uint32_t count=MEM32(g_esp+4),array=MEM32(g_esp+8),kind=MEM32(g_esp+12);
    uint32_t mode=MEM32(g_esp+16),alertable=MEM8(g_esp+20),timeout=MEM32(g_esp+24);
    if(!count || count>64 || kind>1 || mode>1 || alertable)
        stop("unqualified multi-object wait parameters",count);
    if(!wait_range(array,count*4,0) || (timeout && !wait_range(timeout,8,0)))
        stop("unqualified multi-object wait range",array);
    uint32_t ids[64];HANDLE handles[64]={0};memcpy(ids,memory+array,count*4);
    for(unsigned i=0;i<count;++i) {
#ifdef PILOT_FILES_LLE
        if(ids[i]<0x30000000u)stop("original-handle multiwait unqualified",ids[i]);
#endif
    }
    /* WaitAll duplicates are not admitted: no state may be consumed twice. */
    if(!kind)for(unsigned i=0;i<count;++i)for(unsigned j=0;j<i;++j)
        if(ids[i]==ids[j])stop("duplicate WaitAll object unqualified",ids[i]);
    LARGE_INTEGER due;if(timeout)memcpy(&due,memory+timeout,8);
    g_eax=0;
    for(unsigned i=0;i<count;++i) {
        uint32_t status;handles[i]=wait_reference(ids[i],&status);
        if(!handles[i]){g_eax=status;break;}
    }
    if(!g_eax)g_eax=(uint32_t)event_nt_wait_multiple(count,handles,kind,FALSE,timeout?&due:NULL);
    for(unsigned i=0;i<count;++i)if(handles[i] && !CloseHandle(handles[i]))
        stop("multiwait operation reference close failed",ids[i]);
    g_esp+=28;
}
static void event_kernel(unsigned ordinal)
{
    if(!InitOnceExecuteOnce(&event_once,event_start,NULL,NULL))stop("event initialization failed",0);
    if(ordinal==233 || ordinal==234){wait_single_kernel(ordinal);return;}
    uint32_t first=MEM32(g_esp+4),second=MEM32(g_esp+8);
    if(ordinal==189) {
        uint32_t type=MEM32(g_esp+12),state=MEM32(g_esp+16);g_eax=0xc000000du;
        if(second && (MEM32(second) || MEM32(second+4) || (MEM32(second+8)&~0x40u)))
            stop("named/inherited event outside qualified scope",second);
        if(first && type<=1) {
            struct PilotWaitHandle *e=calloc(1,sizeof(*e));g_eax=0xc000009au;
            if(e) {
                e->kind=PILOT_WAIT_EVENT;
                g_eax=(uint32_t)event_nt_create(&e->host,EVENT_ALL_ACCESS,NULL,type,(BOOLEAN)state);
                if(!g_eax)g_eax=wait_publish(e,first);else free(e);
            }
        }
        g_esp+=20;return;
    }
    uint32_t status;HANDLE host=event_duplicate(first,&status);g_eax=status;
    if(host) {
        if(ordinal==225 || ordinal==186) {
            LONG previous;g_eax=(uint32_t)(ordinal==225?event_nt_set:event_nt_reset)(host,&previous);
            if(!g_eax && ordinal==225 && second)MEM32(second)=(uint32_t)previous;
        } else if(ordinal==209) {
            struct {ULONG type;LONG state;} info;
            g_eax=second?(uint32_t)event_nt_query(host,0,&info,sizeof(info),NULL):0xc000000du;
            if(!g_eax)memcpy(memory+second,&info,sizeof(info));
        }
        if(!CloseHandle(host))stop("event operation reference close failed",first);
    }
    g_esp+=ordinal==186?8:12;
}
static int mutant_output(uint32_t address,unsigned bytes)
{return wait_range(address,bytes,1);}
static void mutant_kernel(unsigned ordinal)
{
    wait_native_thread_context(ordinal);
    if(!InitOnceExecuteOnce(&event_once,event_start,NULL,NULL))stop("mutant initialization failed",0);
    uint32_t first=MEM32(g_esp+4),second=MEM32(g_esp+8);
    if(ordinal==192) {
        BOOLEAN initial=MEM8(g_esp+12);
        if(second && (!wait_range(second,12,0) || MEM32(second) || MEM32(second+4) || (MEM32(second+8)&~0x40u)))
            stop("named/inherited mutant outside qualified scope",second);
        if(!mutant_output(first,4))stop("unqualified mutant create output",first);
        /* Reserve bookkeeping before granting real initial ownership. */
        struct PilotWaitHandle *e=calloc(1,sizeof(*e));g_eax=0xc000009au;
        if(e) {
            e->kind=PILOT_WAIT_MUTANT;
            g_eax=(uint32_t)mutant_nt_create(&e->host,0x001f0001u,NULL,initial);
            if(!g_eax)g_eax=wait_publish(e,first);else free(e);
        }
        g_esp+=16;return;
    }
    uint32_t status;HANDLE host=wait_duplicate(first,PILOT_WAIT_MUTANT,&status);g_eax=status;
    if(host) {
        if(ordinal==221) {
            if(second && !mutant_output(second,4)) {
                CloseHandle(host);stop("unqualified mutant release output",second);
            }
            LONG previous=0;g_eax=(uint32_t)mutant_nt_release(host,&previous);
            /* Original NtReleaseMutant catches KeReleaseMutant's exception
             * and returns its code; this is not propagated to the caller. */
            if(!g_eax && second)MEM32(second)=(uint32_t)previous;
        } else {
            /* Xbox ABI is two arguments; desktop query-class/size stay local. */
            if(!mutant_output(second,6)) {
                CloseHandle(host);stop("unqualified mutant query output",second);
            }
            struct { LONG count;UCHAR owned,abandoned; } info={0};
            g_eax=(uint32_t)mutant_nt_query(host,0,&info,sizeof(info),NULL);
            /* The original writes six field bytes and leaves padding alone. */
            if(!g_eax)memcpy(memory+second,&info,6);
        }
        if(!CloseHandle(host))stop("mutant operation reference close failed",first);
    }
    g_esp+=12;
}
