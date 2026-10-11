/* Source-bound whole D3D fence entries run on one real original CPU owner.
 * All completion comes from actual queued native GPU work. A fast GPU may
 * retire it before a CPU wait; that case proves transport/lifetime, not the
 * original-clock blocked branch or implicit64-slot pressure. */
#define THREAD_GRAPHICS_FENCE_CPU 0x17400u
#define THREAD_GRAPHICS_FENCE_HELPER 0x17c80u
#define GRAPHICS_FENCE_CODE(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
enum {GRAPHICS_FENCE_CONTEXT=0x1b000,GRAPHICS_FENCE_INSERTS=72,GRAPHICS_FENCE_TEXTURE_SIDE=512};
static unsigned graphics_fence_fixture_calls[2],graphics_fence_idle_explicit,graphics_fence_idle_implicit;
static unsigned graphics_fence_lock_samples;
static uint32_t graphics_fence_fixture_texture,graphics_fence_fixture_thread;
static uint64_t graphics_fence_tick_first,graphics_fence_tick_last;
static void test_memory_snapshot(uint32_t out[9]);
static const char *test_graphics_fence_board_port(uint16_t port,int size,int write,uint32_t *value)
{return pilot_usb_board_port(port,size,write,value);}
static const char *test_graphics_fence_work(void)
{
    if(!MEM32(GRAPHICS_FENCE_CONTEXT+128) || !graphics_clock_waiters ||
       MEM32(0x8003da64u)!=MEM32(0x8003da6cu))return NULL;
    CHECK(graphics_fence_fixture_thread && graphics_fence_fixture_thread!=MEM32(0x8003da64u));
    /* The real original CPU is executing idle while the native caller is in
     * its original wait. Acquiring this host lock would deadlock the owner
     * if the fence path had retained it across the service handoff. */
    CHECK(TryAcquireSRWLockExclusive(&graphics_lock));
    CHECK(TryAcquireSRWLockExclusive(&vm_lock));ReleaseSRWLockExclusive(&vm_lock);
    CHECK(graphics_original_gate && graphics_original_gate==native_component_original_gate);
    CHECK(MEM32(graphics_original_gate+20)==1 && MEM32(graphics_original_gate+24)==graphics_fence_fixture_thread);
    ++graphics_fence_lock_samples;
    uint32_t phase=MEM32(GRAPHICS_FENCE_CONTEXT+132);
    if(phase==1) {
        CHECK(graphics_fence_next-graphics_fence_done==64);++graphics_fence_idle_implicit;
    } else {CHECK(phase==2);++graphics_fence_idle_explicit;}
    ReleaseSRWLockExclusive(&graphics_lock);
    uint64_t ticks=0,next=0;CHECK(!pilot_usb_pit_clock(&ticks,&next) && next);
    if(!graphics_fence_tick_first)graphics_fence_tick_first=ticks;
    CHECK(ticks>=graphics_fence_tick_last);graphics_fence_tick_last=ticks;
    return NULL;
}
static const char *test_graphics_fence_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==THREAD_GRAPHICS_FENCE_HELPER && pilot_io_device_thread_component_active());
    unsigned phase=regs[1];uint32_t context=regs[2];CHECK(phase<2 && context==GRAPHICS_FENCE_CONTEXT);
    CHECK(regs[3]==0x11223344 && regs[5]==0x44556677 && regs[6]==0x22334455 && regs[7]==context);
    CHECK(MEM32(context)==MEM32(0x8003da64u));graphics_fence_fixture_thread=MEM32(context);
    ++graphics_fence_fixture_calls[phase];
    const char *error=graphics_original_enter();if(error)return error;
    AcquireSRWLockExclusive(&graphics_lock);
    CHECK(graphics_active && !graphics_gpu_head && !graphics_gpu_running && !graphics_callback && !graphics_clock_waiters);
    uint32_t color=MEM32(context+100);struct PilotGpuProgress before,after;
    pilot_gfx_completion_stats(&before);
    if(!phase) {
        CHECK(!graphics_fence_fixture_texture);
        CHECK(!pilot_gfx_texture_create_usage(GRAPHICS_FENCE_TEXTURE_SIDE,GRAPHICS_FENCE_TEXTURE_SIDE,1,0x12,1,&graphics_fence_fixture_texture));
        CHECK(graphics_fence_fixture_texture);
        /* Finite owned workload, not a withheld completion or forced busy
         * query. Alternate contents before the exact final color so the
         * consumer bytes require real execution, not initial allocation. */
        for(unsigned batch=0;batch<128;++batch) {
            uint32_t work_color=batch==127?color:(color^(batch&1?0x0055aa55u:0x00aa55aau));
            CHECK(!pilot_gfx_texture_clear(graphics_fence_fixture_texture,work_color));
        }
        pilot_gfx_completion_stats(&after);CHECK(after.submitted==before.submitted+128);
        CHECK(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,graphics_fence_fixture_texture)==after.submitted);
        MEM64(context+136)=after.submitted;MEM32(context+152)=graphics_fence_next;
    } else {
        CHECK(graphics_fence_fixture_texture && !MEM32(context+108));
        uint32_t first=MEM32(context+80),last=MEM32(context+84);
        CHECK(first==MEM32(context+152) && last==first+GRAPHICS_FENCE_INSERTS-1);
        CHECK(graphics_fence_next==last+1 && graphics_fence_done>=last);
        for(unsigned i=0;i<GRAPHICS_FENCE_INSERTS;++i)CHECK(MEM32(context+256+i*4)==first+i);
        uint64_t serial=graphics_fences[last%64];CHECK(serial>=MEM64(context+136));
        /* Check genuine completion before any readback can itself synchronize
         * the device. Repeated retired tokens must not manufacture work. */
        CHECK(!pilot_gfx_checkpoint_busy(serial));pilot_gfx_completion_stats(&after);
        CHECK(after.completed>=serial && !after.pending_queries);
        CHECK(after.submitted==MEM64(context+136)+GRAPHICS_FENCE_INSERTS);
        unsigned char *pixels=malloc(GRAPHICS_FENCE_TEXTURE_SIDE*GRAPHICS_FENCE_TEXTURE_SIDE*4);CHECK(pixels);
        CHECK(!pilot_gfx_texture_readback(graphics_fence_fixture_texture,0,pixels,GRAPHICS_FENCE_TEXTURE_SIDE*GRAPHICS_FENCE_TEXTURE_SIDE*4));
        for(unsigned pixel=0;pixel<GRAPHICS_FENCE_TEXTURE_SIDE*GRAPHICS_FENCE_TEXTURE_SIDE;++pixel) {
            uint32_t observed;memcpy(&observed,pixels+pixel*4,4);CHECK(observed==color);
        }
        free(pixels);unsigned char back[64*64*4];CHECK(!pilot_gfx_surface_readback(0,back,sizeof(back)));
        for(unsigned pixel=0;pixel<64*64;++pixel) {
            CHECK(back[pixel*4]==((color>>16)&255) && back[pixel*4+1]==((color>>8)&255));
            CHECK(back[pixel*4+2]==(color&255) && back[pixel*4+3]==255);
        }
        pilot_gfx_texture_destroy(graphics_fence_fixture_texture);graphics_fence_fixture_texture=0;
        MEM64(context+144)=serial;
    }
    ReleaseSRWLockExclusive(&graphics_lock);
    error=graphics_original_leave();if(error)return error;
    regs[0]=0x76543210;regs[4]+=4;return NULL;
}
static void graphics_fence_fixture_call(struct ThreadChildCode *c,unsigned entry)
{
    GRAPHICS_FENCE_CODE(c,0xb8);child_word(c,pilot_d3d_calls[entry].address);
    GRAPHICS_FENCE_CODE(c,0xff,0xd0);
}
static void graphics_fence_fixture_check(struct ThreadChildCode *c,unsigned phase)
{
    GRAPHICS_FENCE_CODE(c,0xb9);child_word(c,phase);
    GRAPHICS_FENCE_CODE(c,0x89,0xfa,0xb8);child_word(c,THREAD_GRAPHICS_FENCE_HELPER);
    GRAPHICS_FENCE_CODE(c,0xff,0xd0);
}
static void test_thread_graphics_fence_lle(const char *path,int invalid_token)
{
    CHECK(invalid_token>=0 && invalid_token<=2);
    install_fault_handler();CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    pilot_io_device_port_bind(0x80cf,0x80cf,test_graphics_fence_board_port);
    vm_ensure();uint32_t page=PILOT_D3D_DEVICE_GLOBAL&~4095u;
    uint32_t bytes=((PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET+4+4095)&~4095u)-page;
    commit(page,bytes);vm_fixed_region(page,bytes);test_graphics_component_sources(path);
    CHECK(pilot_d3d_calls[PILOT_D3D_D3DDevice_InsertFence].stack_bytes==0 &&
          pilot_d3d_calls[PILOT_D3D_D3DDevice_BlockOnFence].stack_bytes==4 &&
          pilot_d3d_calls[PILOT_D3D_D3DDevice_Clear].stack_bytes==24);
    /* One reviewed native constructor supplies fixture resources; repeated
     * CPU lives exercise fences and their actual owner, not construction. */
    const uint32_t parameters[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};
    memcpy(memory+0x1a000,parameters,sizeof(parameters));uint32_t create[]={0,1,0,0x40,0x1a000,0x1a080};
    CHECK(!call_graphics(PILOT_D3D_Direct3D_CreateDevice,create) && graphics_active);
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};char identity[65];
    memcpy(memory+THREAD_GRAPHICS_FENCE_HELPER,helper,sizeof(helper));test_component_hash(helper,sizeof(helper),identity);
    CHECK(!pilot_io_device_thread_component_bind_abi(THREAD_GRAPHICS_FENCE_HELPER,sizeof(helper),identity,0,test_graphics_fence_body));
    struct ThreadChildCode code={0},*c=&code;
    GRAPHICS_FENCE_CODE(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68);
    if(invalid_token) {
        child_push(c,invalid_token==1?0:graphics_fence_next);
        graphics_fence_fixture_call(c,PILOT_D3D_D3DDevice_BlockOnFence);
        GRAPHICS_FENCE_CODE(c,0xcc);
    } else {
        child_call(c,125);GRAPHICS_FENCE_CODE(c,0x89,0x47,88,0x89,0x57,92,
            0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22);
        graphics_fence_fixture_check(c,0);
        GRAPHICS_FENCE_CODE(c,0xc7,0x87,128,0,0,0,1,0,0,0,0xc7,0x87,132,0,0,0,1,0,0,0,
            0x31,0xf6,0xbd);child_word(c,GRAPHICS_FENCE_INSERTS);
        unsigned loop=c->size;
        child_push(c,0);child_push(c,0x3f800000);child_push_field(c,100);child_push(c,0xf1);child_push(c,0);child_push(c,0);
        graphics_fence_fixture_call(c,PILOT_D3D_D3DDevice_Clear);
        GRAPHICS_FENCE_CODE(c,0x09,0x47,108);
        graphics_fence_fixture_call(c,PILOT_D3D_D3DDevice_InsertFence);
        GRAPHICS_FENCE_CODE(c,0x89,0x84,0xb7,0,1,0,0,0x89,0x47,84,0x85,0xf6,0x75,3,0x89,0x47,80,
            0x46,0x4d,0x0f,0x85);unsigned jump=c->size;child_word(c,0);
        int32_t distance=(int32_t)loop-(int32_t)(jump+4);memcpy(c->bytes+jump,&distance,4);
        GRAPHICS_FENCE_CODE(c,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22,
            0xc7,0x87,132,0,0,0,2,0,0,0,0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8);
        child_push_field(c,84);graphics_fence_fixture_call(c,PILOT_D3D_D3DDevice_BlockOnFence);
        child_push_field(c,80);graphics_fence_fixture_call(c,PILOT_D3D_D3DDevice_BlockOnFence);
        child_push_field(c,84);graphics_fence_fixture_call(c,PILOT_D3D_D3DDevice_BlockOnFence);
        GRAPHICS_FENCE_CODE(c,0xc7,0x87,128,0,0,0,0,0,0,0);
        graphics_fence_fixture_check(c,1);GRAPHICS_FENCE_CODE(c,0x89,0x47,4,0x9c,0x5a,0x89,0x57,12);
        child_call(c,125);GRAPHICS_FENCE_CODE(c,0x89,0x87,160,0,0,0,0x89,0x97,164,0,0,0,
            0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
            0x89,0x5f,112,0x89,0x6f,116,0x89,0x77,120,
            0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    }
    CHECK(c->size<THREAD_GRAPHICS_FENCE_HELPER-THREAD_GRAPHICS_FENCE_CPU);
    memcpy(memory+THREAD_GRAPHICS_FENCE_CPU,c->bytes,c->size);
    CHECK(!pilot_io_device_resident(0x10000,0x10000));
    CHECK(!pilot_io_device_clock_start() && pilot_io_device_clock_bound());
    CHECK(!pilot_io_device_work_bind(test_graphics_fence_work));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);unsigned background_retired=0;
    uint32_t captured_query[]={0x10,0x19880};MEM32(0x19880)=MEM32(0x19884)=0xabcdef12;
    CHECK(event_lle_call(209,captured_query)==0xc0000024u && MEM32(0x19880)==0xabcdef12 && MEM32(0x19884)==0xabcdef12);
    uint32_t baseline_memory[9],after_memory[9];test_memory_snapshot(baseline_memory);
    for(unsigned life=0;life<(invalid_token?1u:3u);++life) {
        uint32_t context=GRAPHICS_FENCE_CONTEXT,out=0x19800;memset(memory+context,0,640);
        MEM32(context+64)=THREAD_GRAPHICS_FENCE_CPU;MEM32(context+96)=0xa5a55a5a;
        MEM32(context+100)=0xff13579bu+life*0x00102030u;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7);
        double expected=12345.6789;memcpy(memory+context+48,&expected,8);
        unsigned implicit_before=graphics_fence_idle_implicit,explicit_before=graphics_fence_idle_explicit;
        uint64_t ticks_before=0,next=0;CHECK(!pilot_usb_pit_clock(&ticks_before,&next) && next);
        uint32_t old_next=graphics_fence_next,old_done=graphics_fence_done;uint64_t old_slots[64];memcpy(old_slots,graphics_fences,sizeof(old_slots));
        struct PilotGpuProgress gpu_before;pilot_gfx_completion_stats(&gpu_before);
        uint32_t args[]={out,0,THREAD_GRAPHICS_FENCE_CPU,context,0};CHECK(!event_lle_call(254,args));uint32_t handle=MEM32(out);
        uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);MEM32(context)=object;
        uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
        uint32_t join[]={object,0,0,0,0};
        if(invalid_token) {
            uint32_t result=0xbad0cafe;const char *error=pilot_io_device_lle(159,memory,join,&result);
            CHECK(error && !strcmp(error,"unowned GPU fence"));
            CHECK(!MEM32(context+76) && !graphics_fence_fixture_texture && !graphics_fence_fixture_calls[0]);
            CHECK(graphics_fence_next==old_next && graphics_fence_done==old_done && !memcmp(old_slots,graphics_fences,sizeof(old_slots)));
            struct PilotGpuProgress gpu_after;pilot_gfx_completion_stats(&gpu_after);CHECK(!memcmp(&gpu_before,&gpu_after,sizeof(gpu_before)));
            CHECK(!graphics_clock_waiters && !graphics_gpu_head && !graphics_gpu_running && !graphics_callback);
            CHECK(MEM32(native_component_original_gate+16)==UINT32_MAX && !MEM32(native_component_original_gate+20) && !MEM32(native_component_original_gate+24));
            fprintf(stderr,"PASS: actual original CPU rejects %s GPU fence before GPU work/wait mutation; original gate retired, terminal thread retained\n",invalid_token==1?"zero":"future");return;
        }
        CHECK(!event_lle_call(159,join));CHECK(!event_lle_call(187,&handle));event_lle_call(250,&object);
        CHECK(event_lle_call(187,&handle)==0xc0000008u);
        CHECK(MEM32(context+4)==0x76543210 && MEM32(context+76)==1 && MEM32(context+96)==0xa5a55a5a);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(MEM32(context+112)==0x11223344 && MEM32(context+116)==0x44556677 && MEM32(context+120)==0x22334455);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(!memcmp(memory+THREAD_GRAPHICS_FENCE_HELPER,helper,sizeof(helper)));
        CHECK(!graphics_fence_fixture_texture && !graphics_clock_waiters && !graphics_gpu_head && !graphics_gpu_running && !graphics_callback);
        CHECK(MEM32(native_component_original_gate+16)==UINT32_MAX && !MEM32(native_component_original_gate+20) && !MEM32(native_component_original_gate+24));
        uint64_t ticks_after=0;CHECK(!pilot_usb_pit_clock(&ticks_after,&next));
        unsigned observed=graphics_fence_idle_implicit-implicit_before+graphics_fence_idle_explicit-explicit_before;
        if(observed)CHECK(ticks_after>ticks_before && MEM64(context+160)>MEM64(context+88));
        CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==baseline_threads);
        fprintf(stderr,"[GRAPHICS FENCE HANDLES] life=%u baseline=%u captured_retired=%u current=%u threads=%u baseline_threads=%u\n",
            life,baseline_handles,background_retired,MEM32(stats+8),MEM32(stats+4),baseline_threads);
        /* Only one known captured background handle may retire, in any
         * blocked life. All authored thread handles were closed and rejected
         * above; no increase or second drop is admitted. */
        if(!background_retired && baseline_handles && MEM32(stats+8)==baseline_handles-1)background_retired=1;
        CHECK(MEM32(stats+8)==baseline_handles-background_retired);
        MEM32(0x19880)=MEM32(0x19884)=0xabcdef12;
        CHECK(event_lle_call(209,captured_query)==(background_retired?0xc0000008u:0xc0000024u) &&
              MEM32(0x19880)==0xabcdef12 && MEM32(0x19884)==0xabcdef12);
        test_memory_snapshot(after_memory);CHECK(!memcmp(baseline_memory,after_memory,sizeof(baseline_memory)));
        fprintf(stderr,"[GRAPHICS FENCE] life=%u insertions=72 exact_texture_bytes=%u original_idle_explicit=%u original_idle_capacity=%u elapsed100ns=%llu\n",
            life,GRAPHICS_FENCE_TEXTURE_SIDE*GRAPHICS_FENCE_TEXTURE_SIDE*4,graphics_fence_idle_explicit-explicit_before,
            graphics_fence_idle_implicit-implicit_before,(unsigned long long)(MEM64(context+160)-MEM64(context+88)));
    }
    CHECK(graphics_fence_fixture_calls[0]==3 && graphics_fence_fixture_calls[1]==3 && !thread_cpu_calls);
    CHECK(graphics_fence_lock_samples==graphics_fence_idle_explicit+graphics_fence_idle_implicit);
    CHECK(!pilot_io_device_work_bind(NULL));CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    CHECK(!call_graphics(PILOT_D3D_D3DDevice_Release,NULL) && !graphics_active);
    uint32_t gate=native_component_original_gate;graphics_original_gate=native_component_original_gate=0;pool_free(gate);
    fprintf(stderr,"PASS: three raw original CPU fence lives, source-bound RET0/RET4/Clear,216 real fence identities/slot wrap, genuine native GPU completion and exact bytes, retired-token reuse, GPR/ESP/flags/x87/XMM and original memory/thread/handle cleanup\n");
    if(!graphics_fence_idle_explicit)fprintf(stderr,"OBLIGATION: GPU completed before explicit wait reached original idle; blocked BlockOnFence branch not exercised by this device generation\n");
    if(!graphics_fence_idle_implicit)fprintf(stderr,"OBLIGATION: no genuine64-slot outstanding wait observed; slot wrap does not qualify implicit capacity contention\n");
}
#undef GRAPHICS_FENCE_CODE
