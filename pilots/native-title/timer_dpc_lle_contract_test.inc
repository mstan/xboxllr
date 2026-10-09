/* The original clock and dispatcher invoke these authored generated-body
 * equivalents through the same ABI bridge used by title callbacks. */
static unsigned timer_lle_callbacks,timer_lle_queued;
static int timer_lle_cancel;
static void test_timer_lle_dpc(void)
{
    uint32_t object=MEM32(g_esp+4),context=MEM32(g_esp+8);
    CHECK(dispatch_in_dpc && pilot_io_device_irql()==2 && MEM8(g_fs_base+0x24)==2);
    CHECK(!MEM8(object+2) && event_lle_call(121,NULL)!=0);
    CHECK(g_esp>=0xd001e000u && g_esp<0xd0021000u); // qualified original stack, never relocated
    if(object==0x19540) {
        CHECK(context==0x195a0);++timer_lle_callbacks;
        uint32_t out=context+32;event_lle_call(128,&out);
        CHECK(MEM64(g_esp+12)==MEM64(out));
        if(timer_lle_callbacks==1) {
            uint32_t queue[]={0x19560,0x12345678,0x87654321};
            CHECK((event_lle_call(119,queue)&255)==1);
            CHECK((event_lle_call(119,queue)&255)==0);
            CHECK((event_lle_call(137,queue)&255)==1);
            CHECK((event_lle_call(137,queue)&255)==0);
            CHECK((event_lle_call(119,queue)&255)==1);
        }
        if(timer_lle_cancel && timer_lle_callbacks==3) {
            uint32_t timer=0x19500;CHECK((event_lle_call(97,&timer)&255)==1);
        }
    } else {
        CHECK(object==0x19560 && context==0xabcdef01);
        CHECK(MEM32(g_esp+12)==0x12345678 && MEM32(g_esp+16)==0x87654321);
        ++timer_lle_queued;
    }
    /* Caller state must survive real generated callback clobbers. */
    g_eax=0xaabbccdd;g_fp_stack[0]=-7.5;g_fp_top=6;memset(&g_xmm0,0x12,sizeof(g_xmm0));
    g_esp+=20;
}
static void test_timer_dpc_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision) && revision==0x10);
    uint32_t dpc[]={0x19540,0x12480,0x195a0};event_lle_call(107,dpc);
    dpc[0]=0x19560;dpc[2]=0xabcdef01;event_lle_call(107,dpc);
    uint32_t command=0x34,low=0x65,high=4;
    CHECK(!pilot_usb_pic_port(0x43,1,1,&command));
    CHECK(!pilot_usb_pic_port(0x40,1,1,&low));CHECK(!pilot_usb_pic_port(0x40,1,1,&high));
    uint32_t init[]={0x19500,1},set[]={0x19500,0xffffffffu,0xffffffffu,3,0x19540};
    event_lle_call(113,init);CHECK((event_lle_call(150,set)&255)==0);timer_lle_cancel=1;
    g_eax=0x76543210;g_fp_stack[0]=3.25;g_fp_top=2;memset(&g_xmm0,0xab,sizeof(g_xmm0));
    uint32_t sp=g_esp;RecompXmm xmm=g_xmm0;
    for(unsigned tick=1;tick<=14;++tick) {
        int delivered=0;CHECK(!pilot_usb_pit_time((uint64_t)tick*1125));
        const char *error=pilot_io_device_irq(memory,&delivered);
        if(error)fprintf(stderr,"timer DPC LLE: %s\n",error);
        CHECK(!error && delivered);
        CHECK(timer_lle_callbacks==(tick<5?1:tick<9?2:3));
        CHECK(timer_lle_queued==1);
        CHECK(g_eax==0x76543210 && g_esp==sp && g_fp_stack[0]==3.25 && g_fp_top==2);
        CHECK(!memcmp(&g_xmm0,&xmm,sizeof(xmm)) && !dispatch_in_dpc && !pilot_io_device_irql());
    }
    CHECK(!MEM8(0x19503));
    /* High IRQL defers actual original DPC invocation. Canceling an expired
     * timer must retain its already queued callback. */
    event_lle_call(129,NULL);CHECK(pilot_io_device_irql()==2);
    uint32_t past[]={0x19500,0,0,0x19540};CHECK((event_lle_call(149,past)&255)==0);
    CHECK(MEM8(0x19542) && timer_lle_callbacks==3);
    CHECK((event_lle_call(97,init)&255)==0 && MEM8(0x19542));
    uint32_t passive=0;event_lle_call(161,&passive);
    CHECK(timer_lle_callbacks==4 && !MEM8(0x19542) && !pilot_io_device_irql());
    CHECK(g_eax==0x76543210 && g_esp==sp && !dispatch_in_dpc);
    fprintf(stderr,"PASS: original timer DPCs into native callbacks, original stack, nested queue/remove/cancel, IRQL deferral, queued-work lifetime and register/FP restoration\n");
}
