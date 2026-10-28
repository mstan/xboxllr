#include "memory_commit_owner.h"
#if defined(_WIN32) && !defined(PILOT_COMMIT_OWNER_PORTABLE)
#include <windows.h>
#include "memory_write_watch_win32.h"
#include <stdlib.h>
#include <string.h>
enum {PAGE_BYTES=4096,COMMITTED=128,PERMISSIONS_KNOWN=64};
static struct {
    unsigned char *base,*access;
    size_t bytes,pages;
    uint64_t committed;
    int valid;
} owner;
static SRWLOCK owner_lock=SRWLOCK_INIT;
int pilot_commit_owner_initialize(void *base,size_t bytes)
{
    SYSTEM_INFO system;GetSystemInfo(&system);
    MEMORY_BASIC_INFORMATION info;
    if(!base || !bytes || bytes>UINT64_C(0x100000000) ||
       ((uintptr_t)base&(PAGE_BYTES-1)) || (bytes&(PAGE_BYTES-1)) ||
       system.dwPageSize!=PAGE_BYTES || !VirtualQuery(base,&info,sizeof(info)) ||
       info.AllocationBase!=base || info.BaseAddress!=base ||
       info.State!=MEM_RESERVE || info.RegionSize<bytes)return 0;
    unsigned char *access=calloc(bytes/PAGE_BYTES,1);
    if(!access)return 0;
    AcquireSRWLockExclusive(&owner_lock);
    if(owner.access){ReleaseSRWLockExclusive(&owner_lock);free(access);return 0;}
    owner.base=base;owner.access=access;owner.bytes=bytes;owner.pages=bytes/PAGE_BYTES;
    owner.committed=0;owner.valid=1;
    ReleaseSRWLockExclusive(&owner_lock);return 1;
}
void pilot_commit_owner_finish(void)
{
    AcquireSRWLockExclusive(&owner_lock);
    free(owner.access);memset(&owner,0,sizeof(owner));
    ReleaseSRWLockExclusive(&owner_lock);
}
void pilot_commit_owner_publish(const void *data,size_t bytes,int committed)
{
    /* Initialization/retirement own all workers; base never changes mid-run. */
    if(!owner.base || !bytes)return;
    uintptr_t address=(uintptr_t)data,base=(uintptr_t)owner.base;
    if(address<base || address-base>=owner.bytes)return;
    DWORD error=GetLastError();
    AcquireSRWLockExclusive(&owner_lock);
    size_t offset=(size_t)(address-base);
    if(bytes>owner.bytes-offset)owner.valid=0;
    else if(owner.valid) {
        size_t first=offset/PAGE_BYTES,last=(offset+bytes-1)/PAGE_BYTES;
        for(size_t page=first;page<=last;) {
            size_t end=last+1;unsigned char access=0;
            if(committed) {
                MEMORY_BASIC_INFORMATION info;uintptr_t at=base+page*PAGE_BYTES;
                /* Query only when a successful mapping/protection operation
                 * changes ownership. The write-watch view preserves logical
                 * RW on pages temporarily armed as physical readonly. */
                if(!pilot_write_watch_virtual_query((void*)at,&info,sizeof(info)) ||
                   info.State!=MEM_COMMIT || (uintptr_t)info.BaseAddress>at ||
                   info.RegionSize<=at-(uintptr_t)info.BaseAddress) {owner.valid=0;break;}
                uintptr_t region_end=(uintptr_t)info.BaseAddress+info.RegionSize;
                if(region_end<at || region_end>base+owner.bytes){owner.valid=0;break;}
                if((region_end-base)/PAGE_BYTES<end)end=(region_end-base)/PAGE_BYTES;
                if(end<=page){owner.valid=0;break;}
                access=COMMITTED;
                if(!(info.Protect&PAGE_GUARD)) {
                    access|=PERMISSIONS_KNOWN;
                    DWORD protection=info.Protect&255;
                    if(!(info.Protect&PAGE_NOACCESS) && protection!=PAGE_EXECUTE) {
                        if(protection&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|
                            PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))access|=PILOT_MEMORY_READ;
                        if(protection&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|
                            PAGE_EXECUTE_WRITECOPY))access|=PILOT_MEMORY_WRITE;
                        if(protection==PAGE_READWRITE || protection==PAGE_WRITECOPY)access|=PILOT_MEMORY_DATA_WRITE;
                    }
                }
            }
            for(;page<end;++page) {
                int before=(owner.access[page]&COMMITTED)!=0;
                if(before!=!!committed) {
                    if(committed)owner.committed+=PAGE_BYTES;else owner.committed-=PAGE_BYTES;
                }
                owner.access[page]=access;
            }
        }
    }
    ReleaseSRWLockExclusive(&owner_lock);SetLastError(error);
}
void pilot_commit_owner_release(const void *data,size_t bytes,int reservation)
{
    if(!owner.base)return;
    if(bytes && !reservation){pilot_commit_owner_publish(data,bytes,0);return;}
    uintptr_t address=(uintptr_t)data,base=(uintptr_t)owner.base;
    if(address<base || address-base>=owner.bytes)return;
    AcquireSRWLockExclusive(&owner_lock);
    if(!bytes && data==owner.base && !reservation) {
        memset(owner.access,0,owner.pages);owner.committed=0;
    } else owner.valid=0; /* reservation retired or extent not proved */
    ReleaseSRWLockExclusive(&owner_lock);
}
int pilot_commit_owner_query(uint32_t base,uint64_t bytes,uint64_t *committed)
{
    if(!owner.base || !committed)return 0;
    AcquireSRWLockShared(&owner_lock);
    int valid=owner.valid && base<=owner.bytes && bytes<=owner.bytes-base;
    uint64_t count=0;
    if(valid && !base && bytes==owner.bytes)count=owner.committed;
    else if(valid && bytes) {
        uint64_t end=(uint64_t)base+bytes;
        for(uint64_t page=base/PAGE_BYTES;page<=(end-1)/PAGE_BYTES;++page)
            if(owner.access[page]&COMMITTED) {
                uint64_t low=page*PAGE_BYTES,high=low+PAGE_BYTES;
                if(low<base)low=base;if(high>end)high=end;
                count+=high-low;
            }
    }
    if(valid)*committed=count;
    ReleaseSRWLockShared(&owner_lock);return valid;
}
int pilot_commit_owner_permissions(uint32_t base,uint64_t bytes,unsigned *permissions)
{
    if(!owner.base || !permissions || !bytes)return 0;
    AcquireSRWLockShared(&owner_lock);
    int valid=owner.valid && base<owner.bytes && bytes<=owner.bytes-base;
    unsigned common=PILOT_MEMORY_READ|PILOT_MEMORY_WRITE|PILOT_MEMORY_DATA_WRITE;
    if(valid)for(uint64_t page=base/PAGE_BYTES;page<=((uint64_t)base+bytes-1)/PAGE_BYTES;++page) {
        unsigned access=owner.access[page];
        if(!(access&COMMITTED)){common=0;break;}
        if(!(access&PERMISSIONS_KNOWN)){valid=0;break;}
        common&=access;
    }
    if(valid)*permissions=common;
    ReleaseSRWLockShared(&owner_lock);return valid;
}
#else
int pilot_commit_owner_initialize(void *base,size_t bytes){(void)base;(void)bytes;return 0;}
void pilot_commit_owner_finish(void){}
void pilot_commit_owner_publish(const void *data,size_t bytes,int committed){(void)data;(void)bytes;(void)committed;}
void pilot_commit_owner_release(const void *data,size_t bytes,int reservation){(void)data;(void)bytes;(void)reservation;}
int pilot_commit_owner_query(uint32_t base,uint64_t bytes,uint64_t *committed){(void)base;(void)bytes;(void)committed;return 0;}
int pilot_commit_owner_permissions(uint32_t base,uint64_t bytes,unsigned *permissions){(void)base;(void)bytes;(void)permissions;return 0;}
#endif
