/* Actual callback fixture for the isolated scalar package. Its explicit test
 * effect is separate from the original game's private callback certificate. */
static uint32_t scalar_test_cache;
static uint64_t scalar_test_serial;
static unsigned scalar_test_callbacks;
static void test_scalar_callback(void)
{
    CHECK(dispatch_in_dpc && MEM8(g_fs_base+0x24)==2);
    AcquireSRWLockExclusive(&graphics_lock);uint32_t host=99;
    CHECK(!pilot_gfx_checkpoint_busy(scalar_test_serial));
    CHECK(MEM32(scalar_test_cache)==0 && pilot_gfx_scalar_current(147,&host) && host==1);
    unsigned char pixels[64*64*4];CHECK(!pilot_gfx_readback(pixels,sizeof pixels));
    CHECK(pixels[0]==0x10 && pixels[1]==0x20 && pixels[2]==0x30);
    ReleaseSRWLockExclusive(&graphics_lock);
    uint32_t output=MEM32(g_esp+4);MEM32(output)=0x12345678;MEM32(output+4)=0x87654321;
    ++scalar_test_callbacks;g_esp+=4;
}
#ifndef PILOT_SCHEDULING_LLE
static void test_graphics_scalar_preparation(void)
{
    GRAPHICS_TEST_REQUIRE("scalar callback preparation",PILOT_D3D_D3DDevice_InsertCallback);
    vm_ensure();dispatch_ensure();CHECK(pilot_write_watch_initialize(memory,(SIZE_T)0x100000000ull));
    uint32_t page=PILOT_D3D_DEVICE_GLOBAL&~4095u;
    uint32_t bytes=((PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET+4+4095)&~4095u)-page;
    commit(page,bytes);vm_fixed_region(page,bytes);
    uint32_t proof_page=(0x408b28u+900u*16)&~4095u;commit(proof_page,4096);vm_fixed_region(proof_page,4096);
    struct GraphicsGpuCallback known={0},unknown={0};uint32_t writes[128],address=0;unsigned count=0;
    known.routine=0xbcc00;known.context=900;unknown.routine=0xdeadbeef;
    CHECK(!graphics_scalar_effects(1,NULL,NULL,writes,&count));
    CHECK(!graphics_scalar_effects(1,&unknown,NULL,writes,&count));
    CHECK(!graphics_scalar_effects(0,NULL,&unknown,writes,&count));
    if(graphics_scalar_callback_effect(&known,&address)){
        CHECK(address==0x408b28u+900u*16 && graphics_scalar_effects(1,&known,&known,writes,&count) && count==2);
        CHECK(!graphics_scalar_effect_span(address,8,writes,count));
        known.context=UINT32_MAX;CHECK(!graphics_scalar_callback_effect(&known,&address));
    }
    const uint32_t parameters[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};memcpy(memory+0x19000,parameters,sizeof parameters);
    scheduler_enabled=1;scheduler_publish(0,0,0);scheduler_enter(0);
    uint32_t create[]={0,1,0,PILOT_D3D_CREATE_BEHAVIOR_MASK,0x19000,0x19100};CHECK(!call_graphics(PILOT_D3D_Direct3D_CreateDevice,create));
    AcquireSRWLockExclusive(&graphics_lock);graphics_render_state(147,0,1);
    scalar_test_cache=PILOT_D3D_RENDER_STATES+4*(unsigned)graphics_render_guest_index(147);
    CHECK(!pilot_gfx_clear(1,0xff102030,1,0));scalar_test_serial=pilot_gfx_checkpoint();
    ReleaseSRWLockExclusive(&graphics_lock);
    uint32_t callback[]={1,0x12380,0x19f00};MEM32(0x19f00)=0;call_graphics(PILOT_D3D_D3DDevice_InsertCallback,callback);
    AcquireSRWLockExclusive(&graphics_lock);CHECK(!scalar_test_callbacks && (graphics_gpu_head || graphics_gpu_running));
    uint32_t saved_stack=g_esp;g_esp=0x19e00;MEM32(g_esp)=0;MEM32(g_esp+4)=0x900;
    uint32_t test_writes[]={0x19f00};struct GraphicsScalarPreparation scalar={0};
    CHECK(graphics_scalar_capture(PILOT_D3D_D3DDevice_SetRenderState_CullMode,test_writes,1,&scalar));
    CHECK(MEM32(scalar_test_cache)==0);g_esp=saved_stack;ReleaseSRWLockExclusive(&graphics_lock);
    uint32_t output=0x19600;call_graphics(PILOT_D3D_D3DDevice_GetDisplayMode,&output);
    CHECK(scalar_test_callbacks==1 && MEM32(0x19f00)==0x12345678 && MEM32(0x19f04)==0x87654321 && scheduler_owner==1);
    AcquireSRWLockExclusive(&graphics_lock);CHECK(!graphics_gpu_head && !graphics_gpu_running);
    CHECK(graphics_scalar_consume(&scalar) && MEM32(scalar_test_cache)==0x900);
    uint32_t host=0;CHECK(pilot_gfx_scalar_current(147,&host) && host==2);
    g_esp=0x19e00;CHECK(graphics_scalar_capture(PILOT_D3D_D3DDevice_SetRenderState_CullMode,test_writes,1,&scalar));
    MEM32(g_esp+4)=0x901;CHECK(!graphics_scalar_consume(&scalar) && MEM32(scalar_test_cache)==0x900);
    test_writes[0]=g_esp+4;CHECK(!graphics_scalar_capture(PILOT_D3D_D3DDevice_SetRenderState_CullMode,test_writes,1,&scalar));
    test_writes[0]=scalar_test_cache;CHECK(!graphics_scalar_capture(PILOT_D3D_D3DDevice_SetRenderState_CullMode,test_writes,1,&scalar));
    test_writes[0]=0x19f00;CHECK(pilot_write_watch_native_begin(memory+g_esp,8));
    CHECK(!graphics_scalar_capture(PILOT_D3D_D3DDevice_SetRenderState_CullMode,test_writes,1,&scalar));
    pilot_write_watch_native_end(memory+g_esp,8);g_esp=saved_stack;
    struct PilotPreparedScalarState lowered;
    CHECK(pilot_gfx_scalar_prepare(144,0,&lowered) && lowered.value==0);
    CHECK(pilot_gfx_scalar_prepare(144,1,&lowered) && lowered.value==1);
    CHECK(!pilot_gfx_scalar_prepare(144,2,&lowered) && !pilot_gfx_scalar_prepare(147,0x902,&lowered));
    ReleaseSRWLockExclusive(&graphics_lock);call_graphics(PILOT_D3D_D3DDevice_Release,NULL);scheduler_retire();
    CHECK(!graphics_active && !scheduler_owner);pilot_write_watch_finish();
    fprintf(stderr,"PASS: isolated scalar preparation; actual callback old guest/native state and GPU pixels; late publication; changed input, unknown/running callbacks, alias and writer refusals\n");
}
#else
static void test_graphics_scalar_preparation(void){fprintf(stderr,"SKIP: native scheduling scalar fixture\n");exit(77);}
#endif
