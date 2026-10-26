/* SPDX-License-Identifier: GPL-3.0-or-later
 * Xbox adapter for the pinned shared UI. Physical IDs come from the same SDL
 * enumeration used to draw Player 1; GUIDs cross the native launch boundary.
 * The name registry survives disconnection and is independent of game saves. */
#include "controller_handoff.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
static std::map<std::string,std::string> names;
static std::filesystem::path store;
static bool automatic_pending;
static bool automatic_source;
static SDL_JoystickID activating_pad=-1;
static bool valid_guid(const char *guid)
{return guid && std::strlen(guid)==32 && std::strspn(guid,"0123456789abcdef")==32;}
const char *xboxllr_controller_label(const char *guid,const char *name)
{
    static char label[128];
    if(!name || !*name || !std::strcmp(name,"Gamepad"))name="Controller";
    std::snprintf(label,sizeof(label),"%.48s (..%.4s)",name,valid_guid(guid)?guid+28:"????");
    return label;
}
void xboxllr_controller_begin(const char *catalog,int automatic)
{
    names.clear();activating_pad=-1;automatic_source=automatic_pending=automatic!=0;
    store=std::filesystem::u8path(catalog).parent_path()/"controller-names.txt";
    if(const char *configured=SDL_getenv("XLLE_LAUNCHER_CONTROLLER_NAMES"))store=std::filesystem::u8path(configured);
    std::error_code error;
    if(!std::filesystem::is_regular_file(store,error) || std::filesystem::file_size(store,error)>32768)return;
    std::ifstream input(store,std::ios::binary);std::string line;
    if(!std::getline(input,line) || line!="xboxllr-controller-names 1")return;
    while(std::getline(input,line) && names.size()<64) {
        if(line.size()<34 || line.size()>160 || line[32]!=' ')continue;
        std::string guid=line.substr(0,32),name=line.substr(33);
        if(valid_guid(guid.c_str()) && name.find_first_of("\r\n\0",0,3)==std::string::npos && !name.empty())
            names[guid]=name;
    }
}
void xboxllr_controller_no_source(void){automatic_source=automatic_pending=false;}
int xboxllr_controller_automatic(void){return automatic_source?1:0;}
static void choose(LauncherModel *model,const LauncherPad &pad)
{
    launcher_model_set_source(model,0,2,pad.id,pad.name,pad.guid);
    automatic_pending=false;
}
void xboxllr_controller_sync(LauncherModel *model,const LauncherPad *pads,int count)
{
    for(int i=0;i<count;++i)if(valid_guid(pads[i].guid) && pads[i].name[0] && (names.count(pads[i].guid) || names.size()<64))
        names[pads[i].guid]=pads[i].name;
    if(automatic_pending) {
        if(model->s.player_src[0]!=0)automatic_pending=false;
        else if(count>0)choose(model,pads[0]);
    }
    if(model->s.player_src[0]!=2)return;
    const char *guid=model->s.player_gamepad_guid[0];
    for(int i=0;i<count;++i)if(!std::strcmp(guid,pads[i].guid)) {
        model->player_pad_id[0]=pads[i].id;
        model->s.player_gamepad_instance[0]=pads[i].id+1;
        break;
    }
    auto known=names.find(guid);
    std::snprintf(model->player_pad_name[0],sizeof(model->player_pad_name[0]),"%s",
        xboxllr_controller_label(guid,known==names.end()?"Controller":known->second.c_str()));
}
void xboxllr_controller_event(const SDL_Event *event)
{
    if(event->type==SDL_CONTROLLERBUTTONDOWN)activating_pad=event->cbutton.which;
    else if(event->type==SDL_KEYDOWN || event->type==SDL_MOUSEBUTTONDOWN)activating_pad=-1;
}
void xboxllr_controller_finish(LauncherModel *model,const LauncherPad *pads,int count)
{
    if(model->action==LNG_ACTION_LAUNCH || model->action==LNG_ACTION_RELAUNCH)
        for(int i=0;i<count;++i)if((SDL_JoystickID)pads[i].id==activating_pad){choose(model,pads[i]);break;}
    xboxllr_controller_sync(model,pads,count);
    if(store.empty())return;
    const auto temporary=store.parent_path()/"controller-names.tmp";
    std::error_code error;
    if(std::filesystem::exists(temporary,error) || std::filesystem::is_symlink(std::filesystem::symlink_status(temporary,error)))return;
    std::ofstream output(temporary,std::ios::binary);
    output<<"xboxllr-controller-names 1\n";
    for(const auto &pair:names)output<<pair.first<<' '<<pair.second<<'\n';
    output.close();if(!output){std::filesystem::remove(temporary,error);return;}
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),store.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        std::filesystem::remove(temporary,error);
#else
    std::filesystem::rename(temporary,store,error);
#endif
}
