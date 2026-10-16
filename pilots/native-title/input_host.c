/* Windows controller mapping adapted from sp00nznet/xboxrecomp,
 * src/input/xinput_device.c at 1409a7d7801d3e931fb1074be6104209ddd9a33e.
 * Copyright (c) 2026 sp00nz. MIT; see input-sp00nz-MIT.txt.
 * Xbox ABI/model code is separate. Physical XInput reports actual host devices.
 */
#include <windows.h>
#include <xinput.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "input_host.h"
#include "input_replay.inc"

/* Startup-selected virtual gamepad, inspired by Phobos's keyboard source:
 * phobos665/xboxrecomp bcccf8c, src/input/input_bindings.c. Independent code;
 * the donor's parser/sampler implementation is not copied here.
 * Separate from physical XInput ports; no keys are synthesized by the runtime.
 * Real OS keys are read only while this process owns the foreground window. */
static INIT_ONCE input_host_once=INIT_ONCE_STATIC_INIT;
static int input_host_keyboard;
#ifdef PILOT_INPUT_REFERENCE_CLOCK
static uint64_t input_host_reference_ms;
uint32_t pilot_input_host_reference_time(uint64_t milliseconds)
{
    if(milliseconds<input_host_reference_ms)return ERROR_INVALID_PARAMETER;
    input_host_reference_ms=milliseconds;return ERROR_SUCCESS;
}
#endif
static uint64_t input_host_replay_time(void)
{
#ifdef PILOT_INPUT_REFERENCE_CLOCK
    return input_host_reference_ms;
#else
    return GetTickCount64();
#endif
}
static BOOL CALLBACK input_host_initialize(PINIT_ONCE once,PVOID parameter,PVOID *context)
{
    char value[24]={0};
    GetEnvironmentVariableA("XLLE_PILOT_INPUT",value,sizeof(value));
    input_host_keyboard=!strcmp(value,"keyboard");
    if(!strcmp(value,"replay")) {
        const char *path=getenv("XLLE_PILOT_INPUT_REPLAY");FILE *file=path?fopen(path,"r"):NULL;
        int ok=file && input_replay_load(file);if(file && fclose(file))ok=0;
        if(!ok){fprintf(stderr,"[PILOT INPUT STOP] invalid/missing input replay\n");ExitProcess(2);}
        uint32_t error=input_replay_trigger_configure(getenv("XLLE_PILOT_INPUT_REPLAY_TRIGGER"));
        if(error){fprintf(stderr,"[PILOT INPUT STOP] invalid replay trigger configuration error=%u\n",error);ExitProcess(2);}
        if(input_replay_trigger.enabled)fprintf(stderr,"[INPUT REPLAY TRIGGER] neutral connected until marker: %s\n",input_replay_trigger.path);
        fprintf(stderr,"[INPUT REPLAY] %u real controller-state transitions; port 0\n",input_replay_count);
    }
    return TRUE;
}
static int input_host_is_keyboard(void)
{ InitOnceExecuteOnce(&input_host_once,input_host_initialize,NULL,NULL); return input_host_keyboard; }
static void input_host_keyboard_convert(const uint8_t keys[256],XINPUT_GAMEPAD *pad)
{
    memset(pad,0,sizeof(*pad));
    const struct {unsigned key;WORD button;} buttons[]={
        {VK_UP,1},{VK_DOWN,2},{VK_LEFT,4},{VK_RIGHT,8},{VK_RETURN,0x10},{VK_BACK,0x20},
        {VK_SPACE,XINPUT_GAMEPAD_A},{VK_SHIFT,XINPUT_GAMEPAD_B},
        {'Q',XINPUT_GAMEPAD_X},{'E',XINPUT_GAMEPAD_Y},
        {'1',XINPUT_GAMEPAD_LEFT_SHOULDER},{'2',XINPUT_GAMEPAD_RIGHT_SHOULDER}};
    for(unsigned i=0;i<sizeof(buttons)/sizeof(buttons[0]);++i)
        if(keys[buttons[i].key])pad->wButtons|=buttons[i].button;
    pad->sThumbLX=(SHORT)((!!keys['D']-!!keys['A'])*32767);
    pad->sThumbLY=(SHORT)((!!keys['W']-!!keys['S'])*32767);
    pad->sThumbRX=(SHORT)((!!keys['L']-!!keys['J'])*32767);
    pad->sThumbRY=(SHORT)((!!keys['I']-!!keys['K'])*32767);
    pad->bLeftTrigger=keys['Z']?255:0;pad->bRightTrigger=keys['C']?255:0;
}

