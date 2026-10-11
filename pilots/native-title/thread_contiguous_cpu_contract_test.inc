/* Real original allocations from authored x86 threads share published PFNs
 * with USB DMA. Observers do not create native allocator records or PTEs. */
#define THREAD_CONTIGUOUS_CPU 0x13500u
#define CONTIG_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static void contiguous_second(struct ThreadChildCode *c)
{
    CONTIG_BYTES(c,0x8b,0x07,0x05,0,0x10,0,0,0x50);
}
static void contiguous_signal_wait(struct ThreadChildCode *c)
{
    child_push(c,0);child_push_field(c,32);child_call(c,225);
    child_push(c,0);child_push(c,0);child_push_field(c,36);child_call(c,233);
}
static void thread_contiguous_code(int borrowed_release)
{
    struct ThreadChildCode code={0};struct ThreadChildCode *c=&code;
    CONTIG_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14);
    /* A valid but impossible original physical extent returns NULL. */
    child_push(c,4);child_push(c,4096);child_push(c,0x3ffffff);
    child_push(c,0x3fff000);child_push(c,8192);child_call(c,166);vm_status(c,44);
    CONTIG_BYTES(c,0x83,0x7f,16,0);
    unsigned extended=child_branch(c,1);
    child_push(c,5001);child_call(c,165);
    unsigned allocated=child_branch(c,0);child_patch(c,extended);
    child_push_field(c,20);child_push(c,0x10000);child_push(c,0x3ffffff);
    child_push(c,0x3000000);child_push(c,5001);child_call(c,166);child_patch(c,allocated);
    vm_status(c,0);
    child_push_field(c,0);child_call(c,180);vm_status(c,4);
    child_push_field(c,0);child_call(c,173);vm_status(c,8);
    child_push_field(c,0);child_call(c,179);vm_status(c,12);
    contiguous_second(c);child_call(c,173);vm_status(c,40);
    CONTIG_BYTES(c,0x8b,0x37,0xc7,0x06,0x68,0x24,0x57,0x13,
        0xc7,0x86,0,0x10,0,0,0x57,0x13,0x68,0x24);
    child_push(c,1);child_push(c,8192);child_push_field(c,0);child_call(c,178);
    child_push_field(c,0);child_call(c,4);child_call(c,1);vm_status(c,60);
    contiguous_signal_wait(c);
    if(borrowed_release) {
        child_push_field(c,0);child_call(c,171);CONTIG_BYTES(c,0xcc);
    } else {
        child_push(c,PAGE_READONLY);child_push(c,4096);contiguous_second(c);child_call(c,182);
        contiguous_second(c);child_call(c,179);vm_status(c,24);
        contiguous_signal_wait(c);
        child_push_field(c,20);child_push(c,4096);contiguous_second(c);child_call(c,182);
        contiguous_second(c);child_call(c,179);vm_status(c,28);
        child_push(c,0);child_call(c,4);child_call(c,1);vm_status(c,56);
        child_push(c,0);child_push(c,8192);child_push_field(c,0);child_call(c,178);
        child_push_field(c,0);child_call(c,171);
        CONTIG_BYTES(c,0xc7,0x47,52,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    }
    CHECK(c->size<512);memcpy(memory+THREAD_CONTIGUOUS_CPU,c->bytes,c->size);
}
static void test_thread_contiguous_cpu_lle(int borrowed_release)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));thread_contiguous_code(borrowed_release);
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    uint32_t old_bases[6]={0};unsigned reused=0;
    for(unsigned life=0;life<(borrowed_release?1u:6u);++life) {
        const uint32_t context=0x1b000,out=0x19800;uint32_t events[2];
        memset(memory+context,0,256);MEM32(context+16)=life&1;
        MEM32(context+64)=THREAD_CONTIGUOUS_CPU;
        MEM32(context+20)=life==3?(PAGE_READWRITE|PAGE_WRITECOMBINE):PAGE_READWRITE;
        MEM32(context+128)=0xa5a55a5a;
        for(unsigned i=0;i<2;++i) {
            uint32_t create[]={out,0,1,0};CHECK(!event_lle_call(189,create));events[i]=MEM32(out);
            MEM32(context+32+i*4)=events[i];
        }
        uint32_t create[]={out,0,THREAD_CONTIGUOUS_CPU,context,0};CHECK(!event_lle_call(254,create));uint32_t handle=MEM32(out);
        uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
        uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
        uint32_t base=0,pa=0;MEMORY_BASIC_INFORMATION host;
        for(unsigned phase=0;phase<(borrowed_release?1u:2u);++phase) {
            uint32_t wait[]={events[0],0,0};CHECK(!event_lle_call(233,wait));
            base=MEM32(context);pa=MEM32(context+8);CHECK(base && pa && !MEM32(context+44));
            CHECK(MEM32(context+4)==8192 && MEM32(context+40)==pa+4096);
            CHECK(MEM32(context+60)==base);
            CHECK(MEM32(context+12)==MEM32(context+20));
            if(life&1)CHECK(!(base&0xffff) && pa>=0x3000000 && pa+8192<=0x4000000);
            CHECK(VirtualQuery(memory+base+4096,&host,sizeof(host)) && host.State==MEM_COMMIT);
            CHECK(host.Protect==(phase?PAGE_READONLY:PAGE_READWRITE));
            if(phase)CHECK(MEM32(context+24)==PAGE_READONLY && test_write_faults(base+4096));
            CHECK(MEM32(base)==0x13572468 && MEM32(base+4096)==0x24681357);
            uint32_t observed=0;CHECK(usb_dma_read(pa,&observed,4) && observed==0x13572468);
            CHECK(!physical_find(base,1)); /* No duplicate native allocation. */
            unsigned char cross[16],before[16],input[16];
            memcpy(before,memory+base+4088,sizeof(before));memset(input,0x7d,sizeof(input));
            CHECK(usb_dma_read(pa+4088,cross,sizeof(cross)) && !memcmp(cross,before,sizeof(cross)));
            if(phase) {
                CHECK(!usb_dma_write(pa+4088,input,sizeof(input)));
                CHECK(!memcmp(memory+base+4088,before,sizeof(before)));
            } else {
                CHECK(usb_dma_write(pa+4088,input,sizeof(input)));
                CHECK(!memcmp(memory+base+4088,input,sizeof(input)));
                CHECK(usb_dma_write(pa+4088,before,sizeof(before)));
            }
            memset(cross,0xcd,sizeof(cross));memcpy(before,cross,sizeof(cross));
            CHECK(!usb_dma_read(pa+8192-8,cross,sizeof(cross)) && !memcmp(cross,before,sizeof(cross)));
            CHECK(!pilot_io_device_contiguous_dma(pa,&observed,4,0,2));
            if(!borrowed_release) {
                uint32_t reset=1;CHECK(!usb_access(0xfed00008,1,&reset));
            }
            uint32_t signal[]={events[1],0};CHECK(!event_lle_call(225,signal));
        }
        uint32_t join[]={object,0,0,0,0},result=0;
        const char *error=pilot_io_device_lle(159,memory,join,&result);
        if(borrowed_release) {
            CHECK(error && !strcmp(error,"device LLE contiguous release while DMA borrowed"));
            CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_COMMIT);
            CHECK(MEM32(base)==0x13572468 && MEM32(base+4096)==0x24681357);
            CHECK(!MEM32(context+52) && MEM32(context+128)==0xa5a55a5a);
            fprintf(stderr,"PASS: original CPU free stops before mutating a USB-borrowed contiguous owner; bytes and output canary retained\n");return;
        }
        CHECK(!error && !result && MEM32(context+52)==1 && MEM32(context+28)==MEM32(context+20));
        CHECK(!MEM32(context+56));
        CHECK(MEM32(context+128)==0xa5a55a5a);
        CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_RESERVE);
        CHECK(VirtualQuery(memory+base+4096,&host,sizeof(host)) && host.State==MEM_RESERVE);
        uint32_t observed=0x11223344;CHECK(!usb_dma_read(pa,&observed,4) && observed==0x11223344);
        for(unsigned j=0;j<life;++j)if(old_bases[j]==base){++reused;break;}old_bases[life]=base;
        CHECK(!event_lle_call(187,&handle));event_lle_call(250,&object);
        for(unsigned i=0;i<2;++i)CHECK(!event_lle_call(187,events+i));
        CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    CHECK(thread_cpu_calls==6 && reused);
    fprintf(stderr,"PASS: six original CPU contiguous lifetimes, both allocation ABIs, real extents/PFNs/cache policies, USB bytes/full-range guards/reset leases, protection/free/reuse and settled handles\n");
}
#undef CONTIG_BYTES
