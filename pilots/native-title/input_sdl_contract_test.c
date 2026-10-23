/* Exercise real SDL virtual devices, not a replacement for the transport. */
#include <windows.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_sdl.h"
#ifdef PILOT_INPUT_SDL_HOST_TEST
#include "input_host.h"
#define pilot_input_sdl_state pilot_input_host_state
#define pilot_input_sdl_caps pilot_input_host_caps
#define pilot_input_sdl_rumble pilot_input_host_rumble
#endif
#define CHECK(v) do{if(!(v)){fprintf(stderr,"FAIL line %d: %s (%s)\n",__LINE__,#v,SDL_GetError());return 1;}}while(0)
static int await_state(uint32_t expected,int a,int x)
{
    for(unsigned i=0;i<250;++i) {
        uint8_t state[18];memset(state,0xcc,18);
        uint32_t status=pilot_input_sdl_state(0,state);
        if(status==expected&&((status&&state[0]==0xcc)||(!status&&state[2]==a&&state[4]==x)))return 1;
        Sleep(4);
    }
    return 0;
}
static SDL_VirtualJoystickDesc descriptor(unsigned product)
{
    SDL_VirtualJoystickDesc d;SDL_zero(d);d.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;d.naxes=SDL_CONTROLLER_AXIS_MAX;d.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
    d.vendor_id=0x1209;d.product_id=(Uint16)product;d.name="xboxlle virtual controller fixture";
    d.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;d.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1;return d;
}
int main(int argc,char **argv)
{
    (void)argc;(void)argv;
#ifdef PILOT_INPUT_SDL_HOST_TEST
    int invert_x=argc==2 && strchr(argv[1],'x')!=NULL;
    int invert_y=argc==2 && strchr(argv[1],'y')!=NULL;
    CHECK(!_putenv_s("XLLE_PILOT_CAMERA_INVERT_X",invert_x?"1":"0"));
    CHECK(!_putenv_s("XLLE_PILOT_CAMERA_INVERT_Y",invert_y?"1":"0"));
#else
    const int invert_x=0,invert_y=0;
#endif
    CHECK(pilot_input_sdl_start(NULL)==ERROR_INVALID_PARAMETER);
    CHECK(pilot_input_sdl_start("not-a-guid")==ERROR_INVALID_PARAMETER);
    CHECK(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)==0);
    SDL_VirtualJoystickDesc a=descriptor(1),b=descriptor(2);
    int first=SDL_JoystickAttachVirtualEx(&a),second=SDL_JoystickAttachVirtualEx(&b);CHECK(first>=0&&second>=0);
    SDL_Joystick *other=SDL_JoystickOpen(first),*chosen=SDL_JoystickOpen(second);CHECK(other&&chosen);
    char guid[33];SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(chosen),guid,sizeof(guid));
    CHECK(SDL_JoystickSetVirtualButton(other,SDL_CONTROLLER_BUTTON_A,1)==0);
#ifdef PILOT_INPUT_SDL_HOST_TEST
    CHECK(!_putenv_s("XLLE_PILOT_INPUT","sdl"));
    CHECK(!_putenv_s("XLLE_PILOT_INPUT_GUID",guid));
    uint8_t initial[18];pilot_input_host_state(0,initial);
#else
    CHECK(pilot_input_sdl_start(guid)==0);
#endif
    CHECK(await_state(0,0,0));
    CHECK(SDL_JoystickSetVirtualButton(chosen,SDL_CONTROLLER_BUTTON_X,1)==0);
    CHECK(SDL_JoystickSetVirtualButton(chosen,SDL_CONTROLLER_BUTTON_START,1)==0);
    CHECK(SDL_JoystickSetVirtualAxis(chosen,SDL_CONTROLLER_AXIS_LEFTX,-32768)==0);
    CHECK(SDL_JoystickSetVirtualAxis(chosen,SDL_CONTROLLER_AXIS_LEFTY,-32768)==0);
    CHECK(SDL_JoystickSetVirtualAxis(chosen,SDL_CONTROLLER_AXIS_RIGHTX,-32768)==0);
    CHECK(SDL_JoystickSetVirtualAxis(chosen,SDL_CONTROLLER_AXIS_RIGHTY,-12345)==0);
    CHECK(SDL_JoystickSetVirtualAxis(chosen,SDL_CONTROLLER_AXIS_TRIGGERLEFT,32767)==0);
    uint8_t state[18],caps[18],rumble[4];int ready=0;
    /* SetVirtual calls publish separately; wait for the complete authored
     * state rather than mistaking the first changed button for all axes. */
    for(unsigned i=0;i<250;++i) {
        if(!pilot_input_sdl_state(0,state)) {
            int16_t x,y,rx,ry;memcpy(&x,state+10,2);memcpy(&y,state+12,2);
            memcpy(&rx,state+14,2);memcpy(&ry,state+16,2);
            if(state[0]==16&&state[1]==0&&state[2]==0&&state[4]==255&&state[8]==255&&x==-32768&&y==32767&&
               rx==(invert_x?32767:-32768)&&ry==(invert_y?-12345:12345)){ready=1;break;}
        }
        Sleep(4);
    }
    CHECK(ready);
    CHECK(!pilot_input_sdl_caps(0,caps,rumble)&&caps[0]==255&&caps[1]==0&&caps[10]==255);
    CHECK(caps[14]==255&&caps[15]==255&&caps[16]==255&&caps[17]==255);
    CHECK(pilot_input_sdl_state(1,state)==ERROR_DEVICE_NOT_CONNECTED);
    CHECK(!pilot_input_sdl_rumble(0,0,0));
    SDL_JoystickClose(chosen);CHECK(!SDL_JoystickDetachVirtual(second));
    CHECK(await_state(ERROR_DEVICE_NOT_CONNECTED,0,0));
    CHECK(pilot_input_sdl_rumble(0,1,0)==ERROR_DEVICE_NOT_CONNECTED);
    /* Another attached controller must not take over the disconnected seat. */
    CHECK(pilot_input_sdl_state(0,state)==ERROR_DEVICE_NOT_CONNECTED);
    second=SDL_JoystickAttachVirtualEx(&b);CHECK(second>=0);chosen=SDL_JoystickOpen(second);CHECK(chosen);
    CHECK(SDL_JoystickSetVirtualButton(chosen,SDL_CONTROLLER_BUTTON_A,1)==0);CHECK(await_state(0,255,0));
    pilot_input_sdl_shutdown();CHECK(pilot_input_sdl_state(0,state)==ERROR_DEVICE_NOT_CONNECTED);
    SDL_JoystickClose(chosen);CHECK(!SDL_JoystickDetachVirtual(second));
    SDL_JoystickClose(other);CHECK(!SDL_JoystickDetachVirtual(first));SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    puts("PASS SDL selected-device mapping, axes/triggers, isolation, disconnect/reconnect and cleanup");return 0;
}
