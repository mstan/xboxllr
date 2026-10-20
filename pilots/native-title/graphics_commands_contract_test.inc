#ifdef PILOT_D3D_COMMANDS_VERSION
#include "graphics_fixed_contract_test.inc"
#include "graphics_borrowed_texture_contract_test.inc"
#include "graphics_borrowed_vertex_contract_test.inc"
#include "graphics_index_contract_test.inc"
static void test_graphics_command_null_textures(uint32_t base,const char *failure)
{
    uint32_t create[]={2,2,1,1,0,0x12,3};
    uint32_t texture=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,create);CHECK(texture);
    for(unsigned stage=0;stage<4;++stage) {
        uint32_t binding[]={stage,texture};call_graphics(PILOT_D3D_D3DDevice_SetTexture,binding);
    }
    unsigned slot=graphics_texture_slot(texture);CHECK(graphics_textures[slot].bindings==4);
    uint32_t at=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_CURRENT_OFFSET);
    CHECK(at>=base && at-base<=64);
    /* Independent original NULL-setter effects: release each internal ref,
     * clear its guest cache and publish the matching stage packet. All four
     * original stores occur before the host consumes the first packet. */
    for(unsigned stage=0;stage<4;++stage) {
        if(!failure || strcmp(failure,"texture-live"))
            MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+stage*4)=0;
        if(!failure || strcmp(failure,"texture-reference"))MEM32(texture)-=0x80000;
        MEM32(texture+8)=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_RESOURCE_FENCE);
        MEM32(at+stage*8)=0x41b0c+stage*64;
        MEM32(at+stage*8+4)=failure && !strcmp(failure,"texture-enabled")?0x40000000:0;
    }
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_CURRENT_OFFSET)=at+32;
    uint32_t common=MEM32(texture),last_use=MEM32(texture+8),out=0x19500;
    call_graphics(PILOT_D3D_D3DDevice_GetDeviceCaps,&out);CHECK(!failure);
    CHECK(!graphics_textures[slot].bindings && MEM32(texture)==common && MEM32(texture+8)==last_use);
    for(unsigned stage=0;stage<4;++stage)CHECK(!graphics_native_texture_headers[stage]);
    uint32_t binding[]={0,texture};call_graphics(PILOT_D3D_D3DDevice_SetTexture,binding);
    CHECK(graphics_textures[slot].bindings==1 && graphics_native_texture_headers[0]==texture);
    binding[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetTexture,binding);
    CHECK(!graphics_textures[slot].bindings && !graphics_native_texture_headers[0]);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&texture)==0 && !test_pool_owned(texture));
}
static void test_graphics_commands(const char *failure)
{
    vm_ensure();dispatch_ensure();
    uint32_t low=PILOT_D3D_DEVICE_OBJECT<PILOT_D3D_TEXTURE_STATES?PILOT_D3D_DEVICE_OBJECT:PILOT_D3D_TEXTURE_STATES;
    uint32_t high=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_DEVICE_BYTES;
    const uint32_t ends[]={PILOT_D3D_DEVICE_GLOBAL+4,PILOT_D3D_COMMANDS_CAPACITY_GLOBAL+4,
        PILOT_D3D_COMMANDS_REQUEST_GLOBAL+4,PILOT_D3D_RENDER_STATES+166*4,PILOT_D3D_STREAMS+16*12};
    for(unsigned i=0;i<sizeof(ends)/sizeof(*ends);++i)if(ends[i]>high)high=ends[i];
    low&=~4095u;high=(high+4095)&~4095u;commit(low,high-low);vm_fixed_region(low,high-low);
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t parameters[]={64,64,7,1,0x11,1,0,0,1,0x2e,0,60,1};
    memcpy(memory+0x19000,parameters,sizeof(parameters));
    uint32_t create[]={0,1,0,PILOT_D3D_CREATE_BEHAVIOR_MASK,0x19000,0x19100};
    for(unsigned lifetime=0;lifetime<2;++lifetime) {
        CHECK(!call_graphics(PILOT_D3D_Direct3D_CreateDevice,create));
        CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_DEVICE_REFERENCES)==1);
        if(failure && !strcmp(failure,"device-reference")) {
            MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_DEVICE_REFERENCES)=UINT32_MAX;
            call_graphics(PILOT_D3D_D3DDevice_AddRef,NULL);CHECK(0);
        }
        CHECK(call_graphics(PILOT_D3D_D3DDevice_AddRef,NULL)==2);
        ++MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_DEVICE_REFERENCES); /* original inline reference */
        CHECK(call_graphics(PILOT_D3D_D3DDevice_Release,NULL)==2 && graphics_active);
        CHECK(call_graphics(PILOT_D3D_D3DDevice_Release,NULL)==1 && graphics_active);
        CHECK(graphics_commands.base && MEM32(PILOT_D3D_COMMANDS_REQUEST_GLOBAL)==PILOT_D3D_COMMANDS_REQUEST_DEFAULT);
        uint32_t request=MEM32(PILOT_D3D_COMMANDS_REQUEST_GLOBAL),args[]={request/2,request};
        g_esp=0x18000;MEM32(g_esp)=0x12340;
        g_ebx=0x12345678;g_esi=0x23456789;g_edi=0x3456789a;g_ebp=0x456789ab;
        pilot_d3d_dispatch_frame(PILOT_D3D_COMMANDS_RESERVE_ENTRY,args,2,8);
        uint32_t base=g_eax;CHECK(base==graphics_commands.base && g_esp==0x1800c && MEM32(0x18000)==0x12340);
        CHECK(g_ebx==0x12345678 && g_esi==0x23456789 && g_edi==0x3456789a && g_ebp==0x456789ab);
        CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_LIMIT_OFFSET)==base+request-PILOT_D3D_COMMANDS_MARGIN);
        if(failure && !strncmp(failure,"texture-",8))test_graphics_command_null_textures(base,failure);
        if(failure && !strncmp(failure,"declaration-",12))test_graphics_fixed_declarations(failure);
        if(failure && !strncmp(failure,"borrowed-",9))test_graphics_borrowed_textures(failure);
        if(failure && !strncmp(failure,"vertex-",7)) {
            test_graphics_fixed_declarations(NULL);test_graphics_borrowed_vertices(failure);
        }
        if(failure && !strncmp(failure,"index-",6)) {
            test_graphics_fixed_declarations(NULL);test_graphics_indices(failure);
        }
        struct PilotInitializerState observed;CHECK(pilot_gfx_initializer_snapshot(&observed) && !observed.dither);
        MEM32(base)=0x41710;MEM32(base+4)=0;MEM32(base+8)=0x40310;MEM32(base+12)=1;
        MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_CURRENT_OFFSET)=base+16;
        uint32_t fences=graphics_fence_next,out=0x19500;
        if(failure && !strcmp(failure,"unknown")) {
            MEM32(base+16)=0x417fc;MEM32(base+20)=3;
            MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_CURRENT_OFFSET)=base+24;
        }
        if(failure && !strcmp(failure,"record"))MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_FLAGS_OFFSET)|=PILOT_D3D_COMMANDS_RECORD_MASK;
        call_graphics(PILOT_D3D_D3DDevice_GetDeviceCaps,&out);
        CHECK(!failure && pilot_gfx_initializer_snapshot(&observed) && observed.dither==1);
        CHECK(graphics_commands.methods==2 && graphics_commands.consumed==16 && graphics_fence_next==fences);
        call_graphics(PILOT_D3D_D3DDevice_GetDeviceCaps,&out);CHECK(graphics_commands.methods==2);
        CHECK(call_graphics(PILOT_D3D_COMMANDS_SPACE_ENTRY,NULL)==base && !graphics_commands.consumed);
        MEM32(base)=0x40310;MEM32(base+4)=0;
        MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_CURRENT_OFFSET)=base+8;
        g_ecx=PILOT_D3D_DEVICE_OBJECT;call_graphics(PILOT_D3D_COMMANDS_KICKOFF_ENTRY,NULL);
        CHECK(pilot_gfx_initializer_snapshot(&observed) && !observed.dither && graphics_commands.methods==3 && graphics_fence_next==fences);
        test_graphics_command_null_textures(base,NULL);CHECK(graphics_commands.methods==7);
        test_graphics_fixed_declarations(NULL);
        uint32_t borrowed_data=test_graphics_borrowed_textures(NULL);
        test_graphics_borrowed_vertices(NULL);
        test_graphics_indices(NULL);
        // This title does not expose the public InsertFence/BlockOnFence pair.
        // Exercise their real shared completion owner without admitting a
        // nonexistent guest wrapper just to make a fixture pass.
        AcquireSRWLockExclusive(&graphics_lock);
        uint32_t fence=graphics_fence_insert();graphics_fence_wait(fence);
        ReleaseSRWLockExclusive(&graphics_lock);
        call_graphics(PILOT_D3D_D3DDevice_Release,NULL);graphics_abi_thread_retire();
        CHECK(!MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_DEVICE_REFERENCES));
        CHECK(physical_find(borrowed_data,1) && !physical_find(borrowed_data,1)->borrowers && MEM32(0x19600)==0x01040003);
        physical_free(borrowed_data);
        CHECK(MEM32(0x19800)==1 && MEM32(0x19920)==1 && MEM32(0x19a40)==1);
        CHECK(!graphics_commands.base && test_write_faults(base) && vm_committed(0,0x100000000ull)==resident);
    }
    puts("PASS actual inline graphics owner: register/stack ABI, original default projection, queued-state visibility, ordered API drain, no duplicate replay, recycle/kickoff without fence retirement, real fence completion, two complete lifetimes");
}
#endif
