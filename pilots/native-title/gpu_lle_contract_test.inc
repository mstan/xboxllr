/* Transport qualification with real PFIFO semaphore DMA into original-kernel
 * pages. Original title driver/callback behavior is a separate integration gate. */
__declspec(noinline) static uint32_t gpu_test_read(uint32_t offset)
{return *(volatile uint32_t*)(memory+0xfd000000ull+offset);}
__declspec(noinline) static void gpu_test_write(uint32_t offset,uint32_t value)
{*(volatile uint32_t*)(memory+0xfd000000ull+offset)=value;}
static void test_gpu_lle_transport(void)
{
    install_fault_handler();
    uint32_t query[]={UINT32_MAX,0x15000};MEM32(0x15000)=0xcdcdcdcd;
    call_kernel(168,query,2);uint32_t top=g_eax;
    CHECK(top==0x83ff0000 && MEM32(0x15000)==0x10000);
    uint32_t first=top-0x10000,keep=top-0x5000,protect=0;
    CHECK(!pilot_io_device_lle(179,memory,&first,&protect) && protect==0x204);
    query[0]=0x5000;call_kernel(168,query,2);CHECK(g_eax==top);
    CHECK(!pilot_io_device_lle(179,memory,&first,&protect) && protect==0);
    CHECK(!pilot_io_device_lle(179,memory,&keep,&protect) && protect==0x204);
    call_kernel(168,query,2);CHECK(g_eax==top && MEM32(0x15000)==0x10000);
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        gpu_lle_open();
        uint8_t mask=pilot_cpu_in8(0xa1);
        pilot_cpu_out8(0xa1,(uint8_t)(mask^0x80));
        CHECK(pilot_cpu_in8(0xa1)==(uint8_t)(mask^0x80));
        pilot_cpu_out8(0xa1,mask);CHECK(pilot_cpu_in8(0xa1)==mask);
        pilot_cpu_out8(0x80c0,1);
        CHECK((pilot_cpu_in8(0x80c0)&0x1f)==1);
        /* Actual original HalReadWritePCISpace, including its CF8/CFC IN/OUT
         * and partial-buffer handling. Both config routes own the same state. */
        uint32_t config[]={1,0,0,0x15010,4,0};
        MEM32(0x15010)=0;MEM32(0x15014)=0xaabbccdd;
        call_kernel(46,config,6);
        CHECK(MEM32(0x15010)==0x02a010de && MEM32(0x15014)==0xaabbccdd);
        config[2]=0x4c;MEM32(0x15010)=0x1f000000;config[5]=1;
        call_kernel(46,config,6);CHECK(gpu_test_read(0x184c)==0x1f000000);
        gpu_test_write(0x184c,0x07000000);
        config[2]=0x4f;config[4]=1;config[5]=0;MEM32(0x15010)=0xabcdef00;
        call_kernel(46,config,6);CHECK(MEM32(0x15010)==0xabcdef07);
        config[2]=0x0d;config[5]=1;MEM8(0x15010)=0xf8;
        call_kernel(46,config,6);CHECK(gpu_test_read(0x180c)==0xf800);
        config[0]=2;config[2]=0;config[4]=4;config[5]=0;
        call_kernel(46,config,6);CHECK(MEM32(0x15010)==0xffffffff);
        gpu_test_write(0x1804,4);CHECK(gpu_test_read(0x1804)==4);
        uint32_t base=physical_allocate(8192,0,0x03ffffff,4096,PAGE_READWRITE);
        CHECK(base);uint32_t pa=base&0x03ffffff;
        const uint32_t packet[]={0x40000,1,0x401a4,2,0x81d6c,4096,0x12345678};
        memcpy(memory+base,packet,sizeof(packet));MEM32(base+4096)=0xabcdef01;
        const uint32_t setup[][2]={
            {0x700008,1},{0x70000c,0x80010020},{0x700200,0x97},
            {0x700010,2},{0x700014,0x80010030},
            {0x700300,0x3d},{0x700304,8191},{0x700308,pa},
            {0x700180,0x3d},{0x700184,8191},{0x700188,pa},
            {0x002504,1},{0x003204,0x100},{0x00322c,0x18},{0x003244,0},
            {0x003220,1},{0x003250,1},{0x003200,1},{0x400720,0}};
        for(unsigned i=0;i<sizeof(setup)/sizeof(setup[0]);++i)gpu_test_write(setup[i][0],setup[i][1]);
        gpu_test_write(0x800040,sizeof(packet));
        CHECK(MEM32(base+4096)==0xabcdef01); // held PGRAPH cannot complete DMA
        gpu_test_write(0x400720,1);
        CHECK(MEM32(base+4096)==0x12345678);
        CHECK(gpu_test_read(0x800044)==sizeof(packet));
        uint32_t observed=0;
        CHECK(gpu_lle_dma_read(pa+4096,&observed,4) && observed==0x12345678);
        CHECK(!gpu_lle_dma_write(0x04000000,&observed,4));
        physical_protect(base+4096,4096,PAGE_READONLY);
        CHECK(!gpu_lle_dma_write(pa+4096,&observed,4));
        physical_protect(base+4096,4096,PAGE_READWRITE);
        physical_free(base);CHECK(!gpu_lle_dma_read(pa+4096,&observed,4));
        CHECK(!gpu_lle.error(gpu_lle.device));
        /* This fixture has consumed its entire FIFO, no connected ISR and no
         * enabled interrupt. It does not claim general title teardown proof. */
        gpu_lle.destroy(gpu_lle.device);gpu_lle.device=NULL;
        pilot_io_device_port_bind(0,0,NULL);
        CHECK(FreeLibrary(gpu_lle.library));gpu_lle.library=NULL;
    }
    fprintf(stderr,"PASS: original GPU reservation/query/repeat/page release; compiled MMIO, owned DMA, held/released semaphore, protection/release rejection, three transport lifetimes\n");
}
