/* Real original allocations force growth of the interpreter alias cache.
 * Every page is written/read by original CPU instructions before an actual
 * event handoff; release/reuse must restore allocator and object baselines. */
#define THREAD_VM_ALIAS_CPU 0x12d00u
#define THREAD_VM_ALIAS_PAGES 320u
#define ALIAS_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static void test_memory_snapshot(uint32_t out[9]);
static void thread_vm_alias_code(unsigned pages,int pressure)
{
    struct ThreadChildCode code={0};struct ThreadChildCode *c=&code;
    ALIAS_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14);
    vm_allocate_code(c,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);vm_status(c,8);
    ALIAS_BYTES(c,0x8b,0x37,0xb9);child_word(c,pages);
    ALIAS_BYTES(c,0xb8);child_word(c,0x13572468u);
    ALIAS_BYTES(c,0x89,0x06,0x81,0xc6,0,0x10,0,0,0x40,0xe2,0xf5);
    ALIAS_BYTES(c,0x8b,0x37,0xb9);child_word(c,pages);
    ALIAS_BYTES(c,0x31,0xc0,0x03,0x06,0x81,0xc6,0,0x10,0,0,0xe2,0xf6,0x89,0x47,12);
    if(pressure) {
        ALIAS_BYTES(c,0xb9);child_word(c,0xfed00020u);
        ALIAS_BYTES(c,0xb8);child_word(c,0x013579a0u);
        ALIAS_BYTES(c,0x89,1,0x8b,1,0x89,0x47,20);
        ALIAS_BYTES(c,0x83,0xe9,0x20,0x8b,1,0x89,0x47,24);
        ALIAS_BYTES(c,0xc7,0x41,0x20,0,0,0,0);
    }
    child_push(c,0);child_push_field(c,80);child_call(c,225);
    child_push(c,0);child_push(c,0);child_push_field(c,84);child_call(c,233);
    ALIAS_BYTES(c,0xc7,0x47,4,0,0,0,0);
    child_push(c,MEM_RELEASE);child_output(c,4);child_output(c,0);child_call(c,199);vm_status(c,16);
    ALIAS_BYTES(c,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<256);memcpy(memory+THREAD_VM_ALIAS_CPU,c->bytes,c->size);
}
static void test_thread_vm_alias_cpu_case(int pressure)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    unsigned pages=pressure?1536u:THREAD_VM_ALIAS_PAGES;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));thread_vm_alias_code(pages,pressure);
    /* Six MiB of original REP zeroing exceeds a clockless service slice.
     * Use the real runtime PIT/PIC owner to continue that work, as the title
     * does; keep the ordinary320-page clockless fixture unchanged. */
    if(pressure)CHECK(!pilot_io_device_clock_start());
    uint32_t stats=0x19700,baseline[9],after[9];MEM32(stats)=12;
    CHECK(!event_lle_call(256,&stats));uint32_t threads=MEM32(stats+4),handles=MEM32(stats+8);
    test_memory_snapshot(baseline);unsigned retained_capacity=0;
    unsigned original_retired=0;CHECK(!pilot_io_device_alias_cache_retired(&original_retired));
    for(unsigned life=0;life<3;++life) {
        const uint32_t context=0x1b000,out=0x19800;uint32_t events[2];
        memset(memory+context,0,512);MEM32(context+4)=pages*4096;
        MEM32(context+64)=THREAD_VM_ALIAS_CPU;
        for(unsigned i=0;i<2;++i) {
            uint32_t create[]={out,0,1,0};CHECK(!event_lle_call(189,create));events[i]=MEM32(out);
            MEM32(context+80+i*4)=events[i];
        }
        uint32_t create[]={out,0,THREAD_VM_ALIAS_CPU,context,0};
        CHECK(!event_lle_call(254,create));uint32_t handle=MEM32(out);
        uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
        uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
        uint32_t wait[]={events[0],0,0};CHECK(!event_lle_call(233,wait));
        unsigned count=0,capacity=0;CHECK(!pilot_io_device_alias_cache_info(&count,&capacity));
        CHECK(count>256 && capacity>=512 && count<=capacity && count<3584);
        if(pressure) {
            unsigned retired=0;CHECK(!pilot_io_device_alias_cache_retired(&retired));
            CHECK(retired>original_retired);original_retired=retired;
            CHECK(MEM32(context+20)==0x013579a0u && MEM32(context+24)==0x10);
            uint32_t head=0xdeadbeef;CHECK(!usb_access(0xfed00020u,0,&head) && !head);
        }
        if(life)CHECK(capacity==retained_capacity);else retained_capacity=capacity;
        CHECK(!MEM32(context+8));uint32_t base=MEM32(context),checksum=0;
        CHECK(base && MEM32(context+4)==pages*4096);
        for(unsigned page=0;page<pages;++page) {
            uint32_t expected=0x13572468u+page;
            CHECK(MEM32(base+page*4096)==expected);checksum+=expected;
        }
        CHECK(MEM32(context+12)==checksum);
        uint32_t signal[]={events[1],0};CHECK(!event_lle_call(225,signal));
        uint32_t join[]={object,0,0,0,0};CHECK(!event_lle_call(159,join));CHECK(!MEM32(context+16));
        uint32_t info[7];CHECK(!vm_query(base,info) && info[4]==MEM_FREE);
        CHECK(test_write_faults(base) && test_write_faults(base+(pages-1)*4096));
        CHECK(!event_lle_call(187,&handle));event_lle_call(250,&object);
        for(unsigned i=0;i<2;++i)CHECK(!event_lle_call(187,events+i));
        CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==threads && MEM32(stats+8)==handles);
        test_memory_snapshot(after);CHECK(!memcmp(baseline,after,sizeof(baseline)));
    }
    if(pressure)CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    fprintf(stderr,"PASS: three original CPU%u-page allocations/write-read/event/release/reuse lifetimes; alias growth%s, exact host bytes/checksum and all memory/thread/handle baselines\n",pages,
        pressure?"/retirement/re-admission and real OHCI MMIO":" and retained capacity");
}
static void test_thread_vm_alias_cpu_lle(void){test_thread_vm_alias_cpu_case(0);}
static void test_thread_vm_alias_pressure_cpu_lle(void){test_thread_vm_alias_cpu_case(1);}
#undef ALIAS_BYTES
#undef THREAD_VM_ALIAS_PAGES
#undef THREAD_VM_ALIAS_CPU
