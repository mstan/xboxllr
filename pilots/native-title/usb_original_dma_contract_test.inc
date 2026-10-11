/* Real original pages and OHCI HCCA work while an unrelated host thread owns
 * vm_lock. This proves selected transport/retention, not ISR/DPC scheduling. */
#ifdef PILOT_MEMORY_LLE
struct UsbOriginalVmLock {
    HANDLE ready,release,thread;
    volatile LONG held,expired;
};
static DWORD WINAPI usb_original_vm_lock_holder(void *opaque)
{
    struct UsbOriginalVmLock *owner=opaque;
    AcquireSRWLockExclusive(&vm_lock);
    InterlockedExchange(&owner->held,1);CHECK(SetEvent(owner->ready));
    DWORD wait=WaitForSingleObject(owner->release,5000);
    if(wait!=WAIT_OBJECT_0)InterlockedExchange(&owner->expired,1);
    InterlockedExchange(&owner->held,0);
    ReleaseSRWLockExclusive(&vm_lock);return 0;
}
static void usb_original_vm_lock_assert(struct UsbOriginalVmLock *owner)
{
    /* A bounded pre-fix run times out in the holder, then fails here. It must
     * not pass merely because the native-lock-first callback finally resumed. */
    CHECK(!InterlockedCompareExchange(&owner->expired,0,0));
    CHECK(InterlockedCompareExchange(&owner->held,0,0)==1);
    int acquired=TryAcquireSRWLockExclusive(&vm_lock);
    if(acquired)ReleaseSRWLockExclusive(&vm_lock);
    CHECK(!acquired);
}
static void usb_original_vm_lock_begin(struct UsbOriginalVmLock *owner)
{
    memset(owner,0,sizeof(*owner));
    owner->ready=CreateEventW(NULL,FALSE,FALSE,NULL);
    owner->release=CreateEventW(NULL,FALSE,FALSE,NULL);
    CHECK(owner->ready && owner->release);
    owner->thread=CreateThread(NULL,0,usb_original_vm_lock_holder,owner,0,NULL);CHECK(owner->thread);
    CHECK(WaitForSingleObject(owner->ready,2000)==WAIT_OBJECT_0);
    usb_original_vm_lock_assert(owner);
}
static void usb_original_vm_lock_end(struct UsbOriginalVmLock *owner)
{
    usb_original_vm_lock_assert(owner);CHECK(SetEvent(owner->release));
    CHECK(WaitForSingleObject(owner->thread,2000)==WAIT_OBJECT_0);
    CHECK(!InterlockedCompareExchange(&owner->expired,0,0));
    CHECK(!InterlockedCompareExchange(&owner->held,0,0));
    CHECK(CloseHandle(owner->thread) && CloseHandle(owner->ready) && CloseHandle(owner->release));
}
static uint32_t usb_original_dma_service(unsigned ordinal,const uint32_t *args)
{
    uint32_t result=0xdeadbeef;CHECK(!pilot_io_device_lle(ordinal,memory,args,&result));return result;
}
static void test_usb_original_dma_lock_lle(int borrowed_free)
{
    install_fault_handler();uint32_t revision=0;
    CHECK(!usb_access(0xfed00000u,0,&revision) && revision==0x10);
    uint32_t previous=0;unsigned reused=0;
    for(unsigned life=0;life<(borrowed_free?1u:3u);++life) {
        uint32_t bytes=8192,base=usb_original_dma_service(165,&bytes);CHECK(base && !(base&4095));
        uint32_t pa=usb_original_dma_service(173,&base);CHECK(pa==(base&0x03ffffffu));
        CHECK(usb_original_dma_service(180,&base)==bytes && !physical_find(base,1));
        if(base==previous)++reused;previous=base;
        uint8_t expected[32],observed[32],input[32];
        for(unsigned i=0;i<32;++i){expected[i]=(uint8_t)(17+i*7+life);input[i]=(uint8_t)(0xe3-i*3-life);}
        memcpy(memory+base+4080,expected,32);
        uint32_t image=0x15030,pin[]={image,12,0};
        memset(memory+image-1,0x65,14);memcpy(memory+image,input,12);
        (void)usb_original_dma_service(175,pin);
        uint32_t image_pa=usb_original_dma_service(173,&image);CHECK(image_pa);
        struct UsbOriginalVmLock holder;usb_original_vm_lock_begin(&holder);
        CHECK(usb_dma_read(pa+4080,observed,32) && !memcmp(observed,expected,32));
        usb_original_vm_lock_assert(&holder);
        CHECK(usb_dma_write(pa+4080,input,32) && !memcmp(memory+base+4080,input,32));
        uint32_t protect[]={base+4096,4096,PAGE_READONLY};
        (void)usb_original_dma_service(182,protect);
        CHECK(!usb_dma_write(pa+4080,expected,32) && !memcmp(memory+base+4080,input,32));
        memset(observed,0xa7,32);memset(expected,0xa7,32);
        CHECK(!usb_dma_read(pa+bytes-16,observed,32) && !memcmp(observed,expected,32));
        protect[2]=PAGE_READWRITE;(void)usb_original_dma_service(182,protect);
        CHECK(!usb_dma_read(64u*1024*1024-16,observed,32) && !memcmp(observed,expected,32));
        CHECK(usb_dma_read(image_pa,observed,12) && !memcmp(observed,input,12));
        memset(observed,0xa7,32);
        CHECK(!usb_dma_read(image_pa-1,observed,4) && !memcmp(observed,expected,32));
        CHECK(!usb_dma_read(image_pa+9,observed,4) && !memcmp(observed,expected,32));
        const uint8_t replacement[]={0x3e,0x20,0x61,0x8c,0x5b,0xa2,0x09,0xf4,0x18,0x92,0xde,0x47};
        CHECK(usb_dma_write(image_pa,replacement,sizeof(replacement)));
        CHECK(!memcmp(memory+image,replacement,sizeof(replacement)) && MEM8(image-1)==0x65 && MEM8(image+12)==0x65);
        pin[2]=1;(void)usb_original_dma_service(175,pin);
        CHECK(!usb_dma_read(image_pa,observed,4) && !memcmp(observed,expected,32));
        usb_original_vm_lock_assert(&holder);

        /* The maintained consumer, rather than a caller's successful DMA flag,
         * writes the actual owned HCCA through these same selected callbacks. */
        memset(memory+base,0x5a,256);MEM16(base+0x82)=0;
        uint32_t value=pa;CHECK(!usb_access(0xfed00018u,1,&value));
        value=0x80;CHECK(!usb_access(0xfed00004u,1,&value));
        uint32_t initial_frame=0;CHECK(!usb_access(0xfed0003cu,0,&initial_frame));
        uint64_t deadline=GetTickCount64()+1000;uint32_t frame=initial_frame;
        while(frame==initial_frame && GetTickCount64()<deadline) {
            Sleep(1);CHECK(!usb_access(0xfed0003cu,0,&frame));
        }
        CHECK(frame!=initial_frame && MEM16(base+0x80)==(uint16_t)frame && !MEM16(base+0x82));
        for(unsigned i=0;i<256;++i)if(i<0x80 || i>=0x84)CHECK(MEM8(base+i)==0x5a);
        usb_original_vm_lock_assert(&holder);
        if(borrowed_free) {
            uint8_t retained[32];memcpy(retained,memory+base+4080,32);
            uint32_t result=0xdeadbeef;const char *error=pilot_io_device_lle(171,memory,&base,&result);
            CHECK(error && !strcmp(error,"device LLE contiguous release while DMA borrowed"));
            MEMORY_BASIC_INFORMATION host;CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_COMMIT);
            CHECK(!memcmp(memory+base+4080,retained,32));
            usb_original_vm_lock_end(&holder);
            fprintf(stderr,"PASS: selected original USB lease blocks actual free before mutation while another thread owns vm_lock; retained bytes/pages\n");return;
        }
        value=1;CHECK(!usb_access(0xfed00008u,1,&value)); // Actual reset releases the original lease.
        usb_original_vm_lock_assert(&holder);
        (void)usb_original_dma_service(171,&base);
        memset(observed,0xa7,32);CHECK(!usb_dma_read(pa,observed,4) && !memcmp(observed,expected,32));
        usb_original_vm_lock_end(&holder);
        MEMORY_BASIC_INFORMATION host;CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_RESERVE);
    }
    CHECK(reused);
    fprintf(stderr,"PASS: three selected original USB DMA lifetimes under an independently held vm_lock, exact cross-page/pinned bytes, atomic protection/range guards, real HCCA/reset, stale rejection and owned-page reuse\n");
}
#endif
