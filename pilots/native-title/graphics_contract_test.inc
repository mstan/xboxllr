static int graphics_test_has_entries(const char *fixture,const unsigned *entries,unsigned count)
{
    for(unsigned i=0;i<count;++i)if(entries[i]>=PILOT_D3D_COUNT) {
        fprintf(stderr,"SKIP: %s requires an unavailable graphics boundary (entry=%u profile_device=%08x bindings=%u)\n",
            fixture,entries[i],PILOT_D3D_DEVICE_OBJECT,(unsigned)PILOT_D3D_COUNT);
        return 0;
    }
    return 1;
}
#define GRAPHICS_TEST_REQUIRE(fixture,...) do { \
    const unsigned required_entries[]={__VA_ARGS__}; \
    if(!graphics_test_has_entries((fixture),required_entries,sizeof(required_entries)/sizeof(*required_entries)))exit(77); \
} while(0)
static uint32_t call_graphics(unsigned entry,const uint32_t *args)
{
    CHECK(entry<PILOT_D3D_COUNT);
    g_esp=0x18000; MEM32(g_esp)=0x12340;
    unsigned bytes=pilot_d3d_calls[entry].stack_bytes;
    if (bytes) memcpy(memory+g_esp+4,args,bytes);
    g_ebx=0x12345678; g_esi=0x23456789; g_edi=0x3456789a; g_ebp=0x456789ab;
    pilot_d3d_dispatch(entry);
    CHECK(g_esp==0x18004+bytes);
    CHECK(g_ebx==0x12345678 && g_esi==0x23456789 && g_edi==0x3456789a && g_ebp==0x456789ab);
    return g_eax;
}
#include "graphics_state_contract_test.inc"
#include "graphics_interpolation_contract_test.inc"
#include "graphics_texture_contract_test.inc"
#include "graphics_write_watch_contract_test.inc"
#include "graphics_vertex_contract_test.inc"
#include "graphics_borrowed_surface_contract_test.inc"
#include "graphics_shader_contract_test.inc"
#include "graphics_clock_contract_test.inc"
#include "graphics_stateblock_contract_test.inc"
#include "graphics_binding_contract_test.inc"
#include "graphics_transform_contract_test.inc"
#include "graphics_completion_contract_test.inc"
#include "graphics_fvf_contract_test.inc"
#include "graphics_visibility_contract_test.inc"
#include "graphics_gpu_callback_contract_test.inc"
#include "graphics_alias_contract_test.inc"
#include "graphics_x8_texture_contract_test.inc"
#include "graphics_reset_contract_test.inc"
#include "graphics_present_contract_test.inc"
#include "graphics_resource_wait_contract_test.inc"
#include "graphics_commands_contract_test.inc"
#include "graphics_capacity_contract_test.inc"
#include "graphics_immediate_contract_test.inc"
#include "graphics_fvf_beta_contract_test.inc"
static void test_graphics_guest_scheduler(void)
{
    GRAPHICS_TEST_REQUIRE("graphics scheduler callbacks/fences",PILOT_D3D_D3DDevice_InsertCallback,
        PILOT_D3D_D3DDevice_InsertFence,PILOT_D3D_D3DDevice_BlockOnFence);
    vm_ensure();dispatch_ensure();
    uint32_t page=PILOT_D3D_DEVICE_GLOBAL&~4095u;
    uint32_t bytes=((PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET+4+4095)&~4095u)-page;
    commit(page,bytes);vm_fixed_region(page,bytes);
    const uint32_t parameters[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};
    memcpy(memory+0x19000,parameters,sizeof(parameters));
    scheduler_enabled=1;scheduler_publish(0,0,0);scheduler_enter(0);
    uint32_t create[]={0,1,0,PILOT_D3D_CREATE_BEHAVIOR_MASK,0x19000,0x19100};
    CHECK(call_graphics(PILOT_D3D_Direct3D_CreateDevice,create)==0);
    gpu_test_caller=GetCurrentThreadId();gpu_test_count=0;
    gpu_test_entered=CreateEventW(NULL,TRUE,FALSE,NULL);
    gpu_test_release=CreateEventW(NULL,TRUE,TRUE,NULL);CHECK(gpu_test_entered && gpu_test_release);
    uint32_t clear[]={0,0,0xf1,0xffff0000,0x3f800000,0};
    CHECK(call_graphics(PILOT_D3D_D3DDevice_Clear,clear)==0);
    AcquireSRWLockExclusive(&graphics_lock);gpu_test_serial=pilot_gfx_checkpoint();ReleaseSRWLockExclusive(&graphics_lock);
    for(unsigned i=1;i<=70;++i) {
        uint32_t args[]={i&1,0x12360,i},out=0x19600;
        call_graphics(PILOT_D3D_D3DDevice_InsertCallback,args);
        call_graphics(PILOT_D3D_D3DDevice_GetDisplayMode,&out);
        CHECK(gpu_test_count==(LONG)i && scheduler_owner==1);
    }
    uint32_t fence=call_graphics(PILOT_D3D_D3DDevice_InsertFence,NULL);
    call_graphics(PILOT_D3D_D3DDevice_BlockOnFence,&fence);
    CHECK(graphics_gpu_delivered==70 && !graphics_gpu_head && !graphics_gpu_running);
    call_graphics(PILOT_D3D_D3DDevice_Release,NULL);scheduler_retire();
    CHECK(!graphics_active && !scheduler_owner);
    CloseHandle(gpu_test_entered);CloseHandle(gpu_test_release);
    fprintf(stderr,"PASS: scheduled real GPU WRITE/READ callbacks, pixels, ordering waits, reentrant guest queries, fences and teardown without CPU/renderer lock inversion\n");
}
static void test_graphics(int focused_fixture)
{
    if(focused_fixture==19) {
#ifdef PILOT_D3D_PHOBOS
        struct PilotVisualMods options;pilot_visual_mods_default(&options);
        options.frame_mode=PILOT_FRAMES_INTERPOLATED;options.frame_rate=60;
        CHECK(pilot_gfx_visual_mods(&options));
#else
        fprintf(stderr,"SKIP: interpolation requires the qualified PHOBOS backend\n");exit(77);
#endif
    }
    if(focused_fixture>=4) {
        GRAPHICS_TEST_REQUIRE("deferred state relevance fixture",PILOT_D3D_Direct3D_CreateDevice,
            PILOT_D3D_D3DDevice_SetVertexShader,PILOT_D3D_D3DDevice_Release);
    } else {
        GRAPHICS_TEST_REQUIRE("graphics lifecycle/front-back/fence fixture",PILOT_D3D_D3DDevice_GetBackBuffer2,
            PILOT_D3D_D3DDevice_InsertCallback,PILOT_D3D_D3DDevice_InsertFence,PILOT_D3D_D3DDevice_BlockOnFence);
    }
    vm_ensure();
    dispatch_ensure(); /* Persistent shared callback stack precedes allocation baseline. */
    uint32_t global_page=PILOT_D3D_DEVICE_GLOBAL&~4095u;
    uint32_t state_bytes=((PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET+4+4095)&~4095u)-global_page;
    if(focused_fixture>=4) {
        uint32_t low=PILOT_D3D_DEVICE_OBJECT<PILOT_D3D_TEXTURE_STATES?PILOT_D3D_DEVICE_OBJECT:PILOT_D3D_TEXTURE_STATES;
        if(PILOT_D3D_DEVICE_GLOBAL<low)low=PILOT_D3D_DEVICE_GLOBAL;
        uint32_t high=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET+4;
#ifdef PILOT_D3D_DEVICE_BYTES
        high=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_DEVICE_BYTES;
#endif
        const uint32_t ends[]={PILOT_D3D_DEVICE_GLOBAL+4,PILOT_D3D_RENDER_STATES+166*4,
            PILOT_D3D_TEXTURE_STATES+512,PILOT_D3D_STREAMS+16*12,PILOT_D3D_FVF_OBJECT+256};
        for(unsigned i=0;i<sizeof(ends)/sizeof(*ends);++i)if(ends[i]>high)high=ends[i];
        global_page=low&~4095u;state_bytes=((high+4095)&~4095u)-global_page;
    }
    commit(global_page,state_bytes); vm_fixed_region(global_page,state_bytes);
    uint32_t *parameters=(uint32_t*)(memory+0x19000);
    const uint32_t valid[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};
    uint32_t create[]={0,1,0,PILOT_D3D_CREATE_BEHAVIOR_MASK,0x19000,0x19100};
    memcpy(parameters,valid,sizeof(valid)); parameters[0]=0;
    MEM32(0x19100)=0xdeadbeef;
    CHECK(call_graphics(PILOT_D3D_Direct3D_CreateDevice,create)&0x80000000u);
    CHECK(!MEM32(0x19100) && !MEM32(PILOT_D3D_DEVICE_GLOBAL) && !graphics_active);
    parameters[0]=64; parameters[4]=0x1021;
    CHECK(call_graphics(PILOT_D3D_Direct3D_CreateDevice,create)==0x80004001u);
    CHECK(!graphics_active && !MEM32(PILOT_D3D_DEVICE_GLOBAL));
    parameters[4]=0x11;
    if(focused_fixture) {
        uint32_t warm=pool_allocate(24,0x48584554u);CHECK(warm);pool_free(warm);
    }
    uint64_t resident=vm_committed(0,0x100000000ull);
    unsigned char pixels[64*64*4];
    for (unsigned pass=0;pass<3;++pass) {
        create[5]=pass==1?0:0x19100; /* Original Create permits a null output. */
        CHECK(call_graphics(PILOT_D3D_Direct3D_CreateDevice,create)==0);
        CHECK(graphics_active && MEM32(PILOT_D3D_DEVICE_GLOBAL)==PILOT_D3D_DEVICE_OBJECT);
        CHECK(!av_saved_data);
        if(focused_fixture) {
            if(focused_fixture==1)test_graphics_depth_sampling_header();
            else if(focused_fixture==2)test_graphics_x8_texture_lifetime();
            else if(focused_fixture==3)test_graphics_present_status();
            else if(focused_fixture==9)test_graphics_1555_storage();
            else if(focused_fixture==10)test_graphics_geometry_capacity();
            else if(focused_fixture==11 || focused_fixture==12)test_graphics_immediate_vertices(focused_fixture==12);
            else if(focused_fixture==13 || focused_fixture==14)test_graphics_fvf_beta_declaration(focused_fixture==14);
            else if(focused_fixture==15)test_graphics_vertex_header_storage();
            else if(focused_fixture==16)test_graphics_borrowed_depth_surface();
            else if(focused_fixture==17)test_graphics_states();
            else if(focused_fixture==18)test_graphics_texture_write_watch();
            else if(focused_fixture==19)test_graphics_interpolation_service();
            else test_graphics_deferred_relevance((unsigned)focused_fixture-4);
            CHECK(!call_graphics(PILOT_D3D_D3DDevice_Release,NULL));
            CHECK(!graphics_active && !graphics_headers && !MEM32(PILOT_D3D_DEVICE_GLOBAL));
            if(focused_fixture==10)CHECK(!graphics_vertices && !graphics_indices && !graphics_vertex_capacity && !graphics_index_capacity);
            CHECK(vm_committed(0,0x100000000ull)==resident);
            continue;
        }
        if(pass==2) {
            test_graphics_callback_close();
            CHECK(!graphics_active && !graphics_headers && !MEM32(PILOT_D3D_DEVICE_GLOBAL));
            CHECK(vm_committed(0,0x100000000ull)==resident);
            continue;
        }
        if(!pass){test_graphics_clock();test_graphics_callbacks();}
        CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET)==0);
        if (create[5]) CHECK(MEM32(create[5])==PILOT_D3D_DEVICE_OBJECT);
        uint32_t caps=0x19304;
        memset(memory+caps-4,0xcd,sizeof(pilot_d3d_device_caps)+8);
        call_graphics(PILOT_D3D_D3DDevice_GetDeviceCaps,&caps);
        CHECK(!memcmp(memory+caps,pilot_d3d_device_caps,sizeof(pilot_d3d_device_caps)));
        CHECK(MEM32(caps-4)==0xcdcdcdcdu && MEM32(caps+sizeof(pilot_d3d_device_caps))==0xcdcdcdcdu);
        uint32_t mode=0x19404,expected_mode[]={64,64,60,0x40,0x12};
        memset(memory+mode-4,0xcd,28);
        call_graphics(PILOT_D3D_D3DDevice_GetDisplayMode,&mode);
        CHECK(!memcmp(memory+mode,expected_mode,sizeof(expected_mode)));
        CHECK(MEM32(mode-4)==0xcdcdcdcdu && MEM32(mode+20)==0xcdcdcdcdu);
        CHECK(!memcmp(parameters,valid,sizeof(valid)));
        uint32_t arg=0,back=call_graphics(PILOT_D3D_D3DDevice_GetBackBuffer2,&arg);
        arg=0xffffffffu; uint32_t front=call_graphics(PILOT_D3D_D3DDevice_GetBackBuffer2,&arg);
        CHECK(back!=front && back==graphics_headers && front==graphics_headers+32);
        CHECK(MEM32(back)==0x010d0001u && MEM32(front)==0x010d0001u);
        CHECK(MEM32(back+12)==0x11221 && MEM32(back+16)==(63u|(63u<<12)|(3u<<24)));
        uint32_t surface_desc[]={front,0x19444},expected_desc[]={0x12,1,1,64*64*4,0x11,64,64};
        memset(memory+0x19440,0xcd,36);
        call_graphics(PILOT_D3D_D3DSurface_GetDesc,surface_desc);
        CHECK(!memcmp(memory+0x19444,expected_desc,28));
        CHECK(MEM32(0x19440)==0xcdcdcdcdu && MEM32(0x19460)==0xcdcdcdcdu);
        MEMORY_BASIC_INFORMATION info;
        CHECK(VirtualQuery(memory+MEM32(back+4),&info,sizeof(info)) && info.State==MEM_RESERVE);
        CHECK(call_graphics(PILOT_D3D_D3DResource_AddRef,&back)==2);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&back)==1);
        uint32_t viewport[]={2,3,48,40,0x3e800000,0x3f400000},observed[6];
        memcpy(memory+0x19200,viewport,sizeof(viewport)); arg=0x19200;
        CHECK(call_graphics(PILOT_D3D_D3DDevice_SetViewport,&arg)==0);
        CHECK(pilot_gfx_get_viewport(observed)==0 && !memcmp(viewport,observed,sizeof(viewport)));
        uint32_t clear[]={0,0,0xf3,0xffff0000,0x3f800000,0};
        CHECK(call_graphics(PILOT_D3D_D3DDevice_Clear,clear)==0);
        CHECK(pilot_gfx_readback(pixels,sizeof(pixels))>=0);
        CHECK(pixels[0]==255 && pixels[1]==0 && pixels[2]==0);
        arg=0;uint32_t submitted=call_graphics(PILOT_D3D_D3DDevice_Swap,&arg);
        CHECK(submitted==1 && MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SWAP_COUNTER)==submitted);
        DWORD wait_start=GetTickCount();uint32_t completed=0;
        do {
            AcquireSRWLockShared(&graphics_lock);completed=graphics_swap_completed;ReleaseSRWLockShared(&graphics_lock);
            if(completed==submitted)break;Sleep(1);
        } while(GetTickCount()-wait_start<2000);
        CHECK(completed==submitted);
        if(!pass) {
            uint32_t first_fence=0,last_fence=0;
            for(unsigned n=0;n<70;++n) {
                CHECK(call_graphics(PILOT_D3D_D3DDevice_Clear,clear)==0);
                uint32_t fence=call_graphics(PILOT_D3D_D3DDevice_InsertFence,NULL);
                if(!first_fence)first_fence=fence;
                CHECK(fence>last_fence);last_fence=fence;
            }
            call_graphics(PILOT_D3D_D3DDevice_BlockOnFence,&last_fence);
            CHECK(graphics_fence_done>=last_fence);
            call_graphics(PILOT_D3D_D3DDevice_BlockOnFence,&first_fence);
        }
        clear[3]=0xff0000ff; CHECK(call_graphics(PILOT_D3D_D3DDevice_Clear,clear)==0);
        CHECK(pilot_gfx_surface_readback(1,pixels,sizeof(pixels))>=0);
        CHECK(pixels[0]==255 && pixels[1]==0 && pixels[2]==0);
        CHECK(pilot_gfx_surface_readback(0,pixels,sizeof(pixels))>=0);
        CHECK(pixels[0]==0 && pixels[1]==0 && pixels[2]==255);
        if(!pass) {
            uint64_t before=vm_committed(0,0x100000000ull);
            for(unsigned repeat=0;repeat<3;++repeat) {
                CHECK(call_graphics(PILOT_D3D_D3DDevice_PersistDisplay,NULL)==0);
                uint32_t saved=av_saved_data;CHECK(saved && physical_find(saved,64+64*64*4));
                CHECK(MEM32(saved)==0x81050001 && MEM32(saved+4)==((saved+64)&0x0fffffff));
                CHECK(MEM32(saved+12)==0x11229 && MEM32(saved+16)==0x0303f03f);
                for(unsigned pixel=0;pixel<64*64;++pixel)CHECK(MEM32(saved+64+pixel*4)==0xffff0000);
                for(unsigned page=0;page<5;++page)CHECK(physical_persistent[(saved&0x0fffffff)/4096+page]);
                CHECK(vm_committed(0,0x100000000ull)==before+5*4096);
            }
        }
        if(!pass) { test_graphics_states(); test_graphics_textures(); test_graphics_vertices(); test_graphics_transforms(); test_graphics_shaders(); test_graphics_fvf(); test_graphics_pixels(); test_graphics_stream_consumption(); test_graphics_blocks(); test_graphics_completion(); test_graphics_resource_bindings(); }
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&front)==0);
        test_graphics_visibility();
        test_graphics_aliases();
        test_graphics_depth_sampling_header();
        test_graphics_gpu_callbacks();
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&back)==0);
        if(pass==1)test_graphics_reset();
        uint32_t final_gpu_callback[]={0,0x12360,71};
        call_graphics(PILOT_D3D_D3DDevice_InsertCallback,final_gpu_callback);
        CHECK(call_graphics(PILOT_D3D_D3DDevice_Release,NULL)==0);
        CHECK(gpu_test_count==71 && !graphics_gpu_head && !graphics_gpu_running && !dispatch_gpu_routine);
        CHECK(!graphics_active && !graphics_headers && !MEM32(PILOT_D3D_DEVICE_GLOBAL));
        CHECK(vm_committed(0,0x100000000ull)==resident+(!pass?5*4096:0));
        if(!pass)CHECK(av_saved_data && MEM32(av_saved_data+64)==0xffff0000);
        CHECK(!vm_find(back,24));
    }
    if(focused_fixture==19) {
        struct PilotVisualMods options;pilot_visual_mods_default(&options);CHECK(pilot_gfx_visual_mods(&options));
    }
    fprintf(stderr,"PASS: guest graphics ABI, failures/null output, real clear/present pixels, distinct front/back resources, references and release/recreate\n");
}
