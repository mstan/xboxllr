#include "controller_handoff.h"
#include "launcher_profile.h"
#include "console_setup.h"
XboxllrConsoleSetup *xboxllr_console_setup=nullptr;
#include <memory>
#include <cstdio>
#include <cstring>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#x);return 1;}}while(0)
static std::unique_ptr<LauncherModel> model()
{
    auto m=std::make_unique<LauncherModel>();RecompLauncherCSettings settings={};RecompLauncherCGameInfo info={};
    launcher_profile_apply("xbox",&info);info.num_players=1;
    launcher_model_init(m.get(),&settings,&info,"");return m;
}
int main(int argc,char **argv)
{
    CHECK(argc==2);CHECK(!SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER));
    SDL_VirtualJoystickDesc desc={};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;desc.naxes=SDL_CONTROLLER_AXIS_MAX;desc.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
    desc.vendor_id=0x1234;desc.product_id=1;desc.name="First controller";
    int first=SDL_JoystickAttachVirtualEx(&desc);CHECK(first>=0);
    desc.product_id=2;desc.name="Xbox One Controller";
    int second=SDL_JoystickAttachVirtualEx(&desc);CHECK(second>=0);
    SDL_Joystick *chosen=SDL_JoystickOpen(second);CHECK(chosen);
    LauncherPad pads[LNG_MAX_PADS];int count=launcher_input_poll(pads,LNG_MAX_PADS,0);
    const LauncherPad *a=nullptr,*b=nullptr;
    for(int i=0;i<count;++i){if(!std::strcmp(pads[i].name,"First controller"))a=&pads[i];if(!std::strcmp(pads[i].name,"Xbox One Controller"))b=&pads[i];}
    CHECK(a&&b);LauncherPad pair[]={*a,*b};
    xboxllr_controller_begin(argv[1],1);auto m=model();xboxllr_controller_sync(m.get(),pair,2);
    CHECK(m->s.player_src[0]==2&&!std::strcmp(m->s.player_gamepad_guid[0],a->guid));
    launcher_model_set_source(m.get(),0,1,0,nullptr,nullptr);
    CHECK(!SDL_JoystickSetVirtualButton(chosen,SDL_CONTROLLER_BUTTON_A,1));
    bool delivered=false;
    for(int tick=0;tick<100&&!delivered;++tick) {
        SDL_PumpEvents();SDL_GameControllerUpdate();SDL_Event event;
        while(SDL_PollEvent(&event))if(event.type==SDL_CONTROLLERBUTTONDOWN && event.cbutton.which==(SDL_JoystickID)b->id) {
            xboxllr_controller_event(&event);delivered=true;
        }
        if(!delivered)SDL_Delay(2);
    }
    CHECK(delivered);m->action=LNG_ACTION_LAUNCH;xboxllr_controller_finish(m.get(),pair,2);
    CHECK(m->s.player_src[0]==2&&!std::strcmp(m->s.player_gamepad_guid[0],b->guid));
    char saved[40];std::snprintf(saved,sizeof(saved),"%s",b->guid);
    xboxllr_controller_begin(argv[1],0);auto reopened=model();reopened->s.player_src[0]=2;
    std::snprintf(reopened->s.player_gamepad_guid[0],sizeof(reopened->s.player_gamepad_guid[0]),"%s",saved);
    xboxllr_controller_sync(reopened.get(),nullptr,0);
    CHECK(std::strstr(reopened->player_pad_name[0],"Xbox One Controller (.."));
    CHECK(!std::strcmp(reopened->s.player_gamepad_guid[0],saved));
    pair[1].id+=100;xboxllr_controller_sync(reopened.get(),pair,2);
    CHECK(reopened->player_pad_id[0]==pair[1].id);
    launcher_model_set_source(reopened.get(),0,1,0,nullptr,nullptr);
    SDL_Event key={};key.type=SDL_KEYDOWN;xboxllr_controller_event(&key);reopened->action=LNG_ACTION_LAUNCH;
    xboxllr_controller_finish(reopened.get(),pair,2);CHECK(reopened->s.player_src[0]==1);
    launcher_input_shutdown();SDL_JoystickClose(chosen);CHECK(!SDL_JoystickDetachVirtual(second));CHECK(!SDL_JoystickDetachVirtual(first));
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    puts("PASS actual SDL button event selects Play controller, automatic/default handoff, offline name persistence, reconnect identity, keyboard override");return 0;
}
