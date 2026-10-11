/* An independent maintained device IRQ arrives while the real original
 * scheduler is idle. No authored idle frame or ready queue completes work. */
static unsigned idle_interrupt_sent,idle_interrupt_calls,idle_interrupt_samples,idle_dpc_calls;
static uint32_t idle_interrupt_base,idle_interrupt_top;
static const char *test_idle_interrupt_work(void)
{
    if(++idle_interrupt_samples<=8)
        fprintf(stderr,"[IDLE IRQ CLOCK] sample=%u current=%08x idle=%08x\n",idle_interrupt_samples,MEM32(0x8003da64),MEM32(0x8003da6c));
    if(!idle_interrupt_sent && MEM32(0x8003da64)==MEM32(0x8003da6c)) {
        idle_interrupt_sent=1;pilot_usb_pic_raise(3,1);
    }
    return NULL;
}
static void test_idle_interrupt_callback(void)
{
    CHECK(MEM32(0x8003da64)==MEM32(0x8003da6c) && pilot_io_device_irql()==23);
    CHECK(g_esp>=idle_interrupt_base+0x100 && (uint64_t)g_esp+12<=idle_interrupt_top);
    CHECK(MEM32(g_esp+4)==0x1c200 && MEM32(g_esp+8)==0xabcdef01);
    uint32_t saved_sp=g_esp;g_esp-=0x100;
    MEM32(g_esp)=0x11223344;MEM32(g_esp+4)=0xcccccccc;MEM32(g_esp+8)=0x55667788;
    pilot_usb_pic_raise(3,0);
    /* Original kernel instructions write into these native local bytes over
     * the same PTE/PFN, proving two-way backing rather than an initial copy. */
    uint32_t args[]={0,6,0,g_esp+4},result=0;
    CHECK(!pilot_io_device_lle(2,memory,args,&result));
    CHECK(MEM32(g_esp)==0x11223344 && MEM32(g_esp+4)==0x004e0101 && MEM32(g_esp+8)==0x55667788);
    uint32_t queue[]={0x1c300,0x13579bdf,0x2468ace0};
    CHECK(!pilot_io_device_lle(119,memory,queue,&result) && result==1);
    ++idle_interrupt_calls;g_eax=1;g_esp=saved_sp+12;
}
static void test_idle_dpc_callback(void)
{
    CHECK(MEM32(0x8003da64)==MEM32(0x8003da6c) && pilot_io_device_irql()==2 && dispatch_in_dpc);
    CHECK(g_esp>=idle_interrupt_base+0x100 && (uint64_t)g_esp+20<=idle_interrupt_top);
    CHECK(MEM32(g_esp+4)==0x1c300 && MEM32(g_esp+8)==0x10203040);
    CHECK(MEM32(g_esp+12)==0x13579bdf && MEM32(g_esp+16)==0x2468ace0);
    uint32_t saved=g_esp;g_esp-=0x100;MEM32(g_esp)=0x11223344;
    MEM32(g_esp+4)=0xcccccccc;MEM32(g_esp+8)=0x55667788;
    uint32_t args[]={0,6,0,g_esp+4},result=0;
    CHECK(!pilot_io_device_lle(2,memory,args,&result));
    CHECK(MEM32(g_esp)==0x11223344 && MEM32(g_esp+4)==0x004e0101 && MEM32(g_esp+8)==0x55667788);
    ++idle_dpc_calls;g_esp=saved+20;
}
static void test_idle_interrupt_stack_lle(int changed_bounds)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;uint32_t revision=0;
    CHECK(!usb_access(0xfed00000,0,&revision));
    uint32_t intr[]={0x1c200,0x12520,0xabcdef01,0x33,23,0,0},result=0;
    CHECK(!pilot_io_device_lle(109,memory,intr,&result));
    CHECK(!pilot_io_device_lle(98,memory,intr,&result) && (result&255)==1);
    uint32_t dpc[]={0x1c300,0x12560,0x10203040};CHECK(!pilot_io_device_lle(107,memory,dpc,&result));
    uint32_t out=0x19800,event_create[]={out,0,1,0};
    CHECK(!event_lle_call(189,event_create));uint32_t event=MEM32(out);
    uint32_t context=0x19900;memset(memory+context,0,96);thread_device_code();
    MEM64(context+8)=(uint64_t)-(int64_t)5000000;MEM32(context+16)=3;
    MEM32(context+20)=0xfe000000+233*4;MEM32(context+32)=event;
    MEM32(context+36)=0;MEM32(context+40)=context+8;MEM32(context+64)=THREAD_DEVICE_CPU;
    uint32_t create[]={out,0,THREAD_DEVICE_CPU,context,0};CHECK(!event_lle_call(254,create));
    uint32_t handle=MEM32(out),ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));
    uint32_t object=MEM32(out+4),priority[]={object,30};CHECK(event_lle_call(148,priority)==8);
    uint32_t idle=MEM32(0x8003da6c);idle_interrupt_base=MEM32(idle+0x20);idle_interrupt_top=MEM32(idle+0x1c);
    CHECK(idle_interrupt_top-idle_interrupt_base==0x3000);
    MEMORY_BASIC_INFORMATION info;
    CHECK(VirtualQuery(memory+idle_interrupt_base,&info,sizeof(info)) && info.State==MEM_RESERVE);
    if(changed_bounds)MEM32(idle+0x1c)+=4;
    CHECK(!pilot_io_device_clock_start());CHECK(!pilot_io_device_work_bind(test_idle_interrupt_work));
    uint64_t before=0,next=0;CHECK(!pilot_usb_pit_clock(&before,&next));
    uint32_t join[]={object,0,0,0,0};result=0xcccccccc;
    const char *error=pilot_io_device_lle(159,memory,join,&result);
    fprintf(stderr,"[IDLE IRQ RESULT] sent=%u calls=%u samples=%u dpcs=%u result=%08x completed=%u error=%s\n",
        idle_interrupt_sent,idle_interrupt_calls,idle_interrupt_samples,idle_dpc_calls,MEM32(context),MEM32(context+4),error?error:"none");
    if(changed_bounds) {
        CHECK(error && !strcmp(error,"device LLE idle callback stack owner or bounds changed"));
        CHECK(result==0xcccccccc && idle_interrupt_sent && !idle_interrupt_calls);
        CHECK(VirtualQuery(memory+idle_interrupt_base,&info,sizeof(info)) && info.State==MEM_RESERVE);
        fprintf(stderr,"PASS: changed idle-owner stack metadata stops before publication/callback/result\n");return;
    }
    if(error)fprintf(stderr,"[IDLE ISR ERROR] %s\n",error);CHECK(!error && !result);
    CHECK(idle_interrupt_sent && idle_interrupt_calls==1 && idle_dpc_calls==1 && MEM32(context)==0x102 && MEM32(context+4)==1);
    uint64_t after=0;CHECK(!pilot_usb_pit_clock(&after,&next) && after-before>=562500);
    CHECK(VirtualQuery(memory+idle_interrupt_base,&info,sizeof(info)) && info.State==MEM_COMMIT);
    CHECK(MEM32(idle+0x1c)==idle_interrupt_top && MEM32(idle+0x20)==idle_interrupt_base);
    CHECK(!pilot_io_device_work_bind(NULL));CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    CHECK(!pilot_io_device_lle(100,memory,intr,&result) && (result&255)==1);
    CHECK(!event_lle_call(187,&handle));event_lle_call(250,&object);CHECK(!event_lle_call(187,&event));
    fprintf(stderr,"PASS: real original idle IRQ/queued DPC, unaligned original stack, native spills/original query shared bytes, actual timeout and original IRQ/thread cleanup\n");
}
