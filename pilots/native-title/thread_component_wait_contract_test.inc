/* Actual original CPU flags/FP surround a selected component that blocks on
 * its privately created unsignalled event. Actual original125 and maintained
 * PIT/KTHREAD/wait-block observations establish elapsed/blocking ownership independently
 * of the returned timeout status. This does not qualify GPU completion. */
#define THREAD_COMPONENT_WAIT_CPU 0x17000u
#define THREAD_COMPONENT_WAIT_HELPER 0x17e80u
#define THREAD_COMPONENT_WAIT_PEER 0x17600u
#define COMPONENT_WAIT_PEER_CONTEXT 0x1b100u
#define COMPONENT_WAIT_CODE(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static unsigned component_wait_calls,component_wait_timeouts,component_wait_idle_samples,component_wait_vm_lives;
static unsigned component_wait_pending_samples,component_wait_sampled_pairs;
static unsigned component_wait_original_waiting_samples;
static unsigned component_wait_private_wait_samples;
static unsigned component_wait_peer_samples,component_wait_peer_progress_samples;
static uint32_t component_wait_peer_thread,component_wait_peer_last_counter;
static uint32_t component_wait_observed_pairs[8][2];
static int component_wait_workspace_guard;
static uint32_t component_wait_observed_thread;
static void test_memory_snapshot(uint32_t out[9]);
static const char *test_component_wait_board_port(uint16_t port,int size,int write,uint32_t *value)
{return pilot_usb_board_port(port,size,write,value);}
/* nxdk xboxkrnl.h KTHREAD and Cxbx common/types.h2052-2122 agree on these
 * Xbox offsets. Only native-readable original published bytes are sampled;
 * do not materialize an unowned captured object's mapping for diagnosis. */
