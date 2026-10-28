#include "memory_commit_owner.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#if defined(_WIN32) && !defined(PILOT_COMMIT_OWNER_PORTABLE)
#include <windows.h>
#include "memory_write_watch_win32.h"
typedef LONG (NTAPI *Allocate)(HANDLE,PVOID*,ULONG_PTR,PSIZE_T,ULONG,ULONG);
typedef LONG (NTAPI *Decommit)(HANDLE,PVOID*,PSIZE_T,ULONG);
static uint64_t host_count(unsigned char *memory,uint32_t low,uint64_t bytes)
{
    uint64_t end=(uint64_t)low+bytes,count=0;
    for(uint64_t at=low;at<end;) {
        MEMORY_BASIC_INFORMATION info;
        assert(VirtualQuery(memory+at,&info,sizeof(info)));
        uint64_t next=(unsigned char*)info.BaseAddress-memory+info.RegionSize;
        if(next>end)next=end;
        assert(next>at);
        if(info.State==MEM_COMMIT)count+=next-at;
        at=next;
    }
    return count;
}
static void check(unsigned char *memory,uint32_t low,uint64_t bytes)
{
    uint64_t owned=UINT64_MAX;
    assert(pilot_commit_owner_query(low,bytes,&owned));
    assert(owned==host_count(memory,low,bytes));
}
int main(void)
{
    const SIZE_T bytes=(SIZE_T)UINT64_C(0x100000000);
    unsigned char *memory=VirtualAlloc(NULL,bytes,MEM_RESERVE,PAGE_NOACCESS);
    assert(memory);
    assert(!pilot_commit_owner_initialize(memory,65535));
    assert(pilot_commit_owner_initialize(memory,bytes));
    assert(!pilot_commit_owner_initialize(memory,bytes));
    check(memory,0,bytes);
    /* Unaligned requests cross a page: the returned address alone is not
     * enough to reconstruct the affected byte interval. OS queries are the
     * independent oracle for both full and byte-granular range accounting. */
    assert(pilot_write_watch_virtual_alloc(memory+0x10001,4096,MEM_COMMIT,PAGE_READWRITE));
    check(memory,0,bytes);check(memory,0x10001,4096);check(memory,0x10001,3);
    assert(pilot_write_watch_virtual_alloc(memory+0x10000,8192,MEM_COMMIT,PAGE_READWRITE));
    check(memory,0,bytes); /* idempotent recommit */
    assert(pilot_write_watch_virtual_alloc(memory+0x80000000u,8192,MEM_COMMIT,PAGE_READWRITE));
    check(memory,0,bytes);check(memory,0x7ffffff0,8224);
    DWORD previous;
    assert(pilot_write_watch_virtual_protect(memory+0x10000,4096,PAGE_READONLY,&previous));
    check(memory,0,bytes); /* protection is not a commitment mutation */
    assert(pilot_write_watch_virtual_free(memory+0x11000,4096,MEM_DECOMMIT));
    check(memory,0,bytes);check(memory,0x10fff,2);
    assert(!pilot_write_watch_virtual_alloc(memory+0xfffff000u,8192,MEM_COMMIT,PAGE_READWRITE));
    check(memory,0,bytes); /* failed operation publishes nothing */
    uint64_t sentinel=123;
    assert(!pilot_commit_owner_query(0xfffffff0u,17,&sentinel) && sentinel==123);
    Allocate allocate=(Allocate)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtAllocateVirtualMemory");
    Decommit decommit=(Decommit)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtFreeVirtualMemory");
    assert(allocate && decommit);
    PVOID address=memory+0x40001;SIZE_T size=4096;
    assert(allocate(GetCurrentProcess(),&address,0,&size,MEM_COMMIT,PAGE_READWRITE)>=0);
    pilot_commit_owner_publish(address,size,1);check(memory,0,bytes);
    assert(decommit(GetCurrentProcess(),&address,&size,MEM_DECOMMIT)>=0);
    pilot_commit_owner_release(address,size,0);check(memory,0,bytes);
    assert(pilot_write_watch_virtual_free(memory,0,MEM_DECOMMIT));
    check(memory,0,bytes);
    assert(pilot_write_watch_virtual_free(memory,0,MEM_RELEASE));
    assert(!pilot_commit_owner_query(0,bytes,&sentinel));
    pilot_commit_owner_finish();
    memory=VirtualAlloc(NULL,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(memory);
    assert(!pilot_commit_owner_initialize(memory,65536)); /* no false initial knowledge */
    assert(VirtualFree(memory,0,MEM_RELEASE));
    puts("PASS: native commitment agrees with actual OS mappings, NT/Win32 writes, failed/unaligned/repeated commits, partial decommit, protection and retirement");
    return 0;
}
#else
int main(void)
{
    uint64_t sentinel=123;
    assert(!pilot_commit_owner_initialize((void*)65536,65536));
    pilot_commit_owner_publish((void*)65536,4096,1);
    pilot_commit_owner_release((void*)65536,4096,0);
    assert(!pilot_commit_owner_query(0,65536,&sentinel) && sentinel==123);
    pilot_commit_owner_finish();
    puts("PASS: unqualified portable owner requires the authoritative caller fallback");
    return 0;
}
#endif
