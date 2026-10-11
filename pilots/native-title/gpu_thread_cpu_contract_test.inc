/* Authored original CPU instructions drive the same NV2A instance and actual
 * original contiguous PFNs. Device callbacks borrow only validated RAM; they
 * cannot execute a second original CPU or bypass wholetransaction checks. */
#ifdef PILOT_GPU_FILES_WORKER
#define GPU_THREAD_CPU 0x13c00u
static uint32_t gpu_cpu_pa;
static unsigned gpu_cpu_fault_checks,gpu_cpu_mmio_calls;
static const char *gpu_cpu_checked_mmio(uint32_t address,unsigned size,int write,uint32_t *value)
{
    ++gpu_cpu_mmio_calls;
    if(gpu_cpu_fault_checks) {
        uint32_t result=0xaabbccdd,args[]={1},regs[8]={0},saved[8]={0},outputs[7],status=0x13572468;
        memset(outputs,0xa5,sizeof(outputs));
        CHECK(pilot_io_device_lle(1,memory,args,&result) && result==0xaabbccdd);
        CHECK(pilot_io_device_irq(memory,(int*)&result) && result==0xaabbccdd);
        CHECK(pilot_io_device_thread_execute(GPU_THREAD_CPU,regs) && !memcmp(regs,saved,sizeof(regs)));
        uint32_t input[]={0,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,0};
        CHECK(pilot_io_device_vm(184,memory,input,outputs,&status) && status==0x13572468);
        for(unsigned i=0;i<7;++i)CHECK(outputs[i]==0xa5a5a5a5);
        uint64_t payload=0x123456789abcdef0ull;
        CHECK(!pilot_io_device_contiguous_dma(gpu_cpu_pa+4094,&payload,8,1,PILOT_IO_DMA_SYNC));
        CHECK(!pilot_io_device_contiguous_dma(gpu_cpu_pa,&payload,8,0,PILOT_IO_DMA_USB));
        CHECK(pilot_io_device_work_bind(NULL));
        CHECK(pilot_io_device_mmio_bind(0,0,NULL));
    }
    return gpu_lle_kernel_mmio(address,size,write,value);
}
static void gpu_thread_cpu_code(void)
{
    struct ThreadChildCode code={0},*c=&code;
    const uint8_t prologue[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x8b,0x77,4,0x8b,0x5f,8};child_emit(c,prologue,sizeof(prologue));unsigned loop=c->size;
    const uint8_t read[]={0x8b,0x56,4,0x83,0x3e,0};child_emit(c,read,sizeof(read));
    unsigned writing=child_branch(c,1);
    const uint8_t load[]={0x8b,0x02,0x89,0x46,12};child_emit(c,load,sizeof(load));unsigned next=child_branch(c,0);
    child_patch(c,writing);const uint8_t store[]={0x8b,0x46,8,0x89,0x02};child_emit(c,store,sizeof(store));child_patch(c,next);
    const uint8_t advance[]={0x83,0xc6,16,0x4b,0x0f,0x85};child_emit(c,advance,sizeof(advance));
    child_word(c,(uint32_t)(loop-(c->size+4)));
    const uint8_t finish[]={0xc7,0x47,12,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    child_emit(c,finish,sizeof(finish));CHECK(c->size<0x100);memcpy(memory+GPU_THREAD_CPU,c->bytes,c->size);
}
static void gpu_thread_cpu_ops(const uint32_t (*ops)[4],unsigned count,unsigned lifetime)
{
    uint32_t context=0x1b000,table=context+0x100;memset(memory+context,0,96);
    CHECK(count<=32);memcpy(memory+table,ops,count*16);
    MEM32(context+4)=table;MEM32(context+8)=count;MEM32(context+64)=GPU_THREAD_CPU;
    thread_cpu_create_join(GPU_THREAD_CPU,context,lifetime&1);CHECK(MEM32(context+12)==1);
}
static void test_gpu_thread_cpu_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    gpu_thread_cpu_code();uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned lifetime=0;lifetime<3;++lifetime) {
        gpu_lle_open();CHECK(!pilot_io_device_mmio_bind(0,0,NULL));
        CHECK(!pilot_io_device_mmio_bind(0xfd000000,0x1000000,gpu_cpu_checked_mmio));
        CHECK(pilot_io_device_work_bind(gpu_lle_sample)); /* duplicate does not replace owner */
        if(!lifetime)CHECK(!pilot_io_device_clock_start());
        uint32_t bytes=8192,base=0;CHECK(!pilot_io_device_lle(165,memory,&bytes,&base));
        CHECK(base && !physical_find(base,1));gpu_cpu_pa=base&0x03ffffff;
        const uint32_t packet[]={0x40000,1,0x401a4,2,0x81d6c,4096,0x12345678};
        memset(memory+base,0x5a,8192);memcpy(memory+base,packet,sizeof(packet));MEM32(base+4096)=0xabcdef01;
        uint32_t setup[][4]={
            {1,0x80000000,gpu_cpu_pa|1,0},{1,0xfd001804,4,0},{0,0xfd001804,0,0},{0,0xfd001800,0,0},
            {1,0xfd700008,1,0},{1,0xfd70000c,0x80010020,0},{1,0xfd700200,0x97,0},
            {1,0xfd700010,2,0},{1,0xfd700014,0x80010030,0},
            {1,0xfd700300,0x3d,0},{1,0xfd700304,8191,0},{1,0xfd700308,gpu_cpu_pa,0},
            {1,0xfd700180,0x3d,0},{1,0xfd700184,0x03ffffff,0},{1,0xfd700188,0,0},
            {1,0xfd002504,1,0},{1,0xfd003204,0x100,0},{1,0xfd00322c,0x18,0},{1,0xfd003244,0,0},
            {1,0xfd003220,1,0},{1,0xfd003250,1,0},{1,0xfd003200,1,0},{1,0xfd400720,0,0},
            {1,0xfd800040,gpu_cpu_pa+sizeof(packet),0}};
        gpu_thread_cpu_ops(setup,sizeof(setup)/sizeof(setup[0]),lifetime);
        CHECK(MEM32(0x1b100+2*16+12)==4 && MEM32(0x1b100+3*16+12)==0x02a010de);
        CHECK(MEM32(base+4096)==0xabcdef01 && !memcmp(memory+base,packet,sizeof(packet)));
        const uint32_t release[][4]={{1,0xfd400720,1,0},{0,0xfd800044,0,0}};
        gpu_thread_cpu_ops(release,2,lifetime);
        CHECK(MEM32(base+4096)==0x12345678 && MEM32(0x1b100+16+12)==gpu_cpu_pa+sizeof(packet));
        CHECK(gpu_test_read(0x800044)==gpu_cpu_pa+sizeof(packet) && gpu_test_read(0x1804)==4);
        for(unsigned i=sizeof(packet);i<8192;++i)if(i<4096 || i>=4100)CHECK(MEM8(base+i)==0x5a);
        uint32_t protect[]={base+4096,4096,PAGE_READONLY},result=0;
        CHECK(!pilot_io_device_lle(182,memory,protect,&result));uint64_t canary=MEM64(base+4094);
        gpu_cpu_fault_checks=1;const uint32_t guarded[][4]={{0,0xfd001804,0,0}};
        gpu_thread_cpu_ops(guarded,1,lifetime);gpu_cpu_fault_checks=0;
        CHECK(MEM64(base+4094)==canary && MEM32(0x1b100+12)==4);
        protect[2]=PAGE_READWRITE;CHECK(!pilot_io_device_lle(182,memory,protect,&result));
        CHECK(!pilot_io_device_lle(171,memory,&base,&result));
        CHECK(!gpu_lle_dma_read(gpu_cpu_pa+4096,&result,4));
        CHECK(!pilot_io_device_work_bind(NULL));
        CHECK(!gpu_lle.error(gpu_lle.device));gpu_lle.destroy(gpu_lle.device);gpu_lle.device=NULL;
        CHECK(!pilot_io_device_mmio_bind(0,0,NULL));pilot_io_device_port_bind(0,0,NULL);
        CHECK(FreeLibrary(gpu_lle.library));gpu_lle.library=NULL;
        CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    CHECK(thread_cpu_calls==9 && gpu_cpu_mmio_calls>=3*26);
    fprintf(stderr,"PASS: three original CPU/GPU lifetimes, same PBUS/PFIFO owner and physical-zero jump, held/released actual semaphore DMA, whole RAM checks, scoped crosspage/RO/lease/CPU-reentry/binding guards and original cleanup\n");
}
#else
static void test_gpu_thread_cpu_lle(void){CHECK(0);}
#endif
