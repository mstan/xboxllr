/* Authored generated callback bodies exercise the actual original KTIMER/KDPC
 * transport. The separately frozen title callback disassembly establishes its
 * cdecl ABI; this fixture does not substitute these authored bodies for that
 * original code or qualify an arbitrary callback's stack/spill behavior. */
#define GRAPHICS_CALLBACK_CPU 0x18100u
#define GRAPHICS_CALLBACK_HELPER 0x18f80u
#define GRAPHICS_CALLBACK_VBLANK_A 0x17a00u
#define GRAPHICS_CALLBACK_VBLANK_B 0x17a40u
#define GRAPHICS_CALLBACK_GPU 0x17a80u
#define GRAPHICS_CALLBACK_CONTEXT 0x1bc00u
#define GRAPHICS_CALLBACK_PEER_CONTEXT 0x1bb00u
#define GRAPHICS_CALLBACK_CODE(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
enum { GRAPHICS_CALLBACK_COUNT=8,GRAPHICS_CALLBACK_SLICE=10000 };
static uint32_t graphics_callback_thread,graphics_callback_peer,graphics_callback_peer_counter;
static unsigned graphics_callback_body_calls[5],graphics_callback_busy_samples;
static unsigned graphics_callback_wait_samples,graphics_callback_peer_progress,graphics_callback_dpc_samples;
static uint64_t graphics_callback_first_tick,graphics_callback_last_tick;

static const char *test_graphics_callback_work(void)
{
    const char *error=graphics_original_clock_work();if(error)return error;
    if(!graphics_callback_thread)return NULL;
    if(!TryAcquireSRWLockExclusive(&graphics_lock))return NULL;
    if(!graphics_gpu_running && graphics_gpu_head && graphics_gpu_head->type && pilot_gfx_checkpoint_busy(graphics_gpu_head->serial)) {
        /* Observe the actual WRITE at the FIFO head. This query neither
         * forces busy nor marks a GPU serial complete. Later READs stay put. */
        CHECK(graphics_gpu_head->context==MEM32(GRAPHICS_CALLBACK_CONTEXT+168)+1);
        if(graphics_gpu_head->context==1)++graphics_callback_busy_samples;
    }
    ReleaseSRWLockExclusive(&graphics_lock);
    if(!MEM32(GRAPHICS_CALLBACK_CONTEXT+128))return NULL;
    uint64_t ticks=0,next=0;CHECK(!pilot_usb_pit_clock(&ticks,&next) && next);
    CHECK(graphics_clock_now()-MEM64(GRAPHICS_CALLBACK_CONTEXT+152)<20000000);
    if(!graphics_callback_first_tick)graphics_callback_first_tick=ticks;
    CHECK(ticks>=graphics_callback_last_tick);graphics_callback_last_tick=ticks;
    unsigned char waiter[0x140],peer[0x140];
    if(!component_wait_thread_snapshot(graphics_callback_thread,waiter) || waiter[0x2c]!=5)return NULL;
    uint32_t current=MEM32(0x8003da64u);
    CHECK(current==graphics_callback_peer && component_wait_thread_snapshot(current,peer));
    CHECK(peer[0x2c]==2 && peer[0x32]==29 && component_wait_snapshot_word(peer,0x130)==THREAD_COMPONENT_WAIT_PEER);
    uint32_t frame=MEM32(GRAPHICS_CALLBACK_CONTEXT+136),bottom=component_wait_snapshot_word(waiter,0x20),top=component_wait_snapshot_word(waiter,0x1c);
    CHECK(frame>=bottom+68 && (uint64_t)frame+COMPONENT_WAIT_BYTES<=top);
    uint32_t event=0,caller[5],resolved=0,block[6];
    CHECK(component_wait_owned_bytes(frame,&event,4) && event);
    CHECK(MEM32(frame+4)==0x13579bdf && MEM32(frame+24)==0x2468ace0 && MEM32(frame+28)==0x89abcdef);
    CHECK(component_wait_owned_bytes(frame-36,caller,sizeof(caller)) && caller[0]==0x800249f1u && caller[1]==event &&
          !caller[2] && !caller[3] && caller[4]==frame+8);
    CHECK(component_wait_owned_bytes(frame-68,&resolved,4) && resolved);
    uint32_t wait_block=component_wait_snapshot_word(waiter,0x58);
    CHECK(wait_block>=bottom && (uint64_t)wait_block+sizeof(block)<=top);
    CHECK(component_wait_owned_bytes(wait_block,block,sizeof(block)) && block[2]==graphics_callback_thread && block[3]==resolved && waiter[0x57]==6);
    /* Its private event body is deliberately unpublished. Actual209 queries
     * inside component_wait establish synchronization type1/state0, and the
     * original wait block proves the actual caller/reference identity. */
    CHECK(TryAcquireSRWLockExclusive(&graphics_lock));CHECK(TryAcquireSRWLockExclusive(&vm_lock));
    CHECK(graphics_original_gate && graphics_original_gate==native_component_original_gate);
    CHECK(MEM32(graphics_original_gate+20)==1 && MEM32(graphics_original_gate+24)==graphics_callback_thread);
    ReleaseSRWLockExclusive(&vm_lock);ReleaseSRWLockExclusive(&graphics_lock);
    uint32_t counter=MEM32(GRAPHICS_CALLBACK_PEER_CONTEXT+4);CHECK(counter>=graphics_callback_peer_counter);
    if(counter>graphics_callback_peer_counter)++graphics_callback_peer_progress;
    graphics_callback_peer_counter=counter;++graphics_callback_wait_samples;return NULL;
}

