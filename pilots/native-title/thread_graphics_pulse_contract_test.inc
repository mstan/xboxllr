/* Source-bound whole Swap/vblank calls on actual original CPU threads. The
 * only pulse/retirement producer is the original clock work callback; a real
 * integer peer proves dispatcher/IRQ0 progress while native locks are released. */
#define GRAPHICS_PULSE_CPU 0x17800u
#define GRAPHICS_PULSE_HELPER 0x17f80u
#define GRAPHICS_PULSE_CONTEXT 0x1b700u
#define GRAPHICS_PULSE_PEER_CONTEXT 0x1b900u
#define GRAPHICS_PULSE_CODE(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static uint32_t graphics_pulse_thread,graphics_pulse_peer;
static unsigned graphics_pulse_wait_samples[4],graphics_pulse_peer_progress;
static uint32_t graphics_pulse_last_counter;
static const char *test_graphics_pulse_work(void)
{
    const char *error=graphics_original_clock_work();if(error)return error;
    uint32_t phase=MEM32(GRAPHICS_PULSE_CONTEXT+132);
    if(!phase || !graphics_pulse_thread)return NULL;
    CHECK(phase<=3);
    unsigned char waiter[0x140],peer[0x140];
    if(!component_wait_thread_snapshot(graphics_pulse_thread,waiter) || waiter[0x2c]!=5)return NULL;
    uint32_t current=MEM32(0x8003da64u);
    CHECK(current==graphics_pulse_peer && component_wait_thread_snapshot(current,peer));
    CHECK(peer[0x2c]==2 && peer[0x32]==29 && component_wait_snapshot_word(peer,0x130)==THREAD_COMPONENT_WAIT_PEER);
    uint32_t frame=MEM32(GRAPHICS_PULSE_CONTEXT+128),bottom=component_wait_snapshot_word(waiter,0x20);
    CHECK(frame>=bottom+68 && (uint64_t)frame+32<=component_wait_snapshot_word(waiter,0x1c));
    uint32_t event=0,caller[5],block[6];
    CHECK(component_wait_owned_bytes(frame,&event,4) && event);
    CHECK(MEM32(frame+4)==0x13579bdf && MEM32(frame+24)==0x2468ace0 && MEM32(frame+28)==0x89abcdef);
    CHECK(component_wait_owned_bytes(frame-36,caller,sizeof(caller)));
    CHECK(caller[0]==0x800249f1u && caller[1]==event && !caller[2] && !caller[3] && caller[4]==frame+8);
    uint32_t resolved=0;CHECK(component_wait_owned_bytes(frame-68,&resolved,4) && resolved);
    uint32_t wait_block=component_wait_snapshot_word(waiter,0x58);
    CHECK(wait_block>=bottom && (uint64_t)wait_block+sizeof(block)<=component_wait_snapshot_word(waiter,0x1c));
    CHECK(component_wait_owned_bytes(wait_block,block,sizeof(block)));
    CHECK(block[2]==graphics_pulse_thread && block[3]==resolved && waiter[0x57]==6);
    CHECK(TryAcquireSRWLockExclusive(&graphics_lock));CHECK(TryAcquireSRWLockExclusive(&vm_lock));
    if(phase==2) {
        /* Original BlockUntilVerticalBlank takes only the display event.
         * A lower-priority waiter must not block a renderer/audio contender
         * behind a shared gate while a compute thread remains runnable. */
        uint32_t owner=MEM32(graphics_original_gate+24);
        CHECK(!owner || owner==graphics_pulse_peer);
        CHECK(MEM32(graphics_original_gate+20)==(owner?1u:0u));
    } else CHECK(MEM32(graphics_original_gate+20)==1 && MEM32(graphics_original_gate+24)==graphics_pulse_thread);
    CHECK(!dispatch_changed && !dispatch_in_dpc && !graphics_callback && !graphics_gpu_head);
    ReleaseSRWLockExclusive(&vm_lock);ReleaseSRWLockExclusive(&graphics_lock);
    uint32_t counter=MEM32(GRAPHICS_PULSE_PEER_CONTEXT+4);CHECK(counter>=graphics_pulse_last_counter);
    if(counter>graphics_pulse_last_counter)++graphics_pulse_peer_progress;
    graphics_pulse_last_counter=counter;++graphics_pulse_wait_samples[phase];return NULL;
}
static const char *test_graphics_pulse_complete(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==GRAPHICS_PULSE_HELPER && pilot_io_device_thread_component_active());
    CHECK(regs[7]==GRAPHICS_PULSE_CONTEXT);
    const char *error=graphics_original_enter();if(error)return error;
    uint32_t expected=MEM32(GRAPHICS_PULSE_CONTEXT+160),status=0;
    CHECK(expected && graphics_swap_submitted==expected);
    MEM32(GRAPHICS_PULSE_CONTEXT+128)=regs[4]-32;MEM32(GRAPHICS_PULSE_CONTEXT+132)=3;
    uint64_t begin=graphics_clock_now();struct ComponentWait wait={0};
    while(graphics_swap_completed<expected) {
        CHECK(graphics_clock_now()-begin<20000000);
        if(!wait.event){error=component_wait_open(&wait);if(error)return error;}
        error=component_wait_slice(&wait,10000,&status);if(error)return error;
    }
    if(wait.event){error=component_wait_close(&wait);if(error)return error;}
    MEM32(GRAPHICS_PULSE_CONTEXT+132)=0;
    CHECK(!graphics_present_count && !graphics_clock_waiters && !graphics_display_waiters);
    struct PilotGpuProgress progress;pilot_gfx_completion_stats(&progress);
    CHECK(!pilot_gfx_checkpoint_busy(progress.submitted));
    unsigned char pixels[64*64*4];CHECK(!pilot_gfx_surface_readback(1,pixels,sizeof(pixels)));
    for(unsigned i=0;i<64*64;++i)CHECK(!pixels[i*4] && !pixels[i*4+1] && pixels[i*4+2]==255 && pixels[i*4+3]==255);
    MEM32(GRAPHICS_PULSE_CONTEXT+168)=graphics_swap_completed;
    MEM64(GRAPHICS_PULSE_CONTEXT+176)=graphics_clock_now();
    error=graphics_original_leave();if(error)return error;
    regs[0]=0x76543210;regs[4]+=4;return NULL;
}
static void graphics_pulse_phase(struct ThreadChildCode *c,unsigned phase)
{
    GRAPHICS_PULSE_CODE(c,0x89,0xa7);child_word(c,128);
    GRAPHICS_PULSE_CODE(c,0x83,0xaf);child_word(c,128);GRAPHICS_PULSE_CODE(c,36);
    /* SUB above intentionally changes flags before the real call; restore
     * the original authored flags from context before testing API preservation. */
    GRAPHICS_PULSE_CODE(c,0xff,0x77,8,0x9d,0xc7,0x87);child_word(c,132);child_word(c,phase);
}
static void graphics_pulse_call(struct ThreadChildCode *c,unsigned index)
{GRAPHICS_PULSE_CODE(c,0xb8);child_word(c,pilot_d3d_calls[index].address);GRAPHICS_PULSE_CODE(c,0xff,0xd0);}
static void graphics_pulse_clear(struct ThreadChildCode *c,uint32_t color)
{
    const uint32_t args[]={0,0,0xf0,color,0x3f800000,0};
    for(unsigned i=6;i;--i){GRAPHICS_PULSE_CODE(c,0x68);child_word(c,args[i-1]);}
    graphics_pulse_call(c,PILOT_D3D_D3DDevice_Clear);
}
static void test_thread_graphics_pulse_lle(const char *path)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000,0,&revision));vm_ensure();
    uint32_t page=PILOT_D3D_DEVICE_GLOBAL&~4095u;
    uint32_t bytes=((PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET+4+4095)&~4095u)-page;
    commit(page,bytes);vm_fixed_region(page,bytes);test_graphics_component_sources(path);
    const uint32_t parameters[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};
    memcpy(memory+0x1a000,parameters,sizeof(parameters));uint32_t create[]={0,1,0,0x40,0x1a000,0x1a080};
    CHECK(!call_graphics(PILOT_D3D_Direct3D_CreateDevice,create));
    const uint8_t helper[]={0xb8,0x10,0x32,0x54,0x76,0xc3};memcpy(memory+GRAPHICS_PULSE_HELPER,helper,sizeof(helper));
    char identity[65];test_component_hash(helper,sizeof(helper),identity);
    CHECK(!pilot_io_device_thread_component_bind_abi(GRAPHICS_PULSE_HELPER,sizeof(helper),identity,0,test_graphics_pulse_complete));
    struct ThreadChildCode peer_code={0};struct ThreadChildCode *p=&peer_code;
    GRAPHICS_PULSE_CODE(p,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xb9,0xdf,0x9b,0x57,0x13,0xba,0xe0,0xac,0x68,0x24,
        0xbb,0x98,0xba,0xdc,0xfe,0xbd,0x21,0x43,0x65,0x87,0xbe,0x78,0x56,0x34,0x12);
    unsigned loop=p->size;
    GRAPHICS_PULSE_CODE(p,0x83,0x3f,0);unsigned done=child_branch(p,1);
    GRAPHICS_PULSE_CODE(p,0x83,0x3d);child_word(p,GRAPHICS_PULSE_CONTEXT+132);GRAPHICS_PULSE_CODE(p,2);
    unsigned other_phase=child_branch(p,1);
    GRAPHICS_PULSE_CODE(p,0x83,0x7f,96,0);unsigned entered=child_branch(p,1);
    /* Actual original 277/294 execute on this CPU peer while the whole
     * vertical-blank API is still blocked, not on a host work callback. */
    child_push(p,graphics_original_gate);child_call(p,277);
    GRAPHICS_PULSE_CODE(p,0xff,0x47,96);
    child_push(p,graphics_original_gate);child_call(p,294);
    GRAPHICS_PULSE_CODE(p,0xb9,0xdf,0x9b,0x57,0x13,0xba,0xe0,0xac,0x68,0x24);
    child_patch(p,other_phase);child_patch(p,entered);
    GRAPHICS_PULSE_CODE(p,0xff,0x47,4);unsigned repeat=child_branch(p,0);
    int32_t distance=(int32_t)loop-(int32_t)(repeat+4);memcpy(p->bytes+repeat,&distance,4);
    child_patch(p,done);
    GRAPHICS_PULSE_CODE(p,
        0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x4f,76,0x89,0x57,80,0x89,0x5f,84,0x89,0x6f,88,0x89,0x77,92,
        0xc7,0x47,8,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(p->size<0x200);memcpy(memory+THREAD_COMPONENT_WAIT_PEER,p->bytes,p->size);
    struct ThreadChildCode code={0};struct ThreadChildCode *c=&code;
    GRAPHICS_PULSE_CODE(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8);
    for(unsigned draw=0;draw<2;++draw) {
        graphics_pulse_clear(c,draw?0xff0000ff:0xffff0000);
        GRAPHICS_PULSE_CODE(c,0x6a,0);graphics_pulse_phase(c,1);graphics_pulse_call(c,PILOT_D3D_D3DDevice_Swap);
        GRAPHICS_PULSE_CODE(c,0x89,0x87);child_word(c,160);
        GRAPHICS_PULSE_CODE(c,0xc7,0x87);child_word(c,132);child_word(c,0);
    }
    graphics_pulse_phase(c,2);graphics_pulse_call(c,PILOT_D3D_D3DDevice_BlockUntilVerticalBlank);
    GRAPHICS_PULSE_CODE(c,0x89,0x87);child_word(c,164);
    GRAPHICS_PULSE_CODE(c,0xc7,0x87);child_word(c,132);child_word(c,0);
    GRAPHICS_PULSE_CODE(c,0xb8);child_word(c,GRAPHICS_PULSE_HELPER);GRAPHICS_PULSE_CODE(c,0xff,0xd0,
        0x9c,0x5a,0x89,0x57,12,0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<0x300);memcpy(memory+GRAPHICS_PULSE_CPU,c->bytes,c->size);
#ifdef PILOT_MEMORY_LLE
    CHECK(!pilot_io_device_resident(0x10000,0x10000));
#endif
    CHECK(!pilot_io_device_clock_start());CHECK(!graphics_original_clock_bind());
    CHECK(!strcmp(graphics_original_clock_bind(),"original native display clock already bound"));
    CHECK(!pilot_io_device_work_bind(NULL));CHECK(!pilot_io_device_work_bind(test_graphics_pulse_work));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8),baseline[9],after[9];test_memory_snapshot(baseline);
    for(unsigned life=0;life<3;++life) {
        uint32_t context=GRAPHICS_PULSE_CONTEXT,peer=GRAPHICS_PULSE_PEER_CONTEXT,out=0x19800;
        memset(memory+context,0,512);memset(memory+peer,0,128);MEM32(context+64)=GRAPHICS_PULSE_CPU;
        MEM32(peer+64)=THREAD_COMPONENT_WAIT_PEER;
        for(unsigned i=0;i<16;++i){MEM8(context+16+i)=(uint8_t)(0x35+i*7);MEM8(peer+16+i)=(uint8_t)(0x7d+i*3);}
        double fp=12345.6789;memcpy(memory+context+48,&fp,8);memcpy(memory+peer+48,&fp,8);
        MEM32(PILOT_D3D_PRESENT_INTERVAL)=life==1?0x80000000u:1;
        unsigned pacing=graphics_pulse_wait_samples[1],vblank=graphics_pulse_wait_samples[2],progress=graphics_pulse_peer_progress;
        graphics_pulse_last_counter=0;
        uint32_t spawn[]={out+16,0,THREAD_COMPONENT_WAIT_PEER,peer,0};CHECK(!event_lle_call(254,spawn));uint32_t peer_handle=MEM32(out+16);
        uint32_t ref[]={peer_handle,0,out+20};CHECK(!event_lle_call(246,ref));graphics_pulse_peer=MEM32(out+20);
        uint32_t priority[]={graphics_pulse_peer,29};CHECK(event_lle_call(148,priority)==8);
        spawn[0]=out;spawn[2]=GRAPHICS_PULSE_CPU;spawn[3]=context;CHECK(!event_lle_call(254,spawn));uint32_t handle=MEM32(out);
        ref[0]=handle;ref[2]=out+4;CHECK(!event_lle_call(246,ref));graphics_pulse_thread=MEM32(out+4);
        priority[0]=graphics_pulse_thread;priority[1]=30;CHECK(event_lle_call(148,priority)==8);
        uint32_t join[]={graphics_pulse_thread,0,0,0,0};CHECK(!event_lle_call(159,join));
        CHECK(!event_lle_call(187,&handle));event_lle_call(250,&graphics_pulse_thread);CHECK(event_lle_call(187,&handle)==0xc0000008u);
        MEM32(peer)=1;join[0]=graphics_pulse_peer;CHECK(!event_lle_call(159,join));
        CHECK(!event_lle_call(187,&peer_handle));event_lle_call(250,&graphics_pulse_peer);CHECK(event_lle_call(187,&peer_handle)==0xc0000008u);
        graphics_pulse_thread=graphics_pulse_peer=0;
        CHECK(MEM32(context+76)==1 && MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(!MEM32(context+164) && MEM32(context+160)==MEM32(context+168) && MEM32(context+168)==(life+1)*2);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(MEM32(peer+8)==1 && MEM32(peer+4) && MEM32(peer+68)==MEM32(peer+72));
        CHECK(MEM32(peer+96)==1);
        CHECK(MEM32(peer+76)==0x13579bdf && MEM32(peer+80)==0x2468ace0 && MEM32(peer+84)==0xfedcba98 && MEM32(peer+88)==0x87654321 && MEM32(peer+92)==0x12345678);
        CHECK(!memcmp(memory+peer+16,memory+peer+32,16) && !memcmp(memory+peer+48,memory+peer+56,8));
        CHECK(graphics_pulse_wait_samples[2]>vblank && graphics_pulse_peer_progress>progress);
        if(life!=1)CHECK(graphics_pulse_wait_samples[1]>pacing);
        CHECK(!dispatch_changed && !graphics_present_count && !graphics_callback && !graphics_gpu_head);
        CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
        test_memory_snapshot(after);CHECK(!memcmp(baseline,after,sizeof(baseline)));
        CHECK(!memcmp(memory+GRAPHICS_PULSE_CPU,c->bytes,c->size) && !memcmp(memory+THREAD_COMPONENT_WAIT_PEER,p->bytes,p->size));
        fprintf(stderr,"[ORIGINAL DISPLAY PULSE] life=%u interval=%s pacing_wait_samples=%u vblank_wait_samples=%u peer_progress=%u submitted/completed=%u exact_front_bytes=16384 no_host_dispatcher=1\n",
            life,life==1?"immediate":"paced",graphics_pulse_wait_samples[1]-pacing,graphics_pulse_wait_samples[2]-vblank,graphics_pulse_peer_progress-progress,graphics_swap_completed);
    }
    CHECK(!pilot_io_device_work_bind(NULL));graphics_original_clock_bound=0;
    CHECK(!call_graphics(PILOT_D3D_D3DDevice_Release,NULL) && !graphics_active);
    uint32_t gate=native_component_original_gate;graphics_original_gate=native_component_original_gate=0;pool_free(gate);
    fprintf(stderr,"PASS: source-bound original Swap/vblank over three CPU/compute-peer lives, genuine privateevent/KWAIT/IRQ0 pacing, actual GPU retirement/front pixels, released graphics/VM locks, actual CPU peer 277/294 through the unowned shared gate during vblank, no host guest dispatcher and exact original cleanup\n");
}
#undef GRAPHICS_PULSE_CODE
