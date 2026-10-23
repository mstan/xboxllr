#include "input_sdl.h"
#ifdef PILOT_HAVE_SDL2
#include <windows.h>
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One owner thread pumps SDL and owns the selected handle. Guest threads read
 * a locked snapshot; reconnect scans never run on the guest execution path.
 * GUIDs use exactly the shared launcher's SDL2 controller vocabulary. */
static SRWLOCK s_lock=SRWLOCK_INIT,s_command=SRWLOCK_INIT;
static HANDLE s_thread,s_wake,s_ready,s_done;
static volatile LONG s_quit;
static char s_guid[33];
static uint8_t s_report[18];
static uint32_t s_status=ERROR_DEVICE_NOT_CONNECTED,s_start_status;
static int s_has_rumble;
static uint16_t s_left,s_right;
static uint64_t s_request,s_ack;
static uint32_t s_rumble_status;

static void sdl_report(SDL_GameController *pad,uint8_t report[18])
{
    memset(report,0,18);
    const struct {SDL_GameControllerButton button;uint16_t mask;} digital[]={
        {SDL_CONTROLLER_BUTTON_DPAD_UP,1},{SDL_CONTROLLER_BUTTON_DPAD_DOWN,2},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT,4},{SDL_CONTROLLER_BUTTON_DPAD_RIGHT,8},
        {SDL_CONTROLLER_BUTTON_START,16},{SDL_CONTROLLER_BUTTON_BACK,32},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK,64},{SDL_CONTROLLER_BUTTON_RIGHTSTICK,128}};
    uint16_t bits=0;
    for(unsigned i=0;i<sizeof(digital)/sizeof(digital[0]);++i)
        if(SDL_GameControllerGetButton(pad,digital[i].button))bits|=digital[i].mask;
    memcpy(report,&bits,2);
    const SDL_GameControllerButton analog[]={SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_X,SDL_CONTROLLER_BUTTON_Y,SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
        SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
    for(unsigned i=0;i<6;++i)report[2+i]=SDL_GameControllerGetButton(pad,analog[i])?255:0;
    for(unsigned i=0;i<2;++i) {
        int v=SDL_GameControllerGetAxis(pad,(SDL_GameControllerAxis)(SDL_CONTROLLER_AXIS_TRIGGERLEFT+i));
        report[8+i]=(uint8_t)(v>0?((uint32_t)v*255u+16383u)/32767u:0);
    }
    for(unsigned i=0;i<4;++i) {
        int v=SDL_GameControllerGetAxis(pad,(SDL_GameControllerAxis)i);
        /* SDL down-positive Y becomes Xbox up-positive, saturating -32768. */
        if(i&1)v=v==-32768?32767:-v;
        const int16_t axis=(int16_t)v;memcpy(report+10+i*2,&axis,2);
    }
}
static DWORD WINAPI sdl_worker(void *unused)
{
    (void)unused;
    /* Rendering owns a native HWND, not an SDL window. Input routing is the
     * selected physical device, as in the existing XInput transport. */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    /* SDL2's Windows RawInput/device-notification HWND needs a Win32 message
     * pump. SDL_PumpEvents alone cannot dispatch it without SDL video, which
     * this native renderer does not initialize. Let SDL own that pump too. */
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD,"1");
    if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)!=0) {
        s_start_status=ERROR_NOT_SUPPORTED;
        fprintf(stderr,"[PILOT INPUT STOP] SDL controller initialization failed: %s\n",SDL_GetError());
        SetEvent(s_ready);return 0;
    }
    SDL_GameController *pad=NULL;uint64_t scan_at=0;int ready=0;
    while(!InterlockedCompareExchange(&s_quit,0,0)) {
        SDL_PumpEvents();SDL_GameControllerUpdate();
        if(pad&&!SDL_GameControllerGetAttached(pad)) {
            SDL_GameControllerClose(pad);pad=NULL;scan_at=0;
            fprintf(stderr,"[INPUT SDL] selected controller disconnected; waiting for guid=%s\n",s_guid);
        }
        uint64_t now=GetTickCount64();
        if(!pad&&now>=scan_at) {
            scan_at=now+250;
            for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)) {
                char guid[33];SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),guid,sizeof(guid));
                if(!strcmp(guid,s_guid)) {
                    pad=SDL_GameControllerOpen(i);
                    if(pad)fprintf(stderr,"[INPUT SDL] connected name=\"%s\" guid=%s\n",SDL_GameControllerName(pad),guid);
                    break;
                }
            }
        }
        uint8_t report[18]={0};if(pad)sdl_report(pad,report);
        AcquireSRWLockExclusive(&s_lock);
        s_status=pad?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED;
        s_has_rumble=pad&&SDL_GameControllerHasRumble(pad);
        memcpy(s_report,report,18);
        if(s_request!=s_ack) {
            s_rumble_status=!pad?ERROR_DEVICE_NOT_CONNECTED:
                !s_has_rumble?(s_left||s_right?ERROR_NOT_SUPPORTED:ERROR_SUCCESS):
                SDL_GameControllerRumble(pad,s_left,s_right,SDL_MAX_UINT32)==0?ERROR_SUCCESS:ERROR_NOT_SUPPORTED;
            s_ack=s_request;SetEvent(s_done);
        }
        ReleaseSRWLockExclusive(&s_lock);
        if(!ready){ready=1;SetEvent(s_ready);}
        WaitForSingleObject(s_wake,4);
    }
    if(pad){SDL_GameControllerRumble(pad,0,0,0);SDL_GameControllerClose(pad);}
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    AcquireSRWLockExclusive(&s_lock);s_status=ERROR_DEVICE_NOT_CONNECTED;s_has_rumble=0;
    ReleaseSRWLockExclusive(&s_lock);
    return 0;
}
void pilot_input_sdl_shutdown(void)
{
    if(!s_thread)return;
    InterlockedExchange(&s_quit,1);SetEvent(s_wake);WaitForSingleObject(s_thread,INFINITE);
    CloseHandle(s_thread);CloseHandle(s_wake);CloseHandle(s_ready);CloseHandle(s_done);
    s_thread=s_wake=s_ready=s_done=NULL;
}
uint32_t pilot_input_sdl_start(const char *guid)
{
    if(!guid||strlen(guid)!=32||strspn(guid,"0123456789abcdef")!=32)return ERROR_INVALID_PARAMETER;
    if(s_thread)return ERROR_ALREADY_INITIALIZED;
    memcpy(s_guid,guid,33);InterlockedExchange(&s_quit,0);s_start_status=0;
    s_wake=CreateEventA(NULL,FALSE,FALSE,NULL);s_ready=CreateEventA(NULL,FALSE,FALSE,NULL);s_done=CreateEventA(NULL,FALSE,FALSE,NULL);
    if(!s_wake||!s_ready||!s_done) {
        if(s_wake)CloseHandle(s_wake);if(s_ready)CloseHandle(s_ready);if(s_done)CloseHandle(s_done);
        s_wake=s_ready=s_done=NULL;return ERROR_NOT_ENOUGH_MEMORY;
    }
    s_thread=CreateThread(NULL,0,sdl_worker,NULL,0,NULL);
    if(!s_thread){CloseHandle(s_wake);CloseHandle(s_ready);CloseHandle(s_done);s_wake=s_ready=s_done=NULL;return ERROR_NOT_ENOUGH_MEMORY;}
    WaitForSingleObject(s_ready,INFINITE);
    if(s_start_status){uint32_t status=s_start_status;pilot_input_sdl_shutdown();return status;}
    atexit(pilot_input_sdl_shutdown);
    fprintf(stderr,"[INPUT SDL] Player 1 selected guid=%s; hot-plug enabled\n",guid);
    return 0;
}
uint32_t pilot_input_sdl_state(unsigned port,uint8_t report[18])
{
    if(port)return ERROR_DEVICE_NOT_CONNECTED;
    AcquireSRWLockShared(&s_lock);uint32_t status=s_status;
    if(!status)memcpy(report,s_report,18);ReleaseSRWLockShared(&s_lock);return status;
}
uint32_t pilot_input_sdl_caps(unsigned port,uint8_t report[18],uint8_t rumble[4])
{
    if(port)return ERROR_DEVICE_NOT_CONNECTED;
    AcquireSRWLockShared(&s_lock);uint32_t status=s_status;
    if(!status){memset(report,255,18);report[1]=0;memset(rumble,s_has_rumble?255:0,4);}
    ReleaseSRWLockShared(&s_lock);return status;
}
uint32_t pilot_input_sdl_rumble(unsigned port,uint16_t left,uint16_t right)
{
    if(port||!s_thread)return ERROR_DEVICE_NOT_CONNECTED;
    AcquireSRWLockExclusive(&s_command);AcquireSRWLockExclusive(&s_lock);
    s_left=left;s_right=right;uint64_t generation=++s_request;
    ReleaseSRWLockExclusive(&s_lock);SetEvent(s_wake);
    uint32_t status=ERROR_TIMEOUT;uint64_t end=GetTickCount64()+1000;
    while(GetTickCount64()<end) {
        AcquireSRWLockShared(&s_lock);int done=s_ack>=generation;
        if(done)status=s_rumble_status;ReleaseSRWLockShared(&s_lock);
        if(done)break;WaitForSingleObject(s_done,10);
    }
    ReleaseSRWLockExclusive(&s_command);return status;
}
#else
/* No SDL headers, imports or runtime dependency in the portable build floor. */
uint32_t pilot_input_sdl_start(const char *guid){(void)guid;return 50;}
uint32_t pilot_input_sdl_state(unsigned p,uint8_t r[18]){(void)p;(void)r;return 1167;}
uint32_t pilot_input_sdl_caps(unsigned p,uint8_t r[18],uint8_t v[4]){(void)p;(void)r;(void)v;return 1167;}
uint32_t pilot_input_sdl_rumble(unsigned p,uint16_t l,uint16_t r){(void)p;(void)l;(void)r;return 1167;}
void pilot_input_sdl_shutdown(void){}
#endif
