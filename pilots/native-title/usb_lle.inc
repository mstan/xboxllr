/* Guest DMA bytes remain owned by the native contiguous allocator. Retain an
 * allocation once hardware references it, until a real controller reset drops
 * its schedules. This bounded bridge does not alias arbitrary guest VM pages. */
#include "usb_lle.h"
#include "mmio_fault.h"
static INIT_ONCE usb_once=INIT_ONCE_STATIC_INIT;
static uint8_t usb_dma_leases[256];
static int usb_dma_transfer(uint32_t pa,void *buffer,size_t bytes,int write)
{
    if(!bytes || (uint64_t)pa+bytes>64u*1024*1024)return 0;
    AcquireSRWLockExclusive(&vm_lock);
    struct PhysicalAllocation *allocation=physical_find(pa|0x80000000u,(uint32_t)bytes);
    int result=0;
    if(allocation) {
        uint32_t first=pa/4096,last=(uint32_t)((pa+bytes+4095)/4096);
        for(uint32_t page=first;page<last;++page)
            if(write && (physical_protection[page]&6)!=PAGE_READWRITE)goto done;
        unsigned slot=(unsigned)(allocation-physical_allocations);
        if(!usb_dma_leases[slot]){usb_dma_leases[slot]=1;++allocation->borrowers;}
        if(write)memcpy(memory+0x80000000ull+pa,buffer,bytes);
        else memcpy(buffer,memory+0x80000000ull+pa,bytes);
        result=1;
    }
done:
    ReleaseSRWLockExclusive(&vm_lock);return result;
}
static int usb_dma_read(uint32_t pa,void *out,size_t size){return usb_dma_transfer(pa,out,size,0);}
static int usb_dma_write(uint32_t pa,const void *in,size_t size){return usb_dma_transfer(pa,(void*)in,size,1);}
static void usb_dma_release(void)
{
    AcquireSRWLockExclusive(&vm_lock);
    for(unsigned i=0;i<256;++i)if(usb_dma_leases[i]) {
        if(!physical_allocations[i].bytes || !physical_allocations[i].borrowers)
            stop("USB DMA ownership lost",i);
        --physical_allocations[i].borrowers;usb_dma_leases[i]=0;
    }
    ReleaseSRWLockExclusive(&vm_lock);
}
static BOOL CALLBACK usb_start(PINIT_ONCE once,void *parameter,void **context)
{
    (void)once;(void)parameter;(void)context;
    const char *error=pilot_usb_lle_bind(usb_dma_read,usb_dma_write,usb_dma_release);
    if(error)stop(error,0);
    fprintf(stderr,"[USB LLE] shared OHCI/XID model; owned DMA; IRQ delivery unqualified\n");
    return TRUE;
}
static const char *usb_access(uint32_t address,int write,uint32_t *value)
{
    if(!InitOnceExecuteOnce(&usb_once,usb_start,NULL,NULL))return "USB LLE initialization failed";
    return pilot_usb_lle_access(address,write,value);
}
