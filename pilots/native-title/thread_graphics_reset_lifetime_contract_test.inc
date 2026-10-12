#include "io_device_config.h"
/* Original CPU ownership fixture. Only authored adapter helpers queue a
 * finite native workload; original whole Reset/Release entries perform all
 * waits and retirement. Every success check precedes diagnostic readback. */
#define RESET_LIFE_CPU 0x16800u
#define RESET_LIFE_HELPER 0x16d80u
#define RESET_LIFE_CONTEXT 0x1b000u
#define RESET_LIFE_HEADER 0x1c000u
#define RESET_LIFE_CODE(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static uint32_t reset_life_thread,reset_life_peer,reset_life_counter;
static unsigned reset_life_samples[3],reset_life_progress[3],reset_life_calls[4],reset_life_closes;
static uint64_t reset_life_token,reset_life_serial,reset_life_first_tick,reset_life_last_tick;
static const char *test_reset_life_work(void)
{
    const char *error=graphics_original_clock_work();if(error)return error;
    unsigned phase=MEM32(RESET_LIFE_CONTEXT+132);
    if(!phase || !reset_life_thread || !graphics_clock_waiters)return NULL;
    CHECK(phase<=2);
    unsigned char waiter[0x140],peer[0x140];
    if(!component_wait_thread_snapshot(reset_life_thread,waiter) || waiter[0x2c]!=5)return NULL;
    uint32_t current=MEM32(0x8003da64u);
    CHECK(current==reset_life_peer && component_wait_thread_snapshot(current,peer) && peer[0x2c]==2 && peer[0x32]==29);
    CHECK(component_wait_snapshot_word(peer,0x130)==THREAD_GRAPHICS_FENCE_PEER);
    uint32_t frame=MEM32(RESET_LIFE_CONTEXT+176),bottom=component_wait_snapshot_word(waiter,0x20),top=component_wait_snapshot_word(waiter,0x1c);
    CHECK(frame>=bottom+68 && (uint64_t)frame+COMPONENT_WAIT_BYTES<=top);
    uint32_t words[8],caller[5],event_object=0,block[6];
    CHECK(component_wait_owned_bytes(frame,words,sizeof(words)) && words[0] && words[1]==0x13579bdf && words[6]==0x2468ace0 && words[7]==0x89abcdef);
    CHECK((int64_t)MEM64(frame+8)==-10000 && words[4]==1 && !words[5]);
    CHECK(component_wait_owned_bytes(frame-68,&event_object,4) && event_object);
    CHECK(component_wait_owned_bytes(frame-36,caller,sizeof(caller)) && caller[0]==0x800249f1u && caller[1]==words[0] && !caller[2] && !caller[3] && caller[4]==frame+8);
    uint32_t wait_block=component_wait_snapshot_word(waiter,0x58);
    CHECK(wait_block>=bottom && (uint64_t)wait_block+sizeof(block)<=top);
    CHECK(component_wait_owned_bytes(wait_block,block,sizeof(block)) && block[2]==reset_life_thread && block[3]==event_object && waiter[0x57]==6);
    CHECK(TryAcquireSRWLockExclusive(&graphics_lock));CHECK(TryAcquireSRWLockExclusive(&vm_lock));ReleaseSRWLockExclusive(&vm_lock);
    CHECK(graphics_original_gate==native_component_original_gate && MEM32(graphics_original_gate+20)==1 && MEM32(graphics_original_gate+24)==reset_life_thread);
    struct GraphicsVertexObject *vertex=graphics_vertex(RESET_LIFE_HEADER);
    CHECK(!vertex->refs && vertex->bindings==1 && vertex->original_reference==reset_life_token);
    CHECK(graphics_active && !graphics_clock_closing);
    uint32_t counter=MEM32(GRAPHICS_FENCE_PEER_CONTEXT+4);CHECK(counter>=reset_life_counter);
    if(counter>reset_life_counter)++reset_life_progress[phase];reset_life_counter=counter;++reset_life_samples[phase];
    ReleaseSRWLockExclusive(&graphics_lock);
    uint64_t ticks=0,next=0;CHECK(!pilot_usb_pit_clock(&ticks,&next) && next);
    if(!reset_life_first_tick)reset_life_first_tick=ticks;CHECK(ticks>=reset_life_last_tick);reset_life_last_tick=ticks;
    return NULL;
}
static void reset_life_check_completion(void)
{
    struct PilotGpuProgress gpu;pilot_gfx_completion_stats(&gpu);
    CHECK(gpu.completed>=reset_life_serial && !gpu.pending_queries);
    CHECK(!pilot_gfx_checkpoint_busy(reset_life_serial));
    graphics_fence_owned_pixels(reset_life_serial);
}
static void test_lifetime_close(void)
{
    if(MEM32(RESET_LIFE_CONTEXT+132)==2) {
        /* Assertions happen before readback, texture destruction or real
         * backend close can wait/flush. No completion result is replaced. */
        CHECK(!graphics_clock_waiters && !graphics_present_count);
        reset_life_check_completion();
        CHECK(graphics_vertex_find(RESET_LIFE_HEADER)<0);
        CHECK(!pilot_io_device_contiguous_resource_release(reset_life_token));
        uint32_t available=0;CHECK(pilot_io_device_contiguous_resource_query(MEM32(RESET_LIFE_CONTEXT),8192,&available) && available==8192);
        CHECK(MEM32(MEM32(RESET_LIFE_CONTEXT))==0x13572468 && MEM32(MEM32(RESET_LIFE_CONTEXT)+8192-4)==0x24681357);
        pilot_gfx_texture_destroy(graphics_fence_work_texture);graphics_fence_work_texture=0;++reset_life_closes;
    }
    pilot_gfx_close();
}
static const char *test_reset_life_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==RESET_LIFE_HELPER && pilot_io_device_thread_component_active());
    unsigned phase=regs[1];uint32_t context=regs[2];CHECK(context==RESET_LIFE_CONTEXT && phase<4);
    CHECK(regs[3]==0x11223344 && regs[5]==0x44556677 && regs[6]==0x22334455 && regs[7]==context);
    ++reset_life_calls[phase];const char *error=graphics_original_enter();if(error)return error;AcquireSRWLockExclusive(&graphics_lock);
    uint32_t base=MEM32(context);CHECK(MEM32(context+4)==8192 && !physical_find(base,1));
    if(phase<2) {
        struct GraphicsVertexObject *vertex=graphics_vertex(RESET_LIFE_HEADER);
        CHECK(vertex->data==base && vertex->bytes==8192 && vertex->external && !vertex->refs && vertex->bindings==1);
        CHECK(MEM32(PILOT_D3D_STREAMS+8)==RESET_LIFE_HEADER && MEM32(RESET_LIFE_HEADER+4)==(base&0x0fffffffu));
        CHECK(MEM32(base)==0x13572468 && MEM32(base+4096)==0xa5b6c7d8 && MEM32(base+8192-4)==0x24681357);
        if(!phase) {
            reset_life_token=vertex->original_reference;CHECK(reset_life_token);
            pilot_gfx_vertex_upload(vertex->native,memory+base,8192);
            CHECK(!graphics_fence_work_texture);
            CHECK(!pilot_gfx_texture_create_usage(GRAPHICS_FENCE_WORK_SIDE,GRAPHICS_FENCE_WORK_SIDE,1,0x12,1,&graphics_fence_work_texture));
        } else {
            CHECK(vertex->original_reference==reset_life_token && !MEM32(context+84));reset_life_check_completion();
            unsigned char expected[8192]={0},observed[8192];
            uint32_t first=0x13572468,second=0xa5b6c7d8,last=0x24681357;
            memcpy(expected,&first,4);memcpy(expected+4096,&second,4);memcpy(expected+8192-4,&last,4);
            CHECK(!pilot_gfx_vertex_readback(vertex->native,observed,sizeof(observed)) && !memcmp(observed,expected,sizeof(expected)));
            CHECK(graphics_parameters[0]==64 && graphics_parameters[1]==64 && graphics_parameters[12]==1);
        }
        struct PilotGpuProgress before,after;pilot_gfx_completion_stats(&before);graphics_fence_owned_work();pilot_gfx_completion_stats(&after);
        CHECK(after.submitted==before.submitted+65);reset_life_serial=after.submitted;
        CHECK(pilot_gfx_resource_serial(PILOT_GFX_TEXTURE,graphics_fence_work_texture)==reset_life_serial);
        MEM64(context+192+phase*8)=reset_life_serial;
    } else if(phase==2) {
        CHECK(!graphics_active && !MEM32(PILOT_D3D_DEVICE_GLOBAL) && !MEM32(context+88));
        CHECK(graphics_vertex_find(RESET_LIFE_HEADER)<0 && !graphics_clock_waiters && !graphics_present_count && !graphics_fence_work_texture);
        CHECK(!pilot_io_device_contiguous_resource_release(reset_life_token));
        uint32_t available=0;CHECK(pilot_io_device_contiguous_resource_query(base,8192,&available) && available==8192);
        CHECK(MEM32(base)==0x13572468 && MEM32(base+8192-4)==0x24681357);
    } else {
        uint32_t available=0xdeadbeef;MEMORY_BASIC_INFORMATION host;
        CHECK(!pilot_io_device_contiguous_resource_query(base,1,&available) && available==0xdeadbeef);
        CHECK(VirtualQuery(memory+base,&host,sizeof(host)) && host.State==MEM_RESERVE);
    }
    ReleaseSRWLockExclusive(&graphics_lock);error=graphics_original_leave();if(error)return error;regs[4]+=4;return NULL;
}
static void reset_life_check(struct ThreadChildCode *c,unsigned phase)
{RESET_LIFE_CODE(c,0xb9);child_word(c,phase);RESET_LIFE_CODE(c,0x89,0xfa,0xb8);child_word(c,RESET_LIFE_HELPER);RESET_LIFE_CODE(c,0xff,0xd0);}
static void test_thread_graphics_reset_lifetime(const char *path)
{
    install_fault_handler();g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    /* Same real captured worker priority ownership as pilot_io_device_files,
     * without attaching or opening any HDD: this domain has no disk calls. */
    /* No guest PCR page is published before a component owns it. Bind the
     * initial worker from the same SHA-checked immutable capture loaded by
     * initialize(); the actual148/36 calls publish/own its real object. */
    FILE *snapshot=fopen(PILOT_IO_DEVICE_SNAPSHOT,"rb");CHECK(snapshot);
    uint32_t initial_worker=0;CHECK(!fseek(snapshot,0x3da64,SEEK_SET) && fread(&initial_worker,4,1,snapshot)==1);fclose(snapshot);
    CHECK(initial_worker>=0x80010000u && initial_worker<0xe0000000u && !(initial_worker&3));
    uint32_t root_priority[]={initial_worker,31};event_lle_call(148,root_priority);event_lle_call(36,root_priority);
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));pilot_io_device_port_bind(0x80cf,0x80cf,test_graphics_fence_board_port);
    vm_ensure();uint32_t page=PILOT_D3D_DEVICE_GLOBAL&~4095u,bytes=((PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET+4+4095)&~4095u)-page;
    commit(page,bytes);vm_fixed_region(page,bytes);test_graphics_component_sources(path);
    CHECK(pilot_d3d_calls[PILOT_D3D_D3DDevice_Reset].stack_bytes==4 && !pilot_d3d_calls[PILOT_D3D_D3DDevice_Release].stack_bytes);
    const uint32_t parameters[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};memcpy(memory+0x1a200,parameters,sizeof(parameters));
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};char identity[65];memcpy(memory+RESET_LIFE_HELPER,helper,sizeof(helper));test_component_hash(helper,sizeof(helper),identity);
    CHECK(!pilot_io_device_thread_component_bind_abi(RESET_LIFE_HELPER,sizeof(helper),identity,0,test_reset_life_body));
    struct ThreadChildCode code={0},*c=&code;
    RESET_LIFE_CODE(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68);
    const uint32_t create[]={0,1,0,0x40,0x1a200,0x1a280};for(unsigned i=6;i;--i)child_push(c,create[i-1]);graphics_resource_call(c,PILOT_D3D_Direct3D_CreateDevice);
    child_push_field(c,100);child_call(c,165);RESET_LIFE_CODE(c,0x89,0x07);child_push_field(c,0);child_call(c,180);RESET_LIFE_CODE(c,0x89,0x47,4);
    RESET_LIFE_CODE(c,0x8b,0x07,0x57,0x89,0xc7,0x31,0xc0,0xb9,0,8,0,0,0xf3,0xab,0x5f,0x8b,0x07,
        0xc7,0x00,0x68,0x24,0x57,0x13,0xc7,0x80,0,0x10,0,0,0xd8,0xc7,0xb6,0xa5,0xc7,0x80,0xfc,0x1f,0,0,0x57,0x13,0x68,0x24,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,0xbe,0x55,0x44,0x33,0x22);
    child_push_field(c,0);child_push(c,RESET_LIFE_HEADER);graphics_resource_call(c,PILOT_D3D_D3DResource_Register);
    child_push(c,4);child_push(c,RESET_LIFE_HEADER);child_push(c,0);graphics_resource_call(c,PILOT_D3D_D3DDevice_SetStreamSource);
    child_push(c,RESET_LIFE_HEADER);graphics_resource_call(c,PILOT_D3D_D3DResource_Release);RESET_LIFE_CODE(c,0x89,0x47,80);reset_life_check(c,0);
    RESET_LIFE_CODE(c,0xc7,0x87,132,0,0,0,1,0,0,0,0x8d,0x44,0x24,0xd8,0x89,0x87,176,0,0,0);
    child_push(c,0x1a200);graphics_resource_call(c,PILOT_D3D_D3DDevice_Reset);RESET_LIFE_CODE(c,0x89,0x47,84);reset_life_check(c,1);
    RESET_LIFE_CODE(c,0xc7,0x87,132,0,0,0,2,0,0,0,0x8d,0x44,0x24,0xdc,0x89,0x87,176,0,0,0,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8);
    graphics_resource_call(c,PILOT_D3D_D3DDevice_Release);RESET_LIFE_CODE(c,0x89,0x47,88,0x9c,0x5a,0x89,0x57,12);reset_life_check(c,2);
    child_push_field(c,0);child_call(c,171);reset_life_check(c,3);
    RESET_LIFE_CODE(c,0xc7,0x87,132,0,0,0,0,0,0,0,0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x5f,112,0x89,0x6f,116,0x89,0x77,120,0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<RESET_LIFE_HELPER-RESET_LIFE_CPU);memcpy(memory+RESET_LIFE_CPU,c->bytes,c->size);
    const uint8_t peer_code[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,0x0f,0x10,0x47,16,0xdd,0x47,48,0x89,0x67,68,
        0xb9,0xdf,0x9b,0x57,0x13,0xba,0xe0,0xac,0x68,0x24,0xbb,0x98,0xba,0xdc,0xfe,0xbd,0x21,0x43,0x65,0x87,0xbe,0x78,0x56,0x34,0x12,
        0x83,0x3f,0,0x75,5,0xff,0x47,4,0xeb,0xf6,0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0x89,0x4f,76,0x89,0x57,80,0x89,0x5f,84,0x89,0x6f,88,0x89,0x77,92,0xc7,0x47,8,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+THREAD_GRAPHICS_FENCE_PEER,peer_code,sizeof(peer_code));CHECK(!pilot_io_device_resident(0x10000,0x10000));
    CHECK(!pilot_io_device_clock_start() && pilot_io_device_clock_bound());CHECK(!graphics_original_clock_bind());CHECK(!pilot_io_device_work_bind(test_reset_life_work));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    uint32_t baseline_memory[9],after_memory[9];test_memory_snapshot(baseline_memory);
    for(unsigned life=0;life<3;++life) {
        uint32_t context=RESET_LIFE_CONTEXT,out=0x19800,peer=GRAPHICS_FENCE_PEER_CONTEXT;memset(memory+context,0,512);memset(memory+peer,0,128);
        MEM32(context+64)=RESET_LIFE_CPU;MEM32(context+96)=0xa5a55a5a;MEM32(context+100)=8192;MEM32(peer+64)=THREAD_GRAPHICS_FENCE_PEER;MEM32(peer+12)=0xa5a55a5a;
        for(unsigned i=0;i<16;++i){MEM8(context+16+i)=(uint8_t)(0x35+i*7);MEM8(peer+16+i)=(uint8_t)(0x7d+i*3);}
        double value=12345.6789;memcpy(memory+context+48,&value,8);memcpy(memory+peer+48,&value,8);MEM32(RESET_LIFE_HEADER)=1;MEM32(RESET_LIFE_HEADER+4)=MEM32(RESET_LIFE_HEADER+8)=0;
        unsigned sample_before[]={reset_life_samples[1],reset_life_samples[2]},progress_before[]={reset_life_progress[1],reset_life_progress[2]};
        uint32_t peer_create[]={out+16,0,THREAD_GRAPHICS_FENCE_PEER,peer,0};CHECK(!event_lle_call(254,peer_create));uint32_t peer_handle=MEM32(out+16),peer_ref[]={peer_handle,0,out+20};CHECK(!event_lle_call(246,peer_ref));reset_life_peer=MEM32(out+20);reset_life_counter=0;
        uint32_t priority[]={reset_life_peer,29};CHECK(event_lle_call(148,priority)==8);
        uint32_t worker_create[]={out,0,RESET_LIFE_CPU,context,0};CHECK(!event_lle_call(254,worker_create));uint32_t handle=MEM32(out),ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));reset_life_thread=MEM32(out+4);
        priority[0]=reset_life_thread;priority[1]=30;CHECK(event_lle_call(148,priority)==8);uint32_t join[]={reset_life_thread,0,0,0,0};CHECK(!event_lle_call(159,join));
        MEM32(peer)=1;uint32_t peer_join[]={reset_life_peer,0,0,0,0};CHECK(!event_lle_call(159,peer_join));
        CHECK(!event_lle_call(187,&handle));event_lle_call(250,&reset_life_thread);CHECK(event_lle_call(187,&handle)==0xc0000008u);
        CHECK(!event_lle_call(187,&peer_handle));event_lle_call(250,&reset_life_peer);CHECK(event_lle_call(187,&peer_handle)==0xc0000008u);reset_life_thread=reset_life_peer=0;
        CHECK(MEM32(context+76)==1 && MEM32(context+96)==0xa5a55a5a && !MEM32(context+80) && !MEM32(context+84) && !MEM32(context+88));
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72));
        CHECK(MEM32(context+112)==0x11223344 && MEM32(context+116)==0x44556677 && MEM32(context+120)==0x22334455);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(MEM32(peer+8)==1 && MEM32(peer+12)==0xa5a55a5a && MEM32(peer+68)==MEM32(peer+72));
        CHECK(MEM32(peer+76)==0x13579bdf && MEM32(peer+80)==0x2468ace0 && MEM32(peer+84)==0xfedcba98 && MEM32(peer+88)==0x87654321 && MEM32(peer+92)==0x12345678);
        CHECK(!memcmp(memory+peer+16,memory+peer+32,16) && !memcmp(memory+peer+48,memory+peer+56,8));
        CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);test_memory_snapshot(after_memory);CHECK(!memcmp(baseline_memory,after_memory,sizeof(baseline_memory)));
        CHECK(!graphics_active && !graphics_clock_waiters && !graphics_gpu_head && !graphics_gpu_running && !graphics_callback);
        CHECK(MEM32(native_component_original_gate+16)==UINT32_MAX && !MEM32(native_component_original_gate+20) && !MEM32(native_component_original_gate+24));
        CHECK(reset_life_samples[1]>sample_before[0] && reset_life_samples[2]>sample_before[1] && reset_life_progress[1]>progress_before[0] && reset_life_progress[2]>progress_before[1]);
        fprintf(stderr,"[RESET RELEASE LIFE] life=%u Reset_KWAIT=%u Release_KWAIT=%u peer=%u exact_work_bytes_each=16777216 exact_vertex_bytes=8192 exact_original_memory/thread/handles=retired\n",life,reset_life_samples[1]-sample_before[0],reset_life_samples[2]-sample_before[1],MEM32(peer+4));
    }
    CHECK(reset_life_closes==3);for(unsigned i=0;i<4;++i)CHECK(reset_life_calls[i]==3);CHECK(reset_life_last_tick>reset_life_first_tick);
    CHECK(!pilot_io_device_work_bind(NULL));CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    uint32_t gate=native_component_original_gate;graphics_original_gate=native_component_original_gate=0;pool_free(gate);
    fprintf(stderr,"PASS: three actual original CPU Create/Reset/Release ownership lives; real queued GPU work, private KWAIT/peer IRQ0 progress, bound original resource retained across Reset and retired before original171, exact GPU/physical bytes and original memory/thread/handle cleanup; no HDD/hardware\n");
}
#undef RESET_LIFE_CODE
