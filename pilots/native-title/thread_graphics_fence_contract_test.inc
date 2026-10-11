/* Source-bound whole D3D fence entries run on one real original CPU owner.
 * All completion comes from actual queued native GPU work. A fast GPU may
 * retire it before a CPU wait; that case proves transport/lifetime, not the
 * original-clock blocked branch or implicit64-slot pressure. */
#define THREAD_GRAPHICS_FENCE_CPU 0x17400u
#define THREAD_GRAPHICS_FENCE_HELPER 0x17c80u
#define THREAD_GRAPHICS_FENCE_PEER 0x17600u
#define GRAPHICS_FENCE_PEER_CONTEXT 0x1b800u
#define GRAPHICS_FENCE_CODE(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
enum {GRAPHICS_FENCE_CONTEXT=0x1b000,GRAPHICS_FENCE_INSERTS=72,GRAPHICS_FENCE_TEXTURE_SIDE=512,
      GRAPHICS_FENCE_WORK_SIDE=2048,GRAPHICS_FENCE_WORK_DRAWS=64,GRAPHICS_FENCE_CAPACITY_INSERTS=65};
static unsigned graphics_fence_fixture_calls[4],graphics_fence_blocked_explicit,graphics_fence_blocked_implicit;
static unsigned graphics_fence_lock_samples;
static uint32_t graphics_fence_fixture_texture,graphics_fence_fixture_thread;
static uint32_t graphics_fence_work_texture,graphics_fence_peer_thread,graphics_fence_peer_counter;
static unsigned graphics_fence_phase_samples[6],graphics_fence_phase_progress[6];
static uint64_t graphics_fence_tick_first,graphics_fence_tick_last;
static void test_memory_snapshot(uint32_t out[9]);
static const char *test_graphics_fence_board_port(uint16_t port,int size,int write,uint32_t *value)
{return pilot_usb_board_port(port,size,write,value);}
static const char *test_graphics_fence_work(void)
{
    if(!MEM32(GRAPHICS_FENCE_CONTEXT+128) || !graphics_clock_waiters)return NULL;
    unsigned char waiter[0x140],peer[0x140];uint32_t current=MEM32(0x8003da64u);
    CHECK(graphics_fence_fixture_thread && graphics_fence_peer_thread);
    if(!component_wait_thread_snapshot(graphics_fence_fixture_thread,waiter) || waiter[0x2c]!=5 ||
       current==graphics_fence_fixture_thread)return NULL;
    CHECK(current==graphics_fence_peer_thread && component_wait_thread_snapshot(current,peer) && peer[0x2c]==2 && peer[0x32]==29);
    CHECK(component_wait_snapshot_word(peer,0x130)==THREAD_GRAPHICS_FENCE_PEER);
    uint32_t frame=MEM32(GRAPHICS_FENCE_CONTEXT+176),bottom=component_wait_snapshot_word(waiter,0x20),top=component_wait_snapshot_word(waiter,0x1c);
    CHECK(frame>=bottom+68 && (uint64_t)frame+COMPONENT_WAIT_BYTES<=top);
    uint32_t words[8],caller[5],event_object=0,block[6];
    CHECK(component_wait_owned_bytes(frame,words,sizeof(words)) && words[0] && words[1]==0x13579bdf && words[6]==0x2468ace0 && words[7]==0x89abcdef);
    CHECK((int64_t)MEM64(frame+8)==-10000 && words[4]==1 && !words[5]);
    CHECK(component_wait_owned_bytes(frame-68,&event_object,4) && event_object);
    CHECK(component_wait_owned_bytes(frame-36,caller,sizeof(caller)) && caller[0]==0x800249f1u && caller[1]==words[0] &&
          !caller[2] && !caller[3] && caller[4]==frame+8);
    uint32_t wait_block=component_wait_snapshot_word(waiter,0x58);
    CHECK(wait_block>=bottom && (uint64_t)wait_block+sizeof(block)<=top);
    CHECK(component_wait_owned_bytes(wait_block,block,sizeof(block)) && block[2]==graphics_fence_fixture_thread &&
          block[3]==event_object && waiter[0x57]==6);
    /* Actual233/234 owns the private event reference. No host read of its
     * unpublished body or extra reference: component_wait's real209 queries
     * establish synchronization type1 and state0 before/after each slice. */
    CHECK(TryAcquireSRWLockExclusive(&graphics_lock));
    CHECK(TryAcquireSRWLockExclusive(&vm_lock));ReleaseSRWLockExclusive(&vm_lock);
    CHECK(graphics_original_gate && graphics_original_gate==native_component_original_gate);
    CHECK(MEM32(graphics_original_gate+20)==1 && MEM32(graphics_original_gate+24)==graphics_fence_fixture_thread);
    ++graphics_fence_lock_samples;
    uint32_t phase=MEM32(GRAPHICS_FENCE_CONTEXT+132);
    CHECK(phase>=1 && phase<=5);
    if(phase==1 || phase==4) {
        CHECK(graphics_fence_next-graphics_fence_done==64);++graphics_fence_blocked_implicit;
    } else {CHECK(graphics_fence_next>graphics_fence_done+1);++graphics_fence_blocked_explicit;}
    ++graphics_fence_phase_samples[phase];
    uint32_t counter=MEM32(GRAPHICS_FENCE_PEER_CONTEXT+4);CHECK(counter>=graphics_fence_peer_counter);
    if(counter>graphics_fence_peer_counter)++graphics_fence_phase_progress[phase];
    graphics_fence_peer_counter=counter;
    ReleaseSRWLockExclusive(&graphics_lock);
    uint64_t ticks=0,next=0;CHECK(!pilot_usb_pit_clock(&ticks,&next) && next);
    if(!graphics_fence_tick_first)graphics_fence_tick_first=ticks;
    CHECK(ticks>=graphics_fence_tick_last);graphics_fence_tick_last=ticks;
    return NULL;
}
static void graphics_fence_owned_work(void)
{
    CHECK(graphics_fence_work_texture && !pilot_gfx_targets(graphics_fence_work_texture,0));
    uint32_t viewport[]={0,0,GRAPHICS_FENCE_WORK_SIDE,GRAPHICS_FENCE_WORK_SIDE,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
    pilot_gfx_pixel_fixed();for(unsigned i=0;i<4;++i)pilot_gfx_texture_bind(i,0);
    /* Reuse the maintained Xbox state conversion: no donor implementation
     * header or second state decoder enters this original-owner fixture. */
    const uint32_t states[][2]={{143,0},{64,0},{144,0},{60,0},{92,0},{147,0},
        {67,0x01010101},{66,0x1d01},{139,0x1b02},{102,0},{103,0},{151,0},
        {59,1},{62,1},{63,1},{74,0x8006}};
    for(unsigned i=0;i<sizeof(states)/sizeof(*states);++i)CHECK(pilot_gfx_xbox_render_state(states[i][0],states[i][1]));
    CHECK(pilot_gfx_xbox_texture_state(0,12,2) && pilot_gfx_xbox_texture_state(0,14,0));
    CHECK(pilot_gfx_xbox_texture_state(0,16,2) && pilot_gfx_xbox_texture_state(0,18,0));
    for(unsigned i=1;i<4;++i)CHECK(pilot_gfx_xbox_texture_state(i,12,1));
    CHECK(!pilot_gfx_texture_clear(graphics_fence_work_texture,0xff000000));
    struct {float x,y,z,w;uint32_t color;} quad[]={
        {-4,-4,.5f,1,0x00010203},{GRAPHICS_FENCE_WORK_SIDE+4,-4,.5f,1,0x00010203},
        {-4,GRAPHICS_FENCE_WORK_SIDE+4,.5f,1,0x00010203},{GRAPHICS_FENCE_WORK_SIDE+4,GRAPHICS_FENCE_WORK_SIDE+4,.5f,1,0x00010203}};
    for(unsigned draw=0;draw<GRAPHICS_FENCE_WORK_DRAWS;++draw)CHECK(!pilot_gfx_draw_up(5,2,quad,sizeof(quad[0]),0x44));
    /* Every pass contributes an independently known RGB unit count. A
     * final clear cannot hide missing/skipped draws. No poll or readback
     * consumes the queued batch before the real fence path. */
    CHECK(!pilot_gfx_targets(0,UINT32_MAX));viewport[2]=viewport[3]=64;CHECK(!pilot_gfx_viewport(viewport));
    CHECK(pilot_gfx_xbox_render_state(59,0));
}
static void graphics_fence_owned_pixels(uint64_t serial)
{
    CHECK(graphics_fence_work_texture && !pilot_gfx_checkpoint_busy(serial));
    struct PilotGpuProgress gpu;pilot_gfx_completion_stats(&gpu);CHECK(gpu.completed>=serial && !gpu.pending_queries);
    uint32_t count=GRAPHICS_FENCE_WORK_SIDE*GRAPHICS_FENCE_WORK_SIDE,bytes=count*4;
    uint32_t *pixels=malloc(bytes);CHECK(pixels);
    CHECK(!pilot_gfx_texture_readback(graphics_fence_work_texture,0,pixels,bytes));
    for(unsigned i=0;i<count;++i)if(pixels[i]!=0xff4080c0) {
        fprintf(stderr,"[FENCE WORK PIXEL] texel=%u actual=%08x expected=ff4080c0 draws=64\n",i,pixels[i]);
        CHECK(pixels[i]==0xff4080c0);
    }
    free(pixels);
}
static const char *test_graphics_fence_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==THREAD_GRAPHICS_FENCE_HELPER && pilot_io_device_thread_component_active());
    unsigned phase=regs[1];uint32_t context=regs[2];CHECK(phase<4 && context==GRAPHICS_FENCE_CONTEXT);
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
    } else if(phase==1) {
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
        CHECK(!graphics_fence_work_texture);
        CHECK(!pilot_gfx_texture_create_usage(GRAPHICS_FENCE_WORK_SIDE,GRAPHICS_FENCE_WORK_SIDE,1,0x12,1,&graphics_fence_work_texture));
        pilot_gfx_completion_stats(&before);graphics_fence_owned_work();pilot_gfx_completion_stats(&after);
        CHECK(after.submitted==before.submitted+1+GRAPHICS_FENCE_WORK_DRAWS);
        CHECK(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,graphics_fence_work_texture)==after.submitted);
        MEM64(context+192)=after.submitted;MEM32(context+188)=graphics_fence_next;
    } else if(phase==2) {
        uint32_t token=MEM32(context+172);CHECK(token==MEM32(context+188) && graphics_fence_done>=token);
        graphics_fence_owned_pixels(MEM64(context+192));
        pilot_gfx_completion_stats(&before);graphics_fence_owned_work();pilot_gfx_completion_stats(&after);
        CHECK(after.submitted==before.submitted+1+GRAPHICS_FENCE_WORK_DRAWS);
        MEM64(context+200)=after.submitted;MEM32(context+184)=graphics_fence_next;
        CHECK(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,graphics_fence_work_texture)==after.submitted);
        MEM32(context+132)=4;MEM32(context+176)=regs[4]-COMPONENT_WAIT_BYTES;
        /* This distinct authored component keeps the original owner and
         * invokes the actual production capacity path rapidly. Each token
         * covers real work; completion/query answers remain untouched. The
         * prior72-per-life raw calls separately qualify original entry ABI. */
        for(unsigned i=0;i<GRAPHICS_FENCE_CAPACITY_INSERTS;++i)MEM32(context+640+i*4)=graphics_fence_insert();
        MEM32(context+180)=MEM32(context+640+(GRAPHICS_FENCE_CAPACITY_INSERTS-1)*4);
    } else {
        uint32_t first=MEM32(context+184),last=MEM32(context+180);
        CHECK(last==first+GRAPHICS_FENCE_CAPACITY_INSERTS-1 && graphics_fence_next==last+1 && graphics_fence_done>=last);
        for(unsigned i=0;i<GRAPHICS_FENCE_CAPACITY_INSERTS;++i) {
            CHECK(MEM32(context+640+i*4)==first+i);
            if(first+i>last-64)CHECK(graphics_fences[(first+i)%64]==MEM64(context+200));
        }
        graphics_fence_owned_pixels(MEM64(context+200));
        pilot_gfx_texture_destroy(graphics_fence_work_texture);graphics_fence_work_texture=0;
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
            0x8d,0x44,0x24,0xdc,0x89,0x87,176,0,0,0,
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
            0xc7,0x87,132,0,0,0,2,0,0,0,0x8d,0x44,0x24,0xd8,0x89,0x87,176,0,0,0,
            0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8);
        child_push_field(c,84);graphics_fence_fixture_call(c,PILOT_D3D_D3DDevice_BlockOnFence);
        child_push_field(c,80);graphics_fence_fixture_call(c,PILOT_D3D_D3DDevice_BlockOnFence);
        child_push_field(c,84);graphics_fence_fixture_call(c,PILOT_D3D_D3DDevice_BlockOnFence);
        graphics_fence_fixture_check(c,1);
        GRAPHICS_FENCE_CODE(c,0xc7,0x87,132,0,0,0,3,0,0,0);
        graphics_fence_fixture_call(c,PILOT_D3D_D3DDevice_InsertFence);
        GRAPHICS_FENCE_CODE(c,0x89,0x87,172,0,0,0,0x8d,0x44,0x24,0xd8,0x89,0x87,176,0,0,0,
            0xff,0xb7,172,0,0,0);
        graphics_fence_fixture_call(c,PILOT_D3D_D3DDevice_BlockOnFence);
        graphics_fence_fixture_check(c,2);
        GRAPHICS_FENCE_CODE(c,0xc7,0x87,132,0,0,0,5,0,0,0,0x8d,0x44,0x24,0xd8,0x89,0x87,176,0,0,0,
            0xff,0xb7,180,0,0,0);
        graphics_fence_fixture_call(c,PILOT_D3D_D3DDevice_BlockOnFence);
        GRAPHICS_FENCE_CODE(c,0xc7,0x87,128,0,0,0,0,0,0,0);
        graphics_fence_fixture_check(c,3);GRAPHICS_FENCE_CODE(c,0x89,0x47,4,0x9c,0x5a,0x89,0x57,12);
        child_call(c,125);GRAPHICS_FENCE_CODE(c,0x89,0x87,160,0,0,0,0x89,0x97,164,0,0,0,
            0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
            0x89,0x5f,112,0x89,0x6f,116,0x89,0x77,120,
            0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    }
    CHECK(c->size<THREAD_GRAPHICS_FENCE_PEER-THREAD_GRAPHICS_FENCE_CPU);
    memcpy(memory+THREAD_GRAPHICS_FENCE_CPU,c->bytes,c->size);
    /* Same actual sole-CPU compute peer used by the qualified timed-wait
     * fixture. It keeps unrelated captured launch work out of this test and
     * proves genuine timeout preemption with live register/FP state. */
    const uint8_t peer_code[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xb9,0xdf,0x9b,0x57,0x13,0xba,0xe0,0xac,0x68,0x24,
        0xbb,0x98,0xba,0xdc,0xfe,0xbd,0x21,0x43,0x65,0x87,0xbe,0x78,0x56,0x34,0x12,
        0x83,0x3f,0,0x75,5,0xff,0x47,4,0xeb,0xf6,
        0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x4f,76,0x89,0x57,80,0x89,0x5f,84,0x89,0x6f,88,0x89,0x77,92,
        0xc7,0x47,8,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+THREAD_GRAPHICS_FENCE_PEER,peer_code,sizeof(peer_code));
    CHECK(!pilot_io_device_resident(0x10000,0x10000));
    CHECK(!pilot_io_device_clock_start() && pilot_io_device_clock_bound());
    CHECK(!pilot_io_device_work_bind(test_graphics_fence_work));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    uint32_t captured_query[]={0x10,0x19880};MEM32(0x19880)=MEM32(0x19884)=0xabcdef12;
    CHECK(event_lle_call(209,captured_query)==0xc0000024u && MEM32(0x19880)==0xabcdef12 && MEM32(0x19884)==0xabcdef12);
    uint32_t baseline_memory[9],after_memory[9];test_memory_snapshot(baseline_memory);
    for(unsigned life=0;life<(invalid_token?1u:3u);++life) {
        uint32_t context=GRAPHICS_FENCE_CONTEXT,out=0x19800;memset(memory+context,0,1024);
        MEM32(context+64)=THREAD_GRAPHICS_FENCE_CPU;MEM32(context+96)=0xa5a55a5a;
        MEM32(context+100)=0xff13579bu+life*0x00102030u;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7);
        double expected=12345.6789;memcpy(memory+context+48,&expected,8);
        unsigned implicit_before=graphics_fence_blocked_implicit,explicit_before=graphics_fence_blocked_explicit;
        unsigned owned_explicit_before=graphics_fence_phase_samples[3],owned_capacity_before=graphics_fence_phase_samples[4];
        uint64_t ticks_before=0,next=0;CHECK(!pilot_usb_pit_clock(&ticks_before,&next) && next);
        uint32_t old_next=graphics_fence_next,old_done=graphics_fence_done;uint64_t old_slots[64];memcpy(old_slots,graphics_fences,sizeof(old_slots));
        struct PilotGpuProgress gpu_before;pilot_gfx_completion_stats(&gpu_before);
        uint32_t peer_handle=0,peer_object=0;
        if(!invalid_token) {
            uint32_t peer=GRAPHICS_FENCE_PEER_CONTEXT;memset(memory+peer,0,128);
            MEM32(peer+12)=0xa5a55a5a;MEM32(peer+64)=THREAD_GRAPHICS_FENCE_PEER;
            for(unsigned i=0;i<16;++i)MEM8(peer+16+i)=(uint8_t)(0x7d+i*3);
            memcpy(memory+peer+48,&expected,8);
            uint32_t peer_create[]={out+16,0,THREAD_GRAPHICS_FENCE_PEER,peer,0};CHECK(!event_lle_call(254,peer_create));peer_handle=MEM32(out+16);
            uint32_t peer_ref[]={peer_handle,0,out+20};CHECK(!event_lle_call(246,peer_ref));peer_object=MEM32(out+20);
            uint32_t peer_priority[]={peer_object,29};CHECK(event_lle_call(148,peer_priority)==8);
            graphics_fence_peer_thread=peer_object;graphics_fence_peer_counter=0;
        }
        uint32_t args[]={out,0,THREAD_GRAPHICS_FENCE_CPU,context,0};CHECK(!event_lle_call(254,args));uint32_t handle=MEM32(out);
        uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);MEM32(context)=object;
        uint32_t priority[]={object,invalid_token?29u:30u};CHECK(event_lle_call(148,priority)==8);
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
        CHECK(!event_lle_call(159,join));
        uint32_t peer=GRAPHICS_FENCE_PEER_CONTEXT;CHECK(!MEM32(peer) && !MEM32(peer+8));MEM32(peer)=1;
        uint32_t peer_join[]={peer_object,0,0,0,0};CHECK(!event_lle_call(159,peer_join));
        CHECK(!event_lle_call(187,&peer_handle));event_lle_call(250,&peer_object);CHECK(event_lle_call(187,&peer_handle)==0xc0000008u);
        CHECK(MEM32(peer+8)==1 && MEM32(peer+12)==0xa5a55a5a && MEM32(peer+68)==MEM32(peer+72));
        CHECK(MEM32(peer+76)==0x13579bdf && MEM32(peer+80)==0x2468ace0 && MEM32(peer+84)==0xfedcba98 &&
              MEM32(peer+88)==0x87654321 && MEM32(peer+92)==0x12345678);
        CHECK(!memcmp(memory+peer+16,memory+peer+32,16) && !memcmp(memory+peer+48,memory+peer+56,8));
        CHECK(!memcmp(memory+THREAD_GRAPHICS_FENCE_PEER,peer_code,sizeof(peer_code)));graphics_fence_peer_thread=0;
        CHECK(!event_lle_call(187,&handle));event_lle_call(250,&object);
        CHECK(event_lle_call(187,&handle)==0xc0000008u);
        CHECK(MEM32(context+4)==0x76543210 && MEM32(context+76)==1 && MEM32(context+96)==0xa5a55a5a);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(MEM32(context+112)==0x11223344 && MEM32(context+116)==0x44556677 && MEM32(context+120)==0x22334455);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(!memcmp(memory+THREAD_GRAPHICS_FENCE_HELPER,helper,sizeof(helper)));
        CHECK(!graphics_fence_fixture_texture && !graphics_fence_work_texture && !graphics_clock_waiters && !graphics_gpu_head && !graphics_gpu_running && !graphics_callback);
        CHECK(MEM32(native_component_original_gate+16)==UINT32_MAX && !MEM32(native_component_original_gate+20) && !MEM32(native_component_original_gate+24));
        uint64_t ticks_after=0;CHECK(!pilot_usb_pit_clock(&ticks_after,&next));
        unsigned observed=graphics_fence_blocked_implicit-implicit_before+graphics_fence_blocked_explicit-explicit_before;
        if(observed)CHECK(ticks_after>ticks_before && MEM64(context+160)>MEM64(context+88));
        CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==baseline_threads);
        CHECK(MEM32(stats+8)==baseline_handles);
        MEM32(0x19880)=MEM32(0x19884)=0xabcdef12;
        CHECK(event_lle_call(209,captured_query)==0xc0000024u &&
              MEM32(0x19880)==0xabcdef12 && MEM32(0x19884)==0xabcdef12);
        test_memory_snapshot(after_memory);CHECK(!memcmp(baseline_memory,after_memory,sizeof(baseline_memory)));
        fprintf(stderr,"[GRAPHICS FENCE] life=%u raw_insertions=73 capacity_insertions=65 exact_transport_texture_bytes=%u exact_work_texture_bytes=%u private_KWAIT_explicit=%u private_KWAIT_capacity=%u peer_counter=%u elapsed100ns=%llu exact_memory/thread/handles=retired\n",
            life,GRAPHICS_FENCE_TEXTURE_SIDE*GRAPHICS_FENCE_TEXTURE_SIDE*4,GRAPHICS_FENCE_WORK_SIDE*GRAPHICS_FENCE_WORK_SIDE*4,
            graphics_fence_phase_samples[3]-owned_explicit_before,graphics_fence_phase_samples[4]-owned_capacity_before,
            MEM32(peer+4),(unsigned long long)(MEM64(context+160)-MEM64(context+88)));
    }
    for(unsigned i=0;i<4;++i)CHECK(graphics_fence_fixture_calls[i]==3);
    CHECK(!thread_cpu_calls && graphics_fence_lock_samples==graphics_fence_blocked_explicit+graphics_fence_blocked_implicit);
    CHECK(!pilot_io_device_work_bind(NULL));CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    CHECK(!call_graphics(PILOT_D3D_D3DDevice_Release,NULL) && !graphics_active);
    uint32_t gate=native_component_original_gate;graphics_original_gate=native_component_original_gate=0;pool_free(gate);
    if(!graphics_fence_phase_samples[3])fprintf(stderr,"OBLIGATION: fixed owned GPU batch completed before source-bound explicit wait reached actual private KWAIT; no workload escalation\n");
    if(!graphics_fence_phase_samples[4])fprintf(stderr,"OBLIGATION: fixed owned GPU batch produced no genuine64-slot production capacity wait; no fabricated busy result\n");
    CHECK(graphics_fence_phase_samples[3] && graphics_fence_phase_progress[3] && graphics_fence_phase_samples[4] && graphics_fence_phase_progress[4]);
    CHECK(graphics_fence_tick_last>graphics_fence_tick_first);
    fprintf(stderr,"PASS: three raw original CPU fence lives, source-bound RET0/RET4/Clear, original216-token ABI plus real production65-capacity owner, actual private-event KWAIT/compute-peer IRQ progress with graphics+VM locks released and original gate retained, exact accumulated GPU bytes before cleanup, GPR/ESP/flags/x87/XMM and exact memory/thread/handle retirement\n");
}
#undef GRAPHICS_FENCE_CODE
