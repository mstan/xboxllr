/* An independent maintained device IRQ arrives while the real original
 * scheduler is idle. No authored idle frame or ready queue completes work. */
static unsigned idle_interrupt_sent,idle_interrupt_calls,idle_interrupt_samples,idle_dpc_calls;
static uint32_t idle_interrupt_base,idle_interrupt_top;
static unsigned idle_dpc_page_mode,idle_dpc_page_life,idle_dpc_page_calls;
static uint32_t idle_dpc_page_base,idle_dpc_page_physical[2];
static void test_memory_snapshot(uint32_t out[9]);

/* Use the generated callback's genuine original stack, including the real
 * stdcall argument/return frame. call_kernel's host scratch frame would not
 * qualify this ISR/DPC boundary. Every completed call must retain IRQL2 and
 * the nonvolatile caller registers. */
static uint32_t test_idle_dpc_page_call(unsigned ordinal,const uint32_t *args,unsigned count)
{
    uint32_t sp=g_esp,bx=g_ebx,bp=g_ebp,si=g_esi,di=g_edi,fs=g_fs_base;
    CHECK(dispatch_in_dpc && pilot_io_device_irql()==2 && !g_df);
#ifdef PILOT_SCHEDULING_LLE
    CHECK(usb_device_callback_depth);
#endif
    CHECK(count<=3 && sp>=idle_interrupt_base+0x20+(count+1)*4);
    g_esp-=(count+1)*4;MEM32(g_esp)=0x12580;
    for(unsigned i=0;i<count;++i)MEM32(g_esp+4+i*4)=args[i];
    next_kernel_ordinal=ordinal;kernel_dispatch();
    CHECK(g_esp==sp && g_ebx==bx && g_ebp==bp && g_esi==si && g_edi==di && g_fs_base==fs);
    CHECK(pilot_io_device_irql()==2 && MEM8(fs+0x24)==2 && !g_df);
    return g_eax;
}
static uint8_t test_idle_dpc_page_byte(unsigned i,int changed)
{
    return (uint8_t)((changed?0xb7u:0x31u)+idle_dpc_page_life*19u+i*(changed?11u:7u));
}
static void test_idle_dpc_pages(void)
{
    CHECK(idle_dpc_page_base && !idle_dpc_page_calls);
    CHECK(MEM16(0x1c300)==0x13 && !MEM8(0x1c302)); /* Actually dequeued KDPC. */
    uint32_t start=idle_dpc_page_base+4080,pin[]={start,32,0};
    unsigned char observed[16],replacement[16];
    test_idle_dpc_page_call(175,pin,3);
    test_idle_dpc_page_call(175,pin,3); /* Two real pins, not two DMA owners. */
    for(unsigned page=0;page<2;++page) {
        uint32_t address=start+page*16;
        uint32_t pa=test_idle_dpc_page_call(173,&address,1);
        CHECK(pa && (pa&4095u)==(page?0u:4080u));
        idle_dpc_page_physical[page]=pa;
        memset(observed,0xcd,sizeof(observed));
        CHECK(pilot_io_device_pinned_dma(pa,observed,sizeof(observed),0));
        for(unsigned i=0;i<16;++i) {
            CHECK(observed[i]==test_idle_dpc_page_byte(page*16+i,0));
            replacement[i]=test_idle_dpc_page_byte(page*16+i,1);
        }
        CHECK(pilot_io_device_pinned_dma(pa,replacement,sizeof(replacement),1));
        CHECK(!memcmp(memory+address,replacement,sizeof(replacement)));
    }
    pin[2]=1;test_idle_dpc_page_call(175,pin,3);
    /* One remaining genuine pin still authorizes both physical aliases. */
    for(unsigned page=0;page<2;++page) {
        CHECK(pilot_io_device_pinned_dma(idle_dpc_page_physical[page],observed,sizeof(observed),0));
        for(unsigned i=0;i<16;++i)CHECK(observed[i]==test_idle_dpc_page_byte(page*16+i,1));
    }
    test_idle_dpc_page_call(175,pin,3);
    for(unsigned page=0;page<2;++page) {
        memset(observed,0xcd,sizeof(observed));
        CHECK(!pilot_io_device_pinned_dma(idle_dpc_page_physical[page],observed,sizeof(observed),0));
        for(unsigned i=0;i<16;++i)CHECK(observed[i]==0xcd);
        CHECK(!pilot_io_device_pinned_dma(idle_dpc_page_physical[page],observed,sizeof(observed),1));
    }
    for(unsigned i=0;i<8192;++i)
        CHECK(MEM8(idle_dpc_page_base+i)==((i>=4080 && i<4112)?test_idle_dpc_page_byte(i-4080,1):0x5a));
    ++idle_dpc_page_calls;
    uint32_t signal[]={0x1c400,0,0};CHECK(!test_idle_dpc_page_call(145,signal,3));
    /* Generated callback FP state is volatile. The actual interrupted CPU's
     * x87/XMM/control state must survive this boundary and the real wake. */
    g_fp_top=6;g_fp_stack[0]=-7.5;memset(&g_xmm0,0x12,sizeof(g_xmm0));
}
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
    /* Match the generated callback ABI: local EBP restores on LEAVE, while
     * its global nested-call publication still names the callee's frame.
     * The original DPC dispatcher must receive its preserved queue pointer. */
    uint32_t saved=g_esp,incoming=g_ebp,ebp=incoming;
    g_esp-=4;MEM32(g_esp)=ebp;ebp=g_esp;g_ebp=ebp;
    g_esp-=0x100;MEM32(g_esp)=0x11223344;
    MEM32(g_esp+4)=0xcccccccc;MEM32(g_esp+8)=0x55667788;
    uint32_t args[]={0,6,0,g_esp+4},result=0;
    CHECK(!pilot_io_device_lle(2,memory,args,&result));
    CHECK(MEM32(g_esp)==0x11223344 && MEM32(g_esp+4)==0x004e0101 && MEM32(g_esp+8)==0x55667788);
    if(idle_dpc_page_mode)test_idle_dpc_pages();
    g_esp=ebp;ebp=MEM32(g_esp);g_esp+=4;
    CHECK(ebp==incoming && g_esp==saved && g_ebp==saved-4);
    ++idle_dpc_calls;g_esp=saved+20;
}
static const char *test_idle_dpc_adapter(uint32_t entry,uint32_t regs[8])
{
    uint32_t before[8];memcpy(before,regs,sizeof(before));
    const char *error=usb_original_dpc(entry,regs);if(error)return error;
    CHECK(regs[3]==before[3] && regs[5]==before[5] && regs[6]==before[6] && regs[7]==before[7]);
    CHECK(regs[4]==before[4]+20);
    return NULL;
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
    pilot_io_device_dpc_bind(test_idle_dpc_adapter);
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

/* This distinct authored program uses the existing raw13D00 admission. It is
 * installed once in this separate test process, never over an active CPU or
 * translated block. Nonvolatile and FP values span an actual original159
 * block and the original queued KDPC that signals its owned event. */
#define IDLE_DPC_PAGE_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static void test_idle_dpc_page_code(void)
{
    struct ThreadChildCode code={0},*c=&code;
    IDLE_DPC_PAGE_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0xbb,0x98,0xba,0xdc,0xfe,0xbd,0x21,0x43,0x65,0x87,0xbe,0x78,0x56,0x34,0x12,
        0xdd,0x47,40,0x0f,0x10,0x87,128,0,0,0,0xd9,0x7f,88,0x0f,0xae,0x5f,92,
        0x89,0x67,8,0x9c,0x58,0x89,0x47,32);
    child_output(c,104);child_push(c,0);child_push(c,0);child_push(c,0);
    child_push_field(c,112);child_call(c,159);
    IDLE_DPC_PAGE_BYTES(c,0x89,0x07,0x89,0x67,12,0x89,0x5f,16,0x89,0x6f,20,
        0x89,0x77,24,0x89,0x7f,28,0x9c,0x58,0x89,0x47,36,
        0x0f,0x11,0x87,144,0,0,0,0xdd,0x5f,48,0xd9,0x7f,90,0x0f,0xae,0x5f,96,
        0xa1,0x64,0xda,3,0x80,0x89,0x47,116,
        0xc7,0x47,4,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<256);memcpy(memory+THREAD_DEVICE_CPU,c->bytes,c->size);
}
#undef IDLE_DPC_PAGE_BYTES
static void test_idle_dpc_page_memory_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;uint32_t revision=0,result=0;
    CHECK(!usb_access(0xfed00000,0,&revision));test_idle_dpc_page_code();
    uint32_t idle=0;
    pilot_io_device_dpc_bind(test_idle_dpc_adapter);
    CHECK(!pilot_io_device_clock_start());
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t threads=MEM32(stats+4),handles=MEM32(stats+8),before[9],after[9];test_memory_snapshot(before);
    uint32_t captured_query[]={0x10,0x19880};
    MEM32(0x19880)=MEM32(0x19884)=0xabcdef12;
    CHECK(event_lle_call(209,captured_query)==0xc0000024u &&
          MEM32(0x19880)==0xabcdef12 && MEM32(0x19884)==0xabcdef12);
    idle_dpc_page_mode=1;
    /* One measured session settlement precedes three exact reuse lifetimes.
     * Reaching actual idle lets the inherited file worker retire handle10;
     * the original allocator retains one pool page. Neither change is allowed
     * to recur or be absorbed into later per-life baselines. */
    for(unsigned life=0;life<4;++life) {
        idle_dpc_page_life=life;idle_dpc_page_calls=0;
        idle_interrupt_sent=idle_interrupt_calls=idle_interrupt_samples=idle_dpc_calls=0;
        memset(idle_dpc_page_physical,0,sizeof(idle_dpc_page_physical));
        uint32_t base=0,bytes=8192;
        CHECK(!vm_allocate(&base,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE) && base && bytes==8192);
        idle_dpc_page_base=base;memset(memory+base,0x5a,8192);
        for(unsigned i=0;i<32;++i)MEM8(base+4080+i)=test_idle_dpc_page_byte(i,0);
        uint32_t intr[]={0x1c200,0x12520,0xabcdef01,0x33,23,0,0};
        CHECK(!pilot_io_device_lle(109,memory,intr,&result));
        CHECK(!pilot_io_device_lle(98,memory,intr,&result) && (result&255)==1);
        uint32_t dpc[]={0x1c300,0x12560,0x10203040};CHECK(!pilot_io_device_lle(107,memory,dpc,&result));
        uint32_t event[]={0x1c400,1,0};MEM32(0x1c3fc)=0x13579bdf;MEM32(0x1c410)=0x2468ace0;
        CHECK(!pilot_io_device_lle(108,memory,event,&result));
        uint32_t context=0x19900,out=0x19800;memset(memory+context,0,164);
        MEM32(context+64)=THREAD_DEVICE_CPU;MEM64(context+104)=(uint64_t)-(int64_t)5000000;
        MEM32(context+112)=event[0];MEM32(context+120)=0xabcdef01;MEM32(context+160)=0x89abcdef;
        double fp=123.625+life;memcpy(memory+context+40,&fp,8);
        for(unsigned i=0;i<16;++i)MEM8(context+128+i)=(uint8_t)(3+life*17+i*7);
        uint32_t create[]={out,0,THREAD_DEVICE_CPU,context,0};CHECK(!event_lle_call(254,create));
        uint32_t handle=MEM32(out),ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));
        uint32_t object=MEM32(out+4),priority[]={object,30};CHECK(event_lle_call(148,priority)==8);
        idle=MEM32(0x8003da6c);
        idle_interrupt_base=MEM32(idle+0x20);idle_interrupt_top=MEM32(idle+0x1c);
        CHECK(idle_interrupt_top-idle_interrupt_base==0x3000);
        CHECK(!pilot_io_device_work_bind(test_idle_interrupt_work));
        uint32_t join[]={object,0,0,0,0};CHECK(!event_lle_call(159,join));
        CHECK(!pilot_io_device_work_unbind(test_idle_interrupt_work));
        CHECK(!MEM32(context) && MEM32(context+4)==1 && MEM32(context+116)==object);
        CHECK(idle_interrupt_sent && idle_interrupt_calls==1 && idle_dpc_calls==1 && idle_dpc_page_calls==1);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+16)==0xfedcba98 &&
              MEM32(context+20)==0x87654321 && MEM32(context+24)==0x12345678 && MEM32(context+28)==context);
        CHECK((MEM32(context+32)&0x600)==0x200 && (MEM32(context+36)&0x600)==0x200);
        CHECK(!memcmp(memory+context+40,memory+context+48,8) && !memcmp(memory+context+128,memory+context+144,16));
        CHECK(MEM16(context+88)==MEM16(context+90) && MEM32(context+92)==MEM32(context+96));
        CHECK(MEM32(context+64)==THREAD_DEVICE_CPU && MEM32(context+112)==event[0] &&
              MEM32(context+120)==0xabcdef01 && MEM32(context+160)==0x89abcdef);
        CHECK(MEM32(0x1c3fc)==0x13579bdf && MEM32(0x1c410)==0x2468ace0 && !MEM8(0x1c302));
        CHECK(MEM32(idle+0x1c)==idle_interrupt_top && MEM32(idle+0x20)==idle_interrupt_base);
        CHECK(!pilot_io_device_lle(100,memory,intr,&result) && (result&255)==1);
        CHECK(!event_lle_call(187,&handle));event_lle_call(250,&object);
        CHECK(event_lle_call(187,&handle)==0xc0000008u);
        uint32_t live=0;CHECK(!pilot_io_device_next_thread(&live) && !live);
        bytes=0;CHECK(!vm_free(&base,&bytes,MEM_RELEASE));
        MEMORY_BASIC_INFORMATION host;
        CHECK(VirtualQuery(memory+idle_dpc_page_base,&host,sizeof(host)) && host.State==MEM_RESERVE);
        CHECK(VirtualQuery(memory+idle_dpc_page_base+4096,&host,sizeof(host)) && host.State==MEM_RESERVE);
        for(unsigned page=0;page<2;++page) {
            uint32_t unchanged=0xaabbccdd;
            CHECK(!pilot_io_device_pinned_dma(idle_dpc_page_physical[page],&unchanged,4,0) && unchanged==0xaabbccdd);
        }
        idle_dpc_page_base=0;
        CHECK(!event_lle_call(256,&stats));test_memory_snapshot(after);
        fprintf(stderr,"[IDLE DPC CLEANUP] life=%u threads=%u/%u handles=%u/%u memory=",
            life,threads,MEM32(stats+4),handles,MEM32(stats+8));
        for(unsigned i=0;i<9;++i)fprintf(stderr,"%s%u/%u",i?",":"",before[i],after[i]);
        fprintf(stderr,"\n");
        MEM32(0x19880)=MEM32(0x19884)=0xabcdef12;
        CHECK(event_lle_call(209,captured_query)==0xc0000008u &&
              MEM32(0x19880)==0xabcdef12 && MEM32(0x19884)==0xabcdef12);
        CHECK(MEM32(stats+4)==threads);
        if(!life) {
            CHECK(MEM32(stats+8)+1==handles && after[2]+1==before[2] && after[6]==before[6]+1);
            for(unsigned i=0;i<9;++i)if(i!=2 && i!=6)CHECK(after[i]==before[i]);
            handles=MEM32(stats+8);memcpy(before,after,sizeof(before));
            fprintf(stderr,"[IDLE DPC SETTLEMENT] inherited file handle10 retired; one original pool page retained; exact reusable baseline established\n");
        } else {
            CHECK(MEM32(stats+8)==handles && !memcmp(before,after,sizeof(before)));
        }
        if(life)fprintf(stderr,"[IDLE DPC PAGES] life=%u actual175/173 IRQL2 two-page alias/two balanced pins/physical read-write/unpin retirement original145 wake FP/ABI/9memory/threadhandles=exact\n",life-1);
    }
    idle_dpc_page_mode=0;
    CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    fprintf(stderr,"PASS: three real idle IRQ/original KDPC page-service lifetimes on owned VM, exact shared physical bytes and actual wake/return/retirement; EBP regression preserved\n");
}
