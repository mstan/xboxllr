/* Test-only host API provider. Production links Windows XInput directly.
 * Exercise the actual donor-derived conversion as well as the guest ABI. */
#include <xinput.h>
static XINPUT_STATE input_test_states[4];
static XINPUT_CAPABILITIES input_test_caps[4];
static DWORD input_test_presence[4]={0,1167,1167,1167};
static DWORD input_test_caps_status,input_test_rumble_status;
static unsigned input_test_state_calls,input_test_rumble_calls;
static XINPUT_VIBRATION input_test_vibration;
static DWORD WINAPI input_test_get_state(DWORD port,XINPUT_STATE *state)
{
    CHECK(port<4); ++input_test_state_calls;
    if (input_test_presence[port]) return input_test_presence[port];
    *state=input_test_states[port]; return 0;
}
static DWORD WINAPI input_test_get_caps(DWORD port,DWORD flags,XINPUT_CAPABILITIES *caps)
{
    CHECK(port<4 && flags==0);
    if (input_test_caps_status) return input_test_caps_status;
    if (input_test_presence[port]) return input_test_presence[port];
    *caps=input_test_caps[port]; return 0;
}
static DWORD WINAPI input_test_set_state(DWORD port,XINPUT_VIBRATION *vibration)
{
    CHECK(port<4); ++input_test_rumble_calls; input_test_vibration=*vibration;
    return input_test_presence[port] ? input_test_presence[port] : input_test_rumble_status;
}
#define XInputGetState input_test_get_state
#define XInputGetCapabilities input_test_get_caps
#define XInputSetState input_test_set_state
#include "input_host.c"
#undef XInputGetState
#undef XInputGetCapabilities
#undef XInputSetState

