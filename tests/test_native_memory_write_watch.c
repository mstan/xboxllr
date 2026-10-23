#include "memory_write_watch.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include "memory_write_watch_win32.h"
#endif
#if defined(_WIN32) && !defined(PILOT_WRITE_WATCH_PORTABLE)
static void capture(unsigned char *data,size_t bytes,PilotWriteSnapshot *snapshot)
{
    assert(pilot_write_watch_begin(data,bytes,snapshot));
    volatile unsigned sum=0;for(size_t i=0;i<bytes;++i)sum+=data[i];(void)sum;
    pilot_write_watch_end(snapshot);assert(pilot_write_watch_reusable(snapshot));
}
static volatile LONG writing;
static DWORD WINAPI writer(void *data)
{
    while(InterlockedCompareExchange(&writing,0,0))InterlockedIncrement((volatile LONG*)data);
    return 0;
}
int main(void)
{
    unsigned char *memory=VirtualAlloc(NULL,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(memory);
    assert(pilot_write_watch_initialize(memory,65536));
    PilotWriteSnapshot a,b,overlap;
    capture(memory+17,8000,&a);capture(memory+8192,4096,&b);
    SetLastError(0x12345678u);memory[4096]=91;assert(GetLastError()==0x12345678u);
    assert(!pilot_write_watch_reusable(&a));assert(pilot_write_watch_reusable(&b));
    capture(memory+17,8000,&a);
    memset(memory+4080,42,64);assert(!pilot_write_watch_reusable(&a));assert(pilot_write_watch_reusable(&b));
    capture(memory+17,8000,&a);capture(memory+4000,5000,&overlap);
    memory[8192]=5;assert(!pilot_write_watch_reusable(&overlap));assert(pilot_write_watch_reusable(&a));
    assert(!pilot_write_watch_reusable(&b));
    /* A write during capture cannot bless a partially old snapshot. */
    assert(pilot_write_watch_begin(memory,4096,&a));memory[7]=9;pilot_write_watch_end(&a);
    assert(!pilot_write_watch_reusable(&a));
    capture(memory,4096,&a);
    /* An unpublished older capture must stay invalid after another capture
     * rearms overlapping storage following a write. */
    assert(pilot_write_watch_begin(memory,4096,&a));
    memory[11]=81;
    capture(memory,4096,&overlap);
    pilot_write_watch_end(&a);assert(!pilot_write_watch_reusable(&a));
    capture(memory,4096,&a);
    MEMORY_BASIC_INFORMATION info;assert(pilot_write_watch_virtual_query(memory,&info,sizeof(info)));
    assert(info.Protect==PAGE_READWRITE);
    DWORD previous;
    assert(pilot_write_watch_virtual_protect(memory,4096,PAGE_READONLY,&previous));assert(previous==PAGE_READWRITE);
    assert(!pilot_write_watch_reusable(&a));
    assert(pilot_write_watch_virtual_query(memory,&info,sizeof(info)) && info.Protect==PAGE_READONLY);
    assert(!pilot_write_watch_begin(memory,4096,&a));
    assert(pilot_write_watch_virtual_protect(memory,4096,PAGE_READWRITE,&previous));
    capture(memory,4096,&a);
    assert(pilot_write_watch_virtual_free(memory,4096,MEM_DECOMMIT));assert(!pilot_write_watch_reusable(&a));
    assert(pilot_write_watch_virtual_alloc(memory,4096,MEM_COMMIT,PAGE_READWRITE)==memory);
    memory[0]=27;capture(memory,4096,&a);
    /* Native OS reads need explicit writable leases; they do not execute
     * ordinary user-mode store instructions. Use our own executable, no temp. */
    WCHAR path[32768];assert(GetModuleFileNameW(NULL,path,32768));
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    assert(file!=INVALID_HANDLE_VALUE);
    capture(memory+8192,4096,&b);
    assert(pilot_write_watch_native_begin(memory+8192,4096));
    assert(pilot_write_watch_native_begin(memory+8192,4096));
    assert(!pilot_write_watch_reusable(&b));
    assert(!pilot_write_watch_begin(memory+8192,4096,&b));
    DWORD transferred=0;assert(ReadFile(file,memory+8192,4096,&transferred,NULL) && transferred==4096);
    assert(memory[8192]=='M' && memory[8193]=='Z');CloseHandle(file);
    pilot_write_watch_native_end(memory+8192,4096);
    assert(!pilot_write_watch_begin(memory+8192,4096,&b));
    pilot_write_watch_native_end(memory+8192,4096);capture(memory+8192,4096,&b);
    /* VM API outputs may themselves point into watched guest storage. */
    capture(memory+40960,4096,&b);
    MEMORY_BASIC_INFORMATION *guest_info=(MEMORY_BASIC_INFORMATION*)(memory+40960);
    assert(pilot_write_watch_virtual_query(memory,guest_info,sizeof(*guest_info)));
    assert(guest_info->State==MEM_COMMIT && !pilot_write_watch_reusable(&b));
    capture(memory+49152,4096,&b);
    DWORD *guest_previous=(DWORD*)(memory+49152);
    assert(pilot_write_watch_virtual_protect(memory+53248,4096,PAGE_READONLY,guest_previous));
    assert(*guest_previous==PAGE_READWRITE && !pilot_write_watch_reusable(&b));
    /* Repeated rearming and concurrent native stores exercise handler races.
     * Hot-page refusal is permitted and must fall back, never lose writes. */
    InterlockedExchange(&writing,1);
    HANDLE threads[2]={CreateThread(NULL,0,writer,memory+16384,0,NULL),CreateThread(NULL,0,writer,memory+16384,0,NULL)};
    assert(threads[0] && threads[1]);
    for(unsigned i=0;i<1000;++i) {
        if(pilot_write_watch_begin(memory+16384,4096,&b))pilot_write_watch_end(&b);
        (void)pilot_write_watch_reusable(&b);
        if(!(i%10))SwitchToThread();
    }
    InterlockedExchange(&writing,0);assert(WaitForMultipleObjects(2,threads,TRUE,5000)==WAIT_OBJECT_0);
    CloseHandle(threads[0]);CloseHandle(threads[1]);
    assert(pilot_write_watch_faults()>=4);
    pilot_write_watch_finish();memory[0]=39;
    assert(!pilot_write_watch_reusable(&a));
    assert(pilot_write_watch_initialize(memory,65536));assert(!pilot_write_watch_reusable(&a));
    pilot_write_watch_finish();assert(VirtualFree(memory,0,MEM_RELEASE));
    puts("write watch: direct stores, memcpy, overlaps, concurrent writes, policy, lifetime and reload passed");return 0;
}
#else
int main(void)
{
    unsigned char memory[64]={0};PilotWriteSnapshot snapshot={0};
    assert(!pilot_write_watch_initialize(memory,sizeof(memory)));
    assert(!pilot_write_watch_begin(memory,sizeof(memory),&snapshot));
    pilot_write_watch_end(&snapshot);assert(!pilot_write_watch_reusable(&snapshot));
    assert(pilot_write_watch_invalidate(memory,sizeof(memory)));pilot_write_watch_finish();
#if defined(_WIN32)
    /* A Windows build forced onto the portable backend must still link and
     * retain the same VM wrapper interface used by the engine. */
    void *region=pilot_write_watch_virtual_alloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(region);DWORD previous;
    assert(pilot_write_watch_virtual_protect(region,4096,PAGE_READONLY,&previous));
    MEMORY_BASIC_INFORMATION info;
    assert(pilot_write_watch_virtual_query(region,&info,sizeof(info)) && info.Protect==PAGE_READONLY);
    assert(pilot_write_watch_virtual_free(region,0,MEM_RELEASE));
#endif
    puts("portable write-watch fallback retains byte validation");return 0;
}
#endif
