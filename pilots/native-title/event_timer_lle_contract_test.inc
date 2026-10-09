/* Run the original object manager and timer queue through the maintained
 * shared-RAM bridge. Independent expectations: native_event_timer_reference.py.
 * Clock expiration executes original IRQ0/DPC work. Blocking thread/context
 * switches remain a separate qualification from these timer observations. */
static uint32_t event_lle_call(unsigned ordinal,const uint32_t *args)
{
    uint32_t result=0xcccccccc;
    const char *error=pilot_io_device_lle(ordinal,memory,args,&result);
    if(error)fprintf(stderr,"event/timer LLE ordinal %u: %s\n",ordinal,error);
    CHECK(!error);return result;
}
static void test_event_timer_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    const uint32_t out=0x19200,info=0x19210,timeout=0x19230,timer=0x19400;
    for(unsigned lifetime=0;lifetime<16;++lifetime)for(unsigned type=0;type<2;++type) {
        unsigned initial=lifetime&1;
        uint32_t create[]={out,0,type,initial};
        CHECK(!event_lle_call(189,create));uint32_t id=MEM32(out);
        uint32_t query[]={id,info},signal[]={id,info+8},wait[]={id,0,timeout},wait_ex[]={id,0,0,timeout};
        MEM64(timeout)=0;
        CHECK(!event_lle_call(209,query) && MEM32(info)==type && MEM32(info+4)==initial);
        CHECK(event_lle_call(233,wait)==(initial?0:0x102));
        CHECK(event_lle_call(234,wait_ex)==(initial && !type?0:0x102));
        CHECK(!event_lle_call(225,signal) && MEM32(info+8)==(type?0:initial));
        CHECK(!event_lle_call(225,signal) && MEM32(info+8)==1);
        CHECK(!event_lle_call(233,wait));
        CHECK(event_lle_call(233,wait)==(type?0x102:0));
        CHECK(!event_lle_call(186,&id));
        CHECK(!event_lle_call(209,query) && !MEM32(info+4));
        CHECK(!event_lle_call(187,&id));
        MEM32(info)=MEM32(info+4)=0xcccccccc;
        CHECK(event_lle_call(209,query)==0xc0000008u && MEM32(info)==0xcccccccc && MEM32(info+4)==0xcccccccc);
        CHECK(event_lle_call(187,&id)==0xc0000008u);
    }
    uint64_t now=0;CHECK(!pilot_io_device_interrupt_time(memory,&now));
    for(unsigned type=0;type<2;++type)for(unsigned ordinal=149;ordinal<=150;++ordinal) {
        const uint64_t durations[]={1,9999,10000,10001,100000,0x100000001ull};
        for(unsigned i=0;i<sizeof(durations)/sizeof(durations[0]);++i) {
            uint32_t init[]={timer,type};event_lle_call(113,init);
            uint64_t relative=0-durations[i];
            uint32_t set[]={timer,(uint32_t)relative,(uint32_t)(relative>>32),0,0};
            CHECK((event_lle_call(ordinal,set)&255)==0);
            CHECK(MEM8(timer)==8+type && MEM8(timer+2)==10 && MEM8(timer+3)==1);
            CHECK(!MEM32(timer+4) && MEM64(timer+16)==now+durations[i]);
            CHECK((event_lle_call(ordinal,set)&255)==1);
            CHECK((event_lle_call(97,&timer)&255)==1 && !MEM8(timer+3));
            CHECK((event_lle_call(97,&timer)&255)==0);
        }
        uint32_t init[]={timer,type},past[]={timer,1,0,0};event_lle_call(113,init);
        CHECK((event_lle_call(149,past)&255)==0 && !MEM8(timer+3) && MEM32(timer+4)==1);
        CHECK((event_lle_call(97,&timer)&255)==0);
    }
    /* Actual OUTs from the independent original INIT execution, not a host
     * mutation of KeInterruptTime or the timer queue. Explicit ticks let this
     * correctness floor run as slowly as necessary. */
    uint32_t command=0x34,low=0x65,high=4;
    CHECK(!pilot_usb_pic_port(0x43,1,1,&command));
    CHECK(!pilot_usb_pic_port(0x40,1,1,&low));
    CHECK(!pilot_usb_pic_port(0x40,1,1,&high));
    uint64_t ticks=0,clock=now;
    const uint32_t durations[]={1,9999,10000,10001,100000,330000};
    for(unsigned type=0;type<2;++type)for(unsigned i=0;i<sizeof(durations)/sizeof(durations[0]);++i) {
        uint32_t init[]={timer,type};event_lle_call(113,init);
        uint64_t relative=0ull-durations[i];
        uint32_t set[]={timer,(uint32_t)relative,(uint32_t)(relative>>32),0};
        CHECK((event_lle_call(149,set)&255)==0);
        unsigned expiry=1+durations[i]/10000;
        for(unsigned tick=1;tick<=expiry;++tick) {
            CHECK(!MEM32(timer+4));int delivered=0;
            ticks+=1125;CHECK(!pilot_usb_pit_time(ticks));
            const char *error=pilot_io_device_irq(memory,&delivered);
            if(error)fprintf(stderr,"original clock IRQ: %s\n",error);
            CHECK(!error && delivered==1);
            CHECK(!pilot_io_device_interrupt_time(memory,&clock));
            CHECK(clock==now+(ticks/1125)*10000);
        }
        CHECK(MEM32(timer+4)==1 && !MEM8(timer+3));
        uint32_t wait[]={timer,0,0,0,timeout};MEM64(timeout)=0;
        CHECK(!event_lle_call(159,wait));
        CHECK(event_lle_call(159,wait)==(type?0x102:0));
        CHECK((event_lle_call(97,&timer)&255)==0);
    }
    fprintf(stderr,"PASS: 32 original event lifetimes, 24 insert/rearm/cancel cases, 12 IRQ0-driven expirations, real notification/synchronization timer consumption; blocking-thread qualification remains open\n");
}
