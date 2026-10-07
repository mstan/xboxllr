static uint32_t call_graphics(unsigned entry,const uint32_t *args)
{
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
#include "graphics_texture_contract_test.inc"
#include "graphics_vertex_contract_test.inc"
static void test_graphics(void)
{
    vm_ensure();
    uint32_t global_page=PILOT_D3D_DEVICE_GLOBAL&~4095u;
    uint32_t state_bytes=((PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET+4+4095)&~4095u)-global_page;
    commit(global_page,state_bytes); vm_fixed_region(global_page,state_bytes);
    uint32_t *parameters=(uint32_t*)(memory+0x19000);
    const uint32_t valid[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};
    uint32_t create[]={0,1,0,0x40,0x19000,0x19100};
    memcpy(parameters,valid,sizeof(valid)); parameters[0]=0;
    MEM32(0x19100)=0xdeadbeef;
    CHECK(call_graphics(PILOT_D3D_Direct3D_CreateDevice,create)&0x80000000u);
    CHECK(!MEM32(0x19100) && !MEM32(PILOT_D3D_DEVICE_GLOBAL) && !graphics_active);
    parameters[0]=64; parameters[4]=0x1021;
    CHECK(call_graphics(PILOT_D3D_Direct3D_CreateDevice,create)==0x80004001u);
    CHECK(!graphics_active && !MEM32(PILOT_D3D_DEVICE_GLOBAL));
    parameters[4]=0x11;
    uint64_t resident=vm_committed(0,0x100000000ull);
    unsigned char pixels[64*64*4];
    for (unsigned pass=0;pass<3;++pass) {
        create[5]=pass==1?0:0x19100; /* Original Create permits a null output. */
        CHECK(call_graphics(PILOT_D3D_Direct3D_CreateDevice,create)==0);
        CHECK(graphics_active && MEM32(PILOT_D3D_DEVICE_GLOBAL)==PILOT_D3D_DEVICE_OBJECT);
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
        arg=0; CHECK(!(call_graphics(PILOT_D3D_D3DDevice_Swap,&arg)&0x80000000u));
        clear[3]=0xff0000ff; CHECK(call_graphics(PILOT_D3D_D3DDevice_Clear,clear)==0);
        CHECK(pilot_gfx_surface_readback(1,pixels,sizeof(pixels))>=0);
        CHECK(pixels[0]==255 && pixels[1]==0 && pixels[2]==0);
        CHECK(pilot_gfx_surface_readback(0,pixels,sizeof(pixels))>=0);
        CHECK(pixels[0]==0 && pixels[1]==0 && pixels[2]==255);
        if(!pass) { test_graphics_states(); test_graphics_textures(); test_graphics_vertices(); }
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&front)==0);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&back)==0);
        CHECK(call_graphics(PILOT_D3D_D3DDevice_Release,NULL)==0);
        CHECK(!graphics_active && !graphics_headers && !MEM32(PILOT_D3D_DEVICE_GLOBAL));
        CHECK(vm_committed(0,0x100000000ull)==resident);
        CHECK(!vm_find(back,24));
    }
    fprintf(stderr,"PASS: guest graphics ABI, failures/null output, real clear/present pixels, distinct front/back resources, references and release/recreate\n");
}