/* Also used by the deterministic contract fixture with real host structures. */
static void input_host_convert(const XINPUT_GAMEPAD *source, uint8_t dest[18])
{
    WORD digital=source->wButtons & 0x00ff;
    memcpy(dest,&digital,2);
    dest[2]=(source->wButtons & XINPUT_GAMEPAD_A) ? 255 : 0;
    dest[3]=(source->wButtons & XINPUT_GAMEPAD_B) ? 255 : 0;
    dest[4]=(source->wButtons & XINPUT_GAMEPAD_X) ? 255 : 0;
    dest[5]=(source->wButtons & XINPUT_GAMEPAD_Y) ? 255 : 0;
    dest[6]=(source->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 255 : 0;
    dest[7]=(source->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 255 : 0;
    dest[8]=source->bLeftTrigger; dest[9]=source->bRightTrigger;
    memcpy(dest+10,&source->sThumbLX,2); memcpy(dest+12,&source->sThumbLY,2);
    memcpy(dest+14,&source->sThumbRX,2); memcpy(dest+16,&source->sThumbRY,2);
}

uint32_t pilot_input_host_state(unsigned port,uint8_t gamepad[18])
{
    input_host_is_keyboard();
    if(input_replay_count) {
        if(port)return ERROR_DEVICE_NOT_CONNECTED;
        return input_replay_poll(input_host_replay_time(),gamepad);
    }
    if(input_host_keyboard) {
        if(port)return ERROR_DEVICE_NOT_CONNECTED;
        uint8_t keys[256]={0};DWORD process=0;
        HWND foreground=GetForegroundWindow();
        if(foreground)GetWindowThreadProcessId(foreground,&process);
        if(process==GetCurrentProcessId())
            for(unsigned i=0;i<256;++i)keys[i]=!!(GetAsyncKeyState(i)&0x8000);
        XINPUT_GAMEPAD pad;input_host_keyboard_convert(keys,&pad);input_host_convert(&pad,gamepad);
        return ERROR_SUCCESS;
    }
    XINPUT_STATE state;
    DWORD status=XInputGetState(port,&state);
    if (!status) input_host_convert(&state.Gamepad,gamepad);
    return status;
}

uint32_t pilot_input_host_caps(unsigned port,uint8_t gamepad[18],uint8_t rumble[4])
{
    if(input_host_is_keyboard() || input_replay_count) {
        if(port)return ERROR_DEVICE_NOT_CONNECTED;
        if(input_replay_count) {
            uint8_t state[18];uint32_t status=pilot_input_host_state(port,state);
            if(status)return status;
        }
        uint8_t keys[256];memset(keys,1,sizeof(keys));XINPUT_GAMEPAD pad;
        input_host_keyboard_convert(keys,&pad);
        if(input_replay_count)pad.wButtons=0xffff;
        /* Capabilities encode supported bits, not a positive axis position.
         * The virtual source can emit signed values spanning all 16 bits. */
        pad.sThumbLX=pad.sThumbLY=pad.sThumbRX=pad.sThumbRY=(SHORT)0xffff;
        input_host_convert(&pad,gamepad);memset(rumble,0,4);return ERROR_SUCCESS;
    }
    XINPUT_CAPABILITIES caps;
    DWORD status=XInputGetCapabilities(port,0,&caps);
    if (status) return status;
    /* Wheels, guitars, etc. need their own truthful peripheral contract. */
    if (caps.Type!=XINPUT_DEVTYPE_GAMEPAD || caps.SubType!=XINPUT_DEVSUBTYPE_GAMEPAD)
        return ERROR_NOT_SUPPORTED;
    input_host_convert(&caps.Gamepad,gamepad);
    memcpy(rumble,&caps.Vibration.wLeftMotorSpeed,2);
    memcpy(rumble+2,&caps.Vibration.wRightMotorSpeed,2);
    return ERROR_SUCCESS;
}

uint32_t pilot_input_host_rumble(unsigned port,uint16_t left,uint16_t right)
{
    if(input_host_is_keyboard() || input_replay_count) {
        uint8_t state[18];uint32_t status=pilot_input_host_state(port,state);
        return status?status:(left || right)?ERROR_NOT_SUPPORTED:ERROR_SUCCESS;
    }
    XINPUT_VIBRATION vibration={left,right};
    return XInputSetState(port,&vibration);
}
