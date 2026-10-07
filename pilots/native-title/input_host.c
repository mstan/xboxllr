/* Windows controller mapping adapted from sp00nznet/xboxrecomp,
 * src/input/xinput_device.c at 1409a7d7801d3e931fb1074be6104209ddd9a33e.
 * Copyright (c) 2026 sp00nz. MIT; see input-sp00nz-MIT.txt.
 * Xbox ABI/model code is separate. No synthetic connected ports or key presses.
 */
#include <windows.h>
#include <xinput.h>
#include <string.h>
#include "input_host.h"

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
    XINPUT_STATE state;
    DWORD status=XInputGetState(port,&state);
    if (!status) input_host_convert(&state.Gamepad,gamepad);
    return status;
}

uint32_t pilot_input_host_caps(unsigned port,uint8_t gamepad[18],uint8_t rumble[4])
{
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
    XINPUT_VIBRATION vibration={left,right};
    return XInputSetState(port,&vibration);
}
