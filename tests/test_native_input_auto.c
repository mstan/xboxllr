#include <windows.h>
#include <SDL.h>
#include <stdio.h>
#include "input_host.h"
#include "input_sdl.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    CHECK(!SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER));
    if(SDL_NumJoysticks()){puts("SKIP automatic fallback requires an isolated controller inventory");return 77;}
    CHECK(!_putenv_s("XLLE_PILOT_INPUT","auto"));
    uint8_t state[18],caps[18],rumble[4];
    CHECK(!pilot_input_host_state(0,state));CHECK(!state[0]&&!state[2]);
    CHECK(!pilot_input_host_caps(0,caps,rumble));
    SDL_VirtualJoystickDesc desc={0};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;desc.naxes=SDL_CONTROLLER_AXIS_MAX;desc.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
    desc.vendor_id=0x1234;desc.product_id=3;desc.name="Hotplug test controller";
    int index=SDL_JoystickAttachVirtualEx(&desc);CHECK(index>=0);
    SDL_Joystick *joystick=SDL_JoystickOpen(index);CHECK(joystick);
    CHECK(!SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_A,1));
    CHECK(!SDL_JoystickSetVirtualButton(joystick,SDL_CONTROLLER_BUTTON_START,1));
    int ready=0;
    for(int i=0;i<250;++i){CHECK(!pilot_input_host_state(0,state));if((state[0]&16)&&state[2]==255){ready=1;break;}Sleep(4);}
    CHECK(ready);CHECK(pilot_input_host_state(1,state)==ERROR_DEVICE_NOT_CONNECTED);
    SDL_JoystickClose(joystick);CHECK(!SDL_JoystickDetachVirtual(index));
    ready=0;
    for(int i=0;i<250;++i){CHECK(!pilot_input_host_state(0,state));if(!state[0]&&!state[2]){ready=1;break;}Sleep(4);}
    CHECK(ready);pilot_input_sdl_shutdown();SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    puts("PASS automatic fallback, late SDL hotplug, A/Start guest report and disconnect release");return 0;
}
