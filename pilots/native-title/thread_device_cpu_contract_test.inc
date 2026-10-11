/* Actual original x86 CALL/RET consumers of the shared device service owner.
 * The observer never fabricates a device object, allocation, pin or IRQ gate. */
#define THREAD_DEVICE_CPU 0x13d00u
#define THREAD_DEVICE_MMIO_A 0xfea00000u
#define THREAD_DEVICE_MMIO_B 0xfea01000u
#define THREAD_DEVICE_WAIT_PEER 0x17600u
#define THREAD_DEVICE_WAIT_PEER_CONTEXT 0x1b600u
static void test_memory_snapshot(uint32_t out[9]);
static uint32_t thread_device_call(unsigned ordinal,const uint32_t *args,unsigned count,unsigned life);
static uint32_t thread_device_mmio_words[2];
static uint32_t thread_device_dma_base,thread_device_dma_pa,thread_device_hcca;
static unsigned thread_device_mmio_reads[2],thread_device_mmio_writes[2],thread_device_mmio_reentry;
static uint32_t thread_device_wait_object,thread_device_wait_peer_object,thread_device_wait_last_counter;
static unsigned thread_device_wait_samples,thread_device_wait_progress;
static unsigned thread_device_wait_frame_seen,thread_device_wait_frame_progress;
static uint16_t thread_device_wait_first_frame,thread_device_wait_last_frame;
static const char *test_thread_device_wait_work(void)
{
    if(!thread_device_wait_object)return NULL;
    unsigned char waiter[0x140],peer[0x140];
    if(component_wait_thread_snapshot(thread_device_wait_object,waiter) && waiter[0x2c]==5 &&
       MEM32(0x8003da64u)==thread_device_wait_peer_object &&
       component_wait_thread_snapshot(thread_device_wait_peer_object,peer) && peer[0x2c]==2) {
        CHECK(waiter[0x32]==30 && peer[0x32]==29);
        ++thread_device_wait_samples;uint32_t count=MEM32(THREAD_DEVICE_WAIT_PEER_CONTEXT+4);
        if(count>thread_device_wait_last_counter)++thread_device_wait_progress;
        thread_device_wait_last_counter=count;
        /* This is an owned-RAM observation while original99 is genuinely
         * blocked. Counting frame changes here excludes a one-off device
         * update before delay admission as sufficient progress evidence. */
        uint16_t frame=MEM16(thread_device_hcca+0x80);CHECK(!MEM16(thread_device_hcca+0x82));
        if(!thread_device_wait_frame_seen){thread_device_wait_first_frame=frame;thread_device_wait_frame_seen=1;}
        else if(frame!=thread_device_wait_last_frame)++thread_device_wait_frame_progress;
        thread_device_wait_last_frame=frame;
    }
    return NULL; /* Observe only owned RAM; never poll or tick the controller. */
}
static const char *test_thread_device_mmio(uint32_t address,unsigned size,int write,uint32_t *value)
{
    unsigned slot=address==THREAD_DEVICE_MMIO_A?0:address==THREAD_DEVICE_MMIO_B?1:2;
    CHECK(slot<2 && size==4 && value);
    uint32_t result=0xaabbccdd,args[]={1},regs[8]={0},original[8]={0};int delivered=123;
    CHECK(pilot_io_device_lle(1,memory,args,&result) && result==0xaabbccdd);
    CHECK(pilot_io_device_irq(memory,&delivered) && delivered==123);
    CHECK(pilot_io_device_thread_execute(THREAD_DEVICE_CPU,regs) && !memcmp(regs,original,sizeof(regs)));
    CHECK(pilot_io_device_initialize(memory));
    CHECK(pilot_io_device_work_bind(NULL));
    CHECK(pilot_io_device_mmio_bind(THREAD_DEVICE_MMIO_A,4096,NULL));
    if(thread_device_dma_pa) {
        /* A generic MMIO owner has only synchronous byte access. It cannot
         * acquire or retire the real OHCI owner's retained USB lease. Mode2
         * observes the latter through the actual original free guard. */
        uint32_t observed=0xaabbccdd;
        CHECK(!pilot_io_device_contiguous_dma(thread_device_dma_pa,&observed,4,0,PILOT_IO_DMA_USB) && observed==0xaabbccdd);
        CHECK(pilot_io_device_contiguous_dma(thread_device_dma_pa,&observed,4,0,PILOT_IO_DMA_SYNC) && observed==MEM32(thread_device_dma_base));
        observed=0xaabbccdd;
        CHECK(!pilot_io_device_contiguous_dma(thread_device_dma_pa+8190,&observed,4,0,PILOT_IO_DMA_SYNC) && observed==0xaabbccdd);
        pilot_io_device_dma_release(PILOT_IO_DMA_USB);
    }
    ++thread_device_mmio_reentry;
    if(write){thread_device_mmio_words[slot]=*value;++thread_device_mmio_writes[slot];}
    else {*value=thread_device_mmio_words[slot];++thread_device_mmio_reads[slot];}
    return NULL;
}
static void thread_device_code(void)
{
    struct ThreadChildCode code={0},*c=&code;
    const uint8_t start[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,0x83,0x7f,24,0,0x0f,0x85};
    child_emit(c,start,sizeof(start));unsigned mmio=c->size;child_word(c,0);
    /* Optional owned-HCCA snapshots surround an actual original99 call.
     * Both branches are local and fit signed8; the fixed body must remain
     * wholly below the next separately bound raw entry at13E00. */
    const uint8_t delayed[]={0x8b,0x4f,28,0x85,0xc9,0x74,0};
    child_emit(c,delayed,sizeof(delayed));unsigned ordinary=c->size-1;
    const uint8_t before_wait[]={0x0f,0x10,0x87,128,0,0,0,0xdd,0x47,80,0x89,0x67,96,
        0x8b,0x91,128,0,0,0,0x89,0x57,108};
    child_emit(c,before_wait,sizeof(before_wait));CHECK(c->size-ordinary-1<128);c->bytes[ordinary]=(uint8_t)(c->size-ordinary-1);
    const uint8_t service[]={0x8b,0x5f,16,0x8d,0x74,0x9f,32,0x85,0xdb,0x0f,0x84};
    child_emit(c,service,sizeof(service));unsigned empty=c->size;child_word(c,0);
    unsigned loop=c->size;const uint8_t push[]={0x83,0xee,4,0xff,0x36,0x4b,0x0f,0x85};
    child_emit(c,push,sizeof(push));child_word(c,(uint32_t)(loop-(c->size+4)));child_patch(c,empty);
    const uint8_t finish[]={0x8b,0x47,20,0xff,0xd0,0x89,0x07,0x8b,0x4f,28,0x85,0xc9,0x74,0};
    child_emit(c,finish,sizeof(finish));unsigned no_delay=c->size-1;
    const uint8_t after_wait[]={0x8b,0x91,128,0,0,0,0x89,0x57,112,0x89,0x67,100,
        0x0f,0x11,0x47,48,0xdd,0x5f,88};
    child_emit(c,after_wait,sizeof(after_wait));CHECK(c->size-no_delay-1<128);c->bytes[no_delay]=(uint8_t)(c->size-no_delay-1);
    const uint8_t service_done[]={0xc7,0x47,4,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    child_emit(c,service_done,sizeof(service_done));child_patch(c,mmio);
    /* One fixed body handles both raw services and MMIO. Caller mode is data;
     * never rewrite a translated instruction between original CPU lives. */
    const uint8_t load[]={0x0f,0x10,0x47,32,0xdd,0x47,80,0x89,0x67,96,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,104,
        0x8b,0x4f,8,0x83,0x7f,120,0,0x0f,0x84};
    child_emit(c,load,sizeof(load));unsigned reading=c->size;child_word(c,0);
    const uint8_t store[]={0xff,0x77,104,0x9d,0x8b,0x57,12,0x89,0x11,0xe9};
    child_emit(c,store,sizeof(store));unsigned common=c->size;child_word(c,0);child_patch(c,reading);
    const uint8_t restore_flags[]={0xff,0x77,104,0x9d};child_emit(c,restore_flags,sizeof(restore_flags));child_patch(c,common);
    const uint8_t consume[]={0x8b,0x01,0x89,0x07,0x89,0x5f,16,0x89,0x6f,20,0x89,0x77,24,0x89,0x7f,28,
        0x9c,0x58,0x89,0x47,108,0x89,0x67,100,0x0f,0x11,0x47,48,0xdd,0x5f,88,
        0x8b,0x57,116,0x85,0xd2,0x0f,0x84};
    child_emit(c,consume,sizeof(consume));unsigned no_hcca=c->size;child_word(c,0);
    /* Observe the real owned HCCA bytes immediately after the raw MMIO.
     * No observer or host DMA call creates these frame/pad stores. */
    const uint8_t hcca[]={0x8b,0x92,0x80,0,0,0,0x89,0x57,112};
    child_emit(c,hcca,sizeof(hcca));child_patch(c,no_hcca);
    const uint8_t done[]={0xc7,0x47,4,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    child_emit(c,done,sizeof(done));CHECK(c->size<256);
    memcpy(memory+THREAD_DEVICE_CPU,c->bytes,c->size);
}
static uint32_t thread_device_mmio_call(uint32_t address,uint32_t value,int write,unsigned life)
{
    const uint32_t context=0x1b000;memset(memory+context,0,128);
    MEM32(context+8)=address;MEM32(context+12)=value;MEM32(context+24)=1;
    MEM32(context+116)=thread_device_hcca;
    MEM32(context+64)=THREAD_DEVICE_CPU;MEM32(context+120)=write!=0;MEM32(context+124)=0xa5a55a5a;
    for(unsigned i=0;i<16;++i)MEM8(context+32+i)=(uint8_t)(0x35+i*7);
    double fp=12345.6789;memcpy(memory+context+80,&fp,8);
    thread_cpu_create_join(THREAD_DEVICE_CPU,context,life&1);
    CHECK(MEM32(context+4)==1 && MEM32(context+124)==0xa5a55a5a);
    CHECK(MEM32(context+8)==address && MEM32(context+12)==value && MEM32(context+64)==THREAD_DEVICE_CPU);
    CHECK(MEM32(context+116)==thread_device_hcca);
    CHECK(MEM32(context+16)==0x11223344 && MEM32(context+20)==0x44556677 && MEM32(context+24)==0x22334455 && MEM32(context+28)==context);
    CHECK(MEM32(context+96)==MEM32(context+100) && MEM32(context+104)==MEM32(context+108));
    CHECK(!memcmp(memory+context+32,memory+context+48,16) && !memcmp(memory+context+80,memory+context+88,8));
    return MEM32(context);
}
static uint32_t thread_device_usb_read(uint32_t offset)
{uint32_t value=0xaabbccdd;CHECK(!usb_access(0xfed00000u+offset,0,&value));return value;}
static void thread_device_hcca_bytes(uint32_t base,const uint8_t before[8192])
{
    for(unsigned i=0;i<8192;++i)if(i<256+0x80 || i>=256+0x84)CHECK(MEM8(base+i)==before[i]);
}
static void thread_device_wait_peer_code(void)
{
    const uint8_t code[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xb9,0xdf,0x9b,0x57,0x13,0xba,0xe0,0xac,0x68,0x24,
        0xbb,0x98,0xba,0xdc,0xfe,0xbd,0x21,0x43,0x65,0x87,0xbe,0x78,0x56,0x34,0x12,
        0x83,0x3f,0,0x75,5,0xff,0x47,4,0xeb,0xf6,
        0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x4f,76,0x89,0x57,80,0x89,0x5f,84,0x89,0x6f,88,0x89,0x77,92,
        0xc7,0x47,8,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    CHECK(sizeof(code)<256);memcpy(memory+THREAD_DEVICE_WAIT_PEER,code,sizeof(code));
}
static void thread_device_wait_dma(unsigned life)
{
    const uint32_t context=0x1b000,peer_context=THREAD_DEVICE_WAIT_PEER_CONTEXT,out=0x19800;
    memset(memory+context,0,160);memset(memory+peer_context,0,128);
    MEM32(context+16)=3;MEM32(context+20)=0xfe000000u+99*4;MEM32(context+28)=thread_device_hcca;
    MEM32(context+40)=context+144;MEM64(context+144)=(uint64_t)-(int64_t)200000;
    MEM32(context+64)=THREAD_DEVICE_CPU;MEM32(context+76)=0x87654321;
    MEM32(context+124)=0xa5a55a5a;MEM32(context+156)=0x13579bdf;
    MEM32(peer_context+64)=THREAD_DEVICE_WAIT_PEER;MEM32(peer_context+124)=0x2468ace0;
    for(unsigned i=0;i<16;++i){MEM8(context+128+i)=(uint8_t)(0x35+i*7);MEM8(peer_context+16+i)=(uint8_t)(0x7d+i*3);}
    double fp=12345.6789;memcpy(memory+context+80,&fp,8);memcpy(memory+peer_context+48,&fp,8);
    uint32_t handles[2],objects[2],entries[]={THREAD_DEVICE_WAIT_PEER,THREAD_DEVICE_CPU},contexts[]={peer_context,context};
    for(unsigned id=0;id<2;++id) {
        uint32_t create[]={out,0,entries[id],contexts[id],0};CHECK(!event_lle_call(254,create));handles[id]=MEM32(out);
        uint32_t ref[]={handles[id],0,out+4};CHECK(!event_lle_call(246,ref));objects[id]=MEM32(out+4);
        uint32_t priority[]={objects[id],id?30u:29u};CHECK(event_lle_call(148,priority)==8);
    }
    thread_device_wait_object=objects[1];thread_device_wait_peer_object=objects[0];thread_device_wait_last_counter=0;
    thread_device_wait_frame_seen=thread_device_wait_frame_progress=0;
    unsigned waits=thread_device_wait_samples,progress=thread_device_wait_progress;
    uint64_t initial_time=0,final_time=0;CHECK(!pilot_io_device_interrupt_time(memory,&initial_time));
    uint32_t join[]={objects[1],0,0,0,0};CHECK(!event_lle_call(159,join));
    CHECK(!pilot_io_device_interrupt_time(memory,&final_time));uint64_t elapsed=final_time-initial_time;
    /* The original CPU performs the owned-RAM reads immediately before and
     * after original99. There is no device/MMIO access between those reads.
     * Only the production USB work owner can advance these HCCA bytes. */
    uint32_t first=MEM32(context+108),last=MEM32(context+112);
    CHECK(!MEM32(context) && MEM32(context+4)==1 && elapsed>=200000);
    CHECK(!(first>>16) && !(last>>16) && (uint16_t)(last-first));
    CHECK(thread_device_wait_samples>waits && thread_device_wait_progress>progress && MEM32(peer_context+4));
    CHECK(thread_device_wait_frame_seen && thread_device_wait_frame_progress && (uint16_t)(thread_device_wait_last_frame-thread_device_wait_first_frame));
    CHECK(MEM32(context+28)==thread_device_hcca && !MEM32(context+32) && !MEM32(context+36) && MEM32(context+40)==context+144);
    CHECK(MEM64(context+144)==(uint64_t)-(int64_t)200000 && MEM32(context+64)==THREAD_DEVICE_CPU);
    CHECK(MEM32(context+76)==0x87654321 && MEM32(context+124)==0xa5a55a5a && MEM32(context+156)==0x13579bdf);
    CHECK(MEM32(context+96)==MEM32(context+100) && !memcmp(memory+context+128,memory+context+48,16) && !memcmp(memory+context+80,memory+context+88,8));
    MEM32(peer_context)=1;join[0]=objects[0];CHECK(!event_lle_call(159,join));
    CHECK(MEM32(peer_context+8)==1 && MEM32(peer_context+124)==0x2468ace0 && MEM32(peer_context+68)==MEM32(peer_context+72));
    CHECK(!memcmp(memory+peer_context+16,memory+peer_context+32,16) && !memcmp(memory+peer_context+48,memory+peer_context+56,8));
    CHECK(MEM32(peer_context+76)==0x13579bdf && MEM32(peer_context+80)==0x2468ace0 && MEM32(peer_context+84)==0xfedcba98 &&
        MEM32(peer_context+88)==0x87654321 && MEM32(peer_context+92)==0x12345678);
    thread_device_wait_object=thread_device_wait_peer_object=0;
    for(unsigned id=0;id<2;++id){CHECK(!event_lle_call(187,handles+id));event_lle_call(250,objects+id);}
    fprintf(stderr,"[NO-POLL OHCI DMA] life=%u genuine99_elapsed=%llu Waiting5/peerIRQprogress=%u/%u blocked_HCCA_changes=%u actual_owned_HCCA=%u->%u pad=0 no_MMIO_between_raw_RAM_snapshots FP/ESP/canaries=unchanged\n",
        life,(unsigned long long)elapsed,thread_device_wait_samples-waits,thread_device_wait_progress-progress,thread_device_wait_frame_progress,(uint16_t)first,(uint16_t)last);
}
static int thread_device_mmio_life(unsigned life,int borrowed_release,int wait_dma)
{
    CHECK(!pilot_io_device_initialize(memory));
    CHECK(pilot_io_device_initialize(memory+4096));
    CHECK(thread_device_mmio_call(0xfed00000,0,0,life)==0x10 && thread_device_usb_read(0)==0x10);
    /* The actual maintained reset owns register defaults and releases any
     * schedule leases. No descriptor or completion producer is fabricated. */
    CHECK(!thread_device_mmio_call(0xfed00008,1,1,life));
    CHECK(!thread_device_usb_read(4) && !thread_device_usb_read(8) && thread_device_usb_read(0x10)==0x80000000u);
    CHECK(thread_device_mmio_call(0xfed00004,0x20,1,life)==0x20 && thread_device_usb_read(4)==0x20);
    CHECK(!thread_device_mmio_call(0xfed00004,0,1,life) && !thread_device_usb_read(4));
    CHECK(thread_device_mmio_call(0xfed00010,2,1,life)==0x80000002u && thread_device_usb_read(0x10)==0x80000002u);
    thread_device_mmio_call(0xfed00014,2,1,life);
    CHECK(thread_device_usb_read(0x10)==0x80000000u && thread_device_mmio_call(0xfed00010,0,0,life)==0x80000000u);
    uint32_t unchanged=0x12345678;
    CHECK(usb_original_mmio(0xfed00001,4,0,&unchanged) && unchanged==0x12345678);
    CHECK(usb_original_mmio(0xfed00000,1,0,&unchanged) && unchanged==0x12345678);
    /* Allocation, extent, physical translation and page protection come from
     * actual original CPU services. No native allocation record is added. */
    uint32_t bytes=8192,base=thread_device_call(165,&bytes,1,life);
    CHECK(base && !(base&4095));
    uint32_t pa=thread_device_call(173,&base,1,life);
    CHECK(pa && pa==(base&0x03ffffffu) && thread_device_call(180,&base,1,life)==bytes);
    CHECK(thread_device_call(179,&base,1,life)==PAGE_READWRITE && !physical_find(base,1));
    uint32_t second=base+4096;
    CHECK(thread_device_call(173,&second,1,life)==pa+4096 && thread_device_call(179,&second,1,life)==PAGE_READWRITE);
    uint8_t before[8192];memset(memory+base,0x5a,sizeof(before));
    MEM32(base)=0x13572468u+life;MEM32(base+8192-4)=0xa5a55a5a;
    memset(memory+base+256,0,128);MEM32(base+256+0x80)=0xabcdef01;
    memcpy(before,memory+base,sizeof(before));
    thread_device_dma_base=base;thread_device_dma_pa=pa;thread_device_hcca=base+256;
    CHECK(!(thread_device_hcca&255));
    CHECK(thread_device_mmio_call(0xfed00018,pa+256,1,life)==pa+256 && thread_device_usb_read(0x18)==pa+256);
    uint32_t initial_frame=thread_device_mmio_call(0xfed0003c,0,0,life);
    CHECK(thread_device_mmio_call(0xfed00004,0x84,1,life)==0x84);
    /* Operational+PeriodicEnable reads the real zero-entry interrupt table;
     * genuine wall-clock frames write its frame and pad through USB DMA. The
     * finite host deadline bounds observation without advancing that clock. */
    uint64_t deadline=GetTickCount64()+1000;uint32_t frame=initial_frame;
    do {frame=thread_device_mmio_call(0xfed0003c,0,0,life);}
    while(frame==initial_frame && GetTickCount64()<deadline);
    CHECK(frame!=initial_frame && MEM32(0x1b000+112)==(uint16_t)frame);
    uint32_t compiled_frame=thread_device_usb_read(0x3c);
    CHECK(MEM16(thread_device_hcca+0x80)==(uint16_t)compiled_frame && !MEM16(thread_device_hcca+0x82));
    thread_device_hcca_bytes(base,before);
    if(wait_dma){thread_device_wait_dma(life);thread_device_hcca_bytes(base,before);}
    unsigned reads[2],writes[2],reentry=thread_device_mmio_reentry;
    memcpy(reads,thread_device_mmio_reads,sizeof(reads));memcpy(writes,thread_device_mmio_writes,sizeof(writes));
    thread_device_mmio_words[0]=0x13579bdfu+life;thread_device_mmio_words[1]=0x2468ace0u+life;
    CHECK(!pilot_io_device_mmio_bind(THREAD_DEVICE_MMIO_A,4096,test_thread_device_mmio));
    CHECK(!pilot_io_device_mmio_bind(THREAD_DEVICE_MMIO_B,4096,test_thread_device_mmio));
    CHECK(pilot_io_device_mmio_bind(THREAD_DEVICE_MMIO_A,8192,test_thread_device_mmio));
    CHECK(pilot_io_device_mmio_bind(0xfed00000,4096,test_thread_device_mmio));
    CHECK(pilot_io_device_mmio_bind(THREAD_DEVICE_MMIO_A+1,4096,test_thread_device_mmio));
    CHECK(pilot_io_device_mmio_bind(0xfffff000,8192,test_thread_device_mmio));
    CHECK(pilot_io_device_mmio_bind(THREAD_DEVICE_MMIO_A,8192,NULL));
    uint32_t a=0x76543210u+life,b=0xfedcba98u+life;
    CHECK(thread_device_mmio_call(THREAD_DEVICE_MMIO_A,a,1,life)==a);
    CHECK(thread_device_mmio_call(THREAD_DEVICE_MMIO_B,b,1,life)==b);
    CHECK(thread_device_mmio_call(THREAD_DEVICE_MMIO_A,0,0,life)==a);
    CHECK(thread_device_mmio_call(THREAD_DEVICE_MMIO_B,0,0,life)==b);
    CHECK(!pilot_io_device_mmio_bind(THREAD_DEVICE_MMIO_A,4096,NULL));
    CHECK(pilot_io_device_mmio_bind(THREAD_DEVICE_MMIO_A,4096,NULL));
    CHECK(thread_device_mmio_call(THREAD_DEVICE_MMIO_B,0,0,life)==b);
    CHECK(thread_device_mmio_call(0xfed00000,0,0,life)==0x10 && thread_device_usb_read(0x10)==0x80000000u);
    /* Reuse the freed registration slot. Its old absolute base/offset must
     * not redirect the still-live neighboring owner or the real OHCI owner. */
    CHECK(!pilot_io_device_mmio_bind(THREAD_DEVICE_MMIO_A,4096,test_thread_device_mmio));
    CHECK(thread_device_mmio_call(THREAD_DEVICE_MMIO_A,0,0,life)==a);
    CHECK(!pilot_io_device_mmio_bind(THREAD_DEVICE_MMIO_A,4096,NULL));
    CHECK(!pilot_io_device_mmio_bind(THREAD_DEVICE_MMIO_B,4096,NULL));
    CHECK(thread_device_mmio_call(0xfed00000,0,0,life)==0x10 && thread_device_usb_read(0)==0x10);
    for(unsigned i=0;i<2;++i)CHECK(thread_device_mmio_reads[i]==reads[i]+3 && thread_device_mmio_writes[i]==writes[i]+1);
    CHECK(thread_device_mmio_reentry==reentry+8);
    thread_device_hcca_bytes(base,before);
    if(borrowed_release) {
        const uint32_t context=0x1b000,out=0x19800;memset(memory+context,0,128);
        MEM32(context+16)=1;MEM32(context+20)=0xfe000000u+171*4;MEM32(context+32)=base;
        MEM32(context+64)=THREAD_DEVICE_CPU;MEM32(context+124)=0xa5a55a5a;
        uint32_t create[]={out,0,THREAD_DEVICE_CPU,context,0};CHECK(!event_lle_call(254,create));uint32_t handle=MEM32(out);
        uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
        uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
        uint32_t join[]={object,0,0,0,0},result=0xdeadbeef;
        const char *error=pilot_io_device_lle(159,memory,join,&result);
        CHECK(error && !strcmp(error,"device LLE contiguous release while DMA borrowed"));
        CHECK(!MEM32(context+4) && MEM32(context+124)==0xa5a55a5a);
        MEMORY_BASIC_INFORMATION host;
        for(unsigned offset=0;offset<8192;offset+=4096)CHECK(VirtualQuery(memory+base+offset,&host,sizeof(host)) && host.State==MEM_COMMIT);
        thread_device_hcca_bytes(base,before);
        fprintf(stderr,"PASS: actual raw OHCI HCCA DMA retains original USB lease despite genericSYNC callback USB transfer/release attempts; original171 stops before free/caller mutation and retains actual pages/bytes\n");
        return 1;
    }
    CHECK(!thread_device_mmio_call(0xfed00008,1,1,life));
    CHECK(!thread_device_usb_read(4) && !thread_device_usb_read(0x18));
    thread_device_hcca_bytes(base,before);
    thread_device_dma_base=thread_device_dma_pa=thread_device_hcca=0;
    thread_device_call(171,&base,1,life);
    MEMORY_BASIC_INFORMATION host;
    for(unsigned offset=0;offset<8192;offset+=4096)CHECK(VirtualQuery(memory+base+offset,&host,sizeof(host)) && host.State==MEM_RESERVE);
    unchanged=0x12345678;CHECK(!usb_dma_read(pa,&unchanged,4) && unchanged==0x12345678);
    fprintf(stderr,"[ORIGINAL OHCI MMIO] life=%u shared_revision/reset/control/interrupts=actual raw_MOV+compiled HCCA=original_allocation/PFN+actual_periodic_read/frame_write lease=raw_reset_then_original_free independent_owners=2 exact_remove/reuse=retired nonterminal_range/reentry_guards=preserved GPR/flags/x87/XMM/ESP/canaries=unchanged\n",life);
    return 0;
}
static uint32_t thread_device_call(unsigned ordinal,const uint32_t *args,unsigned count,unsigned life)
{
    const uint32_t context=0x1b000;CHECK(count<=7);memset(memory+context,0,96);
    MEM32(context+16)=count;MEM32(context+20)=0xfe000000+ordinal*4;
    MEM32(context+64)=THREAD_DEVICE_CPU;if(count)memcpy(memory+context+32,args,count*4);
    MEM32(context+92)=0xa5a55a5a;
    thread_cpu_create_join(THREAD_DEVICE_CPU,context,life&1);
    CHECK(MEM32(context+4)==1 && MEM32(context+92)==0xa5a55a5a);
    return MEM32(context);
}
static void test_thread_device_cpu_lle(int bad_pin)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;uint32_t revision=0;
    CHECK(!usb_access(0xfed00000,0,&revision));thread_device_code();
    if(bad_pin==3) {
        thread_device_wait_peer_code();CHECK(!pilot_io_device_clock_start());CHECK(pilot_io_device_clock_bound());
        CHECK(!pilot_io_device_work_bind(test_thread_device_wait_work));
        CHECK(pilot_io_device_work_bind(test_thread_device_wait_work));
    }
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    uint32_t baseline_memory[9],after_memory[9];test_memory_snapshot(baseline_memory);
    for(unsigned life=0;life<((bad_pin==1 || bad_pin==2)?1u:3u);++life) {
        if(bad_pin!=1 && thread_device_mmio_life(life,bad_pin==2,bad_pin==3))return;
        uint32_t vector[]={3,0x1c081};MEM32(0x1c080)=0xcdcdcdcd;
        CHECK(thread_device_call(44,vector,2,life)==0x33);
        CHECK(MEM32(0x1c080)==0xcdcd17cd);
        vector[0]=27;MEM32(0x1c080)=0xcdcdcdcd;
        CHECK(!thread_device_call(44,vector,2,life) && MEM32(0x1c080)==0xcdcdcdcd);
        memset(memory+0x1c000,0xcd,0x78);
        uint32_t interrupt[]={0x1c004,0x12300,0x55667788,0x33,23,0,0};
        thread_device_call(109,interrupt,7,life);
        CHECK(MEM32(0x1c000)==0xcdcdcdcd && MEM32(0x1c074)==0xcdcdcdcd);
        CHECK(MEM32(0x1c004)==0x12300 && MEM32(0x1c008)==0x55667788);
        CHECK(MEM32(0x1c00c)==3 && MEM32(0x1c010)==23);
        CHECK((thread_device_call(98,interrupt,1,life)&255)==1);
        CHECK(MEM8(0x1c014)==1);
        CHECK((thread_device_call(100,interrupt,1,life)&255)==1);
        CHECK(MEM8(0x1c014)==0);
        uint32_t event[]={0x1c100,0,1},signal[]={0x1c100,0,0};
        MEM32(0x1c0fc)=0x12345678;MEM32(0x1c110)=0x87654321;
        thread_device_call(108,event,3,life);
        CHECK(thread_device_call(138,event,1,life)==1);
        CHECK(thread_device_call(138,event,1,life)==0);
        CHECK(thread_device_call(145,signal,3,life)==0);
        CHECK(thread_device_call(145,signal,3,life)==1);
        CHECK(thread_device_call(138,event,1,life)==1);
        CHECK(MEM32(0x1c0fc)==0x12345678 && MEM32(0x1c110)==0x87654321);
        uint32_t notify[]={0x1d000,1};MEM32(notify[0])=0x12300;MEM32(notify[0]+4)=5;
        thread_device_call(47,notify,2,life);notify[1]=0;thread_device_call(47,notify,2,life);
        memset(memory+0x1e000,0,128);MEM32(0x1e400)=0xcdcdcdcd;
        uint32_t create[]={0x1e000,64,0,0x22,0,0x1e400};
        CHECK(!thread_device_call(65,create,6,life));uint32_t object=MEM32(0x1e400);
        CHECK(object && MEM16(object)==3 && MEM16(object+2)==0x88);
        CHECK(MEM32(object+8)==0x1e000 && MEM32(object+0x18)==object+0x48);
        for(unsigned i=0;i<64;++i)CHECK(!MEM8(object+0x48+i));
        MEM32(object+0x48)=0x12345678;
        uint32_t pin[]={object+0x48,64,0};thread_device_call(175,pin,3,life);
        uint32_t pa=thread_device_call(173,pin,1,life),observed=0;
        CHECK(pa && pilot_io_device_pinned_dma(pa,&observed,4,0) && observed==0x12345678);
        pin[2]=1;thread_device_call(175,pin,3,life);
        observed=0xaabbccdd;CHECK(!pilot_io_device_pinned_dma(pa,&observed,4,0) && observed==0xaabbccdd);
        if(bad_pin==1) {
            const uint32_t context=0x1b000;memset(memory+context,0,96);
            MEM32(context+16)=3;MEM32(context+20)=0xfe000000+175*4;
            memcpy(memory+context+32,pin,sizeof(pin));MEM32(context+64)=THREAD_DEVICE_CPU;
            uint32_t out=0x19800,birth[]={out,0,THREAD_DEVICE_CPU,context,0};
            CHECK(!event_lle_call(254,birth));uint32_t handle=MEM32(out);
            uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t thread=MEM32(out+4);
            uint32_t join[]={thread,0,0,0,0},result=0;
            const char *error=pilot_io_device_lle(159,memory,join,&result);
            fprintf(stderr,"[DEVICE PIN GUARD] %s\n",error?error:"returned without error");
            CHECK(error && !strcmp(error,"device LLE unmatched pin range or capacity"));
            CHECK(!MEM32(context+4) && MEM32(object+0x48)==0x12345678);
            fprintf(stderr,"PASS: original-thread duplicate unlock stops before caller or device mutation\n");return;
        }
        thread_device_call(68,&object,1,life);
        for(unsigned ordinal=14;ordinal<=15;++ordinal) {
            uint32_t allocation[]={72,0x584c4c45};uint32_t pool=thread_device_call(ordinal,allocation,ordinal==14?1:2,life);
            CHECK(pool);MEM32(pool)=0x87654321;CHECK(MEM32(pool)==0x87654321);
            thread_device_call(17,&pool,1,life);
        }
        CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
        test_memory_snapshot(after_memory);CHECK(!memcmp(baseline_memory,after_memory,sizeof(baseline_memory)));
    }
    if(bad_pin==3){CHECK(!pilot_io_device_work_unbind(test_thread_device_wait_work));CHECK(pilot_io_device_work_unbind(test_thread_device_wait_work));CHECK(!pilot_io_device_clock_bind(NULL,NULL));}
    fprintf(stderr,"PASS: three original-thread HAL/IRQ/device/pool and sharedOHCI/MMIO-owner lifetimes, exact byte outputs, real pin/DMA/unpin, rawMOV ABI/FP/canaries, independent mapping/remove/reuse/reentry guards and exact memory/thread/handles\n");
}
