/* Actual compiled MMIO accesses plus shared OHCI DMA into an original-kernel
 * allocation. This qualifies transport/ownership, not CPU interrupt delivery. */
__declspec(noinline) static uint32_t usb_test_read(unsigned offset)
{return *(volatile uint32_t*)(memory+0xfed00000ull+offset);}
__declspec(noinline) static void usb_test_write(unsigned offset,uint32_t value)
{*(volatile uint32_t*)(memory+0xfed00000ull+offset)=value;}
static volatile LONG usb_page_writer_running,usb_page_writer_started;
static uint32_t usb_page_writer_count;
static DWORD WINAPI usb_page_writer(void *unused)
{
    (void)unused;uint32_t count=0;
    while(InterlockedCompareExchange(&usb_page_writer_running,0,0)) {
        InterlockedIncrement((volatile LONG*)(memory+0x15040));++count;
        InterlockedExchange(&usb_page_writer_started,1);
    }
    usb_page_writer_count=count;return 0;
}
static void usb_test_async_page(void)
{
    /* Model an asynchronous completion touching caller storage while an
     * unrelated original kernel service runs. Whole-page copy-in/copy-out
     * can lose these writes even with a single guest CPU. */
    MEM32(0x15040)=0;usb_page_writer_running=1;usb_page_writer_started=0;
    HANDLE writer=CreateThread(NULL,0,usb_page_writer,NULL,0,NULL);CHECK(writer);
    while(!InterlockedCompareExchange(&usb_page_writer_started,0,0))Sleep(0);
    for(unsigned i=0;i<128;++i) {
        uint32_t unused=0,result=0;
        CHECK(!pilot_io_device_lle(103,memory,&unused,&result));
        CHECK((result&255)==0);
    }
    InterlockedExchange(&usb_page_writer_running,0);
    CHECK(WaitForSingleObject(writer,5000)==WAIT_OBJECT_0 && CloseHandle(writer));
    CHECK(usb_page_writer_count && MEM32(0x15040)==usb_page_writer_count);
}
static void test_usb_lle(void)
{
    install_fault_handler();
    CHECK(!pilot_io_device_image(memory,0x10000,0x10000));
    MEM32(0x15000)=0x78563412;MEM32(0x16000)=0xabcdef01;
    uint32_t vector_args[]={1,0x15021};MEM32(0x15020)=0xcdcdcdcd;
    call_kernel(44,vector_args,2);CHECK(g_eax==0x31 && MEM32(0x15020)==0xcdcd19cd);
    uint32_t image_pin[]={0x15000,8192,0};
    call_kernel(175,image_pin,3);call_kernel(175,image_pin,3);
    CHECK(MEM32(0x15000)==0x78563412 && MEM32(0x16000)==0xabcdef01);
    MEM32(0x15020)=0xcdcdcdcd;
    call_kernel(44,vector_args,2);
    CHECK(g_eax==0x31 && MEM32(0x15020)==0xcdcd19cd);
    uint32_t image_address=0x15000;call_kernel(173,&image_address,1);
    uint32_t image_pa=g_eax,observed=0;
    CHECK(image_pa && !(image_pa&4095));
    usb_test_async_page();
    CHECK(usb_dma_read(image_pa,&observed,4) && observed==0x78563412);
    observed=0x11223344;CHECK(usb_dma_write(image_pa,&observed,4));
    CHECK(MEM32(0x15000)==observed);
    image_pin[2]=1;call_kernel(175,image_pin,3);
    /* A report pins its own allocation, not every pool block sharing the PFN. */
    uint32_t a=pool_allocate(20,0),b=pool_allocate(20,0);
    CHECK(a && b && (a>>12)==(b>>12));
    uint32_t pool_pin[]={a,20,0};call_kernel(175,pool_pin,3);
    call_kernel(173,&a,1);uint32_t pool_pa=g_eax;
    MEM32(a)=0x10203040;CHECK(usb_dma_read(pool_pa,&observed,4) && observed==0x10203040);
    call_kernel(173,&b,1);CHECK(!usb_dma_read(g_eax,&observed,4));
    pool_free(b);CHECK(MEM32(a)==0x10203040);
    pool_pin[2]=1;call_kernel(175,pool_pin,3);CHECK(!usb_dma_read(pool_pa,&observed,4));
    pool_free(a);
    CHECK(usb_dma_read(image_pa,&observed,4) && observed==0x11223344);
    call_kernel(175,image_pin,3);CHECK(!usb_dma_read(image_pa,&observed,4));
    CHECK(!pilot_io_device_image_release(0x15000,8192));
    CHECK(test_write_faults(0x15000) && test_write_faults(0x16000));
    commit(0x15000,8192);MEM32(0x15000)=0x55667788;
    image_pin[2]=0;call_kernel(175,image_pin,3);
    call_kernel(173,&image_address,1);image_pa=g_eax;
    CHECK(usb_dma_read(image_pa,&observed,4) && observed==0x55667788);
    image_pin[2]=1;call_kernel(175,image_pin,3);
    CHECK(usb_test_read(0)==0x10);
    uint32_t bytes=4096;call_kernel(165,&bytes,1);uint32_t buffer=g_eax;CHECK(buffer);
    call_kernel(173,&buffer,1);uint32_t pa=g_eax;CHECK(pa==(buffer&0x03ffffffu));
    memset(memory+buffer,0,bytes);
    uint32_t lock[]={buffer,bytes,0};call_kernel(175,lock,3);
    lock[2]=1;call_kernel(175,lock,3);
    /* GET_DESCRIPTOR: setup -> IN data -> OUT status -> tail. Reuse the
     * maintained OHCI fixture's packet layout, through actual native MMIO. */
    uint32_t ed=buffer+0x100,setup=buffer+0x110,data=buffer+0x120;
    uint32_t status=buffer+0x130,tail=buffer+0x140,request=buffer+0x180,response=buffer+0x200;
    const uint8_t packet[]={0x80,6,0,1,0,0,18,0};memcpy(memory+request,packet,8);
    MEM32(ed)=64u<<16;MEM32(ed+4)=pa+0x140;MEM32(ed+8)=pa+0x110;
    MEM32(setup)=0xf0000000u;MEM32(setup+4)=pa+0x180;
    MEM32(setup+8)=pa+0x120;MEM32(setup+12)=pa+0x187;
    MEM32(data)=0xf0140000u;MEM32(data+4)=pa+0x200;
    MEM32(data+8)=pa+0x130;MEM32(data+12)=pa+0x211;
    MEM32(status)=0xf0080000u;MEM32(status+8)=pa+0x140;
    usb_test_write(0x54,1u<<4); // real port reset/enable
    usb_test_write(0x18,pa);usb_test_write(0x20,pa+0x100);
    usb_test_write(8,2);usb_test_write(4,0x90);
    uint64_t deadline=GetTickCount64()+1000;
    while((MEM32(status)>>28)!=0 && GetTickCount64()<deadline) {
        Sleep(1);(void)usb_test_read(0x0c);
    }
    CHECK((MEM32(setup)>>28)==0 && (MEM32(data)>>28)==0 && (MEM32(status)>>28)==0);
    CHECK((MEM32(ed+8)&~15u)==pa+0x140);
    CHECK((MEM32(buffer+0x84)&~15u)==pa+0x130);
    CHECK(MEM8(response)==18 && MEM8(response+1)==1);
    CHECK(MEM16(response+8)==0x045e && MEM16(response+10)==0x0289);
    CHECK(physical_find(buffer,bytes)->borrowers==1);
    /* Original service calls must see/preserve the DMA-updated caller bytes. */
    call_kernel(173,&buffer,1);CHECK(g_eax==pa && MEM8(response)==18);
    usb_test_write(8,1); // hardware reset releases retained schedules/ownership
    CHECK(physical_find(buffer,bytes)->borrowers==0);
    call_kernel(171,&buffer,1);CHECK(test_write_faults(buffer));
    fprintf(stderr,"PASS: compiled OHCI MMIO, original physical ownership, real descriptor DMA/completion, reset and release\n");
}
static void test_original_physical_memory(void)
{
    install_fault_handler();
    uint32_t args[]={8192,0x3000000,0x3ffffff,0x10000,4};
    call_kernel(166,args,5);uint32_t base=g_eax;CHECK(base && !(base&0xffff));
    CHECK(base>=0x83000000u && base<=0x83ffe000u);
    call_kernel(173,&base,1);CHECK(g_eax==(base&0x03ffffffu));
    call_kernel(180,&base,1);CHECK(g_eax==8192);
    MEM32(base)=0x12345678;MEM32(base+4096)=0xabcdef01;
    uint32_t pin[]={base+4095,2,0};call_kernel(175,pin,3);
    pin[2]=1;call_kernel(175,pin,3);
    uint32_t protect[]={base,8192,2};call_kernel(182,protect,3);
    CHECK(test_write_faults(base) && test_write_faults(base+4096));
    uint32_t persist[]={base,8192,1};call_kernel(178,persist,3);
    CHECK(MEM32(base)==0x12345678 && MEM32(base+4096)==0xabcdef01);
    persist[2]=0;call_kernel(178,persist,3);
    protect[2]=4;call_kernel(182,protect,3);
    CHECK(!test_write_faults(base));
    call_kernel(166,args,5);uint32_t second=g_eax;
    CHECK(second && second!=base && !(second&0xffff));
    call_kernel(171,&base,1);CHECK(test_write_faults(base));
    call_kernel(171,&second,1);CHECK(test_write_faults(second));
    fprintf(stderr,"PASS: original contiguous allocation/translation, pinning, RO/RW protection, persistence and release\n");
}
