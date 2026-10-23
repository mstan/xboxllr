#include "memory_write_watch.h"
#include <string.h>

#if defined(_WIN32) && !defined(PILOT_WRITE_WATCH_PORTABLE)
#include <windows.h>
#include "memory_write_watch_win32.h"
#include <stdlib.h>
enum { WATCH_PAGE=4096,UNTRACKED=0,ARMED=1,HANDLING=2,DIRTY=3,HOT=4,ARMING=5 };
struct WatchPage {volatile LONG state,hits,writers;volatile LONG64 version;};
static struct {
    unsigned char *base;
    size_t bytes,count;
    struct WatchPage *pages;
    void *handler;
    volatile LONG64 epoch,faults;
    uint64_t owner;
} pilot_memory_write_watch_state;
/* Unique matching-PDB diagnostic identity; not a guest/runtime ABI. */
#define watch pilot_memory_write_watch_state
static uint64_t owners;
static SRWLOCK watch_lock=SRWLOCK_INIT;
static uint64_t read64(volatile LONG64 *value)
{return (uint64_t)InterlockedCompareExchange64(value,0,0);}
static LONG read32(volatile LONG *value)
{return InterlockedCompareExchange(value,0,0);}
static int span(const void *data,size_t bytes,size_t *first,size_t *last)
{
    uintptr_t address=(uintptr_t)data,base=(uintptr_t)watch.base;
    if(!watch.pages || !bytes || address<base || bytes>watch.bytes || address-base>watch.bytes-bytes)return 0;
    *first=(size_t)(address-base)/WATCH_PAGE;
    *last=(size_t)(address-base+bytes-1)/WATCH_PAGE;
    return 1;
}
static LONG stable(struct WatchPage *page)
{
    LONG state;
    while((state=read32(&page->state))==HANDLING || state==ARMING)SwitchToThread();
    return state;
}
static LONG fault_body(EXCEPTION_POINTERS *exception)
{
    EXCEPTION_RECORD *record=exception->ExceptionRecord;
    if(!watch.pages || record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION ||
       record->NumberParameters<2 || record->ExceptionInformation[0]!=1)return EXCEPTION_CONTINUE_SEARCH;
    size_t first,last;
    void *address=(void*)record->ExceptionInformation[1];
    if(!span(address,1,&first,&last))return EXCEPTION_CONTINUE_SEARCH;
    struct WatchPage *page=watch.pages+first;
    for(;;) {
        LONG state=read32(&page->state);
        if(state==HANDLING || state==ARMING){SwitchToThread();continue;}
        if(state!=ARMED) {
            /* Another handler/VM invalidation may have repaired this same
             * fault already. Retry only an actually writable former watch. */
            MEMORY_BASIC_INFORMATION info;
            if(read64(&page->version) && VirtualQuery(address,&info,sizeof(info)) &&
               info.State==MEM_COMMIT && info.Protect==PAGE_READWRITE)return EXCEPTION_CONTINUE_EXECUTION;
            return EXCEPTION_CONTINUE_SEARCH;
        }
        if(InterlockedCompareExchange(&page->state,HANDLING,ARMED)!=ARMED)continue;
        /* Publish dirtiness before making the original store possible. The
         * actual instruction retries after returning; it is never emulated. */
        uint64_t version=(uint64_t)InterlockedIncrement64(&watch.epoch);
        InterlockedExchange64(&page->version,(LONG64)version);
        DWORD previous;
        if(!VirtualProtect(watch.base+first*WATCH_PAGE,WATCH_PAGE,PAGE_READWRITE,&previous)) {
            InterlockedExchange(&page->state,ARMED);return EXCEPTION_CONTINUE_SEARCH;
        }
        LONG hits=InterlockedIncrement(&page->hits);
        InterlockedIncrement64(&watch.faults);
        InterlockedExchange(&page->state,hits>=64?HOT:DIRTY);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
}
static LONG CALLBACK fault(EXCEPTION_POINTERS *exception)
{
    DWORD error=GetLastError();LONG result=fault_body(exception);
    SetLastError(error);return result;
}
int pilot_write_watch_initialize(void *base,size_t bytes)
{
    SYSTEM_INFO info;GetSystemInfo(&info);
    if(watch.pages || !base || !bytes || bytes>UINTPTR_MAX-(uintptr_t)base || ((uintptr_t)base&(WATCH_PAGE-1)) ||
       (bytes&(WATCH_PAGE-1)) || info.dwPageSize!=WATCH_PAGE)return 0;
    size_t count=bytes/WATCH_PAGE;
    if(count>SIZE_MAX/sizeof(struct WatchPage))return 0;
    struct WatchPage *pages=calloc(count,sizeof(*pages));if(!pages)return 0;
    watch.base=base;watch.bytes=bytes;watch.count=count;watch.pages=pages;
    watch.owner=++owners;watch.epoch=1;watch.faults=0;
    watch.handler=AddVectoredExceptionHandler(1,fault);
    if(!watch.handler){free(pages);memset(&watch,0,sizeof(watch));return 0;}
    return 1;
}
static int clipped(const void *data,size_t bytes,size_t *first,size_t *last)
{
    if(!watch.pages || !bytes)return 0;
    uintptr_t low=(uintptr_t)data,high=bytes>UINTPTR_MAX-low?UINTPTR_MAX:low+bytes;
    uintptr_t begin=(uintptr_t)watch.base,end=begin+watch.bytes;
    if(low<begin)low=begin;if(high>end)high=end;
    return high>low && span((void*)low,high-low,first,last);
}
static int invalidate_pages(size_t first,size_t last)
{
    int result=1;
    uint64_t version=0;
    for(size_t i=first;i<=last;++i) {
        struct WatchPage *page=watch.pages+i;LONG state;
        for(;;) {
            state=stable(page);
            if(state!=ARMED || InterlockedCompareExchange(&page->state,HANDLING,ARMED)==ARMED)break;
        }
        if(state==UNTRACKED)continue;
        if(!version)version=(uint64_t)InterlockedIncrement64(&watch.epoch);
        InterlockedExchange64(&page->version,(LONG64)version);
        if(state==ARMED) {
            DWORD previous;
            if(!VirtualProtect(watch.base+i*WATCH_PAGE,WATCH_PAGE,PAGE_READWRITE,&previous)) {
                InterlockedExchange(&page->state,ARMED);result=0;break;
            }
        }
        InterlockedExchange(&page->state,UNTRACKED);
    }
    return result;
}
int pilot_write_watch_invalidate(const void *data,size_t bytes)
{
    size_t first,last;if(!clipped(data,bytes,&first,&last))return 1;
    AcquireSRWLockExclusive(&watch_lock);int result=invalidate_pages(first,last);
    ReleaseSRWLockExclusive(&watch_lock);return result;
}
int pilot_write_watch_native_begin(const void *data,size_t bytes)
{
    size_t first,last;if(!clipped(data,bytes,&first,&last))return 1;
    AcquireSRWLockExclusive(&watch_lock);int result=invalidate_pages(first,last);
    if(result)for(size_t i=first;i<=last;++i)InterlockedIncrement(&watch.pages[i].writers);
    ReleaseSRWLockExclusive(&watch_lock);return result;
}
void pilot_write_watch_native_end(const void *data,size_t bytes)
{
    size_t first,last;if(!clipped(data,bytes,&first,&last))return;
    AcquireSRWLockExclusive(&watch_lock);
    for(size_t i=first;i<=last;++i)InterlockedDecrement(&watch.pages[i].writers);
    ReleaseSRWLockExclusive(&watch_lock);
}
int pilot_write_watch_begin(const void *data,size_t bytes,PilotWriteSnapshot *snapshot)
{
    memset(snapshot,0,sizeof(*snapshot));size_t first,last;
    if(!span(data,bytes,&first,&last))return 0;
    int result=1;
    AcquireSRWLockExclusive(&watch_lock);
    for(size_t i=first;i<=last;++i) {
        struct WatchPage *page=watch.pages+i;LONG state=stable(page);
        if(state==ARMED)continue;
        if(state==HOT || read32(&page->hits)>=64 || read32(&page->writers)){result=0;break;}
        MEMORY_BASIC_INFORMATION info;void *at=watch.base+i*WATCH_PAGE;
        if(!VirtualQuery(at,&info,sizeof(info)) || info.State!=MEM_COMMIT || info.Protect!=PAGE_READWRITE){result=0;break;}
        /* A store before protection is installed is included in the following
         * copy. Stores after it fault and invalidate the capture. */
        /* A delayed exception from an earlier protection cycle must not
         * unprotect this page between publishing ARMED and installing RO. */
        InterlockedExchange(&page->state,ARMING);
        DWORD previous;
        if(!VirtualProtect(at,WATCH_PAGE,PAGE_READONLY,&previous)) {
            InterlockedExchange(&page->state,UNTRACKED);result=0;break;
        }
        /* Rearming is a new protection generation, not just another clean
         * flag. A different in-flight capture must not bless old bytes after
         * a dirty page has been rearmed by this capture. */
        InterlockedExchange64(&page->version,InterlockedIncrement64(&watch.epoch));
        InterlockedExchange(&page->state,ARMED);
    }
    if(result) {
        snapshot->data=data;snapshot->bytes=bytes;snapshot->owner=watch.owner;
        snapshot->version=read64(&watch.epoch);
    }
    ReleaseSRWLockExclusive(&watch_lock);return result;
}
static int unchanged(const PilotWriteSnapshot *snapshot)
{
    size_t first,last;
    if(snapshot->owner!=watch.owner || !span(snapshot->data,snapshot->bytes,&first,&last))return 0;
    for(size_t i=first;i<=last;++i)
        if(read32(&watch.pages[i].state)!=ARMED || read64(&watch.pages[i].version)>snapshot->version)return 0;
    return 1;
}
void pilot_write_watch_end(PilotWriteSnapshot *snapshot)
{
    snapshot->checked=read64(&watch.epoch);
    snapshot->valid=unchanged(snapshot);
}
int pilot_write_watch_reusable(PilotWriteSnapshot *snapshot)
{
    if(!snapshot->valid || !watch.pages || snapshot->owner!=watch.owner)return 0;
    uint64_t now=read64(&watch.epoch);
    if(now==snapshot->checked)return 1;
    if(!unchanged(snapshot)){snapshot->valid=0;return 0;}
    snapshot->checked=now;return 1;
}
uint64_t pilot_write_watch_faults(void){return read64(&watch.faults);}
BOOL WINAPI pilot_write_watch_virtual_protect(LPVOID address,SIZE_T bytes,DWORD protect,PDWORD previous)
{
    if(!pilot_write_watch_native_begin(address,bytes))return FALSE;
    if(!pilot_write_watch_native_begin(previous,sizeof(*previous))) {
        DWORD error=GetLastError();pilot_write_watch_native_end(address,bytes);SetLastError(error);return FALSE;
    }
    BOOL result=VirtualProtect(address,bytes,protect,previous);DWORD error=GetLastError();
    pilot_write_watch_native_end(previous,sizeof(*previous));
    pilot_write_watch_native_end(address,bytes);SetLastError(error);return result;
}
LPVOID WINAPI pilot_write_watch_virtual_alloc(LPVOID address,SIZE_T bytes,DWORD type,DWORD protect)
{
    if(address && !pilot_write_watch_native_begin(address,bytes))return NULL;
    LPVOID result=VirtualAlloc(address,bytes,type,protect);DWORD error=GetLastError();
    if(address)pilot_write_watch_native_end(address,bytes);SetLastError(error);return result;
}
BOOL WINAPI pilot_write_watch_virtual_free(LPVOID address,SIZE_T bytes,DWORD type)
{
    const void *range=address;size_t length=bytes;
    if(!bytes && type==MEM_RELEASE) {
        MEMORY_BASIC_INFORMATION info;
        if(VirtualQuery(address,&info,sizeof(info)) &&
           info.AllocationBase==(void*)watch.base){range=watch.base;length=watch.bytes;}
    }
    if(!pilot_write_watch_native_begin(range,length))return FALSE;
    BOOL result=VirtualFree(address,bytes,type);DWORD error=GetLastError();
    pilot_write_watch_native_end(range,length);SetLastError(error);return result;
}
SIZE_T WINAPI pilot_write_watch_virtual_query(LPCVOID address,PMEMORY_BASIC_INFORMATION info,SIZE_T bytes)
{
    size_t output=bytes<sizeof(*info)?bytes:sizeof(*info);
    if(!pilot_write_watch_native_begin(info,output))return 0;
    SIZE_T result=VirtualQuery(address,info,bytes);DWORD error=GetLastError();size_t first,last;
    if(result>=sizeof(*info) && info->State==MEM_COMMIT && info->Protect==PAGE_READONLY && span(address,1,&first,&last)) {
        LONG state=read32(&watch.pages[first].state);
        if(state==ARMED || state==HANDLING || state==ARMING) {
            /* Do not extend logical RW into adjacent genuinely readonly
             * storage that Windows happened to coalesce with a watched page. */
            info->BaseAddress=watch.base+first*WATCH_PAGE;
            info->RegionSize=WATCH_PAGE;info->Protect=PAGE_READWRITE;
        }
    }
    pilot_write_watch_native_end(info,output);SetLastError(error);return result;
}
void pilot_write_watch_finish(void)
{
    if(!watch.pages)return;
    /* Shutdown may not discard protection ownership when restoration fails. */
    if(!pilot_write_watch_invalidate(watch.base,watch.bytes))return;
    RemoveVectoredExceptionHandler(watch.handler);
    free(watch.pages);memset(&watch,0,sizeof(watch));
}
#else
int pilot_write_watch_initialize(void *base,size_t bytes){(void)base;(void)bytes;return 0;}
void pilot_write_watch_finish(void){}
int pilot_write_watch_begin(const void *data,size_t bytes,PilotWriteSnapshot *snapshot)
{(void)data;(void)bytes;memset(snapshot,0,sizeof(*snapshot));return 0;}
void pilot_write_watch_end(PilotWriteSnapshot *snapshot){snapshot->valid=0;}
int pilot_write_watch_reusable(PilotWriteSnapshot *snapshot){(void)snapshot;return 0;}
int pilot_write_watch_invalidate(const void *data,size_t bytes){(void)data;(void)bytes;return 1;}
int pilot_write_watch_native_begin(const void *data,size_t bytes){(void)data;(void)bytes;return 1;}
void pilot_write_watch_native_end(const void *data,size_t bytes){(void)data;(void)bytes;}
uint64_t pilot_write_watch_faults(void){return 0;}
#if defined(_WIN32)
#include "memory_write_watch_win32.h"
BOOL WINAPI pilot_write_watch_virtual_protect(LPVOID address,SIZE_T bytes,DWORD protect,PDWORD previous)
{return VirtualProtect(address,bytes,protect,previous);}
LPVOID WINAPI pilot_write_watch_virtual_alloc(LPVOID address,SIZE_T bytes,DWORD type,DWORD protect)
{return VirtualAlloc(address,bytes,type,protect);}
BOOL WINAPI pilot_write_watch_virtual_free(LPVOID address,SIZE_T bytes,DWORD type)
{return VirtualFree(address,bytes,type);}
SIZE_T WINAPI pilot_write_watch_virtual_query(LPCVOID address,PMEMORY_BASIC_INFORMATION info,SIZE_T bytes)
{return VirtualQuery(address,info,bytes);}
#endif
#endif
