#ifdef PILOT_D3D_COMMANDS_VERSION
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
        CHECK(graphics_commands.base && MEM32(PILOT_D3D_COMMANDS_REQUEST_GLOBAL)==PILOT_D3D_COMMANDS_REQUEST_DEFAULT);
        uint32_t request=MEM32(PILOT_D3D_COMMANDS_REQUEST_GLOBAL),args[]={request/2,request};
        g_esp=0x18000;MEM32(g_esp)=0x12340;
        g_ebx=0x12345678;g_esi=0x23456789;g_edi=0x3456789a;g_ebp=0x456789ab;
        pilot_d3d_dispatch_frame(PILOT_D3D_COMMANDS_RESERVE_ENTRY,args,2,8);
        uint32_t base=g_eax;CHECK(base==graphics_commands.base && g_esp==0x1800c && MEM32(0x18000)==0x12340);
        CHECK(g_ebx==0x12345678 && g_esi==0x23456789 && g_edi==0x3456789a && g_ebp==0x456789ab);
        CHECK(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_COMMANDS_LIMIT_OFFSET)==base+request-PILOT_D3D_COMMANDS_MARGIN);
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
        // This title does not expose the public InsertFence/BlockOnFence pair.
        // Exercise their real shared completion owner without admitting a
        // nonexistent guest wrapper just to make a fixture pass.
        AcquireSRWLockExclusive(&graphics_lock);
        uint32_t fence=graphics_fence_insert();graphics_fence_wait(fence);
        ReleaseSRWLockExclusive(&graphics_lock);
        call_graphics(PILOT_D3D_D3DDevice_Release,NULL);graphics_abi_thread_retire();
        CHECK(!graphics_commands.base && test_write_faults(base) && vm_committed(0,0x100000000ull)==resident);
    }
    puts("PASS actual inline graphics owner: register/stack ABI, original default projection, queued-state visibility, ordered API drain, no duplicate replay, recycle/kickoff without fence retirement, real fence completion, two complete lifetimes");
}
#endif