static uint32_t test_graphics_callback_witness(void)
{
    uint32_t sp=g_esp,owner=graphics_original_callback_owner,outer=sp+8;
    CHECK(owner && test_pool_owned(owner) && graphics_original_callback_armed);
    CHECK(dispatch_in_dpc && usb_device_callback_depth && pilot_io_device_irql()==2 && MEM8(g_fs_base+0x24)==2 && !g_df);
    CHECK(MEM32(sp)==PILOT_IO_GRAPHICS_DPC_ENTRY && MEM32(outer+4)==owner+40 && MEM32(outer+8)==owner);
    CHECK(MEM8(owner)==8 && MEM32(owner+32)==owner+40 && MEM32(owner+36)==1);
    CHECK(MEM16(owner+40)==0x13 && !MEM8(owner+42) && MEM32(owner+52)==PILOT_IO_GRAPHICS_DPC_ENTRY && MEM32(owner+56)==owner);
    CHECK(MEM32(owner+68)==GRAPHICS_ORIGINAL_CALLBACK_MAGIC);
    uint32_t thread=MEM32(0x8003da64u),top=MEM32(0x8003da98u),bottom=top-0x3000u;
    if(outer<bottom || (uint64_t)outer+20>top) {bottom=MEM32(thread+0x20);top=MEM32(thread+0x1c);}
    CHECK(sp>=bottom && (uint64_t)outer+20<=top);
    CHECK(callback_kernel_lle_call(121,NULL,0)!=0 && (callback_kernel_lle_call(103,NULL,0)&255)==2);
    CHECK(TryAcquireSRWLockExclusive(&graphics_lock));CHECK(TryAcquireSRWLockExclusive(&vm_lock));
    ReleaseSRWLockExclusive(&vm_lock);ReleaseSRWLockExclusive(&graphics_lock);
    ++graphics_callback_dpc_samples;return MEM32(sp+4);
}
static void test_graphics_callback_return(void)
{
    /* Real interrupted raw CPU/compute-peer state must survive these generated
     * volatile register/FP clobbers. The cdecl callback preserves nonvolatiles. */
    g_eax=0xaabbccdd;g_ecx=0x11223344;g_edx=0x55667788;
    g_fp_top=6;g_fp_stack[0]=-7.5;memset(&g_xmm0,0x12,sizeof(g_xmm0));g_esp+=4;
}
static void test_graphics_callback_vblank(unsigned which)
{
    uint32_t data=test_graphics_callback_witness(),context=GRAPHICS_CALLBACK_CONTEXT;
    CHECK(which<=1 && data==graphics_headers+96);
    CHECK(graphics_callback==(which?GRAPHICS_CALLBACK_VBLANK_B:GRAPHICS_CALLBACK_VBLANK_A));
    uint32_t frame=MEM32(data),swap=MEM32(data+4),flags=MEM32(data+8),count=160+which*4,last=172+which*4;
    CHECK(frame>MEM32(context+last) && frame==graphics_original_callback_frame && frame<=graphics_original_pulse_frame);
    CHECK(swap==graphics_swap_completed && flags<=2 && flags==graphics_original_pulse_flags);
    MEM32(context+last)=frame;++MEM32(context+count);test_graphics_callback_return();
}
static void test_graphics_callback_vblank_a(void){test_graphics_callback_vblank(0);}
static void test_graphics_callback_vblank_b(void){test_graphics_callback_vblank(1);}
static void test_graphics_callback_gpu(void)
{
    uint32_t argument=test_graphics_callback_witness(),context=GRAPHICS_CALLBACK_CONTEXT;
    CHECK(argument>=1 && argument<=GRAPHICS_CALLBACK_COUNT && argument==MEM32(context+168)+1);
    CHECK(graphics_gpu_running);
    if(argument==1 || !(argument&1)) {
        struct PilotGpuProgress progress;pilot_gfx_completion_stats(&progress);
        CHECK(progress.completed>=MEM64(context+144));
    }
    MEM32(context+256+(argument-1)*4)=argument;++MEM32(context+168);test_graphics_callback_return();
}

