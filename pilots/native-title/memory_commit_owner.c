#include "memory_commit_owner.h"
#if defined(_WIN32) && !defined(PILOT_COMMIT_OWNER_PORTABLE)
#include <windows.h>
#include <stdlib.h>
#include <string.h>
enum {PAGE_BYTES=4096};
static struct {
    unsigned char *base,*bits;
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
    unsigned char *bits=calloc((bytes/PAGE_BYTES+7)/8,1);
    if(!bits)return 0;
    AcquireSRWLockExclusive(&owner_lock);
    if(owner.bits){ReleaseSRWLockExclusive(&owner_lock);free(bits);return 0;}
    owner.base=base;owner.bits=bits;owner.bytes=bytes;owner.pages=bytes/PAGE_BYTES;
    owner.committed=0;owner.valid=1;
    ReleaseSRWLockExclusive(&owner_lock);return 1;
}
void pilot_commit_owner_finish(void)
{
    AcquireSRWLockExclusive(&owner_lock);
    free(owner.bits);memset(&owner,0,sizeof(owner));
    ReleaseSRWLockExclusive(&owner_lock);
}
void pilot_commit_owner_publish(const void *data,size_t bytes,int committed)
{
    /* Initialization/retirement own all workers; base never changes mid-run. */
    if(!owner.base || !bytes)return;
    uintptr_t address=(uintptr_t)data,base=(uintptr_t)owner.base;
    if(address<base || address-base>=owner.bytes)return;
    AcquireSRWLockExclusive(&owner_lock);
    size_t offset=(size_t)(address-base);
    if(bytes>owner.bytes-offset)owner.valid=0;
    else if(owner.valid) {
        size_t first=offset/PAGE_BYTES,last=(offset+bytes-1)/PAGE_BYTES;
        for(size_t page=first;page<=last;++page) {
            unsigned char mask=(unsigned char)(1u<<(page&7)),*entry=owner.bits+page/8;
            if(((*entry&mask)!=0)==!!committed)continue;
            if(committed){*entry|=mask;owner.committed+=PAGE_BYTES;}
            else {*entry&=(unsigned char)~mask;owner.committed-=PAGE_BYTES;}
        }
    }
    ReleaseSRWLockExclusive(&owner_lock);
}
void pilot_commit_owner_release(const void *data,size_t bytes,int reservation)
{
    if(!owner.base)return;
    if(bytes && !reservation){pilot_commit_owner_publish(data,bytes,0);return;}
    uintptr_t address=(uintptr_t)data,base=(uintptr_t)owner.base;
    if(address<base || address-base>=owner.bytes)return;
    AcquireSRWLockExclusive(&owner_lock);
    if(!bytes && data==owner.base && !reservation) {
        memset(owner.bits,0,(owner.pages+7)/8);owner.committed=0;
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
            if(owner.bits[page/8]&(1u<<(page&7))) {
                uint64_t low=page*PAGE_BYTES,high=low+PAGE_BYTES;
                if(low<base)low=base;if(high>end)high=end;
                count+=high-low;
            }
    }
    if(valid)*committed=count;
    ReleaseSRWLockShared(&owner_lock);return valid;
}
#else
int pilot_commit_owner_initialize(void *base,size_t bytes){(void)base;(void)bytes;return 0;}
void pilot_commit_owner_finish(void){}
void pilot_commit_owner_publish(const void *data,size_t bytes,int committed){(void)data;(void)bytes;(void)committed;}
void pilot_commit_owner_release(const void *data,size_t bytes,int reservation){(void)data;(void)bytes;(void)reservation;}
int pilot_commit_owner_query(uint32_t base,uint64_t bytes,uint64_t *committed){(void)base;(void)bytes;(void)committed;return 0;}
#endif
