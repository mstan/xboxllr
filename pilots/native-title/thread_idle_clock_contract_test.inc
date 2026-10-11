static struct ThreadIdleClock {uint64_t origin,base,samples;} thread_idle_clock;
static uint64_t thread_idle_elapsed;
static int thread_idle_runtime_clock;
static unsigned thread_idle_fw_high,thread_idle_fw_low;
static const char *test_thread_idle_board_port(uint16_t port,int size,int write,uint32_t *value)
{
    const char *error=pilot_usb_board_port(port,size,write,value);
    if(error)return error;
    uint32_t known=0,levels=0;
    CHECK(!pilot_usb_board_outputs(&known,&levels));CHECK(known==(1u<<15));
    CHECK(levels==((*value&1)<<15));
    if(*value==5)++thread_idle_fw_high;
    else {CHECK(thread_idle_fw_high);++thread_idle_fw_low;}
    fprintf(stderr,"[IDLE CLOCK GPIO] original FWENB=%u samples=%llu\n",*value&1,
        (unsigned long long)thread_idle_clock.samples);
    return NULL;
}
static const char *test_thread_idle_clock_input(void *context,uint64_t *ticks)
{
    struct ThreadIdleClock *clock=context;
    uint64_t elapsed=dispatch_monotonic()-clock->origin;
    *ticks=clock->base+(elapsed/10000000)*1125000+(elapsed%10000000)*1125000/10000000;
    ++clock->samples;return NULL;
}
static void test_thread_idle_clock_body(void)
{
    uint32_t context=MEM32(g_esp+4);
    MEM64(context+8)=(uint64_t)-(int64_t)330000;
    uint32_t wait[]={MEM32(context+4),0,context+8};
    uint64_t before=thread_interrupt_time();
    CHECK(thread_wait_call(233,wait,3)==0x102);
    thread_idle_elapsed=thread_interrupt_time()-before;
    CHECK(thread_idle_elapsed>=330000);g_esp+=8;
}
static void test_thread_idle_clock_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    uint32_t known=0,levels=0,value=0xa5;
    CHECK(!pilot_usb_board_outputs(&known,&levels) && !known && !levels);
    CHECK(pilot_usb_board_port(0x80cf,1,0,&value) && value==0xa5);
    for(unsigned width=2;width<=4;width+=2)
        CHECK(pilot_usb_board_port(0x80cf,width,1,&value));
    for(unsigned command=0;command<256;++command)if(command!=4 && command!=5) {
        value=command;CHECK(pilot_usb_board_port(0x80cf,1,1,&value));
        CHECK(!pilot_usb_board_outputs(&known,&levels) && !known && !levels);
    }
    pilot_io_device_port_bind(0x80cf,0x80cf,test_thread_idle_board_port);
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));
    if(!thread_idle_runtime_clock) {
        uint32_t command=0x34,low=0x65,high=4;
        CHECK(!pilot_usb_pic_port(0x43,1,1,&command));
        CHECK(!pilot_usb_pic_port(0x40,1,1,&low));CHECK(!pilot_usb_pic_port(0x40,1,1,&high));
    }
    uint32_t out=0x19800,create_event[]={out,0,1,0};
    CHECK(!event_lle_call(189,create_event));uint32_t event=MEM32(out);
    uint32_t context=0x19900;MEM32(context+4)=event;
    uint32_t create[]={out,0,0x126c0,context,0};CHECK(!event_lle_call(254,create));uint32_t handle=MEM32(out);
    uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
    uint32_t priority[]={object,30};CHECK(event_lle_call(148,priority)==8);
    if(thread_idle_runtime_clock) {
        const char *error=pilot_io_device_clock_start();
        if(error)fprintf(stderr,"[RUNTIME CLOCK FAILURE] %s\n",error);
        CHECK(!error);
    }
    uint64_t next=0;CHECK(!pilot_usb_pit_clock(&thread_idle_clock.base,&next) && next>thread_idle_clock.base);
    if(!thread_idle_runtime_clock) {
        thread_idle_clock.origin=dispatch_monotonic();
        CHECK(!pilot_io_device_clock_bind(test_thread_idle_clock_input,&thread_idle_clock));
    }
    uint32_t join[]={object,0,0,0,0};CHECK(!event_lle_call(159,join));
    CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    uint64_t final_ticks=0;CHECK(!pilot_usb_pit_clock(&final_ticks,&next) && final_ticks>thread_idle_clock.base);
    CHECK((thread_idle_runtime_clock || thread_idle_clock.samples>1) && thread_idle_elapsed>=330000);
    CHECK(thread_idle_fw_high && thread_idle_fw_low);
    CHECK(!event_lle_call(187,&handle));event_lle_call(250,&object);CHECK(!event_lle_call(187,&event));
    fprintf(stderr,"PASS: original blocked timeout without tick peer; runtime=%d actual original continuation, modeled PIT/PIC IRQ0 and %llu clock samples\n",thread_idle_runtime_clock,(unsigned long long)thread_idle_clock.samples);
}