static int component_wait_owned_bytes(uint32_t object,void *bytes,uint32_t count)
{
    if(!object || (object&3) || !count || (uint64_t)object+count>0x100000000ull)return 0;
    MEMORY_BASIC_INFORMATION host;
    if(!VirtualQuery(memory+object,&host,sizeof(host)) || host.State!=MEM_COMMIT ||
       (host.Protect&PAGE_GUARD) ||
       !(host.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) ||
       (uintptr_t)(memory+object)+count>(uintptr_t)host.BaseAddress+host.RegionSize)return 0;
    memcpy(bytes,memory+object,count);return 1;
}
static int component_wait_thread_snapshot(uint32_t object,unsigned char bytes[0x140])
{return component_wait_owned_bytes(object,bytes,0x140);}
static uint32_t component_wait_snapshot_word(const unsigned char *bytes,unsigned offset)
{uint32_t value;memcpy(&value,bytes+offset,4);return value;}
static void component_wait_thread_diagnostic(const char *label,uint32_t object,const unsigned char bytes[0x140],int readable)
{
    if(!readable){fprintf(stderr,"[COMPONENT WAIT KTHREAD] %s=%08x original object not published/readable\n",label,object);return;}
    fprintf(stderr,"[COMPONENT WAIT KTHREAD] %s=%08x state=%u priority=%d switches=%u wait_status=%08x wait_irql=%u wait_reason=%u wait_blocks=%08x kernel_stack=%08x start=%08x\n",
        label,object,bytes[0x2c],(int)(int8_t)bytes[0x32],component_wait_snapshot_word(bytes,0x4c),
        component_wait_snapshot_word(bytes,0x50),bytes[0x54],bytes[0x57],component_wait_snapshot_word(bytes,0x58),
        component_wait_snapshot_word(bytes,0x24),component_wait_snapshot_word(bytes,0x130));
}
static const char *test_component_wait_work(void)
{
    if(MEM32(0x1b000+128)) {
        uint32_t current=MEM32(0x8003da64u),idle=MEM32(0x8003da6cu);++component_wait_pending_samples;
        unsigned char fixture_bytes[0x140];int fixture_readable=component_wait_thread_snapshot(component_wait_observed_thread,fixture_bytes);
        /* The existing local KTHREAD_STATE enum gives Waiting=5. Record this
         * separately from idle and returnedSTATUS_TIMEOUT; do not replace
         * either qualification assertion with a guessed successful wait. */
        if(fixture_readable && fixture_bytes[0x2c]==5 && current!=component_wait_observed_thread)
            ++component_wait_original_waiting_samples;
        if(fixture_readable && fixture_bytes[0x2c]==5 && current!=component_wait_observed_thread) {
            unsigned char selected_bytes[0x140];int selected_readable=component_wait_thread_snapshot(current,selected_bytes);
            if(selected_readable && selected_bytes[0x2c]==2) {
                uint32_t frame=MEM32(0x1b000+120),event=MEM32(0x1b000+124);
                uint32_t bottom=component_wait_snapshot_word(fixture_bytes,0x20),top=component_wait_snapshot_word(fixture_bytes,0x1c);
                CHECK(frame>=bottom+68 && (uint64_t)frame+COMPONENT_WAIT_BYTES<=top);
                /* Exact hash-bound original233 wrapper249DE calls234247F6.
                 * EH prolog316D4 makes its EBP=frame-40; real246 publishes
                 * the resolved event at EBP-1C=frame-68. The real234 owns
                 * and later retires this reference. No diagnostic246/250. */
                uint32_t event_object=0;CHECK(component_wait_owned_bytes(frame-68,&event_object,4));
                uint32_t caller[5];CHECK(component_wait_owned_bytes(frame-36,caller,sizeof(caller)));
                CHECK(caller[0]==0x800249f1u && caller[1]==event && !caller[2] && !caller[3] && caller[4]==frame+8);
                uint32_t wait_block=component_wait_snapshot_word(fixture_bytes,0x58),block[6];
                CHECK(wait_block>=bottom && (uint64_t)wait_block+sizeof(block)<=top);
                CHECK(component_wait_owned_bytes(wait_block,block,sizeof(block)));
                CHECK(event_object && block[2]==component_wait_observed_thread && block[3]==event_object && fixture_bytes[0x57]==6);
                /* The original event body remains private kernel memory.
                 * Actual209 before/after each slice proves synchronization
                 * type1 and unsignalled state0; never force publication just
                 * to duplicate that authoritative query through a host read. */
                ++component_wait_private_wait_samples;
                /* A real lower-priority compute peer keeps the inherited
                 * title-launch task out of this authored lifetime. IRQ0
                 * must wake/preempt it with the actual timed-out waiter. */
                if(current==component_wait_peer_thread) {
                    CHECK(selected_bytes[0x32]==29 && component_wait_snapshot_word(selected_bytes,0x130)==THREAD_COMPONENT_WAIT_PEER);
                    uint32_t counter=MEM32(COMPONENT_WAIT_PEER_CONTEXT+4);
                    CHECK(counter>=component_wait_peer_last_counter);
                    if(counter>component_wait_peer_last_counter)++component_wait_peer_progress_samples;
                    component_wait_peer_last_counter=counter;++component_wait_peer_samples;
                }
            }
        }
        unsigned seen=0;
        for(unsigned i=0;i<component_wait_sampled_pairs;++i)
            if(component_wait_observed_pairs[i][0]==current && component_wait_observed_pairs[i][1]==idle)seen=1;
        if(!seen && component_wait_sampled_pairs<8) {
            component_wait_observed_pairs[component_wait_sampled_pairs][0]=current;
            component_wait_observed_pairs[component_wait_sampled_pairs++][1]=idle;
            fprintf(stderr,"[COMPONENT WAIT OBSERVED OWNER] pending_sample=%u current=%08x idle=%08x fixture_thread=%08x\n",
                component_wait_pending_samples,current,idle,component_wait_observed_thread);
            component_wait_thread_diagnostic("fixture",component_wait_observed_thread,fixture_bytes,fixture_readable);
            if(current!=component_wait_observed_thread && current!=idle) {
                unsigned char selected_bytes[0x140];int selected_readable=component_wait_thread_snapshot(current,selected_bytes);
                component_wait_thread_diagnostic("selected",current,selected_bytes,selected_readable);
            }
        }
        if(current==idle) {
            CHECK(component_wait_observed_thread && current!=component_wait_observed_thread);
            ++component_wait_idle_samples;
        }
    }
    return NULL;
}
static uint32_t component_wait_fixture_call(unsigned ordinal,const uint32_t *args)
{
    uint32_t result=0xbad0cafe;CHECK(!pilot_io_device_thread_component_call(ordinal,args,&result));return result;
}
static const char *test_component_wait_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==THREAD_COMPONENT_WAIT_HELPER && pilot_io_device_thread_component_active());
    uint32_t context=regs[2];CHECK(context==0x1b000);
    CHECK(regs[3]==0x11223344 && regs[5]==0x44556677 && regs[6]==0x22334455 && regs[7]==context);
    CHECK(MEM32(context)==MEM32(0x8003da64u));component_wait_observed_thread=MEM32(context);
    ++component_wait_calls;
    unsigned blocked_before=component_wait_private_wait_samples;
    unsigned peer_before=component_wait_peer_samples,progress_before=component_wait_peer_progress_samples;
    struct ComponentWait wait={0},empty={0};
    uint32_t status=0xabcdef12;
    CHECK(component_wait_open(NULL));
    CHECK(component_wait_slice(&wait,1,&status) && status==0xabcdef12);
    CHECK(component_wait_close(&wait) && !memcmp(&wait,&empty,sizeof(wait)));
    /* Real original IRQL transitions exercise rejection before event/stack
     * mutation. The dispatcher callback flag has a separate direct guard. */
    uint32_t raised=2,old=component_wait_fixture_call(160,&raised)&255;CHECK(!old && pilot_io_device_irql()==2);
    CHECK(component_wait_open(&wait) && !memcmp(&wait,&empty,sizeof(wait)));
    (void)component_wait_fixture_call(161,&old);CHECK(!pilot_io_device_irql());
    dispatch_in_dpc=1;CHECK(component_wait_open(&wait) && !memcmp(&wait,&empty,sizeof(wait)));dispatch_in_dpc=0;
    if(component_wait_workspace_guard==1) {
        uint32_t occupied=0;CHECK(!pilot_io_device_thread_component_workspace(32,&occupied));
        memset(memory+occupied,0x65,32);
        const char *error=component_wait_open(&wait);
        CHECK(error && !strcmp(error,"selected helper workspace outside its original stack lease"));
        CHECK(!memcmp(&wait,&empty,sizeof(wait)));
        for(unsigned i=0;i<32;++i)CHECK(MEM8(occupied+i)==0x65);
    } else if(component_wait_workspace_guard==2) {
        uint64_t ticks=0,next=0;CHECK(!pilot_usb_pit_clock(&ticks,&next) && next);
        CHECK(!pilot_io_device_clock_bound());
        const char *error=component_wait_open(&wait);
        CHECK(error && !strcmp(error,"component timed wait requires the bound original clock owner"));
        CHECK(!memcmp(&wait,&empty,sizeof(wait)));
        /* Programmed PIT alone cannot consume even the stack lease. */
        uint32_t untouched=0;CHECK(!pilot_io_device_thread_component_workspace(32,&untouched));
        memset(memory+untouched,0x65,32);
    } else {
        uint64_t ticks=0,next=0;CHECK(!pilot_usb_pit_clock(&ticks,&next) && next);
        CHECK(!component_wait_open(&wait));
        uint32_t frame=wait.frame,event=wait.event;MEM32(context+120)=frame;MEM32(context+124)=event;
        CHECK(wait.thread==MEM32(context) && frame>=MEM32(wait.thread+0x20) && frame+32ull<=MEM32(wait.thread+0x1c));
        struct ComponentWait before=wait;
        unsigned char frame_before[COMPONENT_WAIT_BYTES];memcpy(frame_before,memory+frame,sizeof(frame_before));
        const uint32_t bad_releases[][2]={{frame+4,32},{frame,31},{frame,0},{frame,64}};
        for(unsigned i=0;i<sizeof(bad_releases)/sizeof(*bad_releases);++i) {
            CHECK(pilot_io_device_thread_component_workspace_release(bad_releases[i][0],bad_releases[i][1]));
            CHECK(!memcmp(frame_before,memory+frame,sizeof(frame_before)));
        }
        CHECK(component_wait_open(&wait) && !memcmp(&wait,&before,sizeof(wait)));
        CHECK(component_wait_slice(&wait,0,&status) && status==0xabcdef12);
        CHECK(component_wait_slice(&wait,COMPONENT_WAIT_MAX_100NS+1,&status) && status==0xabcdef12);
        CHECK(component_wait_slice(&wait,1,NULL));
        uint64_t timeout=MEM64(frame+8);uint32_t original_thread=wait.thread;
        wait.thread^=4;CHECK(component_wait_slice(&wait,1,&status) && status==0xabcdef12);
        CHECK(MEM64(frame+8)==timeout);wait.thread=original_thread;
        /* Tiny genuine timeouts can expire during original service overhead.
         * The33ms first wait must reach an actual blocked interval; the
         * one-unit and varied final timeout still exercise finite boundaries. */
        const uint32_t durations[]={330000,1,MEM32(context+100)};
        for(unsigned i=0;i<3;++i) {
            MEM32(context+128)=1;CHECK(!component_wait_slice(&wait,durations[i],&status));MEM32(context+128)=0;
            CHECK(status==0x102 && wait.event==event && wait.frame==frame);++component_wait_timeouts;
        }
        uint64_t after=0;CHECK(!pilot_usb_pit_clock(&after,&next) && after>ticks);
        fprintf(stderr,"[COMPONENT WAIT OBSERVATION] pending_samples=%u original_waiting_samples=%u idle_samples=%u current=%08x idle=%08x fixture_thread=%08x\n",
            component_wait_pending_samples,component_wait_original_waiting_samples,component_wait_idle_samples,
            MEM32(0x8003da64u),MEM32(0x8003da6cu),component_wait_observed_thread);
        CHECK(component_wait_private_wait_samples>blocked_before);
        CHECK(component_wait_peer_samples>peer_before && component_wait_peer_progress_samples>progress_before);
        CHECK(!component_wait_close(&wait) && !memcmp(&wait,&empty,sizeof(wait)));
        for(unsigned i=0;i<32;++i)CHECK(!MEM8(frame+i));
        uint32_t closed_query[]={event,context+168};MEM32(context+168)=MEM32(context+172)=0xabcdef12;
        CHECK(component_wait_fixture_call(209,closed_query)==0xc0000008u &&
              MEM32(context+168)==0xabcdef12 && MEM32(context+172)==0xabcdef12);
        CHECK(pilot_io_device_thread_component_workspace_release(frame,COMPONENT_WAIT_BYTES));
        CHECK(component_wait_close(&wait));
        CHECK(component_wait_slice(&wait,1,&status) && status==0x102);
        /* Close retired both the handle and its exact manual lease. Reuse
         * this same component's original stack for a second actual wait. */
        CHECK(!component_wait_open(&wait) && wait.frame==frame);
        MEM32(context+124)=wait.event;
        MEM32(context+128)=1;CHECK(!component_wait_slice(&wait,100000,&status));MEM32(context+128)=0;
        CHECK(status==0x102);++component_wait_timeouts;
        CHECK(!component_wait_close(&wait) && !memcmp(&wait,&empty,sizeof(wait)));
        CHECK(pilot_io_device_thread_component_workspace_release(frame,COMPONENT_WAIT_BYTES));
        /* The existing actual VM adapter now owns its own64-byte workspace;
         * it is not a manual lease and persists until this component returns. */
        uint32_t input[]={0,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,0},output[7],vm_status=0xbad0cafe;
        memset(output,0x65,sizeof(output));
        CHECK(!pilot_io_device_vm(184,memory,input,output,&vm_status) && !vm_status && output[0] && output[1]==8192);
        uint32_t allocation=output[0];MEMORY_BASIC_INFORMATION host;
        CHECK(VirtualQuery(memory+allocation,&host,sizeof(host)) && host.State==MEM_COMMIT && host.Protect==PAGE_READWRITE);
        MEM32(allocation)=0x13572468;MEM32(allocation+4096)=0x24681357;
        CHECK(pilot_io_device_thread_component_workspace_release(frame-32,64));
        CHECK(component_wait_open(&wait) && !memcmp(&wait,&empty,sizeof(wait)));
        CHECK(MEM32(allocation)==0x13572468 && MEM32(allocation+4096)==0x24681357);
        uint32_t query[]={allocation,0,0,0,0};
        CHECK(!pilot_io_device_vm(217,memory,query,output,&vm_status) && !vm_status);
        CHECK(output[0]==allocation && output[1]==allocation && output[4]==MEM_COMMIT && output[5]==PAGE_READWRITE);
        uint32_t release[]={allocation,0,MEM_RELEASE,0,0};
        CHECK(!pilot_io_device_vm(199,memory,release,output,&vm_status) && !vm_status && output[0]==allocation && output[1]==8192);
        CHECK(VirtualQuery(memory+allocation,&host,sizeof(host)) && host.State==MEM_RESERVE);
        CHECK(!pilot_io_device_vm(217,memory,query,output,&vm_status) && !vm_status && output[4]==MEM_FREE);
        ++component_wait_vm_lives;
    }
    regs[0]=0x76543210;regs[4]+=4;return NULL;
}
static void test_thread_component_wait_lle(int workspace_guard)
{
    component_wait_workspace_guard=workspace_guard;
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    pilot_io_device_port_bind(0x80cf,0x80cf,test_component_wait_board_port);
    struct ComponentWait empty={0},before=empty;
    CHECK(component_wait_open(&empty) && !memcmp(&empty,&before,sizeof(empty)));
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};
    memcpy(memory+THREAD_COMPONENT_WAIT_HELPER,helper,sizeof(helper));char identity[65];test_component_hash(helper,sizeof(helper),identity);
    CHECK(!pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_WAIT_HELPER,sizeof(helper),identity,0,test_component_wait_body));
    struct ThreadChildCode code={0},*c=&code;
    COMPONENT_WAIT_CODE(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68);
    child_call(c,125);COMPONENT_WAIT_CODE(c,0x89,0x47,80,0x89,0x57,84,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8,
        0x89,0xfa,0xb8);child_word(c,THREAD_COMPONENT_WAIT_HELPER);
    COMPONENT_WAIT_CODE(c,0xff,0xd0,0x89,0x47,4,0x9c,0x5a,0x89,0x57,12);
    child_call(c,125);COMPONENT_WAIT_CODE(c,0x89,0x47,88,0x89,0x57,92,
        0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x5f,108,0x89,0x6f,112,0x89,0x77,116,
        0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    memcpy(memory+THREAD_COMPONENT_WAIT_CPU,c->bytes,c->size);
    /* Authored integer loop executes on the same sole original CPU. There
     * are no imported calls, host yields or status stand-ins in the loop.
     * Live GPR/x87/XMM/ESP must survive genuine PIT-driven preemption. */
    const uint8_t peer_code[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xb9,0xdf,0x9b,0x57,0x13,0xba,0xe0,0xac,0x68,0x24,
        0xbb,0x98,0xba,0xdc,0xfe,0xbd,0x21,0x43,0x65,0x87,0xbe,0x78,0x56,0x34,0x12,
        0x83,0x3f,0,0x75,5,0xff,0x47,4,0xeb,0xf6,
        0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x4f,76,0x89,0x57,80,0x89,0x5f,84,0x89,0x6f,88,0x89,0x77,92,
        0xc7,0x47,8,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+THREAD_COMPONENT_WAIT_PEER,peer_code,sizeof(peer_code));
#ifdef PILOT_MEMORY_LLE
    CHECK(!pilot_io_device_resident(0x10000,0x10000));
#endif
    CHECK(!pilot_io_device_clock_bound());
    if(workspace_guard==2) {
        uint32_t command=0x34,low=0x65,high=4;
        CHECK(!pilot_usb_pic_port(0x43,1,1,&command));
        CHECK(!pilot_usb_pic_port(0x40,1,1,&low));CHECK(!pilot_usb_pic_port(0x40,1,1,&high));
    } else {CHECK(!pilot_io_device_clock_start());CHECK(pilot_io_device_clock_bound());}
    CHECK(!pilot_io_device_work_bind(test_component_wait_work));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    uint32_t captured_query[]={0x10,0x19880};MEM32(0x19880)=MEM32(0x19884)=0xabcdef12;
    CHECK(event_lle_call(209,captured_query)==0xc0000024u && MEM32(0x19880)==0xabcdef12 && MEM32(0x19884)==0xabcdef12);
    uint32_t baseline_memory[9],after_memory[9];test_memory_snapshot(baseline_memory);
    for(unsigned life=0;life<3;++life) {
        uint32_t context=0x1b000,out=0x19800;memset(memory+context,0,192);
        MEM32(context+64)=THREAD_COMPONENT_WAIT_CPU;MEM32(context+96)=0xa5a55a5a;
        const uint32_t durations[]={10001,330000,100000};MEM32(context+100)=durations[life];
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7);
        double expected=12345.6789;memcpy(memory+context+48,&expected,8);
        unsigned idle_before=component_wait_idle_samples;
        unsigned blocked_before=component_wait_private_wait_samples;
        unsigned peer_before=component_wait_peer_samples,progress_before=component_wait_peer_progress_samples;
        uint32_t peer_handle=0,peer_object=0;
        if(!workspace_guard) {
            uint32_t peer=COMPONENT_WAIT_PEER_CONTEXT;memset(memory+peer,0,128);
            MEM32(peer+12)=0xa5a55a5a;MEM32(peer+64)=THREAD_COMPONENT_WAIT_PEER;
            for(unsigned i=0;i<16;++i)MEM8(peer+16+i)=(uint8_t)(0x7d+i*3);
            memcpy(memory+peer+48,&expected,8);
            uint32_t peer_create[]={out+16,0,THREAD_COMPONENT_WAIT_PEER,peer,0};CHECK(!event_lle_call(254,peer_create));
            peer_handle=MEM32(out+16);
            uint32_t peer_ref[]={peer_handle,0,out+20};CHECK(!event_lle_call(246,peer_ref));peer_object=MEM32(out+20);
            uint32_t peer_priority[]={peer_object,29};CHECK(event_lle_call(148,peer_priority)==8);
            component_wait_peer_thread=peer_object;component_wait_peer_last_counter=0;
        }
        uint32_t create[]={out,0,THREAD_COMPONENT_WAIT_CPU,context,0};CHECK(!event_lle_call(254,create));uint32_t handle=MEM32(out);
        uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);MEM32(context)=object;
        uint32_t priority[]={object,workspace_guard?29u:30u};CHECK(event_lle_call(148,priority)==8);
        uint32_t join[]={object,0,0,0,0};CHECK(!event_lle_call(159,join));
        if(!workspace_guard) {
            uint32_t peer=COMPONENT_WAIT_PEER_CONTEXT;
            CHECK(!MEM32(peer) && MEM32(peer+4) && !MEM32(peer+8));
            CHECK(component_wait_peer_samples>peer_before && component_wait_peer_progress_samples>progress_before);
            MEM32(peer)=1;
            uint32_t peer_join[]={peer_object,0,0,0,0};CHECK(!event_lle_call(159,peer_join));
            CHECK(!event_lle_call(187,&peer_handle));event_lle_call(250,&peer_object);
            CHECK(event_lle_call(187,&peer_handle)==0xc0000008u);
            CHECK(MEM32(peer+8)==1 && MEM32(peer+12)==0xa5a55a5a && MEM32(peer+68)==MEM32(peer+72));
            CHECK(MEM32(peer+76)==0x13579bdf && MEM32(peer+80)==0x2468ace0 && MEM32(peer+84)==0xfedcba98 &&
                  MEM32(peer+88)==0x87654321 && MEM32(peer+92)==0x12345678);
            CHECK(!memcmp(memory+peer+16,memory+peer+32,16) && !memcmp(memory+peer+48,memory+peer+56,8));
            CHECK(!memcmp(memory+THREAD_COMPONENT_WAIT_PEER,peer_code,sizeof(peer_code)));
            component_wait_peer_thread=0;
        }
        CHECK(!event_lle_call(187,&handle));event_lle_call(250,&object);
        CHECK(event_lle_call(187,&handle)==0xc0000008u);
        CHECK(MEM32(context+4)==0x76543210 && MEM32(context+76)==1 && MEM32(context+96)==0xa5a55a5a);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(MEM32(context+108)==0x11223344 && MEM32(context+112)==0x44556677 && MEM32(context+116)==0x22334455);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(!memcmp(memory+THREAD_COMPONENT_WAIT_HELPER,helper,sizeof(helper)));
        if(!workspace_guard) {
            uint64_t elapsed=MEM64(context+88)-MEM64(context+80);
            CHECK(elapsed>=330000+1+durations[life]+100000 && component_wait_private_wait_samples>blocked_before);
            uint32_t query[]={MEM32(context+124),out+8};MEM32(out+8)=MEM32(out+12)=0xabcdef12;
            CHECK(event_lle_call(209,query)==0xc0000008u && MEM32(out+8)==0xabcdef12 && MEM32(out+12)==0xabcdef12);
            fprintf(stderr,"[COMPONENT WAIT] life=%u elapsed100ns=%llu private_event_waiting_samples=%u peer_running_samples=%u peer_counter_progress_samples=%u idle_samples=%u timeouts=4 events=closed VM=allocated/queried/freed peer=stopped/joined/closed/retired\n",
                life,(unsigned long long)elapsed,component_wait_private_wait_samples-blocked_before,component_wait_peer_samples-peer_before,
                component_wait_peer_progress_samples-progress_before,component_wait_idle_samples-idle_before);
        }
        CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==baseline_threads);
        CHECK(MEM32(stats+8)==baseline_handles);
        /* Actual209 rejects the initial IoFileObjectType before touching its
         * output. Its continued mismatch proves the captured launch task
         * did not retire that unrelated handle during this controlled wait.
         * No new diagnostic reference or object publication is required. */
        MEM32(0x19880)=MEM32(0x19884)=0xabcdef12;
        CHECK(event_lle_call(209,captured_query)==0xc0000024u &&
              MEM32(0x19880)==0xabcdef12 && MEM32(0x19884)==0xabcdef12);
        test_memory_snapshot(after_memory);CHECK(!memcmp(baseline_memory,after_memory,sizeof(baseline_memory)));
    }
    CHECK(!pilot_io_device_work_bind(NULL));CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    CHECK(component_wait_calls==3 && component_wait_timeouts==(workspace_guard?0u:12u));
    CHECK(component_wait_vm_lives==(workspace_guard?0u:3u));
#ifdef PILOT_SCHEDULING_LLE
    CHECK(!thread_cpu_calls);
#else
    CHECK(thread_cpu_calls==(workspace_guard?3u:6u));
#endif
    fprintf(stderr,"PASS: three actual original component lifetimes; %s, passive/DPC/original-IRQL/range/owner guards, ESP/GPR/flags/x87/XMM and real memory/thread/event/handle cleanup\n",
        workspace_guard==2?"programmed PIT without a bound producer rejects before stack/event mutation":
        workspace_guard?"occupied original stack lease rejects before event creation":"twelve private-event timeouts with actual KTHREAD/private-event blocked dispatcher, sole-CPU compute-peer IRQ preemption and exact cleanup, lease release/reopen and wait-to-VM allocation/query/free");
}
#undef COMPONENT_WAIT_CODE