static uint32_t input_test_last_error;
static void input_test_error(void)
{
    input_test_last_error=MEM32(g_esp+4); g_esp+=8;
}
static uint32_t input_test_call(unsigned entry,uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{
    g_esp=0x18000; MEM32(g_esp)=0x12340;
    MEM32(g_esp+4)=a; MEM32(g_esp+8)=b; MEM32(g_esp+12)=c; MEM32(g_esp+16)=d;
    g_ebx=0x12345678; g_esi=0x23456789; g_edi=0x3456789a; g_ebp=0x456789ab;
    pilot_input_dispatch(entry,input_test_error);
    CHECK(g_esp==0x18004+pilot_input_calls[entry].stack_bytes && MEM32(0x18000)==0x12340);
    CHECK(g_ebx==0x12345678 && g_esi==0x23456789 && g_edi==0x3456789a && g_ebp==0x456789ab);
    return g_eax;
}
static void input_test_setup(void)
{
    /* Deterministic XInput provider independent of the user's launch choice. */
    input_host_is_keyboard();input_host_keyboard=0;
    uint8_t keys[256]={0},packed[18];XINPUT_GAMEPAD keyboard;
    keys[VK_RETURN]=keys[VK_SPACE]=keys['W']=keys['D']=keys['Z']=1;
    input_host_keyboard_convert(keys,&keyboard);input_host_convert(&keyboard,packed);
    CHECK((packed[0]&0x10) && packed[2]==255 && packed[8]==255);
    CHECK(keyboard.sThumbLX==32767 && keyboard.sThumbLY==32767);
    keys['A']=keys['S']=1;input_host_keyboard_convert(keys,&keyboard);
    CHECK(!keyboard.sThumbLX && !keyboard.sThumbLY);
    memset(keys,0,sizeof(keys));input_host_keyboard_convert(keys,&keyboard);
    CHECK(!keyboard.wButtons && !keyboard.bLeftTrigger);
    commit(PILOT_INPUT_g_DeviceType_Gamepad&~4095u,4096);
    commit(PILOT_INPUT_g_DeviceType_MU&~4095u,4096);
    commit(PILOT_INPUT_g_DeviceType_IRDongle&~4095u,4096);
    input_test_states[0].dwPacketNumber=20;
    XINPUT_GAMEPAD *g=&input_test_states[0].Gamepad;
    g->wButtons=XINPUT_GAMEPAD_START|XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_LEFT_SHOULDER;
    g->bLeftTrigger=17; g->bRightTrigger=241;
    g->sThumbLX=-32768; g->sThumbLY=32767; g->sThumbRX=-1234; g->sThumbRY=4321;
    for (unsigned i=0;i<4;++i) {
        XINPUT_CAPABILITIES *cap=&input_test_caps[i];
        cap->Type=1; cap->SubType=1; cap->Flags=0x1234; /* not Xbox ABI fields */
        cap->Gamepad.wButtons=0xffff; cap->Gamepad.bLeftTrigger=0xff; cap->Gamepad.bRightTrigger=0xff;
        cap->Gamepad.sThumbLX=cap->Gamepad.sThumbLY=cap->Gamepad.sThumbRX=cap->Gamepad.sThumbRY=(SHORT)0xffc0;
        cap->Vibration.wLeftMotorSpeed=0xffff; cap->Vibration.wRightMotorSpeed=0xff00;
    }
    MEM32(0x19000)=PILOT_INPUT_g_DeviceType_Gamepad; MEM32(0x19004)=4;
    MEM32(0x19008)=PILOT_INPUT_g_DeviceType_MU; MEM32(0x1900c)=2;
    input_test_call(PILOT_INPUT_XInitDevices,2,0x19000,0,0);
}
static void test_input(void)
{
    input_test_setup();
    uint32_t type=PILOT_INPUT_g_DeviceType_Gamepad,out=0x1a020;
    CHECK(input_test_call(PILOT_INPUT_XGetDevices,type,0,0,0)==1);
    CHECK(MEM32(type)==1 && MEM32(type+4)==0 && MEM32(type+8)==1);
    CHECK(input_test_call(PILOT_INPUT_XGetDevices,PILOT_INPUT_g_DeviceType_MU,0,0,0)==0);
    CHECK(input_test_call(PILOT_INPUT_XGetDeviceChanges,type,out,out+4,0)==0);
    CHECK(MEM32(out)==0 && MEM32(out+4)==0);
    uint32_t handle=input_test_call(PILOT_INPUT_XInputOpen,type,0,0,0); CHECK(handle!=0);
    CHECK(input_test_call(PILOT_INPUT_XInputOpen,type,0,0,0)==0 && input_test_last_error==32);
    CHECK(input_test_call(PILOT_INPUT_XInputOpen,type,4,0,0)==0 && input_test_last_error==1167);
    CHECK(input_test_call(PILOT_INPUT_XInputOpen,PILOT_INPUT_g_DeviceType_MU,0,0,0)==0 && input_test_last_error==87);
    memset(memory+out-1,0xcc,72);
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==0);
    const uint8_t expected_state[22]={1,0,0,0,0x10,0,255,0,255,0,255,0,17,241,0,0x80,255,0x7f,0x2e,0xfb,0xe1,0x10};
    CHECK(!memcmp(memory+out,expected_state,22) && MEM8(out-1)==0xcc && MEM8(out+22)==0xcc);
    unsigned calls=input_test_state_calls;
    input_ports[0].next_poll=UINT64_MAX;
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==0 && input_test_state_calls==calls);
    input_test_states[0].dwPacketNumber++; input_ports[0].next_poll=0;
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==0 && MEM32(out)==1);
    input_test_states[0].Gamepad.wButtons|=XINPUT_GAMEPAD_B; input_ports[0].next_poll=0;
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==0 && MEM32(out)==2 && MEM8(out+7)==255);

    memset(memory+out-1,0xcc,28);
    CHECK(input_test_call(PILOT_INPUT_XInputGetCapabilities,handle,out,0,0)==0);
    const uint8_t expected_caps[25]={2,0,0,255,0,255,255,255,255,255,255,255,255,
                                    0xc0,255,0xc0,255,0xc0,255,0xc0,255,255,255,0,255};
    CHECK(!memcmp(memory+out,expected_caps,25) && MEM8(out-1)==0xcc && MEM8(out+25)==0xcc);
    input_test_caps_status=5; memset(memory+out,0xcc,25);
    CHECK(input_test_call(PILOT_INPUT_XInputGetCapabilities,handle,out,0,0)==5);
    CHECK(MEM8(out)==2 && MEM8(out+25)==0xcc);
    for (unsigned i=1;i<25;++i) CHECK(MEM8(out+i)==0);
    input_test_caps_status=0;

    memset(memory+out-1,0xa5,72); MEM32(out+4)=0;
    MEM16(out+66)=12345; MEM16(out+68)=54321;
    CHECK(input_test_call(PILOT_INPUT_XInputSetState,handle,out,0,0)==0);
    CHECK(input_test_vibration.wLeftMotorSpeed==12345 && input_test_vibration.wRightMotorSpeed==54321);
    CHECK(MEM32(out)==0 && MEM8(out+64)==0 && MEM8(out+65)==6 && MEM8(out+70)==0xa5 && MEM8(out-1)==0xa5);
    for (unsigned i=8;i<64;++i) CHECK(MEM8(out+i)==0xa5);
    input_test_rumble_status=5;
    CHECK(input_test_call(PILOT_INPUT_XInputSetState,handle,out,0,0)==5 && MEM32(out)==5);
    input_test_rumble_status=0;

    /* Removal/reinsertion observed between enumeration queries reports BOTH.
     * A stale handle must never silently acquire the replacement controller. */
    input_test_presence[0]=1167; input_ports[0].next_poll=0;
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==1167 && MEM32(out)==2);
    input_test_presence[0]=0; input_ports[0].next_poll=0;
    CHECK(input_test_call(PILOT_INPUT_XInputGetState,handle,out,0,0)==1167);
    CHECK(input_test_call(PILOT_INPUT_XGetDeviceChanges,type,out,out+4,0)==1);
    CHECK(MEM32(out)==1 && MEM32(out+4)==1);
    uint32_t replacement=input_test_call(PILOT_INPUT_XInputOpen,type,0,0,0);
    CHECK(replacement && replacement!=handle);
    unsigned rumble_calls=input_test_rumble_calls;
    input_test_call(PILOT_INPUT_XInputClose,handle,0,0,0);
    CHECK(input_test_rumble_calls==rumble_calls); /* don't touch replacement */
    input_test_call(PILOT_INPUT_XInputClose,replacement,0,0,0);
    CHECK(input_test_rumble_calls==rumble_calls+1 && input_test_vibration.wLeftMotorSpeed==0 && input_test_vibration.wRightMotorSpeed==0);
    for (unsigned i=0;i<100;++i) {
        uint32_t next=input_test_call(PILOT_INPUT_XInputOpen,type,0,0,0);
        CHECK(next>replacement); replacement=next;
        input_test_call(PILOT_INPUT_XInputClose,next,0,0,0);
    }
    for (unsigned i=0;i<4;++i) CHECK(!input_handles[i].id);
    input_test_presence[0]=1167; input_ports[0].next_poll=0;
    CHECK(input_test_call(PILOT_INPUT_XGetDeviceChanges,type,out,out+4,0)==1 && MEM32(out)==0 && MEM32(out+4)==1);
    CHECK(input_test_call(PILOT_INPUT_XInputOpen,type,0,0,0)==0 && input_test_last_error==1167);
    puts("input contract: 22/25/70-byte ABI, mapping, poll cache, hotplug, stale handles, real feedback errors and 100 lifetimes passed");
}
static void test_input_failure(const char *kind)
{
    input_test_setup();
    uint32_t type=PILOT_INPUT_g_DeviceType_Gamepad,out=0x1a020;
    if (!strcmp(kind,"manual")) {
        MEM32(out)=0x800;
        input_test_call(PILOT_INPUT_XInputOpen,type,0,0,out);
    } else if (!strcmp(kind,"unknown")) input_test_call(PILOT_INPUT_XGetDevices,0x1a000,0,0,0);
    else {
        uint32_t h=input_test_call(PILOT_INPUT_XInputOpen,type,0,0,0);
        if (!strcmp(kind,"event")) {
            MEM32(out+4)=0x1234;
            input_test_call(PILOT_INPUT_XInputSetState,h,out,0,0);
        } else if (!strcmp(kind,"closed")) {
            input_test_call(PILOT_INPUT_XInputClose,h,0,0,0);
            input_test_call(PILOT_INPUT_XInputGetState,h,out,0,0);
        }
    }
    ExitProcess(0); /* WILL_FAIL tests require the intended terminal stop. */
}