static const char *test_graphics_callback_body(uint32_t entry,uint32_t regs[8])
{
    uint32_t context=GRAPHICS_CALLBACK_CONTEXT;unsigned phase=regs[1];
    CHECK(entry==GRAPHICS_CALLBACK_HELPER && pilot_io_device_thread_component_active() && phase<5 && regs[2]==context);
    CHECK(regs[3]==0x11223344 && regs[5]==0x44556677 && regs[6]==0x22334455 && regs[7]==context);
    CHECK(MEM32(context)==MEM32(0x8003da64u));++graphics_callback_body_calls[phase];
    const char *error=graphics_original_enter();if(error)return error;
    AcquireSRWLockExclusive(&graphics_lock);
    CHECK(graphics_active && !dispatch_changed && graphics_original_clock_bound);
    if(!phase) {
        CHECK(graphics_callback==GRAPHICS_CALLBACK_VBLANK_A && graphics_original_callback_owner && graphics_original_callback_armed);
        CHECK(!graphics_gpu_head && !graphics_gpu_running && !graphics_fence_work_texture);
        CHECK(!pilot_gfx_texture_create_usage(GRAPHICS_FENCE_WORK_SIDE,GRAPHICS_FENCE_WORK_SIDE,1,0x12,1,&graphics_fence_work_texture));
        struct PilotGpuProgress before,after;pilot_gfx_completion_stats(&before);
        graphics_fence_owned_work();pilot_gfx_completion_stats(&after);
        CHECK(after.submitted==before.submitted+1+GRAPHICS_FENCE_WORK_DRAWS);
        CHECK(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,graphics_fence_work_texture)==after.submitted);
        MEM64(context+144)=after.submitted;MEM32(context+200)=graphics_original_callback_owner;
    } else {
        MEM32(context+128)=1;MEM32(context+132)=phase;MEM32(context+136)=regs[4]-COMPONENT_WAIT_BYTES;
        MEM64(context+152)=graphics_clock_now();
        if(phase==1) {
            graphics_gpu_barrier();CHECK(MEM32(context+168)==GRAPHICS_CALLBACK_COUNT && !graphics_gpu_head && !graphics_gpu_running);
            graphics_fence_owned_pixels(MEM64(context+144));
            pilot_gfx_texture_destroy(graphics_fence_work_texture);graphics_fence_work_texture=0;
        } else if(phase==2) {
            CHECK(graphics_callback==GRAPHICS_CALLBACK_VBLANK_B && MEM32(context+160));
            MEM32(context+184)=MEM32(context+160);
        } else if(phase==3) {
            CHECK(!graphics_callback && !graphics_gpu_head && !graphics_gpu_running);
            CHECK(!graphics_original_callback_owner && !graphics_original_callback_armed && !test_pool_owned(MEM32(context+200)));
            MEM32(context+188)=MEM32(context+164);CHECK(MEM32(context+188)>=2);
        } else {
            CHECK(!graphics_callback && !graphics_gpu_head && !graphics_gpu_running && !graphics_original_callback_owner && !graphics_original_callback_armed);
        }
        ReleaseSRWLockExclusive(&graphics_lock);
        struct ComponentWait wait={0};uint32_t status=0;
        /* A registration replacement must receive two genuine later edges;
         * removal is followed by a real33ms original wait, beyond an edge and
         * several timer expiries. No host sleep or caller-ready publication. */
        while(phase==1?!MEM32(context+160):phase==2?MEM32(context+164)<2:phase==3?graphics_clock_now()-MEM64(context+152)<330000:0) {
            CHECK(graphics_clock_now()-MEM64(context+152)<20000000);
            if(!wait.event){error=component_wait_open(&wait);if(error)return error;}
            error=component_wait_slice(&wait,GRAPHICS_CALLBACK_SLICE,&status);if(error)return error;
            CHECK(status==0x102u);
        }
        if(wait.event){error=component_wait_close(&wait);if(error)return error;}
        AcquireSRWLockExclusive(&graphics_lock);MEM32(context+128)=0;
        if(phase>=2)CHECK(MEM32(context+160)==MEM32(context+184));
        if(phase>=3)CHECK(MEM32(context+164)==MEM32(context+188));
    }
    ReleaseSRWLockExclusive(&graphics_lock);
    error=graphics_original_leave();if(error)return error;
    regs[0]=0x76543210;regs[4]+=4;return NULL;
}
static void graphics_callback_call(struct ThreadChildCode *c,unsigned entry)
{GRAPHICS_CALLBACK_CODE(c,0xb8);child_word(c,pilot_d3d_calls[entry].address);GRAPHICS_CALLBACK_CODE(c,0xff,0xd0);}
static void graphics_callback_check(struct ThreadChildCode *c,unsigned phase)
{
    GRAPHICS_CALLBACK_CODE(c,0xb9);child_word(c,phase);
    GRAPHICS_CALLBACK_CODE(c,0x89,0xfa,0xb8);child_word(c,GRAPHICS_CALLBACK_HELPER);GRAPHICS_CALLBACK_CODE(c,0xff,0xd0);
}
static void test_thread_graphics_callback_lle(const char *path)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));vm_ensure();
    pilot_io_device_port_bind(0x80cf,0x80cf,test_graphics_fence_board_port);
    uint32_t page=PILOT_D3D_DEVICE_GLOBAL&~4095u;
    uint32_t bytes=((PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET+4+4095)&~4095u)-page;
    commit(page,bytes);vm_fixed_region(page,bytes);test_graphics_component_sources(path);
    CHECK(pilot_d3d_calls[PILOT_D3D_D3DDevice_SetVerticalBlankCallback].stack_bytes==4 &&
          pilot_d3d_calls[PILOT_D3D_D3DDevice_InsertCallback].stack_bytes==12);
    const uint32_t parameters[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};
    memcpy(memory+0x1a000,parameters,sizeof(parameters));uint32_t create[]={0,1,0,0x40,0x1a000,0x1a080};
    CHECK(!call_graphics(PILOT_D3D_Direct3D_CreateDevice,create) && graphics_active);
    const uint8_t helper[]={0xb8,0x10,0x32,0x54,0x76,0xc3};char identity[65];
    memcpy(memory+GRAPHICS_CALLBACK_HELPER,helper,sizeof(helper));test_component_hash(helper,sizeof(helper),identity);
    CHECK(!pilot_io_device_thread_component_bind_abi(GRAPHICS_CALLBACK_HELPER,sizeof(helper),identity,0,test_graphics_callback_body));
    /* Authored cdecl1 bodies are bound by the test lookup at the reserved
     * addresses; RET0 bytes record the calling shape, not original title code. */
    MEM8(GRAPHICS_CALLBACK_VBLANK_A)=MEM8(GRAPHICS_CALLBACK_VBLANK_B)=MEM8(GRAPHICS_CALLBACK_GPU)=0xc3;
    struct ThreadChildCode code={0},*c=&code;
    GRAPHICS_CALLBACK_CODE(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22);
    child_call(c,125);GRAPHICS_CALLBACK_CODE(c,0x89,0x47,88,0x89,0x57,92,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8);
    child_push(c,GRAPHICS_CALLBACK_VBLANK_A);graphics_callback_call(c,PILOT_D3D_D3DDevice_SetVerticalBlankCallback);
    graphics_callback_check(c,0);
    for(unsigned i=1;i<=GRAPHICS_CALLBACK_COUNT;++i) {
        child_push(c,i);child_push(c,GRAPHICS_CALLBACK_GPU);child_push(c,i==1 || !(i&1));
        graphics_callback_call(c,PILOT_D3D_D3DDevice_InsertCallback);
    }
    graphics_callback_check(c,1);
    child_push(c,GRAPHICS_CALLBACK_VBLANK_B);graphics_callback_call(c,PILOT_D3D_D3DDevice_SetVerticalBlankCallback);
    graphics_callback_check(c,2);
    child_push(c,0);graphics_callback_call(c,PILOT_D3D_D3DDevice_SetVerticalBlankCallback);
    graphics_callback_check(c,3);graphics_callback_check(c,4);
    GRAPHICS_CALLBACK_CODE(c,0x89,0x47,4,0x9c,0x5a,0x89,0x57,12);
    child_call(c,125);GRAPHICS_CALLBACK_CODE(c,0x89,0x87,240,0,0,0,0x89,0x97,244,0,0,0,
        0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x5f,112,0x89,0x6f,116,0x89,0x77,120,
        0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<512 && c->size<GRAPHICS_CALLBACK_HELPER-GRAPHICS_CALLBACK_CPU);
    memcpy(memory+GRAPHICS_CALLBACK_CPU,c->bytes,c->size);
    const uint8_t peer_code[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xb9,0xdf,0x9b,0x57,0x13,0xba,0xe0,0xac,0x68,0x24,
        0xbb,0x98,0xba,0xdc,0xfe,0xbd,0x21,0x43,0x65,0x87,0xbe,0x78,0x56,0x34,0x12,
        0x83,0x3f,0,0x75,5,0xff,0x47,4,0xeb,0xf6,
        0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x4f,76,0x89,0x57,80,0x89,0x5f,84,0x89,0x6f,88,0x89,0x77,92,
        0xc7,0x47,8,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+THREAD_COMPONENT_WAIT_PEER,peer_code,sizeof(peer_code));
    CHECK(!pilot_io_device_resident(0x10000,0x10000));CHECK(!pilot_io_device_clock_start());CHECK(!graphics_original_clock_bind());
    CHECK(!pilot_io_device_work_bind(NULL));CHECK(!pilot_io_device_work_bind(test_graphics_callback_work));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8),baseline[9],after[9];test_memory_snapshot(baseline);
    uint32_t captured_query[]={0x10,0x19880};
    for(unsigned life=0;life<3;++life) {
        uint32_t context=GRAPHICS_CALLBACK_CONTEXT,peer=GRAPHICS_CALLBACK_PEER_CONTEXT,out=0x19800;
        memset(memory+context,0,512);memset(memory+peer,0,128);MEM32(context+64)=GRAPHICS_CALLBACK_CPU;MEM32(peer+64)=THREAD_COMPONENT_WAIT_PEER;
        MEM32(context+96)=MEM32(peer+12)=0xa5a55a5a;
        for(unsigned i=0;i<16;++i){MEM8(context+16+i)=(uint8_t)(0x35+i*7);MEM8(peer+16+i)=(uint8_t)(0x7d+i*3);}
        double fp=12345.6789;memcpy(memory+context+48,&fp,8);memcpy(memory+peer+48,&fp,8);
        unsigned busy=graphics_callback_busy_samples,waiting=graphics_callback_wait_samples,progress=graphics_callback_peer_progress,dpc=graphics_callback_dpc_samples;
        uint64_t gpu_callbacks=graphics_gpu_delivered,vblank_callbacks=graphics_original_vblank_delivered;
        graphics_callback_peer_counter=0;
        MEM32(0x19880)=MEM32(0x19884)=0xabcdef12;
        CHECK(event_lle_call(209,captured_query)==0xc0000024u && MEM32(0x19880)==0xabcdef12 && MEM32(0x19884)==0xabcdef12);
        uint32_t spawn[]={out+16,0,THREAD_COMPONENT_WAIT_PEER,peer,0};CHECK(!event_lle_call(254,spawn));uint32_t peer_handle=MEM32(out+16);
        uint32_t ref[]={peer_handle,0,out+20};CHECK(!event_lle_call(246,ref));graphics_callback_peer=MEM32(out+20);
        uint32_t priority[]={graphics_callback_peer,29};CHECK(event_lle_call(148,priority)==8);
        spawn[0]=out;spawn[2]=GRAPHICS_CALLBACK_CPU;spawn[3]=context;CHECK(!event_lle_call(254,spawn));uint32_t handle=MEM32(out);
        ref[0]=handle;ref[2]=out+4;CHECK(!event_lle_call(246,ref));graphics_callback_thread=MEM32(out+4);MEM32(context)=graphics_callback_thread;
        priority[0]=graphics_callback_thread;priority[1]=30;CHECK(event_lle_call(148,priority)==8);
        uint32_t join[]={graphics_callback_thread,0,0,0,0};CHECK(!event_lle_call(159,join));
        MEM32(peer)=1;join[0]=graphics_callback_peer;CHECK(!event_lle_call(159,join));
        CHECK(!event_lle_call(187,&handle));event_lle_call(250,&graphics_callback_thread);CHECK(event_lle_call(187,&handle)==0xc0000008u);
        CHECK(!event_lle_call(187,&peer_handle));event_lle_call(250,&graphics_callback_peer);CHECK(event_lle_call(187,&peer_handle)==0xc0000008u);
        graphics_callback_thread=graphics_callback_peer=0;
        CHECK(MEM32(context+4)==0x76543210 && MEM32(context+76)==1 && MEM32(context+96)==0xa5a55a5a);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(MEM32(context+112)==0x11223344 && MEM32(context+116)==0x44556677 && MEM32(context+120)==0x22334455);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(MEM32(peer+8)==1 && MEM32(peer+4) && MEM32(peer+12)==0xa5a55a5a && MEM32(peer+68)==MEM32(peer+72));
        CHECK(MEM32(peer+76)==0x13579bdf && MEM32(peer+80)==0x2468ace0 && MEM32(peer+84)==0xfedcba98 && MEM32(peer+88)==0x87654321 && MEM32(peer+92)==0x12345678);
        CHECK(!memcmp(memory+peer+16,memory+peer+32,16) && !memcmp(memory+peer+48,memory+peer+56,8));
        CHECK(MEM32(context+160) && MEM32(context+164)>=2 && MEM32(context+168)==GRAPHICS_CALLBACK_COUNT);
        CHECK(MEM32(context+160)==MEM32(context+184) && MEM32(context+164)==MEM32(context+188));
        for(unsigned i=1;i<=GRAPHICS_CALLBACK_COUNT;++i)CHECK(MEM32(context+256+(i-1)*4)==i);
        CHECK(graphics_gpu_delivered==gpu_callbacks+GRAPHICS_CALLBACK_COUNT && graphics_original_vblank_delivered==vblank_callbacks+MEM32(context+160)+MEM32(context+164));
        CHECK(!graphics_gpu_head && !graphics_gpu_tail && !graphics_gpu_running && !graphics_callback && !graphics_clock_waiters && !graphics_fence_work_texture);
        CHECK(!graphics_original_callback_owner && !graphics_original_callback_armed && !test_pool_owned(MEM32(context+200)));
        CHECK(MEM32(native_component_original_gate+16)==UINT32_MAX && !MEM32(native_component_original_gate+20) && !MEM32(native_component_original_gate+24));
        CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
        MEM32(0x19880)=MEM32(0x19884)=0xabcdef12;
        CHECK(event_lle_call(209,captured_query)==0xc0000024u && MEM32(0x19880)==0xabcdef12 && MEM32(0x19884)==0xabcdef12);
        test_memory_snapshot(after);CHECK(!memcmp(baseline,after,sizeof(baseline)));
        CHECK(!memcmp(memory+GRAPHICS_CALLBACK_CPU,c->bytes,c->size) && !memcmp(memory+GRAPHICS_CALLBACK_HELPER,helper,sizeof(helper)) &&
              !memcmp(memory+THREAD_COMPONENT_WAIT_PEER,peer_code,sizeof(peer_code)));
        CHECK(graphics_callback_wait_samples>waiting && graphics_callback_peer_progress>progress && graphics_callback_dpc_samples>dpc);
        CHECK(MEM64(context+240)>MEM64(context+88));
        if(graphics_callback_busy_samples==busy)fprintf(stderr,"OBLIGATION: finite owned64-pass GPU workload yielded no actual pending WRITE head; no forced busy or workload escalation\n");
        CHECK(graphics_callback_busy_samples>busy);
        fprintf(stderr,"[ORIGINAL GRAPHICS CALLBACK] life=%u WRITE_busy=%u private_KWAIT=%u peer_progress=%u actual_DPC/cdecl=%u FIFO=8 vblank_A/B=%u/%u exact_GPU_bytes=16777216 owner/event/thread/handle/memory=retired\n",
            life,graphics_callback_busy_samples-busy,graphics_callback_wait_samples-waiting,graphics_callback_peer_progress-progress,
            graphics_callback_dpc_samples-dpc,MEM32(context+160),MEM32(context+164));
    }
    for(unsigned i=0;i<5;++i)CHECK(graphics_callback_body_calls[i]==3);
    CHECK(!thread_cpu_calls && !dispatch_changed && graphics_callback_last_tick>graphics_callback_first_tick);
    CHECK(!pilot_io_device_work_bind(NULL));
    CHECK(!call_graphics(PILOT_D3D_D3DDevice_Release,NULL) && !graphics_active && !graphics_original_callback_owner);
    graphics_original_clock_bound=0;
    uint32_t gate=native_component_original_gate;graphics_original_gate=native_component_original_gate=0;pool_free(gate);
    fprintf(stderr,"PASS: three source-bound raw original CPU SetVBlank/InsertCallback lives, actual timer-owned dequeued IRQL2 KDPC/original stack/cdecl1, mandatory real WRITE busy and FIFO READ/WRITE, later-edge replacement/removal, actual private-event KWAIT/compute-peer IRQ0 progress with graphics+VM locks released, exact accumulated GPU bytes and exact original cleanup/GPR/ESP/flags/x87/XMM; authored callback bodies, no title-callback execution claim\n");
}
#undef GRAPHICS_CALLBACK_CODE
