/* Native object-handle events. Phobos's host-event approach, with actual NT
 * previous-state/query/timeout behavior rather than synthesized state. Kernel
 * Ke* layout/state transitions are independently executed in the LLE oracle.
 * No live switching: handles are native-mode objects, not guest KEVENT pointers. */
struct PilotEventHandle {uint32_t id;HANDLE host;struct PilotEventHandle *next;};
static struct PilotEventHandle *event_handles;
static uint32_t event_next_id=0x30000001;
static SRWLOCK event_lock=SRWLOCK_INIT;
static INIT_ONCE event_once=INIT_ONCE_STATIC_INIT;
typedef LONG (NTAPI *EventCreate)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,ULONG,BOOLEAN);
typedef LONG (NTAPI *EventSet)(HANDLE,PLONG);
typedef LONG (NTAPI *EventQuery)(HANDLE,ULONG,PVOID,ULONG,PULONG);
typedef LONG (NTAPI *EventWait)(HANDLE,BOOLEAN,PLARGE_INTEGER);
static EventCreate event_nt_create;
static EventSet event_nt_set,event_nt_reset;
static EventQuery event_nt_query;
static EventWait event_nt_wait;
static BOOL CALLBACK event_start(PINIT_ONCE once,void *parameter,void **context)
{
    (void)once;(void)parameter;(void)context;HMODULE nt=GetModuleHandleW(L"ntdll.dll");
    event_nt_create=(EventCreate)(void*)GetProcAddress(nt,"NtCreateEvent");
    event_nt_set=(EventSet)(void*)GetProcAddress(nt,"NtSetEvent");
    event_nt_reset=(EventSet)(void*)GetProcAddress(nt,"NtResetEvent");
    event_nt_query=(EventQuery)(void*)GetProcAddress(nt,"NtQueryEvent");
    event_nt_wait=(EventWait)(void*)GetProcAddress(nt,"NtWaitForSingleObject");
    if(!event_nt_create || !event_nt_set || !event_nt_reset || !event_nt_query || !event_nt_wait)
        stop("native event services unavailable",0);
    return TRUE;
}
static HANDLE event_duplicate(uint32_t id,uint32_t *status)
{
    HANDLE host=NULL;*status=0xc0000008u;AcquireSRWLockShared(&event_lock);
    for(struct PilotEventHandle *e=event_handles;e;e=e->next)if(e->id==id) {
        *status=DuplicateHandle(GetCurrentProcess(),e->host,GetCurrentProcess(),&host,0,FALSE,DUPLICATE_SAME_ACCESS)?0:0xc000009au;
        break;
    }
    ReleaseSRWLockShared(&event_lock);return host;
}
static uint32_t event_close(uint32_t id)
{
    uint32_t status=0xc0000008u;AcquireSRWLockExclusive(&event_lock);
    struct PilotEventHandle **at=&event_handles;
    while(*at && (*at)->id!=id)at=&(*at)->next;
    if(*at){struct PilotEventHandle *e=*at;*at=e->next;
        if(!CloseHandle(e->host))stop("owned event close failed",id);free(e);status=0;}
    ReleaseSRWLockExclusive(&event_lock);return status;
}
static void event_kernel(unsigned ordinal)
{
    if(!InitOnceExecuteOnce(&event_once,event_start,NULL,NULL))stop("event initialization failed",0);
    uint32_t first=MEM32(g_esp+4),second=MEM32(g_esp+8);
    if(ordinal==189) {
        uint32_t type=MEM32(g_esp+12),state=MEM32(g_esp+16);g_eax=0xc000000du;
        if(second && (MEM32(second) || MEM32(second+4) || (MEM32(second+8)&~0x40u)))
            stop("named/inherited event outside qualified scope",second);
        if(first && type<=1) {
            struct PilotEventHandle *e=calloc(1,sizeof(*e));
            if(!e)g_eax=0xc000009au;
            else {
                g_eax=(uint32_t)event_nt_create(&e->host,EVENT_ALL_ACCESS,NULL,type,(BOOLEAN)state);
                if(g_eax)free(e);
                else {AcquireSRWLockExclusive(&event_lock);
                    if(event_next_id==0x40000000u)stop("event handle identity exhausted",0);
                    e->id=event_next_id++;e->next=event_handles;event_handles=e;MEM32(first)=e->id;
                    ReleaseSRWLockExclusive(&event_lock);}
            }
        }
        g_esp+=20;return;
    }
    uint32_t status;HANDLE host=event_duplicate(first,&status);g_eax=status;
    if(!host && (ordinal==233 || ordinal==234) && ((first&0xf0000000u)==0x40000000u || (first&0xf0000000u)==0x50000000u))
        stop("non-event handle wait outside qualified scope",first);
    if(host) {
        if(ordinal==225 || ordinal==186) {
            LONG previous;g_eax=(uint32_t)(ordinal==225?event_nt_set:event_nt_reset)(host,&previous);
            if(!g_eax && ordinal==225 && second)MEM32(second)=(uint32_t)previous;
        } else if(ordinal==209) {
            struct {ULONG type;LONG state;} info;
            g_eax=second?(uint32_t)event_nt_query(host,0,&info,sizeof(info),NULL):0xc000000du;
            if(!g_eax)memcpy(memory+second,&info,sizeof(info));
        } else {
            uint32_t alertable=ordinal==234?MEM32(g_esp+12):second;
            uint32_t timeout=MEM32(g_esp+(ordinal==234?16:12));
            if(alertable)stop("alertable event wait requires guest APC delivery",first);
            if(ordinal==234 && second>1)stop("invalid event wait mode",second);
            LARGE_INTEGER due;if(timeout)due.QuadPart=(int64_t)MEM64(timeout);
            g_eax=(uint32_t)event_nt_wait(host,FALSE,timeout?&due:NULL);
        }
        if(!CloseHandle(host))stop("event operation reference close failed",first);
    }
    g_esp+=ordinal==186?8:ordinal==233?16:ordinal==234?20:12;
}
